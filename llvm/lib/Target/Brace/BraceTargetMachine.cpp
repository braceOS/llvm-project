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
#include "llvm/ADT/StringSwitch.h"
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

StringRef getCodeGenPassLimitOption(StringRef OptionName) {
  auto &Options = cl::getRegisteredOptions();
  const auto Found = Options.find(OptionName);
  if (Found == Options.end())
    report_fatal_error("brace64 S3b.7c byte-frame selector: missing " +
                       OptionName + " option");
  return static_cast<cl::opt<std::string> *>(Found->second)->getValue();
}

bool isRegisteredByteFrameRestartSeam(StringRef PassName) {
  return StringSwitch<bool>(PassName)
      .Cases({"finalize-isel", "virtregrewriter"}, true)
      .Cases({"stack-slot-coloring", "brace-finalize-byte-frame"}, true)
      .Case("brace-finalize-branches", true)
      .Default(false);
}

struct FixedLocalPassLimitOption {
  StringRef Value;
  unsigned Occurrences;
};

FixedLocalPassLimitOption
getFixedLocalCodeGenPassLimitOption(StringRef OptionName) {
  auto &Options = cl::getRegisteredOptions();
  const auto Found = Options.find(OptionName);
  if (Found == Options.end())
    report_fatal_error("brace64 S3b.8 fixed-local selector: missing " +
                       OptionName + " option");
  const auto *Option = static_cast<cl::opt<std::string> *>(Found->second);
  return {Option->getValue(),
          static_cast<unsigned>(Option->getNumOccurrences())};
}

bool isRegisteredFixedLocalRestartSeam(StringRef PassName) {
  return StringSwitch<bool>(PassName)
      .Cases({"finalize-isel", "virtregrewriter"}, true)
      .Cases({"stack-slot-coloring", "brace-finalize-fixed-local-byte-frame"},
             true)
      .Case("brace-finalize-branches", true)
      .Default(false);
}

bool isRegisteredFixedLocalAuditStop(StringRef PassName) {
  return PassName == "brace-verify-final-fixed-local-publication";
}

bool isRegisteredFixedLocalPropagationPair(StringRef StartAfter,
                                           StringRef StopAfter) {
  return (StartAfter == "finalize-isel" && StopAfter == "virtregrewriter") ||
         (StartAfter == "virtregrewriter" &&
          StopAfter == "stack-slot-coloring") ||
         (StartAfter == "stack-slot-coloring" &&
          StopAfter == "brace-finalize-fixed-local-byte-frame") ||
         (StartAfter == "brace-finalize-fixed-local-byte-frame" &&
          StopAfter == "brace-finalize-branches");
}

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

  void addPostRegAlloc() override {
    if (getBraceTargetMachine().usesSdagLeafHomeABI() ||
        getBraceTargetMachine().usesSdagDirectCallHomeABI()) {
      addPass(createMachineVerifierPass(
          "Before Brace S3b.4 spill-home finalization"));
      addPass(createBraceFinalizeSpillHomesPass());
      addPass(createMachineVerifierPass(
          "After Brace S3b.4 spill-home finalization"));
      addPass(createBraceVerifyPostHomeFramePass());
    } else if (getBraceTargetMachine().usesSdagDirectCallByteFrameABI()) {
      addPass(createBraceByteFrameMachineVerifierPass(
          "Before Brace S3b.7c byte-frame finalization"));
      addPass(createBraceFinalizeByteFramePass());
      addPass(createBraceByteFrameMachineVerifierPass(
          "After Brace S3b.7c byte-frame finalization"));
      addPass(createBraceVerifyPostByteFramePass());
    } else if (getBraceTargetMachine()
                   .usesSdagDirectCallByteFrameFixedLocalABI()) {
      addPass(createBraceFixedLocalMachineVerifierPass(
          "Before Brace S3b.8 fixed-local byte-frame finalization"));
      addPass(createBraceFinalizeFixedLocalPass());
      addPass(createBraceFixedLocalMachineVerifierPass(
          "After Brace S3b.8 fixed-local byte-frame finalization"));
      addPass(createBraceVerifyPostFixedLocalPass());
    }
  }

  void addPreEmitPass2() override {
    const bool Homes = getBraceTargetMachine().usesSdagSpillHomes();
    const bool ByteFrame =
        getBraceTargetMachine().usesSdagDirectCallByteFrameABI();
    const bool FixedLocal =
        getBraceTargetMachine().usesSdagDirectCallByteFrameFixedLocalABI();
    addPass(createMachineVerifierPass(
        FixedLocal  ? "Before Brace S3b.8 publication"
        : ByteFrame ? "Before Brace S3b.7c publication"
        : Homes     ? "Before Brace S3b.4 publication"
                    : "Before Brace S3b.3 publication"));
    addPass(createBraceFinalizeBranchesPass(getBraceTargetMachine()));
    addPass(
        createMachineVerifierPass(FixedLocal  ? "After Brace S3b.8 publication"
                                  : ByteFrame ? "After Brace S3b.7c publication"
                                  : Homes ? "After Brace S3b.4 publication"
                                          : "After Brace S3b.3 publication"));
    if (ByteFrame)
      addPass(createBraceVerifyFinalByteFramePublicationPass());
    else if (FixedLocal)
      addPass(createBraceVerifyFinalFixedLocalPublicationPass());
  }
};

} // namespace

