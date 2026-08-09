//===-- BraceFrameLowering.cpp - Brace frame lowering --------------------===//

#include "BraceFrameLowering.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

void BraceFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &) const {
  if (MF.getFrameInfo().getStackSize() != 0 ||
      MF.getFrameInfo().getNumObjects() != 0)
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
