//===-- BraceSubtarget.h - Brace subtarget information ---------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACESUBTARGET_H
#define LLVM_LIB_TARGET_BRACE_BRACESUBTARGET_H

#include "BraceFrameLowering.h"
#include "BraceISelLowering.h"
#include "BraceInstrInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "BraceGenSubtargetInfo.inc"

namespace llvm {

class BraceTargetMachine;

class BraceSubtarget final : public BraceGenSubtargetInfo {
  BraceInstrInfo InstrInfo;
  BraceFrameLowering FrameLowering;
  BraceTargetLowering TLInfo;
  SelectionDAGTargetInfo TSInfo;

public:
  BraceSubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                 const BraceTargetMachine &TM);

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);
  BraceSubtarget &initializeSubtargetDependencies(StringRef CPU, StringRef FS);

  const BraceInstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const BraceFrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const BraceRegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }
  const BraceTargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }
  bool enableMachineScheduler() const override { return false; }
  bool enablePostRAScheduler() const override { return false; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACESUBTARGET_H
