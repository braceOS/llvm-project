//===-- BraceFrameLowering.h - Brace frame lowering ------------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACEFRAMELOWERING_H
#define LLVM_LIB_TARGET_BRACE_BRACEFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {

class BraceFrameLowering final : public TargetFrameLowering {
public:
  BraceFrameLowering()
      : TargetFrameLowering(StackGrowsDown, Align(16), 0, Align(16)) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

protected:
  bool hasFPImpl(const MachineFunction &MF) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACEFRAMELOWERING_H