BraceTargetMachine::BraceTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(
          T, TT.computeDataLayout(Options.MCOptions.getABIName()), TT, CPU, FS,
          Options, RM.value_or(Reloc::Static), CM.value_or(CodeModel::Small),
          OL),
      Subtarget(TT, CPU, FS, *this),
      TLOF(std::make_unique<TargetLoweringObjectFileELF>()) {
  const StringRef ABI = Options.MCOptions.getABIName();
  if (ABI == BraceSdagLeafABIName)
    SdagABI = SdagABIKind::Leaf;
  else if (ABI == BraceSdagLeafHomeABIName)
    SdagABI = SdagABIKind::LeafHome;
  else if (ABI == BraceSdagDirectCallABIName)
    SdagABI = SdagABIKind::DirectCall;
  else if (ABI == BraceSdagDirectCallHomeABIName)
    SdagABI = SdagABIKind::DirectCallHome;
  else if (ABI == BraceSdagDirectCallByteFrameABIName)
    SdagABI = SdagABIKind::DirectCallByteFrame;
  else if (ABI == BraceSdagDirectCallByteFrameFixedLocalABIName)
    SdagABI = SdagABIKind::DirectCallByteFrameFixedLocal;
  UnsupportedConfiguration =
      (RM && *RM != Reloc::Static) ||
      ((SdagABI == SdagABIKind::DirectCall ||
        SdagABI == SdagABIKind::DirectCallHome ||
        SdagABI == SdagABIKind::DirectCallByteFrame ||
        SdagABI == SdagABIKind::DirectCallByteFrameFixedLocal) &&
       CM.has_value()) ||
      (CM && *CM != CodeModel::Small) || JIT || OL != CodeGenOptLevel::Less ||
      (!CPU.empty() && CPU != "generic") || !FS.empty() ||
      (!ABI.empty() && ABI != BraceSdagLeafABIName &&
       ABI != BraceSdagLeafHomeABIName && ABI != BraceSdagDirectCallABIName &&
       ABI != BraceSdagDirectCallHomeABIName &&
       ABI != BraceSdagDirectCallByteFrameABIName &&
       ABI != BraceSdagDirectCallByteFrameFixedLocalABIName);
  initAsmInfo();
  setFastISel(false);
  setO0WantsFastISel(false);
}

BraceTargetMachine::~BraceTargetMachine() = default;

MachineFunctionInfo *BraceTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return MachineFunctionInfo::create<BraceMachineFunctionInfo>(Allocator, F,
                                                                STI);
}

StringRef BraceTargetMachine::getSdagABIName() const {
  switch (SdagABI) {
  case SdagABIKind::None:
    return {};
  case SdagABIKind::Leaf:
    return BraceSdagLeafABIName;
  case SdagABIKind::LeafHome:
    return BraceSdagLeafHomeABIName;
  case SdagABIKind::DirectCall:
    return BraceSdagDirectCallABIName;
  case SdagABIKind::DirectCallHome:
    return BraceSdagDirectCallHomeABIName;
  case SdagABIKind::DirectCallByteFrame:
    return BraceSdagDirectCallByteFrameABIName;
  case SdagABIKind::DirectCallByteFrameFixedLocal:
    return BraceSdagDirectCallByteFrameFixedLocalABIName;
  }
  llvm_unreachable("unknown Brace SelectionDAG ABI");
}

TargetPassConfig *BraceTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new BracePassConfig(*this, PM);
}

