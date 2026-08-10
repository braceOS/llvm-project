//===-- BraceFinalizeSpillHomes.cpp - Publish typed resident homes -------===//
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
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "brace-finalize-spill-homes"

namespace {

[[noreturn]] void reject(const Twine &Message) {
  report_fatal_error("brace64 S3b.4 spill-home finalizer: " + Message);
}

[[noreturn]] void rejectPostHome(const Twine &Message) {
  report_fatal_error("brace64 S3b.4 post-home frame verifier: " + Message);
}

bool hasCanonicalPrePEIFrameState(const MachineFrameInfo &Frame) {
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
         Frame.getUnsafeStackSize() == 0 && !Frame.hasCalls() &&
         !Frame.isFrameAddressTaken() && !Frame.isReturnAddressTaken() &&
         !Frame.hasStackMap() && !Frame.hasPatchPoint() &&
         !Frame.adjustsStack() && !Frame.hasOpaqueSPAdjustment() &&
         !Frame.hasVAStart() && !Frame.hasCopyImplyingStackAdjustment() &&
         !Frame.hasMustTailInVarArgFunc() && !Frame.hasTailCall() &&
         Frame.getMaxAlign() <= Align(4);
}

enum class HomeKind : uint8_t { I8, I32 };

struct SpillInfo {
  HomeKind Kind;
  bool HasStore = false;
  bool HasLoad = false;
};

bool isI8Opcode(unsigned Opcode) {
  return Opcode == Brace::SPILL_STORE8 || Opcode == Brace::SPILL_LOAD8;
}

bool isSpillOpcode(unsigned Opcode) {
  return isI8Opcode(Opcode) || Opcode == Brace::SPILL_STORE32 ||
         Opcode == Brace::SPILL_LOAD32;
}

bool isStoreOpcode(unsigned Opcode) {
  return Opcode == Brace::SPILL_STORE8 || Opcode == Brace::SPILL_STORE32;
}

bool isHomeOpcode(unsigned Opcode) {
  return Opcode == Brace::HOME_SAVE8 || Opcode == Brace::HOME_SAVE32 ||
         Opcode == Brace::HOME_RESTORE8 || Opcode == Brace::HOME_RESTORE32;
}

void verifySpillMMO(const MachineInstr &MI, int FrameIndex, HomeKind Kind,
                    bool IsStore) {
  if (std::distance(MI.memoperands_begin(), MI.memoperands_end()) != 1)
    reject("spill pseudo must carry exactly one fixed-stack MMO");
  const MachineMemOperand &Memory = **MI.memoperands_begin();
  const auto *Fixed =
      dyn_cast_or_null<FixedStackPseudoSourceValue>(Memory.getPseudoValue());
  const int64_t Bytes = Kind == HomeKind::I8 ? 1 : 4;
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
  const HomeKind Kind = isI8Opcode(Opcode) ? HomeKind::I8 : HomeKind::I32;
  const Register Reg = MI.getOperand(0).getReg();
  const bool CorrectBank = Kind == HomeKind::I8
                               ? Brace::I8RegsRegClass.contains(Reg)
                               : Brace::I32RegsRegClass.contains(Reg);
  if (!Reg || !Reg.isPhysical() || !CorrectBank ||
      MI.getOperand(0).isDef() == IsStore ||
      MI.getOperand(0).getSubReg() != 0 || MI.getOperand(0).isUndef() ||
      MI.getOperand(0).isInternalRead() || MI.getOperand(0).isEarlyClobber() ||
      MI.getOperand(0).isRenamable() || MI.getOperand(0).isDebug())
    reject("spill pseudo has a wrong typed physical-register operand");

  const int FrameIndex = MI.getOperand(1).getIndex();
  const int64_t Bytes = Kind == HomeKind::I8 ? 1 : 4;
  if (FrameIndex < 0 || FrameIndex >= Frame.getObjectIndexEnd() ||
      Frame.isDeadObjectIndex(FrameIndex) ||
      !Frame.isSpillSlotObjectIndex(FrameIndex) ||
      Frame.getObjectAllocation(FrameIndex) ||
      Frame.isAliasedObjectIndex(FrameIndex) ||
      Frame.getStackID(FrameIndex) != TargetStackID::Default ||
      Frame.getObjectSize(FrameIndex) != Bytes ||
      Frame.getObjectAlign(FrameIndex) != Align(Bytes))
    reject("spill pseudo refers to a noncanonical spill frame index");
  verifySpillMMO(MI, FrameIndex, Kind, IsStore);

  auto [Iterator, Inserted] = Spills.try_emplace(FrameIndex, SpillInfo{Kind});
  if (!Inserted && Iterator->second.Kind != Kind)
    reject("one spill frame index is reused across value types");
  if (IsStore)
    Iterator->second.HasStore = true;
  else
    Iterator->second.HasLoad = true;
}

class BraceFinalizeSpillHomesLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceFinalizeSpillHomesLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (MF.getTarget().Options.MCOptions.getABIName() !=
        BraceSdagLeafHomeABIName)
      reject("pass ran outside its exact codegen profile");

    MachineFrameInfo &Frame = MF.getFrameInfo();
    if (!hasCanonicalPrePEIFrameState(Frame))
      reject("non-spill stack, frame, or call state is forbidden");

