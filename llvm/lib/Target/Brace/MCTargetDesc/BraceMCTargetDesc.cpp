//===-- BraceMCTargetDesc.cpp - Brace MC registration --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BraceMCTargetDesc.h"
#include "TargetInfo/BraceTargetInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "BraceGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "BraceGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "BraceGenRegisterInfo.inc"

namespace {

class BraceMCAsmInfo final : public MCAsmInfo {
public:
  BraceMCAsmInfo(const Triple &, const MCTargetOptions &) {
    CodePointerSize = 8;
    CalleeSaveStackSlotSize = 8;
    CommentString = "#";
  }
};

MCInstrInfo *createBraceMCInstrInfo() {
  auto *Info = new MCInstrInfo();
  InitBraceMCInstrInfo(Info);
  return Info;
}

MCRegisterInfo *createBraceMCRegisterInfo(const Triple &) {
  auto *Info = new MCRegisterInfo();
  InitBraceMCRegisterInfo(Info, 0);
  return Info;
}

MCSubtargetInfo *createBraceMCSubtargetInfo(const Triple &TT, StringRef CPU,
                                            StringRef FS) {
  if (CPU.empty())
    CPU = "generic";
  return createBraceMCSubtargetInfoImpl(TT, CPU, CPU, FS);
}

} // namespace

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBraceTargetMC() {
  Target &T = getTheBraceTarget();
  RegisterMCAsmInfo<BraceMCAsmInfo> X(T);
  TargetRegistry::RegisterMCInstrInfo(T, createBraceMCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createBraceMCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createBraceMCSubtargetInfo);
  TargetRegistry::RegisterELFStreamer(T, Brace::createS2ELFStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(
      T, Brace::createS2ObjectTargetStreamer);
}
