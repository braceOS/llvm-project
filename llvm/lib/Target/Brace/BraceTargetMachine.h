//===-- BraceTargetMachine.h - Brace target machine -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACETARGETMACHINE_H
#define LLVM_LIB_TARGET_BRACE_BRACETARGETMACHINE_H

#include "BraceSubtarget.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include <memory>

namespace llvm {

class TargetLoweringObjectFile;

class BraceTargetMachine final : public CodeGenTargetMachineImpl {
public:
  enum class SdagABIKind {
    None,
    Leaf,
    LeafHome,
    DirectCall,
    DirectCallHome,
    DirectCallByteFrame,
    DirectCallByteFrameFixedLocal
  };

private:
  BraceSubtarget Subtarget;
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  bool UnsupportedConfiguration = false;
  SdagABIKind SdagABI = SdagABIKind::None;

public:
  BraceTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);
  ~BraceTargetMachine() override;

  const BraceSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }

  MachineFunctionInfo *
  createMachineFunctionInfo(BumpPtrAllocator &Allocator, const Function &F,
                            const TargetSubtargetInfo *STI) const override;

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  bool addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
                           raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                           bool DisableVerify,
                           MachineModuleInfoWrapperPass *MMIWP) override;

  Expected<std::unique_ptr<MCStreamer>>
  createMCStreamer(raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                   CodeGenFileType FileType, MCContext &Ctx) override;

  bool usesSdagABI() const { return SdagABI != SdagABIKind::None; }
  bool usesSdagLeafABI() const { return SdagABI == SdagABIKind::Leaf; }
  bool usesSdagLeafHomeABI() const { return SdagABI == SdagABIKind::LeafHome; }
  bool usesSdagDirectCallABI() const {
    return SdagABI == SdagABIKind::DirectCall;
  }
  bool usesSdagDirectCallHomeABI() const {
    return SdagABI == SdagABIKind::DirectCallHome;
  }
  bool usesSdagDirectCallByteFrameABI() const {
    return SdagABI == SdagABIKind::DirectCallByteFrame;
  }
  bool usesSdagDirectCallByteFrameFixedLocalABI() const {
    return SdagABI == SdagABIKind::DirectCallByteFrameFixedLocal;
  }
  bool usesSdagSpillHomes() const {
    return usesSdagLeafHomeABI() || usesSdagDirectCallHomeABI();
  }
  bool usesSdagDirectCalls() const {
    return usesSdagDirectCallABI() || usesSdagDirectCallHomeABI() ||
           usesSdagDirectCallByteFrameABI() ||
           usesSdagDirectCallByteFrameFixedLocalABI();
  }
  StringRef getSdagABIName() const;
  bool isMachineVerifierClean() const override { return true; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACETARGETMACHINE_H
