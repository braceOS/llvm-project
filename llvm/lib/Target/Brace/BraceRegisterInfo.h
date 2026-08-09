//===-- BraceRegisterInfo.h - Brace register information -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACEREGISTERINFO_H
#define LLVM_LIB_TARGET_BRACE_BRACEREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "BraceGenRegisterInfo.inc"

namespace llvm {

class BraceRegisterInfo final : public BraceGenRegisterInfo {
public:
  BraceRegisterInfo();

  const MCPhysReg *
  getCalleeSavedRegs(const MachineFunction *MF = nullptr) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;
  Register getFrameRegister(const MachineFunction &MF) const override;
  const TargetRegisterClass *
  getPointerRegClass(unsigned Kind = 0) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACEREGISTERINFO_H
