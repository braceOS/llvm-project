//===-- Brace.h - Brace target interfaces -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACE_H
#define LLVM_LIB_TARGET_BRACE_BRACE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class BraceTargetMachine;
class FunctionPass;
class MachineFunction;
class Module;
class ModulePass;
class PassRegistry;
class raw_pwrite_stream;
class TargetMachine;

// This target-private marker is deliberately absent from MIR/YAML and is not
// a semantic frame declaration.  A deserialized MachineFunction therefore
// starts unverified even when it was printed immediately after the independent
// S3b.7c final-publication pass.
class BraceMachineFunctionInfo final : public MachineFunctionInfo {
  bool FinalByteFramePublicationVerified = false;

public:
  BraceMachineFunctionInfo(const Function &, const TargetSubtargetInfo *) {}

  bool isFinalByteFramePublicationVerified() const {
    return FinalByteFramePublicationVerified;
  }
  void markFinalByteFramePublicationVerified() {
    FinalByteFramePublicationVerified = true;
  }
};

ModulePass *createBraceS2WriterPass(TargetMachine &TM, raw_pwrite_stream &Out);
ModulePass *createBraceS3IRVerifierPass(const BraceTargetMachine &TM);
void verifyBraceS3IRModule(const Module &M, StringRef RequiredABI);
bool verifyBraceS3ByteFrameIRAndRequiresRootFrame(const Module &M);
void verifyBraceS3LateModuleEnvelope(const Module &M, StringRef RequiredABI);
void verifyBraceS3FinalMachineFunctionEnvelope(MachineFunction &MF,
                                               bool AllowsHomes,
                                               bool DirectCall, bool ByteFrame,
                                               StringRef RequiredABI);
FunctionPass *createBraceISelDag(BraceTargetMachine &TM);
FunctionPass *createBraceFinalizeSpillHomesPass();
FunctionPass *createBraceVerifyPostHomeFramePass();
FunctionPass *createBraceFinalizeByteFramePass();
FunctionPass *createBraceVerifyPostByteFramePass();
FunctionPass *createBraceByteFrameMachineVerifierPass(StringRef Banner);
FunctionPass *createBraceFinalizeBranchesPass(const BraceTargetMachine &TM);
FunctionPass *createBraceVerifyFinalByteFramePublicationPass();

void initializeBraceDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBraceFinalizeSpillHomesLegacyPass(PassRegistry &);
void initializeBraceVerifyPostHomeFrameLegacyPass(PassRegistry &);
void initializeBraceFinalizeByteFrameLegacyPass(PassRegistry &);
void initializeBraceVerifyPostByteFrameLegacyPass(PassRegistry &);
void initializeBraceFinalizeBranchesLegacyPass(PassRegistry &);
void initializeBraceVerifyFinalByteFramePublicationLegacyPass(PassRegistry &);

inline constexpr StringLiteral BraceSdagLeafABIName = "brace-system-s2-leaf-r0";
inline constexpr StringLiteral BraceSdagLeafHomeABIName =
    "brace-system-s2-leaf-home-r0";
inline constexpr StringLiteral BraceSdagDirectCallABIName =
    "brace-system-s2-direct-call-r0";
inline constexpr StringLiteral BraceSdagDirectCallHomeABIName =
    "brace-system-s2-direct-call-home-r0";
inline constexpr StringLiteral BraceSdagDirectCallByteFrameABIName =
    "brace-system-s2-direct-call-byte-frame-r0";
inline constexpr StringLiteral BraceSdagLeafHomeCompilerIdentity =
    "brace.exp.llvm22.brace64.system.r32-physical.sdag-leaf-spill-home@0";
inline constexpr StringLiteral BraceSdagLeafHomeCodegenProfile =
    "brace.exp.llvm22.brace64.system.leaf-spill-home-codegen@0";
inline constexpr StringLiteral BraceSdagDirectCallCompilerIdentity =
    "brace.exp.llvm22.brace64.system.r32-physical.sdag-direct-call@0";
inline constexpr StringLiteral BraceSdagDirectCallCodegenProfile =
    "brace.exp.llvm22.brace64.system.direct-call-codegen@0";
inline constexpr StringLiteral BraceSdagDirectCallHomeCompilerIdentity =
    "brace.exp.llvm22.brace64.system.r32-physical.sdag-direct-call-home@0";
inline constexpr StringLiteral BraceSdagDirectCallHomeCodegenProfile =
    "brace.exp.llvm22.brace64.system.direct-call-home-codegen@0";
inline constexpr StringLiteral BraceSdagDirectCallByteFrameCompilerIdentity =
    "brace.exp.llvm22.brace64.system.r32-physical.sdag-direct-call-byte-frame@"
    "0";
inline constexpr StringLiteral BraceSdagDirectCallByteFrameCodegenProfile =
    "brace.exp.llvm22.brace64.system.direct-call-byte-frame-codegen@0";

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACE_H
