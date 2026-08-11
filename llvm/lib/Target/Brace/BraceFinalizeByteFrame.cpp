//===-- BraceFinalizeByteFrame.cpp - Publish semantic Guest byte frames ---===//
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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "brace-finalize-byte-frame"

namespace {

constexpr int64_t SemanticFrameSize = 16;
constexpr int64_t SemanticSpillOffset = 4;

[[noreturn]] void reject(const Twine &Message) {
  report_fatal_error("brace64 S3b.7c byte-frame finalizer: " + Message);
}

[[noreturn]] void rejectPostFrame(const Twine &Message) {
  report_fatal_error("brace64 S3b.7c post-byte-frame verifier: " + Message);
}

bool isByteFrameABI(const MachineFunction &MF) {
  return MF.getTarget().Options.MCOptions.getABIName() ==
         BraceSdagDirectCallByteFrameABIName;
}

bool expectedHasCalls(const MachineFunction &MF) {
  return MF.getName() == "brace_system_entry";
}

bool hasCanonicalPrePEIFrameState(const MachineFrameInfo &Frame,
                                  bool HasCalls) {
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

bool isSpillOpcode(unsigned Opcode) {
  return Opcode == Brace::SPILL_STORE8 || Opcode == Brace::SPILL_STORE32 ||
         Opcode == Brace::SPILL_LOAD8 || Opcode == Brace::SPILL_LOAD32;
}

bool isStoreOpcode(unsigned Opcode) {
  return Opcode == Brace::SPILL_STORE8 || Opcode == Brace::SPILL_STORE32;
}

bool isI32Opcode(unsigned Opcode) {
  return Opcode == Brace::SPILL_STORE32 || Opcode == Brace::SPILL_LOAD32;
}

bool isFrameOpcode(unsigned Opcode) {
  return Opcode == Brace::FRAME_ENTER || Opcode == Brace::FRAME_LOAD32 ||
         Opcode == Brace::FRAME_STORE32 || Opcode == Brace::FRAME_LEAVE;
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

bool isCanonicalI32RegisterOperand(const MachineOperand &Operand, bool IsDef) {
  const Register Reg = Operand.isReg() ? Operand.getReg() : Register();
  return Reg && Reg.isPhysical() && Brace::I32RegsRegClass.contains(Reg) &&
         Operand.isDef() == IsDef && !Operand.isImplicit() &&
         !Operand.isTied() && Operand.getSubReg() == 0 &&
         Operand.getTargetFlags() == 0 && !Operand.isUndef() &&
         !Operand.isInternalRead() && !Operand.isEarlyClobber() &&
         !Operand.isRenamable() && !Operand.isDebug();
}

void verifyFinalFrameInstruction(const MachineInstr &MI, bool PostFrame) {
  auto Reject = [&](const Twine &Message) {
    PostFrame ? rejectPostFrame(Message) : reject(Message);
  };
  if (!isFrameOpcode(MI.getOpcode()))
    return;
  if (MI.getNumOperands() != MI.getNumExplicitOperands() || MI.getDebugLoc() ||
      MI.getFlags() != MachineInstr::NoFlags || MI.getAsmPrinterFlags() != 0 ||
      MI.getPreInstrSymbol() || MI.getPostInstrSymbol() ||
      MI.getHeapAllocMarker() || MI.getPCSections() || MI.getMMRAMetadata() ||
      MI.getDeactivationSymbol() || MI.getCFIType() != 0 ||
      !MI.memoperands_empty())
    Reject("Frame operation has noncanonical metadata or an LLVM MMO");
  switch (MI.getOpcode()) {
  case Brace::FRAME_ENTER:
    if (MI.getNumExplicitOperands() != 1 || !MI.getOperand(0).isImm() ||
        MI.getOperand(0).getImm() != SemanticFrameSize)
      Reject("FrameEnter operand is not exact frame_size=16");
    return;
  case Brace::FRAME_LOAD32:
    if (MI.getNumExplicitOperands() != 2 ||
        !isCanonicalI32RegisterOperand(MI.getOperand(0), /*IsDef=*/true) ||
        !MI.getOperand(1).isImm() ||
        MI.getOperand(1).getImm() != SemanticSpillOffset)
      Reject("FrameLoad32 operands are not exact i32/offset=4");
    return;
  case Brace::FRAME_STORE32:
    if (MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isImm() ||
        MI.getOperand(0).getImm() != SemanticSpillOffset ||
        !isCanonicalI32RegisterOperand(MI.getOperand(1), /*IsDef=*/false))
      Reject("FrameStore32 operands are not exact offset=4/i32");
    return;
  case Brace::FRAME_LEAVE:
    if (MI.getNumExplicitOperands() != 0)
      Reject("FrameLeave carries an operand");
    return;
  default:
    llvm_unreachable("checked Frame opcode");
  }
}

struct SpillInfo {
  bool IsI32 = false;
  unsigned Stores = 0;
  unsigned Loads = 0;
};

void verifySpillMMO(const MachineInstr &MI, int FrameIndex, bool IsI32,
                    bool IsStore) {
  if (std::distance(MI.memoperands_begin(), MI.memoperands_end()) != 1)
    reject("spill pseudo must carry exactly one fixed-stack MMO");
  const MachineMemOperand &Memory = **MI.memoperands_begin();
  const auto *Fixed =
      dyn_cast_or_null<FixedStackPseudoSourceValue>(Memory.getPseudoValue());
  const int64_t Bytes = IsI32 ? 4 : 1;
  const MachineMemOperand::Flags Expected =
      IsStore ? MachineMemOperand::MOStore : MachineMemOperand::MOLoad;
  if (!Fixed || Fixed->getFrameIndex() != FrameIndex ||
      Memory.getFlags() != Expected || Memory.getOffset() != 0 ||
      Memory.getAddrSpace() != 0 ||
      Memory.getSize() != LocationSize::precise(Bytes) ||
      Memory.getAlign() != Align(Bytes) || Memory.getAAInfo() ||
      Memory.getRanges() || Memory.getSyncScopeID() != SyncScope::System ||
      Memory.getSuccessOrdering() != AtomicOrdering::NotAtomic ||
      Memory.getFailureOrdering() != AtomicOrdering::NotAtomic)
    reject("spill pseudo lost its exact private fixed-stack effect");
}

void verifySpillInstruction(const MachineInstr &MI, MachineFrameInfo &Frame,
                            DenseMap<int, SpillInfo> &Spills) {
  if (MI.getNumOperands() != MI.getNumExplicitOperands() ||
      MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(1).isFI() || MI.getDebugLoc() ||
      MI.getFlags() != MachineInstr::NoFlags || MI.getAsmPrinterFlags() != 0)
    reject("malformed spill pseudo survived register allocation");

  const unsigned Opcode = MI.getOpcode();
  const bool IsStore = isStoreOpcode(Opcode);
  const bool IsI32 = isI32Opcode(Opcode);
  const Register Reg = MI.getOperand(0).getReg();
  const bool CorrectBank = IsI32 ? Brace::I32RegsRegClass.contains(Reg)
                                 : Brace::I8RegsRegClass.contains(Reg);
  if (!Reg || !Reg.isPhysical() || !CorrectBank ||
      MI.getOperand(0).isDef() == IsStore ||
      MI.getOperand(0).getSubReg() != 0 || MI.getOperand(0).isUndef() ||
      MI.getOperand(0).isInternalRead() || MI.getOperand(0).isEarlyClobber() ||
      MI.getOperand(0).isRenamable() || MI.getOperand(0).isDebug())
    reject("spill pseudo has a wrong typed physical-register operand");

  const int FrameIndex = MI.getOperand(1).getIndex();
  const int64_t Bytes = IsI32 ? 4 : 1;
  if (FrameIndex < 0 || FrameIndex >= Frame.getObjectIndexEnd() ||
      Frame.isDeadObjectIndex(FrameIndex) ||
      !Frame.isSpillSlotObjectIndex(FrameIndex) ||
      Frame.getObjectAllocation(FrameIndex) ||
      Frame.isAliasedObjectIndex(FrameIndex) ||
      Frame.getStackID(FrameIndex) != TargetStackID::Default ||
      Frame.getObjectSize(FrameIndex) != Bytes ||
      Frame.getObjectAlign(FrameIndex) != Align(Bytes))
    reject("spill pseudo refers to a noncanonical spill frame index");
  verifySpillMMO(MI, FrameIndex, IsI32, IsStore);

  auto [It, Inserted] = Spills.try_emplace(FrameIndex, SpillInfo{IsI32});
  if (!Inserted && It->second.IsI32 != IsI32)
    reject("one spill frame index is reused across value types");
  if (IsStore)
    ++It->second.Stores;
  else
    ++It->second.Loads;
}

void verifyFinalByteFrameShape(MachineFunction &MF, bool PostFrame) {
  auto Reject = [&](const Twine &Message) {
    PostFrame ? rejectPostFrame(Message) : reject(Message);
  };
  const MachineInstr *Enter = nullptr;
  const MachineInstr *Store = nullptr;
  const MachineInstr *Call = nullptr;
  const MachineInstr *Load = nullptr;
  const MachineInstr *Leave = nullptr;
  const MachineInstr *Return = nullptr;
  unsigned FrameCount = 0;
  unsigned CallCount = 0;
  unsigned ReturnCount = 0;
  unsigned ValueReturnCount = 0;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (isHomeOpcode(MI.getOpcode()))
        Reject("semantic-home operation is forbidden by the byte-frame ABI");
      if (hasFixedStackMMO(MI))
        Reject("fixed-stack MMO survived the byte-frame trust boundary");
      verifyFinalFrameInstruction(MI, PostFrame);
      switch (MI.getOpcode()) {
      case Brace::FRAME_ENTER:
        Enter = Enter ? nullptr : &MI;
        ++FrameCount;
        break;
      case Brace::FRAME_STORE32:
        Store = Store ? nullptr : &MI;
        ++FrameCount;
        break;
      case Brace::CALL_I32:
        Call = Call ? nullptr : &MI;
        ++CallCount;
        break;
      case Brace::FRAME_LOAD32:
        Load = Load ? nullptr : &MI;
        ++FrameCount;
        break;
      case Brace::FRAME_LEAVE:
        Leave = Leave ? nullptr : &MI;
        ++FrameCount;
        break;
      case Brace::RET:
        Return = Return ? nullptr : &MI;
        ++ReturnCount;
        break;
      case Brace::RET_I32:
        ++ValueReturnCount;
        break;
      default:
        break;
      }
    }
  }

  if (!MF.front().pred_empty())
    Reject("function entry block has a predecessor or backedge");
  if (MF.getName() == "brace_system_call_leaf") {
    if (FrameCount != 0 || CallCount != 0 || ReturnCount != 0 ||
        ValueReturnCount != 1)
      Reject("helper must have frame_size=0 and exactly one valued Return");
    return;
  }
  if (MF.getName() != "brace_system_entry" || CallCount != 1 || !Call ||
      ReturnCount != 1 || !Return || ValueReturnCount != 0)
    Reject("root requires exactly one Call and one void Return");
  if (FrameCount == 0)
    return;
  if (FrameCount != 4 || !Enter || !Store || !Load || !Leave)
    Reject("BF1 requires one Enter, Store32, Load32, and Leave");
  auto First = MF.front().begin();
  while (First != MF.front().end() && First->isDebugInstr())
    ++First;
  if (First == MF.front().end() || &*First != Enter ||
      Enter->getNumExplicitOperands() != 1 || !Enter->getOperand(0).isImm() ||
      Enter->getOperand(0).getImm() != 16)
    Reject("FrameEnter(16) is not the first root operation");
  if (Store->getNumExplicitOperands() != 2 || !Store->getOperand(0).isImm() ||
      Store->getOperand(0).getImm() != 4 ||
      Load->getNumExplicitOperands() != 2 || !Load->getOperand(1).isImm() ||
      Load->getOperand(1).getImm() != 4 || Store->getNextNode() != Call ||
      Call->getNextNode() != Load)
    Reject("BF1 Store32(4)/Call/Load32(4) order is not exact");
  if (Leave->getNextNode() != Return)
    Reject("FrameLeave does not immediately precede the root Return");
}

class BraceFinalizeByteFrameLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceFinalizeByteFrameLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!isByteFrameABI(MF))
      reject("pass ran outside its exact codegen profile");
    const bool RootRequiresFrame =
        verifyBraceS3ByteFrameIRAndRequiresRootFrame(
            *MF.getFunction().getParent());

    MachineFrameInfo &Frame = MF.getFrameInfo();
    if (!hasCanonicalPrePEIFrameState(Frame, expectedHasCalls(MF)))
      reject("non-spill stack, frame, or call state is forbidden");

    DenseMap<int, SpillInfo> Spills;
    uint64_t OperationCount = 0;
    unsigned VoidReturnCount = 0;
    unsigned ValueReturnCount = 0;
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        if (!MI.isDebugInstr()) {
          if (OperationCount == 128)
            reject("pre-finalization operation count exceeds 128");
          ++OperationCount;
        }
        VoidReturnCount += MI.getOpcode() == Brace::RET;
        ValueReturnCount += MI.getOpcode() == Brace::RET_I32;
        if (isFrameOpcode(MI.getOpcode()))
          reject("semantic byte-frame operation appeared before finalization");
        if (isHomeOpcode(MI.getOpcode()))
          reject("semantic-home operation appeared in the byte-frame ABI");
        if (isSpillOpcode(MI.getOpcode())) {
          verifySpillInstruction(MI, Frame, Spills);
          continue;
        }
        if (hasFixedStackMMO(MI))
          reject("non-spill instruction carries a fixed-stack MMO");
        if (llvm::any_of(MI.operands(),
                         [](const MachineOperand &MO) { return MO.isFI(); }))
          reject("non-spill frame index survived register allocation");
      }
    }

    int RestartBookkeepingFI = -1;
    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI) {
      if (FI < 0 || !Frame.isSpillSlotObjectIndex(FI) ||
          Frame.getObjectAllocation(FI) || Frame.isAliasedObjectIndex(FI) ||
          Frame.getStackID(FI) != TargetStackID::Default)
        reject("non-spill frame object survived register allocation");
      if (Frame.isDeadObjectIndex(FI) || Spills.contains(FI))
        continue;
      if (RestartBookkeepingFI != -1)
        reject("multiple live spill objects have no canonical transfer pseudo");
      RestartBookkeepingFI = FI;
    }

    if (MF.getName() == "brace_system_entry") {
      if (VoidReturnCount != 1 || ValueReturnCount != 0)
        reject("root requires exactly one void Return before finalization");
      if (RootRequiresFrame != !Spills.empty())
        RootRequiresFrame
            ? reject("BF1 retained IR requires exactly one i32 call-live "
                     "spill")
            : reject("BF0 retained IR requires an empty compiler frame");
      if (Spills.size() > 1)
        reject("root permits BF0 or exactly one i32 call-live spill");
      if (!Spills.empty()) {
        const SpillInfo &Info = Spills.begin()->second;
        if (!Info.IsI32 || Info.Stores != 1 || Info.Loads != 1)
          reject("BF1 requires exactly one i32 save and one restore");
      }
    } else if (MF.getName() == "brace_system_call_leaf") {
      if (VoidReturnCount != 0 || ValueReturnCount != 1)
        reject("helper requires exactly one valued Return before finalization");
      if (!Spills.empty())
        reject("helper requires an empty semantic frame");
    } else {
      reject("unexpected MachineFunction identity");
    }
    if (Spills.empty() &&
        Frame.getObjectIndexBegin() != Frame.getObjectIndexEnd())
      reject("BF0/helper frame object range must be completely empty");
    if (RestartBookkeepingFI != -1) {
      const int LiveFI = Spills.empty() ? -1 : Spills.begin()->first;
      // MIR does not serialize MachineFrameInfo dead holes.  At this exact
      // restart seam only, normalize one leading, otherwise inert spill
      // record before proving that the live spill's private ordinal is FI 1.
      // No source-to-object path creates this record, and every MI FI/MMO use
      // was exhaustively checked above before this first mutation.
      if (MF.getName() != "brace_system_entry" || Spills.size() != 1 ||
          RestartBookkeepingFI != 0 || LiveFI != 1 ||
          Frame.getObjectSize(RestartBookkeepingFI) != 4 ||
          Frame.getObjectAlign(RestartBookkeepingFI) != Align(4) ||
          Frame.getObjectOffset(RestartBookkeepingFI) != 0)
        reject("restart bookkeeping is not one exact leading inert i32 spill "
               "record");
    }
    // There are exactly two functions.  This checked per-function cap implies
    // the writer's checked aggregate cap of 256; BF1 reserves two words for
    // FrameEnter/FrameLeave before any instruction or MFI mutation occurs.
    if (!Spills.empty() && OperationCount > 126)
      reject("FrameEnter/FrameLeave would exceed the 128-operation "
             "publication limit");

    if (RestartBookkeepingFI != -1)
      Frame.RemoveStackObject(RestartBookkeepingFI);

    const auto &TII = *MF.getSubtarget<BraceSubtarget>().getInstrInfo();
    bool Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto I = MBB.begin(); I != MBB.end();) {
        MachineInstr &MI = *I++;
        if (!isSpillOpcode(MI.getOpcode()))
          continue;
        const bool IsStore = isStoreOpcode(MI.getOpcode());
        if (!isI32Opcode(MI.getOpcode()))
          reject("this compiler revision only maps an i32 spill");
        const unsigned NewOpcode =
            IsStore ? Brace::FRAME_STORE32 : Brace::FRAME_LOAD32;
        MachineInstrBuilder Builder =
            BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(NewOpcode));
        if (IsStore)
          Builder.addImm(SemanticSpillOffset).add(MI.getOperand(0));
        else
          Builder.add(MI.getOperand(0)).addImm(SemanticSpillOffset);
        MI.eraseFromParent();
        Changed = true;
      }
    }

    for (const auto &Entry : Spills)
      Frame.RemoveStackObject(Entry.first);

    if (!Spills.empty()) {
      MachineBasicBlock &EntryBlock = MF.front();
      BuildMI(EntryBlock, EntryBlock.begin(), DebugLoc(),
              TII.get(Brace::FRAME_ENTER))
          .addImm(SemanticFrameSize);
      unsigned ReturnCount = 0;
      for (MachineBasicBlock &MBB : MF)
        for (auto I = MBB.begin(); I != MBB.end(); ++I)
          if (I->getOpcode() == Brace::RET) {
            BuildMI(MBB, I, DebugLoc(), TII.get(Brace::FRAME_LEAVE));
            ++ReturnCount;
          }
      if (ReturnCount == 0)
        reject("framed root has no void Return");
      Changed = true;
    }

    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI)
      if (!Frame.isDeadObjectIndex(FI))
        reject("live frame object survived byte-frame finalization");
    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB) {
        if (isSpillOpcode(MI.getOpcode()))
          reject("spill pseudo survived byte-frame finalization");
        if (isHomeOpcode(MI.getOpcode()))
          reject("semantic-home operation survived byte-frame finalization");
        if (hasFixedStackMMO(MI))
          reject("fixed-stack MMO survived byte-frame finalization");
        for (const MachineOperand &MO : MI.operands())
          if (MO.isFI())
            reject("FrameIndex survived byte-frame finalization");
      }
    verifyFinalByteFrameShape(MF, /*PostFrame=*/false);
    return Changed;
  }

  StringRef getPassName() const override {
    return "Brace S3b.7c semantic byte-frame finalizer";
  }
};

class BraceVerifyPostByteFrameLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceVerifyPostByteFrameLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!isByteFrameABI(MF))
      rejectPostFrame("pass ran outside its exact codegen profile");
    const bool RootRequiresFrame =
        verifyBraceS3ByteFrameIRAndRequiresRootFrame(
            *MF.getFunction().getParent());
    const MachineFrameInfo &Frame = MF.getFrameInfo();
    if (!hasCanonicalPrePEIFrameState(Frame, expectedHasCalls(MF)))
      rejectPostFrame("noncanonical pre-PEI frame state is forbidden");
    const bool HasFrame = llvm::any_of(MF, [](const MachineBasicBlock &MBB) {
      return llvm::any_of(MBB, [](const MachineInstr &MI) {
        return isFrameOpcode(MI.getOpcode());
      });
    });
    // Reject published FI/MMO uses before inspecting any accompanying frame
    // record so a forged record cannot mask the actual trust-boundary fault.
    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB) {
        if (isSpillOpcode(MI.getOpcode()))
          rejectPostFrame("spill pseudo survived finalization");
        if (isHomeOpcode(MI.getOpcode()))
          rejectPostFrame(
              "semantic-home operation survived byte-frame finalization");
        if (hasFixedStackMMO(MI))
          rejectPostFrame("fixed-stack MMO survived finalization");
        for (const MachineOperand &MO : MI.operands())
          if (MO.isFI())
            rejectPostFrame("FrameIndex survived finalization");
      }
    if (!HasFrame && Frame.getObjectIndexBegin() != Frame.getObjectIndexEnd())
      rejectPostFrame("BF0/helper frame object range must be completely empty");
    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI)
      if (FI < 0 || !Frame.isDeadObjectIndex(FI) ||
          !Frame.isSpillSlotObjectIndex(FI) || Frame.getObjectAllocation(FI) ||
          Frame.isAliasedObjectIndex(FI) ||
          Frame.getStackID(FI) != TargetStackID::Default)
        rejectPostFrame("live or non-spill frame object survived finalization");
    if (MF.getName() == "brace_system_entry" &&
        RootRequiresFrame != HasFrame)
      RootRequiresFrame
          ? rejectPostFrame("BF1 retained IR requires one semantic frame")
          : rejectPostFrame("BF0 retained IR requires an empty semantic "
                            "frame");
    verifyFinalByteFrameShape(MF, /*PostFrame=*/true);
    return false;
  }

  StringRef getPassName() const override {
    return "Brace S3b.7c post-byte-frame verifier";
  }
};

} // namespace

char BraceFinalizeByteFrameLegacy::ID = 0;
char BraceVerifyPostByteFrameLegacy::ID = 0;

INITIALIZE_PASS(BraceFinalizeByteFrameLegacy, DEBUG_TYPE,
                "Brace S3b.7c semantic byte-frame finalizer", false, false)

INITIALIZE_PASS(BraceVerifyPostByteFrameLegacy, "brace-verify-post-byte-frame",
                "Brace S3b.7c post-byte-frame verifier", false, false)

FunctionPass *llvm::createBraceFinalizeByteFramePass() {
  return new BraceFinalizeByteFrameLegacy();
}

FunctionPass *llvm::createBraceVerifyPostByteFramePass() {
  return new BraceVerifyPostByteFrameLegacy();
}

FunctionPass *llvm::createBraceByteFrameMachineVerifierPass(StringRef Banner) {
  return createMachineVerifierPass(Banner.str());
}