    DenseMap<int, SpillInfo> Spills;
    for (const MachineBasicBlock &MBB : MF) {
      for (const MachineInstr &MI : MBB) {
        if (isHomeOpcode(MI.getOpcode()))
          reject("FI-free home pseudo appeared before home finalization");
        if (isSpillOpcode(MI.getOpcode())) {
          verifySpillInstruction(MI, Frame, Spills);
          continue;
        }
        if (llvm::any_of(MI.operands(),
                         [](const MachineOperand &MO) { return MO.isFI(); }))
          reject("non-spill frame index survived register allocation");
      }
    }

    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI) {
      if (FI < 0 || !Frame.isSpillSlotObjectIndex(FI) ||
          Frame.getObjectAllocation(FI) ||
          Frame.isAliasedObjectIndex(FI) ||
          Frame.getStackID(FI) != TargetStackID::Default)
        reject("non-spill frame object survived register allocation");
      if (!Frame.isDeadObjectIndex(FI) && !Spills.contains(FI))
        reject("live spill object has no canonical transfer pseudo");
    }

    SmallVector<int, 20> Ordered;
    Ordered.reserve(Spills.size());
    for (const auto &Entry : Spills) {
      if (!Entry.second.HasStore || !Entry.second.HasLoad)
        reject("every published spill home requires both save and restore");
      Ordered.push_back(Entry.first);
    }
    llvm::sort(Ordered);
    if (Ordered.size() > 20)
      reject("typed spill-home count exceeds r6..r25");

    DenseMap<int, uint8_t> HomeByFrameIndex;
    for (unsigned Index = 0; Index != Ordered.size(); ++Index)
      HomeByFrameIndex.try_emplace(Ordered[Index], static_cast<uint8_t>(Index));

    const auto &TII = *MF.getSubtarget<BraceSubtarget>().getInstrInfo();
    bool Changed = false;
    for (MachineBasicBlock &MBB : MF) {
      for (auto I = MBB.begin(); I != MBB.end();) {
        MachineInstr &MI = *I++;
        if (!isSpillOpcode(MI.getOpcode()))
          continue;
        const bool IsStore = isStoreOpcode(MI.getOpcode());
        const bool IsI8 = isI8Opcode(MI.getOpcode());
        const int FI = MI.getOperand(1).getIndex();
        const uint8_t Home = HomeByFrameIndex.lookup(FI);
        unsigned NewOpcode = 0;
        if (IsStore)
          NewOpcode = IsI8 ? Brace::HOME_SAVE8 : Brace::HOME_SAVE32;
        else
          NewOpcode = IsI8 ? Brace::HOME_RESTORE8 : Brace::HOME_RESTORE32;

        MachineInstrBuilder Builder =
            BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(NewOpcode));
        if (IsStore)
          Builder.addImm(Home).add(MI.getOperand(0));
        else
          Builder.add(MI.getOperand(0)).addImm(Home);
        MI.eraseFromParent();
        Changed = true;
      }
    }

    for (int FI : Ordered)
      Frame.RemoveStackObject(FI);
    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI)
      if (!Frame.isDeadObjectIndex(FI))
        reject("live frame object survived home finalization");

    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB) {
        if (isSpillOpcode(MI.getOpcode()) ||
            (!MI.memoperands_empty() && isHomeOpcode(MI.getOpcode())))
          reject("spill transport state survived home finalization");
        for (const MachineOperand &MO : MI.operands())
          if (MO.isFI())
            reject("frame index survived home finalization");
      }
    return Changed;
  }

  StringRef getPassName() const override {
    return "Brace S3b.4 typed spill-home finalizer";
  }
};

class BraceVerifyPostHomeFrameLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceVerifyPostHomeFrameLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (MF.getTarget().Options.MCOptions.getABIName() !=
        BraceSdagLeafHomeABIName)
      rejectPostHome("pass ran outside its exact codegen profile");

    const MachineFrameInfo &Frame = MF.getFrameInfo();
    if (!hasCanonicalPrePEIFrameState(Frame))
      rejectPostHome("noncanonical pre-PEI frame state is forbidden");
    for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
         ++FI)
      if (FI < 0 || !Frame.isDeadObjectIndex(FI) ||
          !Frame.isSpillSlotObjectIndex(FI) ||
          Frame.getObjectAllocation(FI) ||
          Frame.isAliasedObjectIndex(FI) ||
          Frame.getStackID(FI) != TargetStackID::Default)
        rejectPostHome("live or non-spill frame object survived finalization");
    return false;
  }

  StringRef getPassName() const override {
    return "Brace S3b.4 post-home frame verifier";
  }
};

} // namespace

char BraceFinalizeSpillHomesLegacy::ID = 0;
char BraceVerifyPostHomeFrameLegacy::ID = 0;

INITIALIZE_PASS(BraceFinalizeSpillHomesLegacy, DEBUG_TYPE,
                "Brace S3b.4 typed spill-home finalizer", false, false)

INITIALIZE_PASS(BraceVerifyPostHomeFrameLegacy,
                "brace-verify-post-home-frame",
                "Brace S3b.4 post-home frame verifier", false, false)

FunctionPass *llvm::createBraceFinalizeSpillHomesPass() {
  return new BraceFinalizeSpillHomesLegacy();
}

FunctionPass *llvm::createBraceVerifyPostHomeFramePass() {
  return new BraceVerifyPostHomeFrameLegacy();
}
