//===--- Brace.cpp - Implement Brace target feature support --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Brace.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

void Brace64TargetInfo::getTargetDefines(const LangOptions &,
                                         MacroBuilder &Builder) const {
  Builder.defineMacro("__brace__", "1");
  Builder.defineMacro("__brace64__", "1");
}
