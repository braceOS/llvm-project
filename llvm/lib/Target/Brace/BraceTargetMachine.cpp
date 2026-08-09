//===-- BraceTargetMachine.cpp - Brace target machine --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BraceTargetMachine.h"
#include "Brace.h"
#include "TargetInfo/BraceTargetInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

BraceTargetMachine::BraceTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : TargetMachine(T, TT.computeDataLayout(), TT, CPU, FS, Options),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  UnsupportedConfiguration = (RM && *RM != Reloc::Static) ||
                             (CM && *CM != CodeModel::Small) || JIT ||
                             OL != CodeGenOptLevel::Less ||
                             (!CPU.empty() && CPU != "generic") || !FS.empty();
  this->RM = RM.value_or(Reloc::Static);
  this->CMModel = CM.value_or(CodeModel::Small);
  this->OptLevel = OL;

  MRI.reset(TheTarget.createMCRegInfo(TT));
  MII.reset(TheTarget.createMCInstrInfo());
  STI.reset(TheTarget.createMCSubtargetInfo(TT, CPU, FS));
  assert(MRI && MII && STI && "Brace TargetMC registration incomplete");
  AsmInfo.reset(TheTarget.createMCAsmInfo(*MRI, TT, Options.MCOptions));
  assert(AsmInfo && "Brace MCAsmInfo registration incomplete");
}

BraceTargetMachine::~BraceTargetMachine() = default;

bool BraceTargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  if (DwoOut || DisableVerify || UnsupportedConfiguration)
    return true;
  switch (FileType) {
  case CodeGenFileType::ObjectFile:
    if (!MMIWP)
      MMIWP = new MachineModuleInfoWrapperPass(this);
    PM.add(MMIWP);
    PM.add(createBraceS2WriterPass(*this, Out));
    return false;
  case CodeGenFileType::Null:
    return true;
  case CodeGenFileType::AssemblyFile:
    return true;
  }
  return true;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeBraceTarget() {
  RegisterTargetMachine<BraceTargetMachine> X(getTheBraceTarget());
}
