//===-- BraceFrameLowering.cpp - Brace frame lowering --------------------===//

#include "BraceFrameLowering.h"
#include "Brace.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

bool usesFinalizedSpillStorage(const MachineFunction &MF) {
  const StringRef ABI = MF.getTarget().Options.MCOptions.getABIName();
  return ABI == BraceSdagLeafHomeABIName ||
         ABI == BraceSdagDirectCallHomeABIName ||
         ABI == BraceSdagDirectCallByteFrameABIName ||
         ABI == BraceSdagDirectCallByteFrameFixedLocalABIName;
}

bool usesFixedLocalStorage(const MachineFunction &MF) {
  return MF.getTarget().Options.MCOptions.getABIName() ==
         BraceSdagDirectCallByteFrameFixedLocalABIName;
}

bool hasOnlyDeadFrameObjects(const MachineFrameInfo &Frame,
                             bool RequireSpillObjects,
                             bool RequireDefaultStackID) {
  if (Frame.getNumFixedObjects() != 0)
    return false;
  for (int FI = Frame.getObjectIndexBegin(); FI != Frame.getObjectIndexEnd();
       ++FI)
    if (!Frame.isDeadObjectIndex(FI) ||
        (RequireSpillObjects && !Frame.isSpillSlotObjectIndex(FI)) ||
        Frame.getObjectAllocation(FI) || Frame.isAliasedObjectIndex(FI) ||
        (RequireDefaultStackID &&
         Frame.getStackID(FI) != TargetStackID::Default))
      return false;
  return true;
}

bool hasOnlyDeadSpillObjects(const MachineFrameInfo &Frame) {
  return hasOnlyDeadFrameObjects(Frame, /*RequireSpillObjects=*/true,
                                 /*RequireDefaultStackID=*/false);
}

bool hasOnlyDeadFixedLocalObjects(const MachineFrameInfo &Frame) {
  return hasOnlyDeadFrameObjects(Frame, /*RequireSpillObjects=*/false,
                                 /*RequireDefaultStackID=*/true);
}

} // namespace

void BraceFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &) const {
  const MachineFrameInfo &Frame = MF.getFrameInfo();
  if (usesFinalizedSpillStorage(MF)) {
    const bool ExactDeadObjects = usesFixedLocalStorage(MF)
                                      ? hasOnlyDeadFixedLocalObjects(Frame)
                                      : hasOnlyDeadSpillObjects(Frame);
    if (Frame.getStackSize() != 0 || !ExactDeadObjects)
      report_fatal_error(
          "brace64 finalized spill transport requires zero LLVM frame storage");
    return;
  }
  if (Frame.getStackSize() != 0 || Frame.getNumObjects() != 0)
    report_fatal_error("brace64 S3b.3 leaf ABI requires an empty frame");
}

void BraceFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &) const {
  if (MF.getFrameInfo().getStackSize() != 0)
    report_fatal_error("brace64 S3b.3 leaf ABI requires an empty frame");
}

MachineBasicBlock::iterator BraceFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &, MachineBasicBlock &,
    MachineBasicBlock::iterator I) const {
  report_fatal_error("brace64 S3b.3 leaf ABI does not admit calls");
}

bool BraceFrameLowering::hasFPImpl(const MachineFunction &) const {
  return false;
}
