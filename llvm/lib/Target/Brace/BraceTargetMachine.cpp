//===-- BraceTargetMachine.cpp - Brace target machine --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BraceTargetMachine.h"
#include "Brace.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "TargetInfo/BraceTargetInfo.h"
#include "llvm/CodeGen/CodeGenTargetMachineImpl.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace {

constexpr StringLiteral SdagLeafABIName = "brace-system-s2-leaf-r0";

class BracePassConfig final : public TargetPassConfig {
public:
  BracePassConfig(BraceTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  BraceTargetMachine &getBraceTargetMachine() const {
    return getTM<BraceTargetMachine>();
  }

  void addIRPasses() override {
    // Reject the incoming optimized module before any generic code-generation
    // IR pass can erase an unsupported construct (for example an unreachable
    // block).  The selector and post-RA publication gate remain closed over
    // any later target-independent rewrite.
    addPass(createBraceS3IRVerifierPass(getBraceTargetMachine()));
    TargetPassConfig::addIRPasses();
  }

  bool addInstSelector() override {
    addPass(createBraceISelDag(getBraceTargetMachine()));
    return false;
  }

  // S2 operation order is architectural fuel/location order.  Keep the
  // checkpoint pipeline intentionally free of target-independent MI motion.
  void addMachineSSAOptimization() override {}
  void addMachineLateOptimization() override {}
  void addBlockPlacement() override {}

  void addPreEmitPass2() override {
    addPass(createMachineVerifierPass("Before Brace S3b.3 publication"));
    addPass(createBraceFinalizeBranchesPass());
    addPass(createMachineVerifierPass("After Brace S3b.3 publication"));
  }
};

} // namespace

BraceTargetMachine::BraceTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               RM.value_or(Reloc::Static),
                               CM.value_or(CodeModel::Small), OL),
      Subtarget(TT, CPU, FS, *this),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  const StringRef ABI = Options.MCOptions.getABIName();
  SdagLeafABI = ABI == SdagLeafABIName;
  UnsupportedConfiguration =
      (RM && *RM != Reloc::Static) || (CM && *CM != CodeModel::Small) || JIT ||
      OL != CodeGenOptLevel::Less || (!CPU.empty() && CPU != "generic") ||
      !FS.empty() || (!ABI.empty() && ABI != SdagLeafABIName);
  initAsmInfo();
  setFastISel(false);
  setO0WantsFastISel(false);
}

BraceTargetMachine::~BraceTargetMachine() = default;

TargetPassConfig *BraceTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new BracePassConfig(*this, PM);
}

bool BraceTargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  if (DwoOut || DisableVerify || UnsupportedConfiguration)
    return true;
  if (SdagLeafABI) {
    if (FileType != CodeGenFileType::ObjectFile)
      return true;
    return CodeGenTargetMachineImpl::addPassesToEmitFile(
        PM, Out, DwoOut, FileType, /*DisableVerify=*/false, MMIWP);
  }
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

Expected<std::unique_ptr<MCStreamer>>
BraceTargetMachine::createMCStreamer(raw_pwrite_stream &Out,
                                     raw_pwrite_stream *DwoOut,
                                     CodeGenFileType FileType, MCContext &Ctx) {
  if (!SdagLeafABI || DwoOut || FileType != CodeGenFileType::ObjectFile)
    return createStringError(
        "brace64 S3b.3 only admits target-local S2 object emission");

  std::unique_ptr<MCObjectWriter> Writer = Brace::createS2ObjectWriter(Out);
  MCStreamer *Streamer = getTarget().createMCObjectStreamer(
      getTargetTriple(), Ctx, /*Backend=*/nullptr, std::move(Writer),
      /*Emitter=*/nullptr, *getMCSubtargetInfo());
  if (!Streamer)
    return createStringError("brace64 S3b.3 S2 streamer creation failed");
  return std::unique_ptr<MCStreamer>(Streamer);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeBraceTarget() {
  RegisterTargetMachine<BraceTargetMachine> X(getTheBraceTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeBraceDAGToDAGISelLegacyPass(PR);
  initializeBraceFinalizeBranchesLegacyPass(PR);
}
