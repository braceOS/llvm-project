//===--- Brace.h - Declare Brace target feature support --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the experimental Brace TargetInfo object.  This frontend
// target intentionally does not imply the presence of a TargetMachine, MC
// layer, assembler, linker, or finalized object ABI.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_BRACE_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_BRACE_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY Brace64TargetInfo final : public TargetInfo {
  std::string ABI;

public:
  Brace64TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    NoAsmVariants = true;
    TLSSupported = false;

    LongWidth = LongAlign = 64;
    LongLongWidth = LongLongAlign = 64;
    PointerWidth = PointerAlign = 64;
    Int128Align = 128;
    SuitableAlign = 128;

    SizeType = UnsignedLong;
    PtrDiffType = SignedLong;
    IntPtrType = SignedLong;
    IntMaxType = SignedLong;
    Int64Type = SignedLong;

    resetDataLayout("e-m:e-p:64:64-i64:64-i128:128-n32:64-S128");
  }

  void getTargetDefines(const LangOptions &,
                        MacroBuilder &Builder) const override;

  bool hasFeature(StringRef Feature) const override {
    return Feature == "brace64";
  }

  bool isValidCPUName(StringRef Name) const override {
    return Name == "generic";
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    Values.emplace_back("generic");
  }

  bool setCPU(const std::string &Name) override { return isValidCPUName(Name); }

  StringRef getABI() const override { return ABI; }

  bool setABI(const std::string &Name) override {
    if (Name != "brace-system-s2-leaf-r0" &&
        Name != "brace-system-s2-leaf-home-r0")
      return false;
    ABI = Name;
    return true;
  }

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    return TargetInfo::VoidPtrBuiltinVaList;
  }

  ArrayRef<const char *> getGCCRegNames() const override { return {}; }

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&,
                             TargetInfo::ConstraintInfo &) const override {
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  CallingConvCheckResult checkCallingConvention(CallingConv CC) const override {
    return CC == CC_C ? CCCR_OK : CCCR_Warning;
  }

  bool hasBitIntType() const override { return true; }
};

} // namespace targets
} // namespace clang

#endif // LLVM_CLANG_LIB_BASIC_TARGETS_BRACE_H
