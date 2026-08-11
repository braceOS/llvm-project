//===-- BraceFinalizeFixedLocal.cpp - Publish one fixed local frame -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Brace.h"
#include "BraceInstrInfo.h"
#include "BraceSubtarget.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <array>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "brace-finalize-fixed-local-byte-frame"

namespace {

constexpr int64_t SemanticFrameSize = 16;
constexpr int64_t SemanticLocalOffset = 8;

[[noreturn]] void reject(const Twine &Message) {
  report_fatal_error("brace64 S3b.8 fixed-local finalizer: " + Message);
}

[[noreturn]] void rejectPost(const Twine &Message) {
  report_fatal_error(
      "brace64 S3b.8 post-fixed-local-byte-frame verifier: " + Message);
}

bool isFixedLocalABI(const MachineFunction &MF) {
  return MF.getTarget().Options.MCOptions.getABIName() ==
         BraceSdagDirectCallByteFrameFixedLocalABIName;
}

bool expectedHasCalls(const MachineFunction &MF) {
  return MF.getName() == "brace_system_entry";
}

bool canonicalPrePEIState(const MachineFrameInfo &Frame, bool HasCalls) {
  return Frame.getStackSize() == 0 && Frame.getNumFixedObjects() == 0 &&
         !Frame.hasVarSizedObjects() && !Frame.hasStackProtectorIndex() &&
         !Frame.hasFunctionContextIndex() && Frame.getOffsetAdjustment() == 0 &&
         !Frame.isMaxCallFrameSizeComputed() &&
         Frame.getCVBytesOfCalleeSavedRegisters() == 0 &&
         Frame.getCalleeSavedInfo().empty() &&
         !Frame.isCalleeSavedInfoValid() &&
         Frame.getLocalFrameObjectCount() == 0 &&
         Frame.getLocalFrameSize() == 0 &&
         Frame.getLocalFrameMaxAlign() == Align(1) &&
         !Frame.getUseLocalStackAllocationBlock() &&
         Frame.getSavePoints().empty() && Frame.getRestorePoints().empty() &&
         Frame.getUnsafeStackSize() == 0 && Frame.hasCalls() == HasCalls &&
         !Frame.isFrameAddressTaken() && !Frame.isReturnAddressTaken() &&
         !Frame.hasStackMap() && !Frame.hasPatchPoint() &&
         !Frame.adjustsStack() && !Frame.hasOpaqueSPAdjustment() &&
         !Frame.hasVAStart() && !Frame.hasCopyImplyingStackAdjustment() &&
         !Frame.hasMustTailInVarArgFunc() && !Frame.hasTailCall() &&
         Frame.getMaxAlign() <= Align(4);
}

bool isLocalOpcode(unsigned Opcode) {
  return Opcode == Brace::LOCAL_STORE32 || Opcode == Brace::LOCAL_LOAD32;
}

bool isLifetimeOpcode(unsigned Opcode) {
  return Opcode == TargetOpcode::LIFETIME_START ||
         Opcode == TargetOpcode::LIFETIME_END;
}

bool isFrameOpcode(unsigned Opcode) {
  return Opcode == Brace::FRAME_ENTER || Opcode == Brace::FRAME_LOAD32 ||
         Opcode == Brace::FRAME_STORE32 || Opcode == Brace::FRAME_LEAVE;
}

bool isSpillOpcode(unsigned Opcode) {
  return Opcode == Brace::SPILL_STORE8 || Opcode == Brace::SPILL_STORE32 ||
         Opcode == Brace::SPILL_LOAD8 || Opcode == Brace::SPILL_LOAD32;
}

bool isHomeOpcode(unsigned Opcode) {
  return Opcode == Brace::HOME_SAVE8 || Opcode == Brace::HOME_SAVE32 ||
         Opcode == Brace::HOME_RESTORE8 || Opcode == Brace::HOME_RESTORE32;
}

bool hasFixedStackMMO(const MachineInstr &MI) {
  return llvm::any_of(MI.memoperands(), [](const MachineMemOperand *Memory) {
    return isa_and_nonnull<FixedStackPseudoSourceValue>(
        Memory->getPseudoValue());
  });
}

bool hasAuxiliaryPublicationMetadata(const MachineInstr &MI) {
  return MI.getPreInstrSymbol() || MI.getPostInstrSymbol() ||
         MI.getHeapAllocMarker() || MI.getPCSections() ||
         MI.getMMRAMetadata() || MI.getDeactivationSymbol() ||
         MI.getCFIType() != 0;
}

void verifyLocalMMO(const MachineInstr &MI, const AllocaInst *Alloca,
                    bool IsLoad) {
  if (std::distance(MI.memoperands_begin(), MI.memoperands_end()) != 1)
    reject("local carrier must have exactly one IR-backed MMO");
  const MachineMemOperand &Memory = **MI.memoperands_begin();
  const MachineMemOperand::Flags Expected =
      MachineMemOperand::MOVolatile |
      (IsLoad ? MachineMemOperand::MOLoad |
                    MachineMemOperand::MODereferenceable
              : MachineMemOperand::MOStore);
  if (Memory.getValue() != Alloca || Memory.getPseudoValue() ||
      Memory.getFlags() != Expected || Memory.getAddrSpace() != 0 ||
      Memory.getOffset() != 0 ||
      Memory.getSize() != LocationSize::precise(4) ||
      Memory.getAlign() != Align(4) || Memory.getAAInfo() ||
      Memory.getRanges() || Memory.getSyncScopeID() != SyncScope::System ||
      Memory.getSuccessOrdering() != AtomicOrdering::NotAtomic ||
      Memory.getFailureOrdering() != AtomicOrdering::NotAtomic)
    reject("local carrier lost its exact volatile i32 IR-backed MMO");
}

void verifyLocalCarrier(const MachineInstr &MI, MachineFrameInfo &Frame,
                        std::optional<int> &LocalFI, unsigned &Stores,
                        unsigned &Loads) {
  const bool IsLoad = MI.getOpcode() == Brace::LOCAL_LOAD32;
  if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
      MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isFI() || MI.getDebugLoc() ||
      MI.getFlags() != MachineInstr::NoFlags ||
      MI.getAsmPrinterFlags() != 0 ||
      MI.getOperand(1).getTargetFlags() != 0 ||
      hasAuxiliaryPublicationMetadata(MI))
    reject("malformed fixed-local carrier survived register allocation");
  const Register Reg = MI.getOperand(0).getReg();
  if (!Reg || !Reg.isPhysical() || !Brace::I32RegsRegClass.contains(Reg) ||
      MI.getOperand(0).isDef() != IsLoad || MI.getOperand(0).isImplicit() ||
      MI.getOperand(0).isTied() || MI.getOperand(0).getSubReg() != 0 ||
      MI.getOperand(0).isUndef() || MI.getOperand(0).isInternalRead() ||
      MI.getOperand(0).isEarlyClobber() ||
      MI.getOperand(0).isRenamable() || MI.getOperand(0).isDebug() ||
      MI.getOperand(0).isKill() || MI.getOperand(0).isDead())
    reject("fixed-local carrier has a wrong i32 physical register");

