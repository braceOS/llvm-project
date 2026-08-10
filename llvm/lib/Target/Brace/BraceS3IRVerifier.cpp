//===-- BraceS3IRVerifier.cpp - S3b.3 leaf IR trust boundary -------------===//

#include "Brace.h"
#include "BraceTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ModRef.h"
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "brace-s3-ir-verifier"

namespace {

constexpr StringLiteral RequiredTriple = "brace64-unknown-none-elf";
constexpr StringLiteral RequiredDataLayout =
    "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";

[[noreturn]] void reject(const Twine &Message) {
  report_fatal_error("brace64 S3b.3 leaf ABI: " + Message);
}

[[noreturn]] void rejectDirect(const Twine &Message) {
  report_fatal_error("brace64 S3b.5 direct-call ABI: " + Message);
}

[[noreturn]] void rejectDirectHome(const Twine &Message) {
  report_fatal_error("brace64 S3b.6 direct-call-home ABI: " + Message);
}

bool isDirectCallABIName(StringRef ABI) {
  return ABI == BraceSdagDirectCallABIName ||
         ABI == BraceSdagDirectCallHomeABIName;
}

bool metadataI32Equals(const Metadata *MD, uint32_t Expected) {
  const auto *Wrapped = dyn_cast_or_null<ConstantAsMetadata>(MD);
  const auto *Integer =
      Wrapped ? dyn_cast<ConstantInt>(Wrapped->getValue()) : nullptr;
  return Integer && Integer->getBitWidth() == 32 &&
         Integer->getZExtValue() == Expected;
}

bool metadataStringEquals(const Metadata *MD, StringRef Expected) {
  const auto *String = dyn_cast_or_null<MDString>(MD);
  return String && String->getString() == Expected;
}

bool canonicalModuleFlags(const NamedMDNode &Named, StringRef RequiredABI) {
  if (Named.getNumOperands() != 2)
    return false;
  auto IsFlag = [](const MDNode *Node, StringRef Key, auto ValuePredicate) {
    return Node && isa<MDTuple>(Node) && Node->isUniqued() &&
           Node->getNumOperands() == 3 &&
           metadataI32Equals(Node->getOperand(0).get(), Module::Error) &&
           metadataStringEquals(Node->getOperand(1).get(), Key) &&
           ValuePredicate(Node->getOperand(2).get());
  };
  return IsFlag(Named.getOperand(0), "wchar_size",
                [](const Metadata *MD) { return metadataI32Equals(MD, 4); }) &&
         IsFlag(Named.getOperand(1), "target-abi", [&](const Metadata *MD) {
           return metadataStringEquals(MD, RequiredABI);
         });
}

bool canonicalLoopMetadata(const MDNode *Loop) {
  if (!Loop || !Loop->isDistinct() || Loop->getNumOperands() != 3 ||
      Loop->getOperand(0).get() != Loop)
    return false;
  auto IsLeaf = [](const Metadata *MD, StringRef Expected) {
    const auto *Node = dyn_cast_or_null<MDNode>(MD);
    return Node && Node->isUniqued() && Node->getNumOperands() == 1 &&
           metadataStringEquals(Node->getOperand(0).get(), Expected);
  };
  return IsLeaf(Loop->getOperand(1).get(), "llvm.loop.mustprogress") &&
         IsLeaf(Loop->getOperand(2).get(), "llvm.loop.unroll.disable");
}

bool allowedStringAttribute(const Attribute &A) {
  const StringRef Kind = A.getKindAsString();
  const StringRef Value = A.getValueAsString();
  return (Kind == "no-builtins" && Value.empty()) ||
         (Kind == "no-trapping-math" && Value == "true") ||
         (Kind == "stack-protector-buffer-size" && Value == "8");
}

std::optional<uint64_t> directPhysicalAddress(const Value *Pointer) {
  const auto *Expression = dyn_cast<ConstantExpr>(Pointer);
  if (!Expression || Expression->getOpcode() != Instruction::IntToPtr ||
      Expression->getNumOperands() != 1 ||
      Expression->getType()->getPointerAddressSpace() != 200)
    return std::nullopt;
  const auto *Integer = dyn_cast<ConstantInt>(Expression->getOperand(0));
  if (!Integer || Integer->getBitWidth() != 64)
    return std::nullopt;
  return Integer->getZExtValue();
}

void checkMemoryShape(Type *Type, const Value *Pointer, Align Alignment) {
  if (!Type->isIntegerTy(8) && !Type->isIntegerTy(32))
    reject("physical memory width must be i8 or i32");
  const std::optional<uint64_t> Address = directPhysicalAddress(Pointer);
  if (!Address)
    reject("physical pointer must be a direct i64 addrspace(200) inttoptr");
  const uint64_t Width = Type->getIntegerBitWidth() / 8;
  const uint64_t DeclaredAlignment = Alignment.value();
  if ((*Address % Width) != 0 || DeclaredAlignment < Width ||
      (*Address % DeclaredAlignment) != 0)
    reject("physical memory access is not naturally aligned");
}

void checkMemory(const LoadInst &Load) {
  if (!Load.isVolatile() || Load.isAtomic())
    reject("loads must be volatile direct addrspace(200) i8/i32 accesses");
  checkMemoryShape(Load.getType(), Load.getPointerOperand(), Load.getAlign());
}

void checkMemory(const StoreInst &Store) {
  Type *Type = Store.getValueOperand()->getType();
  if (!Store.isVolatile() || Store.isAtomic())
    reject("stores must be volatile direct addrspace(200) i8/i32 accesses");
  checkMemoryShape(Type, Store.getPointerOperand(), Store.getAlign());
}

void checkDirectMemoryShape(Type *Type, const Value *Pointer, Align Alignment) {
  if (!Type->isIntegerTy(8) && !Type->isIntegerTy(32))
    rejectDirect("physical memory width must be i8 or i32");
  const std::optional<uint64_t> Address = directPhysicalAddress(Pointer);
  if (!Address)
    rejectDirect(
        "physical pointer must be a direct i64 addrspace(200) inttoptr");
  const uint64_t Width = Type->getIntegerBitWidth() / 8;
  const uint64_t DeclaredAlignment = Alignment.value();
  if ((*Address % Width) != 0 || DeclaredAlignment < Width ||
      (*Address % DeclaredAlignment) != 0)
    rejectDirect("physical memory access is not naturally aligned");
}

void checkDirectMemory(const LoadInst &Load) {
  if (!Load.isVolatile() || Load.isAtomic())
    rejectDirect(
        "loads must be volatile direct addrspace(200) i8/i32 accesses");
  checkDirectMemoryShape(Load.getType(), Load.getPointerOperand(),
                         Load.getAlign());
}

void checkDirectMemory(const StoreInst &Store) {
  if (!Store.isVolatile() || Store.isAtomic())
    rejectDirect(
        "stores must be volatile direct addrspace(200) i8/i32 accesses");
  checkDirectMemoryShape(Store.getValueOperand()->getType(),
                         Store.getPointerOperand(), Store.getAlign());
}

void checkFunctionAttributes(const Function &F) {
  const AttributeList &Attrs = F.getAttributes();
  const AttributeSet FnAttrs = Attrs.getFnAttrs();
  auto StringAttributeEquals = [&](StringRef Kind, StringRef Value) {
    const Attribute A = F.getFnAttribute(Kind);
    return A.isStringAttribute() && A.getValueAsString() == Value;
  };
  if (Attrs.getRetAttrs().getNumAttributes() != 0 ||
      FnAttrs.getNumAttributes() != 7 || !F.hasFnAttribute(Attribute::NoFree) ||
      !F.hasFnAttribute(Attribute::NoRecurse) ||
      !F.hasFnAttribute(Attribute::NoUnwind) ||
      !StringAttributeEquals("no-builtins", "") ||
      !StringAttributeEquals("no-trapping-math", "true") ||
      !StringAttributeEquals("stack-protector-buffer-size", "8"))
    reject("function attribute set mismatch");

  const MemoryEffects ExpectedMemory =
      MemoryEffects::unknown()
          .getWithoutLoc(IRMemLocation::TargetMem0)
          .getWithoutLoc(IRMemLocation::TargetMem1);
  for (Attribute A : FnAttrs) {
    if (A.isStringAttribute()) {
      if (!allowedStringAttribute(A))
        reject("unsupported string function attribute");
      continue;
    }
    const Attribute::AttrKind Kind = A.getKindAsEnum();
    if (A.isEnumAttribute() &&
        (Kind == Attribute::NoFree || Kind == Attribute::NoRecurse ||
         Kind == Attribute::NoUnwind))
      continue;
    if (A.isIntAttribute() && Kind == Attribute::Memory &&
        MemoryEffects::createFromIntValue(A.getValueAsInt()) == ExpectedMemory)
      continue;
    reject("unsupported enum or integer function attribute");
  }
}

void checkInstructionMetadata(const Instruction &Instruction) {
  if (Instruction.getDebugLoc() || Instruction.hasDbgRecords())
    reject("debug records are not admitted");
  SmallVector<std::pair<unsigned, MDNode *>, 2> Metadata;
  Instruction.getAllMetadataOtherThanDebugLoc(Metadata);
  if (Metadata.empty())
    return;
  const auto *Branch = dyn_cast<BranchInst>(&Instruction);
  if (!Branch || Metadata.size() != 1 ||
      Metadata[0].first != LLVMContext::MD_loop ||
      !canonicalLoopMetadata(Metadata[0].second) ||
      llvm::none_of(successors(Branch->getParent()),
                    [&](const BasicBlock *Successor) {
                      return Successor == Branch->getParent();
                    }))
    reject("instruction metadata is outside the finite leaf envelope");
}

void checkDirectInstructionMetadata(const Instruction &Instruction) {
  if (Instruction.getDebugLoc() || Instruction.hasDbgRecords())
    rejectDirect("debug records are not admitted");
  SmallVector<std::pair<unsigned, MDNode *>, 2> Metadata;
  Instruction.getAllMetadataOtherThanDebugLoc(Metadata);
  if (Metadata.empty())
    return;
  const auto *Branch = dyn_cast<BranchInst>(&Instruction);
  if (!Branch || Metadata.size() != 1 ||
      Metadata[0].first != LLVMContext::MD_loop ||
      !canonicalLoopMetadata(Metadata[0].second) ||
      llvm::none_of(successors(Branch->getParent()),
                    [&](const BasicBlock *Successor) {
                      return Successor == Branch->getParent();
                    }))
    rejectDirect(
        "instruction metadata is outside the finite direct-call envelope");
}

void checkFunctionHeader(const Function &F) {
  if (F.getName() != "brace_system_entry" || F.isDeclaration() ||
      !F.hasExternalLinkage() || F.getCallingConv() != CallingConv::C ||
      F.getAddressSpace() != 0 || !F.arg_empty() || F.isVarArg() ||
      !F.getReturnType()->isVoidTy())
    reject("requires one external C void brace_system_entry(void)");
  if (!F.isDSOLocal() || F.getVisibility() != GlobalValue::DefaultVisibility ||
      F.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
      F.getUnnamedAddr() != GlobalValue::UnnamedAddr::Local)
    reject("entry visibility, storage, or unnamed-address state mismatch");
  if (F.hasPersonalityFn() || F.hasGC() || F.hasPrefixData() ||
      F.hasPrologueData() || F.hasSection() || F.hasComdat() || F.getAlign() ||
      !F.getPartition().empty() || F.getSectionPrefix().has_value() ||
      F.hasSanitizerMetadata() || F.hasMetadata())
    reject("entry decoration is outside the leaf ABI");
  checkFunctionAttributes(F);
}

void checkFunction(const Function &F) {
  checkFunctionHeader(F);
  if (F.size() == 0 || F.size() > 4)
    reject("basic-block count is outside 1..4");

  unsigned InstructionCount = 0;
  unsigned EdgeCount = 0;
  unsigned MemoryCount = 0;
  SmallPtrSet<const Value *, 32> CandidateValues;
  SmallPtrSet<const BasicBlock *, 4> Reachable;
  SmallVector<const BasicBlock *, 4> Worklist{&F.getEntryBlock()};
  while (!Worklist.empty()) {
    const BasicBlock *Block = Worklist.pop_back_val();
    if (!Reachable.insert(Block).second)
      continue;
    for (const BasicBlock *Successor : successors(Block))
      Worklist.push_back(Successor);
  }
  if (Reachable.size() != F.size())
    reject("all basic blocks must be reachable from the entry");

  for (const BasicBlock &Block : F)
    for (const Instruction &Instruction : Block)
      if (!Instruction.getType()->isVoidTy() &&
          CandidateValues.insert(&Instruction).second &&
          CandidateValues.size() > 64)
        reject("tracked non-void value count exceeds 64");

  auto IsAdmittedIntegerValue = [&](const Value *Value, Type *ExpectedType) {
    if (Value->getType() != ExpectedType ||
        (!ExpectedType->isIntegerTy(8) && !ExpectedType->isIntegerTy(32)))
      return false;
    if (const auto *Integer = dyn_cast<ConstantInt>(Value))
      return Integer->getBitWidth() == ExpectedType->getIntegerBitWidth();
    const auto *Producer = dyn_cast<Instruction>(Value);
    return Producer && Producer->getFunction() == &F &&
           CandidateValues.contains(Producer);
  };

  for (const BasicBlock &Block : F) {
    if (Block.empty())
      reject("empty IR basic blocks are not admitted");
    EdgeCount += Block.getTerminator()->getNumSuccessors();
    if (EdgeCount > 8)
      reject("CFG edge count exceeds 8");
    for (const Instruction &Instruction : Block) {
      if (++InstructionCount > 128)
        reject("IR instruction count exceeds 128");
      checkInstructionMetadata(Instruction);

      if (const auto *Load = dyn_cast<LoadInst>(&Instruction)) {
        if (++MemoryCount > 64)
          reject("physical memory operand count exceeds 64");
        checkMemory(*Load);
        continue;
      }
      if (const auto *Store = dyn_cast<StoreInst>(&Instruction)) {
        if (++MemoryCount > 64)
          reject("physical memory operand count exceeds 64");
        checkMemory(*Store);
        if (!IsAdmittedIntegerValue(Store->getValueOperand(),
                                    Store->getValueOperand()->getType()))
          reject("store value is outside the admitted integer graph");
        continue;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(&Instruction)) {
        if (Binary->getOpcode() != Instruction::And ||
            (!Binary->getType()->isIntegerTy(8) &&
             !Binary->getType()->isIntegerTy(32)) ||
            !IsAdmittedIntegerValue(Binary->getOperand(0), Binary->getType()) ||
            !IsAdmittedIntegerValue(Binary->getOperand(1), Binary->getType()))
          reject("only i8/i32 integer-and is admitted");
        continue;
      }
      if (const auto *Compare = dyn_cast<ICmpInst>(&Instruction)) {
        const auto *Zero = dyn_cast<ConstantInt>(Compare->getOperand(1));
        if ((Compare->getPredicate() != ICmpInst::ICMP_EQ &&
             Compare->getPredicate() != ICmpInst::ICMP_NE) ||
            !Zero || !Zero->isZero() ||
            (!Compare->getOperand(0)->getType()->isIntegerTy(8) &&
             !Compare->getOperand(0)->getType()->isIntegerTy(32)) ||
            !IsAdmittedIntegerValue(Compare->getOperand(0),
                                    Compare->getOperand(0)->getType()) ||
            !Compare->hasOneUse() || !isa<BranchInst>(*Compare->user_begin()) ||
            cast<BranchInst>(*Compare->user_begin())->getParent() !=
                Compare->getParent())
          reject("comparisons must be one-use i8/i32 eq/ne zero branches");
        continue;
      }
      if (const auto *Branch = dyn_cast<BranchInst>(&Instruction)) {
        if (Branch->isConditional() &&
            (!isa<ICmpInst>(Branch->getCondition()) ||
             Branch->getSuccessor(0) == Branch->getSuccessor(1)))
          reject("conditional branches must consume the admitted comparison");
        continue;
      }
      if (const auto *Return = dyn_cast<ReturnInst>(&Instruction)) {
        if (Return->getReturnValue())
          reject("entry return must be void");
        continue;
      }
      reject("instruction is outside the S3b.3 leaf profile");
    }
  }
}

bool allowedDirectStringAttribute(const Attribute &A) {
  const StringRef Kind = A.getKindAsString();
  const StringRef Value = A.getValueAsString();
  return allowedStringAttribute(A) ||
         (Kind == "disable-tail-calls" && Value == "true");
}

bool hasDirectPhysicalMemory(const Function &F) {
  for (const BasicBlock &Block : F)
    for (const Instruction &I : Block)
      if (isa<LoadInst>(I) || isa<StoreInst>(I))
        return true;
  return false;
}

void checkDirectFunctionAttributes(const Function &F, bool IsHelper,
                                   bool HasPhysicalEffects) {
  const AttributeList &Attrs = F.getAttributes();
  const bool ExpectedNoSync = IsHelper && !HasPhysicalEffects;
  const bool HasMustProgress = F.hasFnAttribute(Attribute::MustProgress);
  const bool HasWillReturn = F.hasFnAttribute(Attribute::WillReturn);
  const bool AllowsFrontendCFGProgressTuple =
      IsHelper && HasPhysicalEffects && F.size() > 1;
  if (!F.hasFnAttribute(Attribute::NoFree) ||
      !F.hasFnAttribute(Attribute::NoRecurse) ||
      !F.hasFnAttribute(Attribute::NoUnwind) ||
      (IsHelper && (!F.hasFnAttribute(Attribute::NoInline) ||
                    HasMustProgress != HasWillReturn ||
                    (!AllowsFrontendCFGProgressTuple && !HasMustProgress))) ||
      (!IsHelper && (F.hasFnAttribute(Attribute::NoInline) || HasMustProgress ||
                     HasWillReturn)) ||
      F.hasFnAttribute(Attribute::NoSync) != ExpectedNoSync)
    rejectDirect("required function attributes are missing");

  auto HasString = [&](StringRef Kind, StringRef Value) {
    const Attribute A = F.getFnAttribute(Kind);
    return A.isStringAttribute() && A.getValueAsString() == Value;
  };
  if (!HasString("disable-tail-calls", "true") ||
      !HasString("no-builtins", "") || !HasString("no-trapping-math", "true") ||
      !HasString("stack-protector-buffer-size", "8"))
    rejectDirect("required string function attributes are missing");

  bool SawMemory = false;
  unsigned EnumCount = 0;
  for (Attribute A : Attrs.getFnAttrs()) {
    if (A.isStringAttribute()) {
      if (!allowedDirectStringAttribute(A))
        rejectDirect("unsupported string function attribute");
      continue;
    }
    const Attribute::AttrKind Kind = A.getKindAsEnum();
    if (A.isEnumAttribute() &&
        (Kind == Attribute::NoFree || Kind == Attribute::NoRecurse ||
         Kind == Attribute::NoUnwind || Kind == Attribute::NoInline ||
         Kind == Attribute::MustProgress || Kind == Attribute::NoSync ||
         Kind == Attribute::WillReturn)) {
      ++EnumCount;
      continue;
    }
    if (A.isIntAttribute() && Kind == Attribute::Memory) {
      const MemoryEffects Actual =
          MemoryEffects::createFromIntValue(A.getValueAsInt());
      const MemoryEffects Expected =
          HasPhysicalEffects ? MemoryEffects::unknown()
                                   .getWithoutLoc(IRMemLocation::TargetMem0)
                                   .getWithoutLoc(IRMemLocation::TargetMem1)
                             : MemoryEffects::none();
      if (!SawMemory && Actual == Expected) {
        SawMemory = true;
        continue;
      }
    }
    rejectDirect("unsupported enum or integer function attribute");
  }
  if (!SawMemory)
    rejectDirect("exact body-consistent memory effects are required");
  const unsigned ExpectedEnumCount =
      3 + (IsHelper ? 1U : 0U) + (HasMustProgress ? 1U : 0U) +
      (HasWillReturn ? 1U : 0U) + (ExpectedNoSync ? 1U : 0U);
  const unsigned ExpectedFnAttributeCount = ExpectedEnumCount + 1 + 4;
  if (EnumCount != ExpectedEnumCount ||
      Attrs.getFnAttrs().getNumAttributes() != ExpectedFnAttributeCount)
    rejectDirect("function attribute set is not the exact registered tuple");

  auto CanonicalHelperReturn = [&](const AttributeSet &Set) {
    if (HasPhysicalEffects) {
      if (Set.getNumAttributes() == 0)
        return true;
      if (Set.getNumAttributes() != 1)
        return false;
      const Attribute Range = *Set.begin();
      if (!Range.isConstantRangeAttribute() ||
          Range.getKindAsEnum() != Attribute::Range)
        return false;
      const ConstantRange Mask15(APInt(32, 0), APInt(32, 16));
      const ConstantRange Mask13(APInt(32, 0), APInt(32, 14));
      return Range.getRange() == Mask15 || Range.getRange() == Mask13;
    }
    bool HasNoUndef = false;
    bool HasRange = false;
    for (Attribute A : Set) {
      if (A.isEnumAttribute() && A.getKindAsEnum() == Attribute::NoUndef) {
        if (HasNoUndef)
          return false;
        HasNoUndef = true;
        continue;
      }
      if (A.isConstantRangeAttribute() &&
          A.getKindAsEnum() == Attribute::Range) {
        const ConstantRange Expected(APInt(32, 0), APInt(32, 16));
        if (HasRange || A.getRange() != Expected)
          return false;
        HasRange = true;
        continue;
      }
      return false;
    }
    return HasNoUndef && HasRange && Set.getNumAttributes() == 2;
  };
  if ((!IsHelper && Attrs.getRetAttrs().getNumAttributes() != 0) ||
      (IsHelper && !CanonicalHelperReturn(Attrs.getRetAttrs())))
    rejectDirect("return attributes are outside the direct-call profile");
  auto CanonicalParameter = [&](const AttributeSet &Set) {
    bool HasNoUndef = false;
    bool HasRange = false;
    for (Attribute A : Set) {
      if (A.isEnumAttribute() && A.getKindAsEnum() == Attribute::NoUndef) {
        if (HasNoUndef)
          return false;
        HasNoUndef = true;
        continue;
      }
      if (A.isConstantRangeAttribute() &&
          A.getKindAsEnum() == Attribute::Range) {
        const ConstantRange Argument28(APInt(32, 0), APInt(32, 1U << 28));
        const ConstantRange Argument24(APInt(32, 0), APInt(32, 1U << 24));
        if (HasRange ||
            (A.getRange() != Argument28 && A.getRange() != Argument24))
          return false;
        HasRange = true;
        continue;
      }
      return false;
    }
    return HasNoUndef && Set.getNumAttributes() == (HasRange ? 2U : 1U);
  };
  for (unsigned Index = 0; Index != F.arg_size(); ++Index) {
    const AttributeSet ParamAttrs = Attrs.getParamAttrs(Index);
    if (!CanonicalParameter(ParamAttrs))
      rejectDirect("parameter attributes are outside the direct-call profile");
  }
}

void checkDirectCallAttributes(const CallInst &Call) {
  const AttributeList &Attrs = Call.getAttributes();
  const AttributeSet FnAttrs = Attrs.getFnAttrs();
  const Attribute NoBuiltins = Call.getFnAttr("no-builtins");
  if (FnAttrs.getNumAttributes() != 2 || !Call.isNoBuiltin() ||
      !NoBuiltins.isStringAttribute() ||
      !NoBuiltins.getValueAsString().empty() ||
      Attrs.getRetAttrs().getNumAttributes() != 0 ||
      Attrs.getParamAttrs(0).getNumAttributes() != 1 ||
      !Attrs.hasParamAttr(0, Attribute::NoUndef))
    rejectDirect("call-site attributes are outside the direct-call profile");
}

void checkDirectFunctionHeader(const Function &F, bool IsHelper,
                               bool HasPhysicalEffects) {
  if (F.isDeclaration() || F.getAddressSpace() != 0 || F.isVarArg())
    rejectDirect("function declaration or signature envelope mismatch");
  if (IsHelper) {
    if (F.getName() != "brace_system_call_leaf" || !F.hasInternalLinkage() ||
        F.getCallingConv() != CallingConv::Fast || F.arg_size() != 1 ||
        !F.getArg(0)->getType()->isIntegerTy(32) ||
        !F.getReturnType()->isIntegerTy(32) ||
        F.getUnnamedAddr() != GlobalValue::UnnamedAddr::Global)
      rejectDirect(
          "requires one private fastcc i32 brace_system_call_leaf(i32)");
  } else if (F.getName() != "brace_system_entry" || !F.hasExternalLinkage() ||
             F.getCallingConv() != CallingConv::C || !F.arg_empty() ||
             !F.getReturnType()->isVoidTy() ||
             F.getUnnamedAddr() != GlobalValue::UnnamedAddr::Local) {
    rejectDirect("requires one external C void brace_system_entry(void)");
  }
  if (!F.isDSOLocal() || F.getVisibility() != GlobalValue::DefaultVisibility ||
      F.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
      F.hasPersonalityFn() || F.hasGC() || F.hasPrefixData() ||
      F.hasPrologueData() || F.hasSection() || F.hasComdat() || F.getAlign() ||
      !F.getPartition().empty() || F.getSectionPrefix().has_value() ||
      F.hasSanitizerMetadata() || F.hasMetadata())
    rejectDirect("function decoration is outside the direct-call profile");
  checkDirectFunctionAttributes(F, IsHelper, HasPhysicalEffects);
}

struct DirectCounts final {
  unsigned Instructions = 0;
  unsigned Edges = 0;
  unsigned Values = 0;
  unsigned Memory = 0;
  const CallInst *Call = nullptr;
};

DirectCounts checkDirectFunction(const Function &F, const Function &Helper,
                                 bool IsHelper, bool HasPhysicalEffects,
                                 bool AllowsCallLiveHome = false) {
  checkDirectFunctionHeader(F, IsHelper, HasPhysicalEffects);
  if (F.empty() || F.size() > 4)
    rejectDirect("basic-block count is outside 1..4");

  SmallPtrSet<const BasicBlock *, 4> Reachable;
  SmallVector<const BasicBlock *, 4> Worklist{&F.getEntryBlock()};
  while (!Worklist.empty()) {
    const BasicBlock *Block = Worklist.pop_back_val();
    if (!Reachable.insert(Block).second)
      continue;
    for (const BasicBlock *Successor : successors(Block))
      Worklist.push_back(Successor);
  }
  if (Reachable.size() != F.size())
    rejectDirect("all basic blocks must be reachable from the entry");

  DirectCounts Counts;
  SmallPtrSet<const Value *, 32> Values;
  if (IsHelper)
    Values.insert(F.getArg(0));
  for (const BasicBlock &Block : F)
    for (const Instruction &I : Block)
      if (!I.getType()->isVoidTy() && Values.insert(&I).second &&
          Values.size() > 64)
        rejectDirect("per-function tracked non-void value count exceeds 64");
  Counts.Values = Values.size();

  auto IsInteger = [&](const Value *V, Type *Expected = nullptr) {
    Type *Ty = V->getType();
    if ((Expected && Ty != Expected) ||
        (!Ty->isIntegerTy(8) && !Ty->isIntegerTy(32)))
      return false;
    if (const auto *Constant = dyn_cast<ConstantInt>(V))
      return Constant->getBitWidth() == Ty->getIntegerBitWidth();
    if (const auto *Argument = dyn_cast<llvm::Argument>(V))
      return Argument->getParent() == &F && IsHelper && Argument == F.getArg(0);
    return Values.contains(V);
  };

  unsigned ReturnCount = 0;
  for (const BasicBlock &Block : F) {
    if (Block.empty())
      rejectDirect("empty IR basic blocks are not admitted");
    Counts.Edges += Block.getTerminator()->getNumSuccessors();
    if (Counts.Edges > 6)
      rejectDirect("CFG edge count exceeds 6");
    for (const Instruction &I : Block) {
      if (++Counts.Instructions > 128)
        rejectDirect("IR instruction count exceeds 128");
      checkDirectInstructionMetadata(I);
      if (const auto *Load = dyn_cast<LoadInst>(&I)) {
        if (++Counts.Memory > 64)
          rejectDirect("physical memory operation count exceeds 64");
        checkDirectMemory(*Load);
        continue;
      }
      if (const auto *Store = dyn_cast<StoreInst>(&I)) {
        if (++Counts.Memory > 64)
          rejectDirect("physical memory operation count exceeds 64");
        if (!IsInteger(Store->getValueOperand(),
                       Store->getValueOperand()->getType()))
          rejectDirect("store is outside the direct-call integer graph");
        checkDirectMemory(*Store);
        continue;
      }
      if (const auto *Binary = dyn_cast<BinaryOperator>(&I)) {
        if (Binary->getOpcode() != Instruction::And || !IsInteger(Binary) ||
            !IsInteger(Binary->getOperand(0), Binary->getType()) ||
            !IsInteger(Binary->getOperand(1), Binary->getType()))
          rejectDirect("only i8/i32 integer-and is admitted");
        continue;
      }
      if (const auto *Compare = dyn_cast<ICmpInst>(&I)) {
        const auto *Zero = dyn_cast<ConstantInt>(Compare->getOperand(1));
        Type *ComparedType = Compare->getOperand(0)->getType();
        if ((Compare->getPredicate() != ICmpInst::ICMP_EQ &&
             Compare->getPredicate() != ICmpInst::ICMP_NE) ||
            !Zero || !Zero->isZero() || !IsInteger(Compare->getOperand(0)) ||
            !IsInteger(Compare->getOperand(1), ComparedType) ||
            !Compare->hasOneUse() || !isa<BranchInst>(*Compare->user_begin()) ||
            cast<BranchInst>(*Compare->user_begin())->getParent() !=
                Compare->getParent())
          rejectDirect(
              "comparisons must be one-use i8/i32 eq/ne zero branches");
        continue;
      }
      if (const auto *Branch = dyn_cast<BranchInst>(&I)) {
        if (Branch->isConditional() &&
            (!isa<ICmpInst>(Branch->getCondition()) ||
             Branch->getSuccessor(0) == Branch->getSuccessor(1)))
          rejectDirect(
              "conditional branches must consume the admitted comparison");
        continue;
      }
      if (const auto *Call = dyn_cast<CallInst>(&I)) {
        if (IsHelper || Counts.Call || Call->getCalledFunction() != &Helper ||
            Call->getCallingConv() != CallingConv::Fast || Call->isTailCall() ||
            Call->arg_size() != 1 || !IsInteger(Call->getArgOperand(0)) ||
            !Call->getArgOperand(0)->getType()->isIntegerTy(32) ||
            !Call->getType()->isIntegerTy(32) || Call->hasOperandBundles() ||
            Call->isInlineAsm())
          rejectDirect("requires one non-tail private i32(i32) direct call");
        checkDirectCallAttributes(*Call);
        Counts.Call = Call;
        continue;
      }
      if (const auto *Return = dyn_cast<ReturnInst>(&I)) {
        ++ReturnCount;
        if ((!IsHelper && Return->getReturnValue()) ||
            (IsHelper &&
             (!Return->getReturnValue() ||
              !IsInteger(Return->getReturnValue()) ||
              !Return->getReturnValue()->getType()->isIntegerTy(32))))
          rejectDirect("function return shape does not match its signature");
        continue;
      }
      rejectDirect("instruction is outside the S3b.5 direct-call profile");
    }
  }
  if (ReturnCount == 0)
    rejectDirect("each direct-call function requires a Return");
  if ((!IsHelper && !Counts.Call) || (IsHelper && Counts.Call))
    rejectDirect("entry must contain the only call and helper must be leaf");
  if (!IsHelper && Counts.Call->use_empty())
    rejectDirect("entry must consume the direct-call result");

  if (!IsHelper) {
    const BasicBlock *CallBlock = Counts.Call->getParent();
    SmallPtrSet<const BasicBlock *, 4> BeforeCallBlocks;
    Worklist.clear();
    Worklist.push_back(&F.getEntryBlock());
    while (!Worklist.empty()) {
      const BasicBlock *Block = Worklist.pop_back_val();
      if (!BeforeCallBlocks.insert(Block).second || Block == CallBlock)
        continue;
      if (isa<ReturnInst>(Block->getTerminator()))
        rejectDirect("a root path can return before the direct call");
      for (const BasicBlock *Successor : successors(Block))
        Worklist.push_back(Successor);
    }

    SmallPtrSet<const BasicBlock *, 4> AfterCallBlocks;
    for (const BasicBlock *Successor : successors(CallBlock))
      Worklist.push_back(Successor);
    while (!Worklist.empty()) {
      const BasicBlock *Block = Worklist.pop_back_val();
      if (Block == CallBlock)
        rejectDirect("the direct call can execute more than once");
      if (!AfterCallBlocks.insert(Block).second)
        continue;
      for (const BasicBlock *Successor : successors(Block))
        Worklist.push_back(Successor);
    }

    SmallPtrSet<const Instruction *, 32> AfterCall;
    SmallPtrSet<const Instruction *, 32> BeforeCall;
    for (const BasicBlock &Block : F) {
      bool SeenCall = false;
      for (const Instruction &I : Block) {
        if (&I == Counts.Call) {
          SeenCall = true;
          continue;
        }
        if ((&Block == CallBlock && SeenCall) ||
            AfterCallBlocks.contains(&Block))
          AfterCall.insert(&I);
        else if (BeforeCallBlocks.contains(&Block))
          BeforeCall.insert(&I);
        else
          rejectDirect("CFG cannot be partitioned around the direct call");
      }
    }
    SmallPtrSet<const Instruction *, 2> CallLiveValues;
    unsigned CallLiveUses = 0;
    for (const Instruction *I : BeforeCall)
      for (const User *U : I->users()) {
        const auto *Use = dyn_cast<Instruction>(U);
        if (!Use || !AfterCall.contains(Use))
          continue;
        if (!AllowsCallLiveHome)
          rejectDirect("caller SSA value remains live across the call");
        const auto *Load = dyn_cast<LoadInst>(I);
        if (!Load || !Load->getType()->isIntegerTy(32))
          rejectDirectHome(
              "call-live home must originate from one i32 physical load");
        CallLiveValues.insert(I);
        ++CallLiveUses;
      }
    if (AllowsCallLiveHome &&
        (CallLiveValues.size() > 1 || CallLiveUses != CallLiveValues.size()))
      rejectDirectHome(
          "root permits H0 or exactly one i32 SSA value with one post-call "
          "use");
    for (const User *U : Counts.Call->users()) {
      const auto *Use = dyn_cast<Instruction>(U);
      if (!Use || !AfterCall.contains(Use))
        rejectDirect("direct-call result must be consumed after the call");
    }
  }
  return Counts;
}

void checkModuleEnvelope(const Module &M, StringRef RequiredABI) {
  const bool DirectCall = isDirectCallABIName(RequiredABI);
  if (M.getTargetTriple().str() != RequiredTriple ||
      M.getDataLayoutStr() != RequiredDataLayout) {
    if (DirectCall)
      rejectDirect("target triple or data layout mismatch");
    reject("target triple or data layout mismatch");
  }
  if (!M.getModuleInlineAsm().empty() || !M.global_empty() ||
      !M.alias_empty() || !M.ifunc_empty()) {
    if (DirectCall)
      rejectDirect("globals, aliases, ifuncs, and module asm are not admitted");
    reject("globals, aliases, ifuncs, and module asm are not admitted");
  }
  if (!M.getIdentifiedStructTypes().empty() ||
      !M.getComdatSymbolTable().empty()) {
    if (DirectCall)
      rejectDirect("identified types and COMDAT are not admitted");
    reject("identified types and COMDAT are not admitted");
  }
  if (std::distance(M.begin(), M.end()) != (DirectCall ? 2 : 1)) {
    if (DirectCall)
      rejectDirect("exactly two functions are required");
    reject("exactly one function is required");
  }

  unsigned NamedCount = 0;
  for (const NamedMDNode &Named : M.named_metadata()) {
    ++NamedCount;
    if (Named.getName() != "llvm.module.flags" ||
        !canonicalModuleFlags(Named, RequiredABI))
      DirectCall ? rejectDirect("noncanonical named metadata or module flags")
                 : reject("noncanonical named metadata or module flags");
  }
  if (NamedCount != 1)
    DirectCall
        ? rejectDirect(
              "the exact wchar and target-abi module flags are required")
        : reject("the exact wchar and target-abi module flags are required");
}

class BraceS3IRVerifier final : public ModulePass {
  std::string RequiredABI;

public:
  static char ID;
  explicit BraceS3IRVerifier(StringRef RequiredABI)
      : ModulePass(ID), RequiredABI(RequiredABI) {}

