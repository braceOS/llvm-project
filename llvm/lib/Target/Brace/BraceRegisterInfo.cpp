//===-- BraceRegisterInfo.cpp - Brace register information --------------===//

#include "BraceRegisterInfo.h"
#include "Brace.h"
#include "BraceFrameLowering.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

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

bool BraceRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI, int,
                                            unsigned, RegScavenger *) const {
  const MachineFunction *MF = MI->getMF();
  if (MF && MF->getTarget().Options.MCOptions.getABIName() ==
                BraceSdagDirectCallABIName)
    report_fatal_error(
        "brace64 S3b.5 call-live activation home is unsupported");
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
