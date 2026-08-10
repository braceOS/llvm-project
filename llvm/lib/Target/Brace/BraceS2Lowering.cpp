//===-- BraceS2Lowering.cpp - Constrained Brace64 S2 lowering ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is intentionally a narrow system-codegen checkpoint.  It validates the
// complete optimized IR shape of the canonical freestanding Hello kernel,
// lowers that shape to eight experiment-local MC forms, and asks the Brace MC
// target streamer to publish the exact accepted S2 object.  It is not a
// SelectionDAG backend, a calling convention, or a general C compiler.
//
//===----------------------------------------------------------------------===//

#include "Brace.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <array>
#include <cstdint>
#include <memory>

using namespace llvm;

namespace {

constexpr StringLiteral RequiredTriple = "brace64-unknown-none-elf";
constexpr StringLiteral RequiredDataLayout =
    "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";
constexpr uint64_t RamBase = UINT64_C(0x80000000);
constexpr uint64_t UartTX = UINT64_C(0x10000000);
constexpr uint64_t UartLineStatus = UINT64_C(0x10000005);
constexpr uint64_t Finisher = UINT64_C(0x00100000);
constexpr uint64_t PassValue = UINT64_C(0x5555);

Error reject(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "brace64 S3b.2 profile: " + Message);
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

bool canonicalModuleFlags(const NamedMDNode &Named) {
  if (Named.getNumOperands() != 1)
    return false;
  const MDNode *Node = Named.getOperand(0);
  return Node && isa<MDTuple>(Node) && Node->isUniqued() &&
         Node->getNumOperands() == 3 &&
         metadataI32Equals(Node->getOperand(0).get(), Module::Error) &&
         metadataStringEquals(Node->getOperand(1).get(), "wchar_size") &&
         metadataI32Equals(Node->getOperand(2).get(), 4);
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

Error checkModuleEnvelope(const Module &M) {
  if (M.getTargetTriple().str() != RequiredTriple)
    return reject("target triple mismatch");
  if (M.getDataLayoutStr() != RequiredDataLayout)
    return reject("data layout mismatch");
  if (!M.getModuleInlineAsm().empty() || !M.global_empty() ||
      !M.alias_empty() || !M.ifunc_empty())
    return reject("globals, aliases, ifuncs, or module asm are unsupported");
  if (!M.getIdentifiedStructTypes().empty() ||
      !M.getComdatSymbolTable().empty())
    return reject("identified types and COMDAT are unsupported");

  unsigned NamedCount = 0;
  for (const NamedMDNode &Named : M.named_metadata()) {
    ++NamedCount;
    if (Named.getName() != "llvm.module.flags" || !canonicalModuleFlags(Named))
      return reject("noncanonical named metadata");
  }
  if (NamedCount != 1)
    return reject("the canonical brace64 module flag is required");

  if (std::distance(M.begin(), M.end()) != 1)
    return reject("exactly one function is required");
  return Error::success();
}

bool allowedStringAttribute(const Attribute &A) {
  const StringRef Kind = A.getKindAsString();
  const StringRef Value = A.getValueAsString();
  return (Kind == "no-builtins" && Value.empty()) ||
         (Kind == "no-trapping-math" && Value == "true") ||
         (Kind == "stack-protector-buffer-size" && Value == "8");
}

Error checkFunctionEnvelope(const Function &F) {
  if (F.getName() != "brace_system_entry" || F.isDeclaration() ||
      !F.hasExternalLinkage() || F.getCallingConv() != CallingConv::C ||
      F.getAddressSpace() != 0 || !F.arg_empty() || F.isVarArg() ||
      !F.getReturnType()->isVoidTy())
    return reject("entry function identity or type mismatch");
  if (!F.isDSOLocal() || F.getVisibility() != GlobalValue::DefaultVisibility ||
      F.getDLLStorageClass() != GlobalValue::DefaultStorageClass ||
      F.getUnnamedAddr() != GlobalValue::UnnamedAddr::Local)
    return reject("entry function visibility/storage/unnamed_addr mismatch");
  if (F.hasPrefixData() || F.hasPrologueData() || F.hasPersonalityFn() ||
      F.hasSection() || F.hasGC() || F.hasComdat() || F.getAlign() ||
      !F.getPartition().empty() || F.getSectionPrefix().has_value() ||
      F.hasSanitizerMetadata() || F.hasMetadata())
    return reject("entry function has unsupported decoration");

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
    return reject("function attribute set mismatch");
  const MemoryEffects ExpectedMemory =
      MemoryEffects::unknown()
          .getWithoutLoc(IRMemLocation::TargetMem0)
          .getWithoutLoc(IRMemLocation::TargetMem1);
  for (Attribute A : Attrs.getFnAttrs()) {
    if (A.isStringAttribute()) {
      if (!allowedStringAttribute(A))
        return reject("unsupported string function attribute");
      continue;
    }
    if (!A.isEnumAttribute() && !A.isIntAttribute())
      return reject("unsupported type function attribute");
    const Attribute::AttrKind Kind = A.getKindAsEnum();
    if (A.isEnumAttribute() &&
        (Kind == Attribute::NoFree || Kind == Attribute::NoRecurse ||
         Kind == Attribute::NoUnwind))
      continue;
    if (A.isIntAttribute() && Kind == Attribute::Memory) {
      const MemoryEffects Effects =
          MemoryEffects::createFromIntValue(A.getValueAsInt());
      if (Effects == ExpectedMemory)
        continue;
    }
    return reject("unsupported enum function attribute");
  }
  return Error::success();
}

Error checkNoMetadata(const Instruction &I) {
  if (I.getDebugLoc() || I.hasDbgRecords() || I.hasMetadataOtherThanDebugLoc())
    return reject("unexpected instruction metadata");
  return Error::success();
}

Error checkLoopBranchMetadata(const BranchInst &Branch) {
  if (Branch.getDebugLoc() || Branch.hasDbgRecords())
    return reject("debug metadata is unsupported");
  SmallVector<std::pair<unsigned, MDNode *>, 2> Metadata;
  Branch.getAllMetadataOtherThanDebugLoc(Metadata);
  if (Metadata.size() != 1 || Metadata[0].first != LLVMContext::MD_loop ||
      !canonicalLoopMetadata(Metadata[0].second))
    return reject("loop metadata mismatch");
  return Error::success();
}

Expected<uint64_t> physicalAddress(const Value *Pointer) {
  const auto *Expression = dyn_cast<ConstantExpr>(Pointer);
  if (!Expression || Expression->getOpcode() != Instruction::IntToPtr ||
      Expression->getNumOperands() != 1 ||
      Expression->getType()->getPointerAddressSpace() != 200)
    return reject("physical pointer must be direct addrspace(200) inttoptr");
  const auto *Integer = dyn_cast<ConstantInt>(Expression->getOperand(0));
  if (!Integer || Integer->getBitWidth() != 64)
    return reject("physical pointer source must be an i64 constant");
  return Integer->getZExtValue();
}

uint64_t requiredAlignment(uint64_t Address, unsigned WidthBytes) {
  if (Address == 0)
    return WidthBytes;
  return Address & (~Address + 1);
}

Error checkMemory(const LoadInst &Load, uint64_t ExpectedAddress,
                  unsigned Width) {
  if (!Load.isVolatile() || Load.isAtomic() ||
      !Load.getType()->isIntegerTy(Width))
    return reject("load volatility/atomicity/width mismatch");
  auto Address = physicalAddress(Load.getPointerOperand());
  if (!Address)
    return Address.takeError();
  if (*Address != ExpectedAddress ||
      Load.getAlign().value() != requiredAlignment(*Address, Width / 8))
    return reject("load address or alignment mismatch");
  return checkNoMetadata(Load);
}

Error checkMemory(const StoreInst &Store, uint64_t ExpectedAddress,
                  unsigned Width) {
  if (!Store.isVolatile() || Store.isAtomic() ||
      !Store.getValueOperand()->getType()->isIntegerTy(Width))
    return reject("store volatility/atomicity/width mismatch");
  auto Address = physicalAddress(Store.getPointerOperand());
  if (!Address)
    return Address.takeError();
  if (*Address != ExpectedAddress ||
      Store.getAlign().value() != requiredAlignment(*Address, Width / 8))
    return reject("store address or alignment mismatch");
  return checkNoMetadata(Store);
}

template <typename T>
Expected<const T *> nextInstruction(BasicBlock::const_iterator &It,
                                    const BasicBlock::const_iterator &End,
                                    StringRef What) {
  if (It == End)
    return reject("missing " + What);
  const auto *Result = dyn_cast<T>(&*It++);
  if (!Result)
    return reject("expected " + What);
  return Result;
}

class S2MCEmission final {
  MCStreamer &Streamer;
  const MCSubtargetInfo &STI;

public:
  S2MCEmission(MCStreamer &Streamer, const MCSubtargetInfo &STI)
      : Streamer(Streamer), STI(STI) {}

  Error emit(unsigned Opcode, std::initializer_list<uint64_t> Operands) {
    MCInst Inst;
    Inst.setOpcode(Opcode);
    for (uint64_t Operand : Operands)
      Inst.addOperand(MCOperand::createImm(static_cast<int64_t>(Operand)));
    Streamer.emitInstruction(Inst, STI);
    if (Streamer.getContext().hadError())
      return reject("MC object streamer rejected an instruction");
    return Error::success();
  }
};

Error emit(S2MCEmission &Emission, unsigned Opcode,
           std::initializer_list<uint64_t> Operands) {
  return Emission.emit(Opcode, Operands);
}

Error lowerCanonicalFunction(const Function &F, S2MCEmission &Streamer) {
  if (F.size() != 3)
    return reject("canonical kernel requires exactly three basic blocks");
  auto Block = F.begin();
  const BasicBlock &Entry = *Block++;
  const BasicBlock &Poll = *Block++;
  const BasicBlock &Body = *Block;

  constexpr std::array<uint64_t, 7> Words{
      UINT64_C(0x6c6c6548), UINT64_C(0x6f57206f), UINT64_C(0x20646c72),
      UINT64_C(0x6d6f7266), UINT64_C(0x61724220), UINT64_C(0x4f206563),
      UINT64_C(0x00000a53)};

  auto It = Entry.begin();
  for (unsigned I = 0; I != Words.size(); ++I) {
    auto Store = nextInstruction<StoreInst>(It, Entry.end(), "RAM store");
    if (!Store)
      return Store.takeError();
    if (Error E = checkMemory(**Store, RamBase + I * 4, 32))
      return E;
    const auto *Value = dyn_cast<ConstantInt>((*Store)->getValueOperand());
    if (!Value || Value->getZExtValue() != Words[I])
      return reject("RAM materialization constant mismatch");
    if (Error E =
            emit(Streamer, Brace::S2_PHYSICAL_ADDRESS, {1, RamBase + I * 4}))
      return E;
    if (Error E = emit(Streamer, Brace::S2_CONSTANT, {4, Brace::I32, Words[I]}))
      return E;
    if (Error E = emit(Streamer, Brace::S2_PHYSICAL_STORE, {Brace::U32, 1, 4}))
      return E;
  }
  auto EntryBranch =
      nextInstruction<BranchInst>(It, Entry.end(), "entry branch");
  if (!EntryBranch)
    return EntryBranch.takeError();
  if (It != Entry.end() || (*EntryBranch)->isConditional() ||
      (*EntryBranch)->getSuccessor(0) != &Poll)
    return reject("entry fallthrough branch mismatch");
  if (Error E = checkNoMetadata(**EntryBranch))
    return E;

  It = Poll.begin();
  auto StatusLoad =
      nextInstruction<LoadInst>(It, Poll.end(), "UART status load");
  auto And = nextInstruction<BinaryOperator>(It, Poll.end(), "status mask");
  auto Compare = nextInstruction<ICmpInst>(It, Poll.end(), "status compare");
  auto PollBranch = nextInstruction<BranchInst>(It, Poll.end(), "poll branch");
  if (!StatusLoad || !And || !Compare || !PollBranch)
    return joinErrors(
        StatusLoad.takeError(),
        joinErrors(And.takeError(),
                   joinErrors(Compare.takeError(), PollBranch.takeError())));
  if (It != Poll.end())
    return reject("unexpected polling instruction");
  if (Error E = checkMemory(**StatusLoad, UartLineStatus, 8))
    return E;
  const auto *Mask = dyn_cast<ConstantInt>((*And)->getOperand(1));
  if ((*And)->getOpcode() != Instruction::And ||
      (*And)->getType() != (*StatusLoad)->getType() ||
      (*And)->getOperand(0) != *StatusLoad || !Mask ||
      Mask->getZExtValue() != 0x20 || !(*StatusLoad)->hasOneUse())
    return reject("UART status mask mismatch");
  if (Error E = checkNoMetadata(**And))
    return E;
  const auto *Zero = dyn_cast<ConstantInt>((*Compare)->getOperand(1));
  if ((*Compare)->getPredicate() != ICmpInst::ICMP_EQ ||
      (*Compare)->getOperand(0) != *And || !Zero || !Zero->isZero() ||
      !(*And)->hasOneUse())
    return reject("UART status comparison mismatch");
  if (Error E = checkNoMetadata(**Compare))
    return E;
  if (!(*PollBranch)->isConditional() ||
      (*PollBranch)->getCondition() != *Compare ||
      (*PollBranch)->getSuccessor(0) != &Poll ||
      (*PollBranch)->getSuccessor(1) != &Body || !(*Compare)->hasOneUse())
    return reject("UART polling CFG mismatch");
  if (Error E = checkLoopBranchMetadata(**PollBranch))
    return E;

  if (Error E = emit(Streamer, Brace::S2_PHYSICAL_ADDRESS, {1, UartLineStatus}))
    return E;
  if (Error E = emit(Streamer, Brace::S2_PHYSICAL_LOAD, {2, Brace::U8, 1}))
    return E;
  if (Error E = emit(Streamer, Brace::S2_CONSTANT, {3, Brace::I8, 0x20}))
    return E;
  if (Error E = emit(Streamer, Brace::S2_INTEGER_AND, {2, 2, 3}))
    return E;
  if (Error E = emit(Streamer, Brace::S2_BRANCH_IF, {2, 26, 21}))
    return E;

  It = Body.begin();
  for (unsigned I = 0; I != 26; ++I) {
    auto Load = nextInstruction<LoadInst>(It, Body.end(), "RAM byte load");
    auto Store = nextInstruction<StoreInst>(It, Body.end(), "UART byte store");
    if (!Load || !Store)
      return joinErrors(Load.takeError(), Store.takeError());
    if (Error E = checkMemory(**Load, RamBase + I, 8))
      return E;
    if (Error E = checkMemory(**Store, UartTX, 8))
      return E;
    if ((*Store)->getValueOperand() != *Load || !(*Load)->hasOneUse())
      return reject("RAM-to-UART SSA use mismatch");
    if (Error E = emit(Streamer, Brace::S2_PHYSICAL_ADDRESS, {1, RamBase + I}))
      return E;
    if (Error E = emit(Streamer, Brace::S2_PHYSICAL_LOAD, {2, Brace::U8, 1}))
      return E;
    if (I == 0)
      if (Error E = emit(Streamer, Brace::S2_PHYSICAL_ADDRESS, {0, UartTX}))
        return E;
    if (Error E = emit(Streamer, Brace::S2_PHYSICAL_STORE, {Brace::U8, 0, 2}))
      return E;
  }

  auto FinishStore =
      nextInstruction<StoreInst>(It, Body.end(), "finisher store");
  auto Return = nextInstruction<ReturnInst>(It, Body.end(), "return");
  if (!FinishStore || !Return)
    return joinErrors(FinishStore.takeError(), Return.takeError());
  if (It != Body.end())
    return reject("unexpected instruction after return");
  if (Error E = checkMemory(**FinishStore, Finisher, 32))
    return E;
  const auto *FinishValue =
      dyn_cast<ConstantInt>((*FinishStore)->getValueOperand());
  if (!FinishValue || FinishValue->getZExtValue() != PassValue ||
      (*Return)->getReturnValue())
    return reject("finisher or return mismatch");
  if (Error E = checkNoMetadata(**Return))
    return E;
  if (Error E = emit(Streamer, Brace::S2_PHYSICAL_ADDRESS, {1, Finisher}))
    return E;
  if (Error E = emit(Streamer, Brace::S2_CONSTANT, {4, Brace::I32, PassValue}))
    return E;
  if (Error E = emit(Streamer, Brace::S2_PHYSICAL_STORE, {Brace::U32, 1, 4}))
    return E;
  return emit(Streamer, Brace::S2_RETURN, {});
}

Error lowerAndWrite(Module &M, TargetMachine &TM, MCContext &Context,
                    raw_pwrite_stream &Out) {
  if (Error E = checkModuleEnvelope(M))
    return E;
  const Function &F = *M.begin();
  if (Error E = checkFunctionEnvelope(F))
    return E;

  Triple TT(M.getTargetTriple());
  const MCSubtargetInfo *STI = TM.getMCSubtargetInfo();
  if (!STI)
    return reject("TargetMachine has no MC subtarget information");
  std::unique_ptr<MCObjectWriter> Writer =
      Brace::createS2ObjectWriter(Out, Brace::S2ObjectMode::Legacy);
  std::unique_ptr<MCStreamer> Streamer(TM.getTarget().createMCObjectStreamer(
      TT, Context, nullptr, std::move(Writer), nullptr, *STI));
  if (!Streamer || !Streamer->getTargetStreamer())
    return reject("registered S2 object streamer is unavailable");
  auto *TargetStreamer =
      static_cast<Brace::S2TargetStreamer *>(Streamer->getTargetStreamer());
  constexpr std::array<uint8_t, 6> Types{
      Brace::PADDR, Brace::PADDR, Brace::I8, Brace::I8, Brace::I32, Brace::I32};
  if (Error E = TargetStreamer->setHeader(Types, 0, Brace::S2RelocationBase))
    return E;
  S2MCEmission Emission(*Streamer, *STI);
  if (Error E = lowerCanonicalFunction(F, Emission))
    return E;
  Streamer->finish();
  if (Context.hadError())
    return reject("MC object streamer failed during finish");
  return Error::success();
}

class BraceS2WriterPass final : public ModulePass {
  TargetMachine &TM;
  raw_pwrite_stream &Out;

public:
  static char ID;
  BraceS2WriterPass(TargetMachine &TM, raw_pwrite_stream &Out)
      : ModulePass(ID), TM(TM), Out(Out) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
  }

  bool runOnModule(Module &M) override {
    MCContext &Context =
        getAnalysis<MachineModuleInfoWrapperPass>().getMMI().getContext();
    if (Error E = lowerAndWrite(M, TM, Context, Out))
      M.getContext().emitError(toString(std::move(E)));
    return false;
  }
};

char BraceS2WriterPass::ID = 0;

} // namespace

ModulePass *llvm::createBraceS2WriterPass(TargetMachine &TM,
                                          raw_pwrite_stream &Out) {
  return new BraceS2WriterPass(TM, Out);
}
