//===-- BraceInstrInfo.h - Brace instruction information -------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACEINSTRINFO_H
#define LLVM_LIB_TARGET_BRACE_BRACEINSTRINFO_H

#include "BraceRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "BraceGenInstrInfo.inc"

namespace llvm {

class BraceSubtarget;

class BraceInstrInfo final : public BraceGenInstrInfo {
  BraceRegisterInfo RI;

public:
  explicit BraceInstrInfo(const BraceSubtarget &STI);

  const BraceRegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;
  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register SrcReg,
      bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;
  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register DestReg,
      int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACEINSTRINFO_H
