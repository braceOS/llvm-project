//===-- BraceTargetInfo.cpp - Brace target registration ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BraceTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

Target &llvm::getTheBraceTarget() {
  static Target TheBraceTarget;
  return TheBraceTarget;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBraceTargetInfo() {
  RegisterTarget<Triple::brace64, false> X(
      getTheBraceTarget(), "brace64", "Brace 64-bit experimental System target",
      "Brace");
}
