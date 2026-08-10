//===-- BraceS3IRVerifier.cpp - S3b.3 leaf IR trust boundary -------------===//

#include "Brace.h"
#include "BraceTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CFG.h"
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

void checkModuleEnvelope(const Module &M, StringRef RequiredABI) {
  if (M.getTargetTriple().str() != RequiredTriple ||
      M.getDataLayoutStr() != RequiredDataLayout)
    reject("target triple or data layout mismatch");
  if (!M.getModuleInlineAsm().empty() || !M.global_empty() ||
      !M.alias_empty() || !M.ifunc_empty())
    reject("globals, aliases, ifuncs, and module asm are not admitted");
  if (!M.getIdentifiedStructTypes().empty() ||
      !M.getComdatSymbolTable().empty())
    reject("identified types and COMDAT are not admitted");
  if (std::distance(M.begin(), M.end()) != 1)
    reject("exactly one function is required");

  unsigned NamedCount = 0;
  for (const NamedMDNode &Named : M.named_metadata()) {
    ++NamedCount;
    if (Named.getName() != "llvm.module.flags" ||
        !canonicalModuleFlags(Named, RequiredABI))
      reject("noncanonical named metadata or module flags");
  }
  if (NamedCount != 1)
    reject("the exact wchar and target-abi module flags are required");
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
    return "Brace S3b.3 leaf IR verifier";
  }
};

} // namespace

void llvm::verifyBraceS3IRModule(const Module &M, StringRef RequiredABI) {
  checkModuleEnvelope(M, RequiredABI);
  checkFunction(*M.begin());
}

void llvm::verifyBraceS3LateModuleEnvelope(const Module &M,
                                           StringRef RequiredABI) {
  checkModuleEnvelope(M, RequiredABI);
  checkFunctionHeader(*M.begin());
}

char BraceS3IRVerifier::ID = 0;

ModulePass *llvm::createBraceS3IRVerifierPass(const BraceTargetMachine &TM) {
  return new BraceS3IRVerifier(TM.getSdagABIName());
}
