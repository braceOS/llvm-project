//===-- Brace.h - Brace target interfaces -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACE_H
#define LLVM_LIB_TARGET_BRACE_BRACE_H

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
void verifyBraceS3IRModule(const Module &M);
void verifyBraceS3LateModuleEnvelope(const Module &M);
FunctionPass *createBraceISelDag(BraceTargetMachine &TM);
FunctionPass *createBraceFinalizeBranchesPass();

void initializeBraceDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBraceFinalizeBranchesLegacyPass(PassRegistry &);

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACE_H