  const int FI = MI.getOperand(1).getIndex();
  if (FI < 0 || FI >= Frame.getObjectIndexEnd() ||
      Frame.isDeadObjectIndex(FI) || Frame.isSpillSlotObjectIndex(FI) ||
      Frame.getStackID(FI) != TargetStackID::Default ||
      Frame.getObjectSize(FI) != 4 || Frame.getObjectAlign(FI) != Align(4) ||
      Frame.getObjectOffset(FI) != 0 ||
      !Frame.getObjectAllocation(FI) || !Frame.isAliasedObjectIndex(FI))
    reject("carrier does not refer to the exact live ordinary i32 local");
  const auto *Alloca = dyn_cast<AllocaInst>(Frame.getObjectAllocation(FI));
  if (!Alloca || !Alloca->getAllocatedType()->isIntegerTy(32) ||
      Alloca->getAlign() != Align(4) || Alloca->getAddressSpace() != 0)
    reject("frame allocation is not the retained exact i32 alloca");
  verifyLocalMMO(MI, Alloca, IsLoad);
  if (LocalFI && *LocalFI != FI)
    reject("local carriers disagree on their frame index");
  LocalFI = FI;
  IsLoad ? ++Loads : ++Stores;
}

void verifyLifetimeCarrier(const MachineInstr &MI, std::optional<int> &LocalFI,
                           unsigned &Starts, unsigned &Ends) {
  if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
      MI.getNumExplicitOperands() != 1 || !MI.getOperand(0).isFI() ||
      MI.getDebugLoc() || MI.getFlags() != MachineInstr::NoFlags ||
      MI.getAsmPrinterFlags() != 0 ||
      MI.getOperand(0).getTargetFlags() != 0 || !MI.memoperands_empty() ||
      hasAuxiliaryPublicationMetadata(MI))
    reject("lifetime carrier is not one metadata-free FrameIndex");
  const int FI = MI.getOperand(0).getIndex();
  if (LocalFI && *LocalFI != FI)
    reject("lifetime and local carriers disagree on their frame index");
  LocalFI = FI;
  MI.getOpcode() == TargetOpcode::LIFETIME_START ? ++Starts : ++Ends;
}

