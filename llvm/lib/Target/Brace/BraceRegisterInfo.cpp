//===-- BraceRegisterInfo.cpp - Brace register information --------------===//

#include "BraceRegisterInfo.h"
#include "Brace.h"
#include "BraceFrameLowering.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "BraceGenRegisterInfo.inc"

using namespace llvm;

BraceRegisterInfo::BraceRegisterInfo() : BraceGenRegisterInfo(0) {}

const MCPhysReg *
BraceRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  return CSR_NoRegs_SaveList;
}

const uint32_t *BraceRegisterInfo::getCallPreservedMask(const MachineFunction &,
                                                        CallingConv::ID) const {
  return CSR_NoRegs_RegMask;
}

BitVector BraceRegisterInfo::getReservedRegs(const MachineFunction &) const {
  return BitVector(getNumRegs());
}

bool BraceRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator, int,
                                            unsigned, RegScavenger *) const {
  report_fatal_error(
      "brace64 S3b.3 leaf ABI does not admit stack frame indices");
}

Register BraceRegisterInfo::getFrameRegister(const MachineFunction &) const {
  return Register();
}

const TargetRegisterClass *
BraceRegisterInfo::getPointerRegClass(unsigned) const {
  return &Brace::PAddrRegsRegClass;
}
