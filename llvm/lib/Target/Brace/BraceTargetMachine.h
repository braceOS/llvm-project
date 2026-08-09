//===-- BraceTargetMachine.h - Brace target machine -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACETARGETMACHINE_H
#define LLVM_LIB_TARGET_BRACE_BRACETARGETMACHINE_H

#include "llvm/Target/TargetMachine.h"
#include <memory>

namespace llvm {

class TargetLoweringObjectFile;

class BraceTargetMachine final : public TargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  bool UnsupportedConfiguration = false;

public:
  BraceTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     std::optional<Reloc::Model> RM,
                     std::optional<CodeModel::Model> CM, CodeGenOptLevel OL,
                     bool JIT);
  ~BraceTargetMachine() override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  bool addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
                           raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                           bool DisableVerify,
                           MachineModuleInfoWrapperPass *MMIWP) override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACETARGETMACHINE_H