void verifyFinalShape(MachineFunction &MF, bool RequiresLocal, bool Post) {
  auto Reject = [&](const Twine &Message) {
    Post ? rejectPost(Message) : reject(Message);
  };
  const MachineInstr *Enter = nullptr;
  const MachineInstr *Store = nullptr;
  const MachineInstr *Call = nullptr;
  const MachineInstr *Load = nullptr;
  const MachineInstr *Leave = nullptr;
  const MachineInstr *Ret = nullptr;
  unsigned FrameOperations = 0;
  unsigned Calls = 0;
  unsigned VoidReturns = 0;
  unsigned ValueReturns = 0;
  unsigned Operations = 0;
  SmallVector<unsigned, 16> Opcodes;
  if (MF.size() != 1 || !MF.front().pred_empty())
    Reject("function entry block has a predecessor or backedge");
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        Reject("debug instruction survived the exact fixed-local profile");
      ++Operations;
      Opcodes.push_back(MI.getOpcode());
      if (isLocalOpcode(MI.getOpcode()) || isLifetimeOpcode(MI.getOpcode()) ||
          isSpillOpcode(MI.getOpcode()) || isHomeOpcode(MI.getOpcode()) ||
          hasFixedStackMMO(MI) ||
          llvm::any_of(MI.operands(),
                       [](const MachineOperand &MO) { return MO.isFI(); }))
        Reject("private local/spill/lifetime carrier survived publication");
      switch (MI.getOpcode()) {
      case Brace::FRAME_ENTER:
        Enter = Enter ? nullptr : &MI;
        ++FrameOperations;
        if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
            MI.getNumExplicitOperands() != 1 ||
            !MI.getOperand(0).isImm() ||
            MI.getOperand(0).getImm() != SemanticFrameSize ||
            MI.getOperand(0).getTargetFlags() != 0 ||
            !MI.memoperands_empty() || MI.getDebugLoc() ||
            MI.getFlags() != MachineInstr::NoFlags ||
            MI.getAsmPrinterFlags() != 0 ||
            hasAuxiliaryPublicationMetadata(MI))
          Reject("FrameEnter is not exact frame_size=16");
        break;
      case Brace::FRAME_STORE32:
        Store = Store ? nullptr : &MI;
        ++FrameOperations;
        if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
            MI.getNumExplicitOperands() != 2 ||
            !MI.getOperand(0).isImm() ||
            MI.getOperand(0).getImm() != SemanticLocalOffset ||
            MI.getOperand(0).getTargetFlags() != 0 ||
            !MI.getOperand(1).isReg() ||
            !MI.getOperand(1).getReg().isPhysical() ||
            !Brace::I32RegsRegClass.contains(MI.getOperand(1).getReg()) ||
            MI.getOperand(1).isDef() || MI.getOperand(1).isImplicit() ||
            MI.getOperand(1).isTied() ||
            MI.getOperand(1).getSubReg() != 0 ||
            MI.getOperand(1).getTargetFlags() != 0 ||
            MI.getOperand(1).isUndef() ||
            MI.getOperand(1).isInternalRead() ||
            MI.getOperand(1).isEarlyClobber() ||
            MI.getOperand(1).isRenamable() ||
            MI.getOperand(1).isDebug() || MI.getOperand(1).isKill() ||
            MI.getOperand(1).isDead() || !MI.memoperands_empty() ||
            MI.getDebugLoc() || MI.getFlags() != MachineInstr::NoFlags ||
            MI.getAsmPrinterFlags() != 0 ||
            hasAuxiliaryPublicationMetadata(MI))
          Reject("FrameStore32 is not exact fixed-local offset=8");
        break;
      case Brace::FRAME_LOAD32:
        Load = Load ? nullptr : &MI;
        ++FrameOperations;
        if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
            MI.getNumExplicitOperands() != 2 ||
            !MI.getOperand(1).isImm() ||
            MI.getOperand(1).getImm() != SemanticLocalOffset ||
            MI.getOperand(1).getTargetFlags() != 0 ||
            !MI.getOperand(0).isReg() ||
            !MI.getOperand(0).getReg().isPhysical() ||
            !Brace::I32RegsRegClass.contains(MI.getOperand(0).getReg()) ||
            !MI.getOperand(0).isDef() || MI.getOperand(0).isImplicit() ||
            MI.getOperand(0).isTied() ||
            MI.getOperand(0).getSubReg() != 0 ||
            MI.getOperand(0).getTargetFlags() != 0 ||
            MI.getOperand(0).isUndef() ||
            MI.getOperand(0).isInternalRead() ||
            MI.getOperand(0).isEarlyClobber() ||
            MI.getOperand(0).isRenamable() ||
            MI.getOperand(0).isDebug() || MI.getOperand(0).isKill() ||
            MI.getOperand(0).isDead() || !MI.memoperands_empty() ||
            MI.getDebugLoc() || MI.getFlags() != MachineInstr::NoFlags ||
            MI.getAsmPrinterFlags() != 0 ||
            hasAuxiliaryPublicationMetadata(MI))
          Reject("FrameLoad32 is not exact fixed-local offset=8");
        break;
      case Brace::FRAME_LEAVE:
        Leave = Leave ? nullptr : &MI;
        ++FrameOperations;
        if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
            MI.getNumExplicitOperands() != 0 || !MI.memoperands_empty() ||
            MI.getDebugLoc() || MI.getFlags() != MachineInstr::NoFlags ||
            MI.getAsmPrinterFlags() != 0 ||
            hasAuxiliaryPublicationMetadata(MI))
          Reject("FrameLeave is not operand-free");
        break;
      case Brace::CALL_I32:
        Call = Call ? nullptr : &MI;
        ++Calls;
        break;
      case Brace::RET:
        Ret = Ret ? nullptr : &MI;
        ++VoidReturns;
        break;
      case Brace::RET_I32:
        ++ValueReturns;
        break;
      default:
        break;
      }
    }

  if (MF.getName() == "brace_system_call_leaf") {
    constexpr std::array<unsigned, 5> Expected{
        Brace::PADDR_IMM, Brace::LOAD32, Brace::AND32, TargetOpcode::COPY,
        Brace::RET_I32};
    if (RequiresLocal || FrameOperations != 0 || Calls != 0 ||
        VoidReturns != 0 || ValueReturns != 1 || Operations != 5 ||
        !llvm::equal(Opcodes, Expected))
      Reject("helper must remain one unframed valued-return function");
    return;
  }
  if (MF.getName() != "brace_system_entry" || Calls != 1 || !Call ||
      VoidReturns != 1 || !Ret || ValueReturns != 0)
    Reject("root requires exactly one Call and one void Return");
  if (!RequiresLocal) {
    constexpr std::array<unsigned, 6> Expected{
        Brace::PADDR_IMM, Brace::LOAD32, Brace::CALL_I32,
        Brace::PADDR_IMM, Brace::STORE32, Brace::RET};
    if (FrameOperations != 0 || Operations != 6 ||
        !llvm::equal(Opcodes, Expected))
      Reject("FL0 requires an empty semantic frame and exact six root ops");
    return;
  }
  if (Operations != 11 || FrameOperations != 4 || !Enter || !Store || !Load ||
      !Leave)
    Reject("FL1 requires exact 11 operations and four frame operations");
  constexpr std::array<unsigned, 11> Expected{
      Brace::FRAME_ENTER, Brace::PADDR_IMM,   Brace::LOAD32,
      Brace::FRAME_STORE32, Brace::CALL_I32, Brace::FRAME_LOAD32,
      Brace::AND32,      Brace::PADDR_IMM,   Brace::STORE32,
      Brace::FRAME_LEAVE, Brace::RET};
  if (!llvm::equal(Opcodes, Expected))
    Reject("FL1 final operation sequence is not exact");
  if (&MF.front().front() != Enter)
    Reject("FrameEnter(16) is not the first root operation");
  if (Store->getNextNode() != Call || Call->getNextNode() != Load)
    Reject("FL1 FrameStore32(8)/Call/FrameLoad32(8) adjacency is not exact");
  if (Leave->getNextNode() != Ret)
    Reject("FrameLeave does not immediately precede root Return");
}

class BraceFinalizeFixedLocalLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceFinalizeFixedLocalLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!isFixedLocalABI(MF))
      reject("pass ran outside its exact codegen profile");
    const bool RequiresLocal = verifyBraceS3FixedLocalIRAndRequiresRootFrame(
        *MF.getFunction().getParent());
    MachineFrameInfo &Frame = MF.getFrameInfo();
    if (!canonicalPrePEIState(Frame, expectedHasCalls(MF)))
      reject("noncanonical pre-PEI stack, frame, or call state");

    std::optional<int> LocalFI;
    unsigned Stores = 0;
    unsigned Loads = 0;
    unsigned Starts = 0;
    unsigned Ends = 0;
    unsigned Operations = 0;
    unsigned VoidReturns = 0;
    unsigned ValueReturns = 0;
    SmallVector<unsigned, 16> Opcodes;
    const MachineInstr *Start = nullptr;
    const MachineInstr *LocalStore = nullptr;
    const MachineInstr *Call = nullptr;
    const MachineInstr *LocalLoad = nullptr;
    const MachineInstr *End = nullptr;
    if (MF.size() != 1 || !MF.front().pred_empty())
      reject("pre-finalization function must be one predecessor-free block");
    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB) {
        if (MI.isDebugInstr())
          reject("debug instruction is outside the exact fixed-local profile");
        if (++Operations > 128)
          reject("pre-finalization operation count exceeds 128");
        Opcodes.push_back(MI.getOpcode());
        VoidReturns += MI.getOpcode() == Brace::RET;
        ValueReturns += MI.getOpcode() == Brace::RET_I32;
        if (isFrameOpcode(MI.getOpcode()))
          reject("semantic frame operation appeared before finalization");
        if (isSpillOpcode(MI.getOpcode()))
          reject("allocator spill is forbidden by the fixed-local profile");
        if (isHomeOpcode(MI.getOpcode()))
          reject("semantic home is forbidden by the fixed-local profile");
        if (isLocalOpcode(MI.getOpcode())) {
          verifyLocalCarrier(MI, Frame, LocalFI, Stores, Loads);
          if (MI.getOpcode() == Brace::LOCAL_STORE32)
            LocalStore = LocalStore ? nullptr : &MI;
          else
            LocalLoad = LocalLoad ? nullptr : &MI;
          continue;
        }
        if (isLifetimeOpcode(MI.getOpcode())) {
          verifyLifetimeCarrier(MI, LocalFI, Starts, Ends);
          if (MI.getOpcode() == TargetOpcode::LIFETIME_START)
            Start = Start ? nullptr : &MI;
          else
            End = End ? nullptr : &MI;
          continue;
        }
        if (MI.getOpcode() == Brace::CALL_I32)
          Call = Call ? nullptr : &MI;
        if (hasFixedStackMMO(MI))
          reject("nonlocal instruction carries a fixed-stack MMO");
        for (const MachineMemOperand *Memory : MI.memoperands())
          if (Memory->getPointerInfo().getAddrSpace() == 0)
            reject("nonlocal instruction carries the fixed-local MMO");
        for (const MachineOperand &MO : MI.operands())
          if (MO.isFI())
            reject("nonlocal instruction carries a FrameIndex");
      }

    if (MF.getName() == "brace_system_entry") {
      if (VoidReturns != 1 || ValueReturns != 0)
        reject("root preflight requires exactly one void Return");
      if (RequiresLocal != LocalFI.has_value())
        RequiresLocal
            ? reject("FL1 retained IR did not select one local FrameIndex")
            : reject("FL0 retained IR selected a local FrameIndex");
      if (RequiresLocal &&
          (Stores != 1 || Loads != 1 || Starts != 1 || Ends != 1 || !Start ||
           !LocalStore || !Call || !LocalLoad || !End))
        reject("FL1 requires one start/store/call/load/end carrier sequence");
      if (RequiresLocal &&
          (Start->getNextNode() != LocalStore ||
           LocalStore->getNextNode() != Call ||
           Call->getNextNode() != LocalLoad || !End->getNextNode() ||
           End->getNextNode()->getOpcode() != Brace::RET))
        reject("FL1 local carrier adjacency is not exact");
      if (RequiresLocal && (Operations > 126 || Operations + 2 > 128))
        reject("FL1 cannot reserve Enter/Leave within the operation limit");
      if (Operations != (RequiresLocal ? 11U : 6U))
        reject("FL0/FL1 pre-finalization root operation count is not exact");
      constexpr std::array<unsigned, 6> FL0Opcodes{
          Brace::PADDR_IMM, Brace::LOAD32, Brace::CALL_I32,
          Brace::PADDR_IMM, Brace::STORE32, Brace::RET};
      constexpr std::array<unsigned, 11> FL1Opcodes{
          Brace::PADDR_IMM,
          Brace::LOAD32,
          TargetOpcode::LIFETIME_START,
          Brace::LOCAL_STORE32,
          Brace::CALL_I32,
          Brace::LOCAL_LOAD32,
          Brace::AND32,
          Brace::PADDR_IMM,
          Brace::STORE32,
          TargetOpcode::LIFETIME_END,
          Brace::RET};
      if ((!RequiresLocal && !llvm::equal(Opcodes, FL0Opcodes)) ||
          (RequiresLocal && !llvm::equal(Opcodes, FL1Opcodes)))
        reject("FL0/FL1 pre-finalization operation sequence is not exact");
      if (RequiresLocal) {
        const MachineInstr *PhysicalLoad = Start->getPrevNode();
        const MachineInstr *And = LocalLoad->getNextNode();
        const MachineInstr *Address = And ? And->getNextNode() : nullptr;
        const MachineInstr *PhysicalStore =
            Address ? Address->getNextNode() : nullptr;
        if (!PhysicalLoad || PhysicalLoad->getOpcode() != Brace::LOAD32 ||
            !And || And->getOpcode() != Brace::AND32 || !Address ||
            Address->getOpcode() != Brace::PADDR_IMM || !PhysicalStore ||
            PhysicalStore->getOpcode() != Brace::STORE32 ||
            PhysicalStore->getNextNode() != End ||
            PhysicalLoad->getNumExplicitOperands() != 2 ||
            LocalStore->getNumExplicitOperands() != 2 ||
            Call->getNumOperands() != 4 ||
            LocalLoad->getNumExplicitOperands() != 2 ||
            And->getNumExplicitOperands() != 3 ||
            PhysicalStore->getNumExplicitOperands() != 2 ||
            PhysicalLoad->getOperand(0).getReg() !=
                LocalStore->getOperand(0).getReg() ||
            LocalStore->getOperand(0).getReg() !=
                Call->getOperand(3).getReg() ||
            Call->getOperand(2).getReg() != And->getOperand(2).getReg() ||
            LocalLoad->getOperand(0).getReg() !=
                And->getOperand(1).getReg() ||
            And->getOperand(0).getReg() !=
                PhysicalStore->getOperand(1).getReg())
          reject("FL1 preflight physical-input/local/call/result observable "
                 "dataflow is not exact");
      }
    } else if (MF.getName() == "brace_system_call_leaf") {
      constexpr std::array<unsigned, 5> HelperOpcodes{
          Brace::PADDR_IMM, Brace::LOAD32, Brace::AND32, TargetOpcode::COPY,
          Brace::RET_I32};
      if (LocalFI || Stores || Loads || Starts || Ends || VoidReturns != 0 ||
          ValueReturns != 1 || Operations != 5 ||
          !llvm::equal(Opcodes, HelperOpcodes))
        reject("helper cannot own the fixed local");
    } else {
      reject("unexpected MachineFunction identity");
    }

    int RestartBookkeepingFI = -1;
    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI) {
      if (LocalFI && FI == *LocalFI)
        continue;
      if (Frame.isDeadObjectIndex(FI))
        reject("pre-finalization dead frame tombstone is not admitted");
      if (RestartBookkeepingFI != -1 || MF.getName() != "brace_system_entry" ||
          !RequiresLocal || !LocalFI || FI != 0 || *LocalFI != 1 ||
          !Frame.isSpillSlotObjectIndex(FI) ||
          Frame.getObjectAllocation(FI) || Frame.isAliasedObjectIndex(FI) ||
          Frame.getStackID(FI) != TargetStackID::Default ||
          Frame.getObjectSize(FI) != 4 || Frame.getObjectAlign(FI) != Align(4) ||
          Frame.getObjectOffset(FI) != 0)
        reject("frame object is neither the natural local nor exact FI1 "
               "restart bookkeeping");
      RestartBookkeepingFI = FI;
    }
    if (!RequiresLocal &&
        Frame.getObjectIndexBegin() != Frame.getObjectIndexEnd())
      reject("FL0/helper frame object range must be empty");

    if (RestartBookkeepingFI != -1)
      Frame.RemoveStackObject(RestartBookkeepingFI);

    const auto &TII = *MF.getSubtarget<BraceSubtarget>().getInstrInfo();
    bool Changed = false;
    for (MachineBasicBlock &MBB : MF)
      for (auto I = MBB.begin(); I != MBB.end();) {
        MachineInstr &MI = *I++;
        if (isLifetimeOpcode(MI.getOpcode())) {
          MI.eraseFromParent();
          Changed = true;
          continue;
        }
        if (!isLocalOpcode(MI.getOpcode()))
          continue;
        const bool IsStore = MI.getOpcode() == Brace::LOCAL_STORE32;
        MachineInstrBuilder Builder = BuildMI(
            MBB, MI, DebugLoc(),
            TII.get(IsStore ? Brace::FRAME_STORE32 : Brace::FRAME_LOAD32));
        if (IsStore)
          Builder.addImm(SemanticLocalOffset).add(MI.getOperand(0));
        else
          Builder.add(MI.getOperand(0)).addImm(SemanticLocalOffset);
        MI.eraseFromParent();
        Changed = true;
      }

    if (LocalFI) {
      const int FI = *LocalFI;
      Frame.clearObjectAllocation(FI);
      Frame.setIsAliasedObjectIndex(FI, false);
      Frame.RemoveStackObject(FI);
      if (!Frame.isDeadObjectIndex(FI) || Frame.isSpillSlotObjectIndex(FI) ||
          Frame.getObjectAllocation(FI) || Frame.isAliasedObjectIndex(FI))
        reject("ordinary local tombstone cleanup did not complete exactly");

      MachineBasicBlock &Entry = MF.front();
      BuildMI(Entry, Entry.begin(), DebugLoc(), TII.get(Brace::FRAME_ENTER))
          .addImm(SemanticFrameSize);
      unsigned ReturnCount = 0;
      for (MachineBasicBlock &MBB : MF)
        for (auto I = MBB.begin(); I != MBB.end(); ++I)
          if (I->getOpcode() == Brace::RET) {
            BuildMI(MBB, I, DebugLoc(), TII.get(Brace::FRAME_LEAVE));
            ++ReturnCount;
          }
      if (ReturnCount != 1)
        reject("framed root does not have exactly one void Return");
      Changed = true;
    }

    verifyFinalShape(
        MF, RequiresLocal && MF.getName() == "brace_system_entry",
        /*Post=*/false);
    MF.getInfo<BraceMachineFunctionInfo>()->markFixedLocalCleanupObserved();
    return Changed;
  }

  StringRef getPassName() const override {
    return "Brace S3b.8 fixed-local byte-frame finalizer";
  }
};

class BraceVerifyPostFixedLocalLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceVerifyPostFixedLocalLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!isFixedLocalABI(MF))
      rejectPost("pass ran outside its exact codegen profile");
    const bool RequiresLocal = verifyBraceS3FixedLocalIRAndRequiresRootFrame(
        *MF.getFunction().getParent());
    const MachineFrameInfo &Frame = MF.getFrameInfo();
    if (!canonicalPrePEIState(Frame, expectedHasCalls(MF)))
      rejectPost("noncanonical post-finalization frame state");
    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI)
      if (!Frame.isDeadObjectIndex(FI) || Frame.getObjectAllocation(FI) ||
          Frame.isAliasedObjectIndex(FI) ||
          Frame.getStackID(FI) != TargetStackID::Default)
        rejectPost("live, allocated, aliased, or nondefault frame object "
                   "survived finalization");
    const bool RootLocal =
        MF.getName() == "brace_system_entry" && RequiresLocal;
    const bool SameProcess = MF.getInfo<BraceMachineFunctionInfo>()
                                 ->wasFixedLocalCleanupObserved();
    if (!SameProcess) {
      if (Frame.getObjectIndexEnd() != 0)
        rejectPost("serialized restart must reconstruct an empty private "
                   "frame-object range");
    } else if (!RootLocal) {
      if (Frame.getObjectIndexEnd() != 0)
        rejectPost("same-process FL0/helper must have no frame tombstone");
    } else if (Frame.getObjectIndexEnd() == 1) {
      if (!Frame.isDeadObjectIndex(0) || Frame.isSpillSlotObjectIndex(0) ||
          Frame.getObjectAlign(0) != Align(4))
        rejectPost("natural FI0 is not one exact dead ordinary local");
    } else if (Frame.getObjectIndexEnd() == 2) {
      if (!Frame.isDeadObjectIndex(0) ||
          !Frame.isSpillSlotObjectIndex(0) ||
          Frame.getObjectAlign(0) != Align(4) ||
          !Frame.isDeadObjectIndex(1) ||
          Frame.isSpillSlotObjectIndex(1) ||
          Frame.getObjectAlign(1) != Align(4))
        rejectPost("constructed FI1 tombstone set is not exact spill FI0 + "
                   "ordinary local FI1");
    } else {
      rejectPost("same-process local tombstone set is missing or duplicated");
    }
    verifyFinalShape(MF, RequiresLocal && MF.getName() == "brace_system_entry",
                     /*Post=*/true);
    return false;
  }

  StringRef getPassName() const override {
    return "Brace S3b.8 post-fixed-local-byte-frame verifier";
  }
};

} // namespace

char BraceFinalizeFixedLocalLegacy::ID = 0;
char BraceVerifyPostFixedLocalLegacy::ID = 0;

INITIALIZE_PASS(BraceFinalizeFixedLocalLegacy, DEBUG_TYPE,
                "Brace S3b.8 fixed-local byte-frame finalizer", false, false)

INITIALIZE_PASS(BraceVerifyPostFixedLocalLegacy,
                "brace-verify-post-fixed-local-byte-frame",
                "Brace S3b.8 post-fixed-local-byte-frame verifier", false,
                false)

FunctionPass *llvm::createBraceFinalizeFixedLocalPass() {
  return new BraceFinalizeFixedLocalLegacy();
}

FunctionPass *llvm::createBraceVerifyPostFixedLocalPass() {
  return new BraceVerifyPostFixedLocalLegacy();
}

FunctionPass *
llvm::createBraceFixedLocalMachineVerifierPass(StringRef Banner) {
  return createMachineVerifierPass(Banner.str());
}