  bool runOnModule(Module &M) override {
    verifyBraceS3IRModule(M, RequiredABI);
    return false;
  }

  StringRef getPassName() const override {
    return "Brace SelectionDAG IR verifier";
  }
};

} // namespace

void llvm::verifyBraceS3IRModule(const Module &M, StringRef RequiredABI) {
  checkModuleEnvelope(M, RequiredABI);
  if (isDirectCallABIName(RequiredABI)) {
    const Function *Entry = M.getFunction("brace_system_entry");
    const Function *Helper = M.getFunction("brace_system_call_leaf");
    if (!Entry || !Helper)
      rejectDirect("required entry or helper identity is missing");
    for (const User *U : Helper->users()) {
      const auto *Call = dyn_cast<CallInst>(U);
      if (!Call || Call->getCalledFunction() != Helper ||
          Call->getFunction() != Entry)
        rejectDirect("helper address escapes the single direct call");
    }
    const bool HelperPhysical = hasDirectPhysicalMemory(*Helper);
    const bool DirectCallHome = RequiredABI == BraceSdagDirectCallHomeABIName;
    const DirectCounts EntryCounts = checkDirectFunction(
        *Entry, *Helper, false,
        hasDirectPhysicalMemory(*Entry) || HelperPhysical, DirectCallHome);
    const DirectCounts HelperCounts =
        checkDirectFunction(*Helper, *Helper, true, HelperPhysical);
    if (EntryCounts.Instructions + HelperCounts.Instructions > 199 ||
        EntryCounts.Edges + HelperCounts.Edges > 12 ||
        EntryCounts.Values + HelperCounts.Values > 128 ||
        EntryCounts.Memory + HelperCounts.Memory > 64)
      rejectDirect("module resource limit exceeded");
    return;
  }
  checkFunction(*M.begin());
}

void llvm::verifyBraceS3LateModuleEnvelope(const Module &M,
                                           StringRef RequiredABI) {
  checkModuleEnvelope(M, RequiredABI);
  if (isDirectCallABIName(RequiredABI)) {
    const Function *Entry = M.getFunction("brace_system_entry");
    const Function *Helper = M.getFunction("brace_system_call_leaf");
    if (!Entry || !Helper)
      rejectDirect("required entry or helper identity is missing");
    const bool HelperPhysical = hasDirectPhysicalMemory(*Helper);
    checkDirectFunctionHeader(
        *Entry, false, hasDirectPhysicalMemory(*Entry) || HelperPhysical);
    checkDirectFunctionHeader(*Helper, true, HelperPhysical);
    return;
  }
  checkFunctionHeader(*M.begin());
}

char BraceS3IRVerifier::ID = 0;

ModulePass *llvm::createBraceS3IRVerifierPass(const BraceTargetMachine &TM) {
  return new BraceS3IRVerifier(TM.getSdagABIName());
}