bool BraceTargetMachine::addPassesToEmitFile(
    PassManagerBase &PM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, bool DisableVerify,
    MachineModuleInfoWrapperPass *MMIWP) {
  if (DwoOut || DisableVerify || UnsupportedConfiguration)
    return true;
  if (usesSdagDirectCallByteFrameABI()) {
    const StringRef StartBefore =
        getCodeGenPassLimitOption("start-before");
    if (!StartBefore.empty())
      report_fatal_error(
          "brace64 S3b.7c byte-frame selector: -start-before=" +
          StartBefore + " is not an admitted restart boundary");
    const StringRef StartAfter = getCodeGenPassLimitOption("start-after");
    if (!StartAfter.empty() &&
        !isRegisteredByteFrameRestartSeam(StartAfter))
      report_fatal_error(
          "brace64 S3b.7c byte-frame selector: -start-after=" + StartAfter +
          " is not one of the five registered restart seams");
  }
  if (usesSdagDirectCallByteFrameFixedLocalABI()) {
    const FixedLocalPassLimitOption StartBefore =
        getFixedLocalCodeGenPassLimitOption("start-before");
    const FixedLocalPassLimitOption StartAfter =
        getFixedLocalCodeGenPassLimitOption("start-after");
    const FixedLocalPassLimitOption StopBefore =
        getFixedLocalCodeGenPassLimitOption("stop-before");
    const FixedLocalPassLimitOption StopAfter =
        getFixedLocalCodeGenPassLimitOption("stop-after");
    if (StartBefore.Occurrences > 1)
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -start-before is specified "
          "more than once");
    if (StartAfter.Occurrences > 1)
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -start-after is specified "
          "more than once");
    if (StopBefore.Occurrences > 1)
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -stop-before is specified "
          "more than once");
    if (StopAfter.Occurrences > 1)
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -stop-after is specified more "
          "than once");
    if ((StartBefore.Occurrences != 0 || StartAfter.Occurrences != 0) &&
        (StopBefore.Occurrences != 0 || StopAfter.Occurrences != 0) &&
        !(StartBefore.Occurrences == 0 && StopBefore.Occurrences == 0 &&
          StartAfter.Occurrences == 1 && StopAfter.Occurrences == 1 &&
          isRegisteredFixedLocalPropagationPair(StartAfter.Value,
                                                StopAfter.Value)))
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: start/stop pass-limit pair is "
          "not one of the four registered forward propagation pairs");
    if (StartBefore.Occurrences != 0)
      report_fatal_error("brace64 S3b.8 fixed-local selector: -start-before=" +
                         StartBefore.Value +
                         " is not an admitted restart boundary");
    if (StartAfter.Occurrences == 1 && StartAfter.Value.empty())
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -start-after requires a "
          "nonempty pass name");
    if (StartAfter.Occurrences == 1 &&
        !isRegisteredFixedLocalRestartSeam(StartAfter.Value))
      report_fatal_error("brace64 S3b.8 fixed-local selector: -start-after=" +
                         StartAfter.Value +
                         " is not one of the five registered restart seams");
    if (StopBefore.Occurrences != 0)
      report_fatal_error("brace64 S3b.8 fixed-local selector: -stop-before=" +
                         StopBefore.Value +
                         " is not an admitted stop boundary");
    if (StopAfter.Occurrences == 1 && StopAfter.Value.empty())
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -stop-after requires a "
          "nonempty pass name");
    if (StopAfter.Occurrences == 1 &&
        !isRegisteredFixedLocalRestartSeam(StopAfter.Value) &&
        !isRegisteredFixedLocalAuditStop(StopAfter.Value))
      report_fatal_error(
          "brace64 S3b.8 fixed-local selector: -stop-after=" + StopAfter.Value +
          " is neither one of the five registered restart seams nor the "
          "audit-only final-publication stop");
  }
  if (usesSdagABI()) {
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
  if (!usesSdagABI() || DwoOut || FileType != CodeGenFileType::ObjectFile)
    return createStringError(
        "brace64 SelectionDAG profiles only admit target-local S2 object "
        "emission");

  Brace::S2ObjectMode Mode = Brace::S2ObjectMode::Legacy;
  if (usesSdagDirectCallABI())
    Mode = Brace::S2ObjectMode::DirectCall;
  else if (usesSdagDirectCallHomeABI())
    Mode = Brace::S2ObjectMode::DirectCallHome;
  else if (usesSdagDirectCallByteFrameABI())
    Mode = Brace::S2ObjectMode::DirectCallByteFrame;
  else if (usesSdagDirectCallByteFrameFixedLocalABI())
    Mode = Brace::S2ObjectMode::DirectCallByteFrameFixedLocal;
  std::unique_ptr<MCObjectWriter> Writer =
      Brace::createS2ObjectWriter(Out, Mode);
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
  initializeBraceFinalizeSpillHomesLegacyPass(PR);
  initializeBraceVerifyPostHomeFrameLegacyPass(PR);
  initializeBraceFinalizeByteFrameLegacyPass(PR);
  initializeBraceVerifyPostByteFrameLegacyPass(PR);
  initializeBraceFinalizeBranchesLegacyPass(PR);
  initializeBraceVerifyFinalByteFramePublicationLegacyPass(PR);
  initializeBraceFinalizeFixedLocalLegacyPass(PR);
  initializeBraceVerifyPostFixedLocalLegacyPass(PR);
  initializeBraceVerifyFinalFixedLocalPublicationLegacyPass(PR);
}
