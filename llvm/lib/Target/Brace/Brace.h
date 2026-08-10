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

namespace llvm {

class BraceTargetMachine;
class FunctionPass;
class Module;
class ModulePass;
class PassRegistry;
class raw_pwrite_stream;
class TargetMachine;

ModulePass *createBraceS2WriterPass(TargetMachine &TM, raw_pwrite_stream &Out);
ModulePass *createBraceS3IRVerifierPass(const BraceTargetMachine &TM);
void verifyBraceS3IRModule(const Module &M, StringRef RequiredABI);
void verifyBraceS3LateModuleEnvelope(const Module &M, StringRef RequiredABI);
FunctionPass *createBraceISelDag(BraceTargetMachine &TM);
FunctionPass *createBraceFinalizeSpillHomesPass();
FunctionPass *createBraceFinalizeBranchesPass(const BraceTargetMachine &TM);

void initializeBraceDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBraceFinalizeSpillHomesLegacyPass(PassRegistry &);
void initializeBraceFinalizeBranchesLegacyPass(PassRegistry &);

inline constexpr StringLiteral BraceSdagLeafABIName = "brace-system-s2-leaf-r0";
inline constexpr StringLiteral BraceSdagLeafHomeABIName =
    "brace-system-s2-leaf-home-r0";
inline constexpr StringLiteral BraceSdagLeafHomeCompilerIdentity =
    "brace.exp.llvm22.brace64.system.r32-physical.sdag-leaf-spill-home@0";
inline constexpr StringLiteral BraceSdagLeafHomeCodegenProfile =
    "brace.exp.llvm22.brace64.system.leaf-spill-home-codegen@0";

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACE_H
