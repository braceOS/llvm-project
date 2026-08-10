//===-- BraceS2ObjectWriter.cpp - Exact experimental S2 object writer -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This writer deliberately does not use LLVM's generic ELF writer.  The S2
// experiment has a byte-for-byte identity, including a one-byte .strtab, an
// independent fixed .shstrtab, an unnamed local ABS relocation-base symbol,
// and exactly nine sections.  A semantically equivalent conventional ELF is
// not an instance of this format.
//
//===----------------------------------------------------------------------===//

#include "BraceMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

using namespace llvm;

namespace llvm::Brace {
namespace {

constexpr uint64_t ElfHeaderSize = 64;
constexpr uint64_t TargetSize = 32;
constexpr uint64_t SectionHeaderSize = 64;
constexpr uint64_t SectionCount = 9;
constexpr uint64_t RelaSize = 24;
constexpr uint64_t SymbolSize = 24;
constexpr uint64_t SymbolCount = 2;
constexpr uint64_t TextWordSize = 4;
constexpr uint64_t LiteralSize = 8;
constexpr uint64_t RelaInfo = UINT64_C(0x0000000100000001);
constexpr uint16_t AbsoluteSection = UINT16_C(0xfff1);
constexpr uint16_t ExperimentalMachine = UINT16_C(0xffb0);
constexpr uint32_t ExperimentalFlags = UINT32_C(0x42520100);
constexpr uint8_t ExperimentalOSABI = UINT8_C(0xff);

constexpr char SectionNames[] =
    "\0.brace.target\0.brace.types\0.brace.literals\0.brace.text\0"
    ".rela.brace.literals\0.symtab\0.strtab\0.shstrtab";
static_assert(sizeof(SectionNames) == 103);

struct Literal final {
  bool IsPhysical = false;
  uint64_t Value = 0;

  friend bool operator==(const Literal &L, const Literal &R) {
    return L.IsPhysical == R.IsPhysical && L.Value == R.Value;
  }
  friend bool operator<(const Literal &L, const Literal &R) {
    if (L.IsPhysical != R.IsPhysical)
      return !L.IsPhysical;
    return L.Value < R.Value;
  }
};

struct Layout final {
  uint64_t TargetOffset = 0;
  uint64_t TypesOffset = 0;
  uint64_t LiteralsOffset = 0;
  uint64_t TextOffset = 0;
  uint64_t RelaOffset = 0;
  uint64_t SymtabOffset = 0;
  uint64_t StrtabOffset = 0;
  uint64_t ShstrtabOffset = 0;
  uint64_t SectionHeadersOffset = 0;
  uint64_t TotalSize = 0;
};

constexpr uint64_t DirectFunctionSize = 64;
constexpr uint64_t DirectDescriptorSize = 24;
constexpr uint64_t DirectSectionCount = 11;
constexpr uint32_t DirectExperimentalFlags = UINT32_C(0x42520200);

constexpr char DirectSectionNames[] =
    "\0.brace.target\0.brace.functions\0.brace.types\0.brace.literals\0"
    ".brace.descriptors\0.brace.text\0.rela.brace.literals\0.symtab\0"
    ".strtab\0.shstrtab";
static_assert(sizeof(DirectSectionNames) == 139);

using DirectDescriptor = std::array<uint8_t, DirectDescriptorSize>;

struct DirectLayout final {
  uint64_t TargetOffset = 0;
  uint64_t FunctionsOffset = 0;
  uint64_t TypesOffset = 0;
  uint64_t LiteralsOffset = 0;
  uint64_t DescriptorsOffset = 0;
  uint64_t TextOffset = 0;
  uint64_t RelaOffset = 0;
  uint64_t SymtabOffset = 0;
  uint64_t StrtabOffset = 0;
  uint64_t ShstrtabOffset = 0;
  uint64_t SectionHeadersOffset = 0;
  uint64_t TotalSize = 0;
};

Error fail(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(),
                           "brace64 exact S2 writer: " + Message);
}

bool checkedAdd(uint64_t L, uint64_t R, uint64_t &Result) {
  if (L > std::numeric_limits<uint64_t>::max() - R)
    return false;
  Result = L + R;
  return true;
}

bool checkedMultiply(uint64_t L, uint64_t R, uint64_t &Result) {
  if (L != 0 && R > std::numeric_limits<uint64_t>::max() / L)
    return false;
  Result = L * R;
  return true;
}

bool alignUp(uint64_t Value, uint64_t Alignment, uint64_t &Result) {
  const uint64_t Mask = Alignment - 1;
  if (Value > std::numeric_limits<uint64_t>::max() - Mask)
    return false;
  Result = (Value + Mask) & ~Mask;
  return true;
}

bool computeLayout(uint64_t RegisterCount, uint64_t OperationCount,
                   uint64_t LiteralCount, uint64_t RelocationCount,
                   Layout &Result) {
  Result.TargetOffset = ElfHeaderSize;
  Result.TypesOffset = ElfHeaderSize + TargetSize;
  uint64_t Cursor = 0;
  if (!checkedAdd(Result.TypesOffset, RegisterCount, Cursor) ||
      !alignUp(Cursor, 8, Result.LiteralsOffset))
    return false;

  uint64_t LiteralBytes = 0;
  if (!checkedMultiply(LiteralCount, LiteralSize, LiteralBytes) ||
      !checkedAdd(Result.LiteralsOffset, LiteralBytes, Result.TextOffset))
    return false;

  uint64_t TextBytes = 0;
  if (!checkedMultiply(OperationCount, TextWordSize, TextBytes) ||
      !checkedAdd(Result.TextOffset, TextBytes, Cursor) ||
      !alignUp(Cursor, 8, Result.RelaOffset))
    return false;

  uint64_t RelaBytes = 0;
  if (!checkedMultiply(RelocationCount, RelaSize, RelaBytes) ||
      !checkedAdd(Result.RelaOffset, RelaBytes, Result.SymtabOffset) ||
      !checkedAdd(Result.SymtabOffset, SymbolCount * SymbolSize,
                  Result.StrtabOffset) ||
      !checkedAdd(Result.StrtabOffset, 1, Result.ShstrtabOffset) ||
      !checkedAdd(Result.ShstrtabOffset, sizeof(SectionNames), Cursor) ||
      !alignUp(Cursor, 8, Result.SectionHeadersOffset))
    return false;

  return checkedAdd(Result.SectionHeadersOffset,
                    SectionCount * SectionHeaderSize, Result.TotalSize);
}

bool computeDirectLayout(uint64_t FunctionCount, uint64_t RegisterCount,
                         uint64_t OperationCount, uint64_t DescriptorCount,
                         uint64_t LiteralCount, uint64_t RelocationCount,
                         DirectLayout &Result) {
  Result.TargetOffset = ElfHeaderSize;
  Result.FunctionsOffset = ElfHeaderSize + TargetSize;
  uint64_t FunctionBytes = 0;
  uint64_t Cursor = 0;
  if (!checkedMultiply(FunctionCount, DirectFunctionSize, FunctionBytes) ||
      !checkedAdd(Result.FunctionsOffset, FunctionBytes, Result.TypesOffset) ||
      !checkedAdd(Result.TypesOffset, RegisterCount, Cursor) ||
      !alignUp(Cursor, 8, Result.LiteralsOffset))
    return false;

  uint64_t LiteralBytes = 0;
  uint64_t DescriptorBytes = 0;
  uint64_t TextBytes = 0;
  if (!checkedMultiply(LiteralCount, LiteralSize, LiteralBytes) ||
      !checkedAdd(Result.LiteralsOffset, LiteralBytes,
                  Result.DescriptorsOffset) ||
      !checkedMultiply(DescriptorCount, DirectDescriptorSize,
                       DescriptorBytes) ||
      !checkedAdd(Result.DescriptorsOffset, DescriptorBytes,
                  Result.TextOffset) ||
      !checkedMultiply(OperationCount, TextWordSize, TextBytes) ||
      !checkedAdd(Result.TextOffset, TextBytes, Cursor) ||
      !alignUp(Cursor, 8, Result.RelaOffset))
    return false;

  uint64_t RelaBytes = 0;
  if (!checkedMultiply(RelocationCount, RelaSize, RelaBytes) ||
      !checkedAdd(Result.RelaOffset, RelaBytes, Result.SymtabOffset) ||
      !checkedAdd(Result.SymtabOffset, SymbolCount * SymbolSize,
                  Result.StrtabOffset) ||
      !checkedAdd(Result.StrtabOffset, 1, Result.ShstrtabOffset) ||
      !checkedAdd(Result.ShstrtabOffset, sizeof(DirectSectionNames), Cursor) ||
      !alignUp(Cursor, 8, Result.SectionHeadersOffset))
    return false;

  return checkedAdd(Result.SectionHeadersOffset,
                    DirectSectionCount * SectionHeaderSize, Result.TotalSize);
}

void storeU16(std::vector<uint8_t> &Bytes, uint64_t Offset, uint16_t Value) {
  Bytes[Offset] = static_cast<uint8_t>(Value);
  Bytes[Offset + 1] = static_cast<uint8_t>(Value >> 8);
}

void storeU32(std::vector<uint8_t> &Bytes, uint64_t Offset, uint32_t Value) {
  for (unsigned I = 0; I != 4; ++I)
    Bytes[Offset + I] = static_cast<uint8_t>(Value >> (I * 8));
}

void storeU64(std::vector<uint8_t> &Bytes, uint64_t Offset, uint64_t Value) {
  for (unsigned I = 0; I != 8; ++I)
    Bytes[Offset + I] = static_cast<uint8_t>(Value >> (I * 8));
}

void storeSection(std::vector<uint8_t> &Bytes, uint64_t Base, uint32_t Name,
                  uint32_t Type, uint64_t Flags, uint64_t Offset, uint64_t Size,
                  uint32_t Link, uint32_t Info, uint64_t Alignment,
                  uint64_t EntrySize) {
  storeU32(Bytes, Base, Name);
  storeU32(Bytes, Base + 4, Type);
  storeU64(Bytes, Base + 8, Flags);
  storeU64(Bytes, Base + 16, 0);
  storeU64(Bytes, Base + 24, Offset);
  storeU64(Bytes, Base + 32, Size);
  storeU32(Bytes, Base + 40, Link);
  storeU32(Bytes, Base + 44, Info);
  storeU64(Bytes, Base + 48, Alignment);
  storeU64(Bytes, Base + 56, EntrySize);
}

uint32_t packWord(uint32_t Opcode, uint32_t Payload) {
  return UINT32_C(3) | (Opcode << 2) | (UINT32_C(2) << 8) | (Payload << 10);
}

Expected<uint64_t> operand(const MCInst &Inst, unsigned Index) {
  if (Index >= Inst.getNumOperands() || !Inst.getOperand(Index).isImm())
    return fail("MC operand is not an immediate");
  return static_cast<uint64_t>(Inst.getOperand(Index).getImm());
}

Error requireOperands(const MCInst &Inst, unsigned Count) {
  if (Inst.getNumOperands() != Count)
    return fail("unexpected MC operand count");
  return Error::success();
}

uint64_t integerMask(uint64_t Kind) {
  switch (Kind) {
  case I8:
    return UINT64_C(0xff);
  case I32:
    return UINT64_C(0xffffffff);
  case I64:
    return UINT64_MAX;
  default:
    return 0;
  }
}

Expected<int64_t> relocationAddend(uint64_t Base, uint64_t Value) {
  if (Value >= Base) {
    const uint64_t Delta = Value - Base;
    if (Delta > static_cast<uint64_t>(INT64_MAX))
      return fail("positive relocation addend overflow");
    return static_cast<int64_t>(Delta);
  }
  const uint64_t Magnitude = Base - Value;
  if (Magnitude > (UINT64_C(1) << 63))
    return fail("negative relocation addend overflow");
  if (Magnitude == (UINT64_C(1) << 63))
    return INT64_MIN;
  return -static_cast<int64_t>(Magnitude);
}

unsigned literalIndex(ArrayRef<Literal> Literals, Literal Key) {
  return static_cast<unsigned>(llvm::lower_bound(Literals, Key) -
                               Literals.begin());
}

Expected<uint32_t> encodingOpcode(unsigned MCOpcode) {
  switch (MCOpcode) {
  case S2_CONSTANT:
    return UINT32_C(2);
  case S2_INTEGER_AND:
    return UINT32_C(4);
  case S2_BRANCH:
    return UINT32_C(18);
  case S2_BRANCH_IF:
    return UINT32_C(19);
  case S2_RETURN:
    return UINT32_C(20);
  case S2_PHYSICAL_ADDRESS:
    return UINT32_C(22);
  case S2_PHYSICAL_LOAD:
    return UINT32_C(23);
  case S2_PHYSICAL_STORE:
    return UINT32_C(24);
  default:
    return fail("unsupported MC opcode");
  }
}

Expected<uint32_t> directEncodingOpcode(unsigned MCOpcode) {
  if (MCOpcode == S2_DIRECT_CALL)
    return UINT32_C(16);
  if (MCOpcode == S2_RETURN_VALUE)
    return UINT32_C(20);
  return encodingOpcode(MCOpcode);
}

uint8_t semanticKindForFunctionResult(uint32_t ResultKind) {
  switch (ResultKind) {
  case 1:
    return I8;
  case 2:
    return I32;
  case 3:
    return I64;
  case 4:
    return PADDR;
  default:
    return UINT8_MAX;
  }
}

Expected<DirectDescriptor>
directDescriptor(const MCInst &Inst, ArrayRef<S2DirectFunction> Functions,
                 const S2DirectFunction &Caller) {
  if (Error E = requireOperands(Inst, 3))
    return std::move(E);
  auto Target = operand(Inst, 0);
  auto Result = operand(Inst, 1);
  auto Argument = operand(Inst, 2);
  if (!Target || !Result || !Argument)
    return joinErrors(Target.takeError(),
                      joinErrors(Result.takeError(), Argument.takeError()));
  if (*Target >= Functions.size() || *Target > UINT8_MAX ||
      *Result >= Caller.RegisterTypes.size() || *Result > UINT8_MAX ||
      *Argument >= Caller.RegisterTypes.size() || *Argument > UINT8_MAX)
    return fail("direct-call descriptor operand is out of range");
  const S2DirectFunction &Callee = Functions[*Target];
  if (!Callee.Present || Callee.ParameterSlots.size() != 1 ||
      Callee.ResultKind == 0)
    return fail("direct-call target signature is unsupported");
  const uint8_t Parameter = Callee.ParameterSlots[0];
  const uint8_t ResultType = semanticKindForFunctionResult(Callee.ResultKind);
  if (Parameter >= Callee.RegisterTypes.size() ||
      Caller.RegisterTypes[*Argument] != Callee.RegisterTypes[Parameter] ||
      ResultType == UINT8_MAX || Caller.RegisterTypes[*Result] != ResultType)
    return fail("direct-call descriptor type does not match its target");

  DirectDescriptor Descriptor{};
  Descriptor[0] = 0;
  Descriptor[1] = 1;
  Descriptor[2] = 1;
  Descriptor[3] = 0;
  Descriptor[4] = static_cast<uint8_t>(*Result);
  Descriptor[5] = static_cast<uint8_t>(*Target);
  Descriptor[6] = UINT8_MAX;
  Descriptor[7] = 0;
  Descriptor[8] = static_cast<uint8_t>(*Argument);
  Descriptor[9] = UINT8_MAX;
  Descriptor[10] = UINT8_MAX;
  Descriptor[11] = UINT8_MAX;
  Descriptor[12] = UINT8_MAX;
  Descriptor[13] = UINT8_MAX;
  Descriptor[14] = UINT8_MAX;
  Descriptor[15] = UINT8_MAX;
  Descriptor[16] = UINT8_MAX;
  return Descriptor;
}

} // namespace

Error S2TargetStreamer::setHeader(ArrayRef<uint8_t> Types, uint32_t EntryBlock,
                                  uint64_t Base) {
  if (HasHeader || !DirectFunctions.empty())
    return fail("header was already set");
  if (Types.empty() || Types.size() > 26)
    return fail("register type count is outside 1..26");
  for (uint8_t Type : Types)
    if (Type != I8 && Type != I32 && Type != I64 && Type != PADDR)
      return fail("unsupported register type");
  RegisterTypes.assign(Types.begin(), Types.end());
  Entry = EntryBlock;
  RelocationBase = Base;
  HasHeader = true;
  return Error::success();
}

Error S2TargetStreamer::beginDirectFunction(
    uint32_t FunctionIndex, ArrayRef<uint8_t> Types, uint32_t EntryOperation,
    uint32_t ResultKind, ArrayRef<uint8_t> ParameterSlots,
    ArrayRef<uint32_t> BlockStarts, uint64_t Base) {
  if (HasHeader)
    return fail("direct function follows a legacy header");
  if (FunctionIndex > 1)
    return fail("direct function index is outside 0..1");
  if (Types.empty() || Types.size() > 26 || ParameterSlots.size() > 4 ||
      BlockStarts.empty() || BlockStarts.size() > 128)
    return fail("direct function resource limit exceeded");
  for (uint8_t Type : Types)
    if (Type != I8 && Type != I32 && Type != I64 && Type != PADDR)
      return fail("unsupported direct function register type");
  for (uint8_t Slot : ParameterSlots)
    if (Slot >= Types.size())
      return fail("direct function parameter slot is out of range");
  if (ResultKind > 4)
    return fail("direct function result kind is out of range");
  if (!DirectFunctions.empty() && RelocationBase != Base)
    return fail("direct functions disagree on relocation base");
  if (DirectFunctions.size() <= FunctionIndex)
    DirectFunctions.resize(FunctionIndex + 1);
  S2DirectFunction &Function = DirectFunctions[FunctionIndex];
  if (Function.Present)
    return fail("direct function was emitted twice");
  Function.RegisterTypes.assign(Types.begin(), Types.end());
  Function.ParameterSlots.assign(ParameterSlots.begin(), ParameterSlots.end());
  Function.BlockStarts.assign(BlockStarts.begin(), BlockStarts.end());
  Function.Entry = EntryOperation;
  Function.ResultKind = ResultKind;
  Function.Present = true;
  RelocationBase = Base;
  CurrentDirectFunction = FunctionIndex;
  return Error::success();
}

Error S2TargetStreamer::emitInstruction(const MCInst &Inst) {
  if (CurrentDirectFunction) {
    S2DirectFunction &Function = DirectFunctions[*CurrentDirectFunction];
    if (Function.Instructions.size() == 128)
      return fail("direct function operation limit exceeded");
    Function.Instructions.push_back(Inst);
    return Error::success();
  }
  if (!HasHeader)
    return fail("instruction emitted before header");
  if (Instructions.size() == 128)
    return fail("operation limit exceeded");
  Instructions.push_back(Inst);
  return Error::success();
}

S2ObjectWriter::S2ObjectWriter(raw_pwrite_stream &Out, S2ObjectMode Mode)
    : Out(Out), Mode(Mode) {}

uint64_t S2ObjectWriter::writeObject() {
  llvm_unreachable(
      "generic MCAssembler emission cannot produce the exact Brace S2 object");
}

std::unique_ptr<S2ObjectWriter> createS2ObjectWriter(raw_pwrite_stream &Out,
                                                     S2ObjectMode Mode) {
  return std::make_unique<S2ObjectWriter>(Out, Mode);
}

Error S2TargetStreamer::writeObject(S2ObjectWriter &Writer) const {
  if (Writer.getMode() == S2ObjectMode::Legacy) {
    if (!HasHeader || !DirectFunctions.empty())
      return fail("legacy writer did not receive exactly one legacy header");
    return Writer.writeExact(RegisterTypes, Instructions, Entry,
                             RelocationBase);
  }
  if (HasHeader || DirectFunctions.size() != 2 || !DirectFunctions[0].Present ||
      !DirectFunctions[1].Present)
    return fail("direct writer requires exactly two direct functions");
  return Writer.writeDirectCallExact(DirectFunctions, /*EntryFunction=*/0,
                                     RelocationBase);
}

Error S2ObjectWriter::writeExact(ArrayRef<uint8_t> RegisterTypes,
                                 ArrayRef<MCInst> Instructions, uint32_t Entry,
                                 uint64_t RelocationBase) {
  if (Mode != S2ObjectMode::Legacy)
    return fail("legacy writeExact used with the direct-call writer mode");
  if (Instructions.empty() || Entry >= Instructions.size())
    return fail("entry is outside the operation table");

  auto HasType = [&](uint64_t Register, uint64_t Type) {
    return Register < RegisterTypes.size() && RegisterTypes[Register] == Type;
  };
  auto HasIntegerType = [&](uint64_t Register) {
    return Register < RegisterTypes.size() &&
           (RegisterTypes[Register] == I8 || RegisterTypes[Register] == I32 ||
            RegisterTypes[Register] == I64);
  };

  SmallVector<Literal, 64> Literals;
  for (const MCInst &Inst : Instructions) {
    switch (Inst.getOpcode()) {
    case S2_CONSTANT: {
      if (Error E = requireOperands(Inst, 3))
        return E;
      auto Destination = operand(Inst, 0);
      auto Kind = operand(Inst, 1);
      auto Value = operand(Inst, 2);
      if (!Destination || !Kind || !Value)
        return joinErrors(Destination.takeError(),
                          joinErrors(Kind.takeError(), Value.takeError()));
      const uint64_t Mask = integerMask(*Kind);
      if (Mask == 0 || !HasType(*Destination, *Kind) || (*Value & ~Mask) != 0)
        return fail("invalid Constant operands");
      Literals.push_back({false, *Value});
      break;
    }
    case S2_PHYSICAL_ADDRESS: {
      if (Error E = requireOperands(Inst, 2))
        return E;
      auto Destination = operand(Inst, 0);
      auto Address = operand(Inst, 1);
      if (!Destination || !Address)
        return joinErrors(Destination.takeError(), Address.takeError());
      if (!HasType(*Destination, PADDR))
        return fail("invalid PhysicalAddress destination");
      Literals.push_back({true, *Address});
      break;
    }
    default:
      break;
    }
  }
  llvm::sort(Literals);
  Literals.erase(std::unique(Literals.begin(), Literals.end()), Literals.end());
  if (Literals.size() > 256)
    return fail("literal limit exceeded");

  const uint64_t RelocationCount =
      llvm::count_if(Literals, [](const Literal &L) { return L.IsPhysical; });
  if (RelocationCount > 256)
    return fail("relocation limit exceeded");

  SmallVector<uint32_t, 128> Words;
  Words.reserve(Instructions.size());
  for (const MCInst &Inst : Instructions) {
    uint32_t Payload = 0;
    switch (Inst.getOpcode()) {
    case S2_CONSTANT: {
      auto Destination = operand(Inst, 0);
      auto Kind = operand(Inst, 1);
      auto Value = operand(Inst, 2);
      if (!Destination || !Kind || !Value)
        return joinErrors(Destination.takeError(),
                          joinErrors(Kind.takeError(), Value.takeError()));
      const unsigned LiteralIndex = literalIndex(Literals, {false, *Value});
      if (LiteralIndex >= 4096)
        return fail("Constant literal index overflow");
      Payload = static_cast<uint32_t>(*Destination | (*Kind << 5) |
                                      (LiteralIndex << 7));
      break;
    }
    case S2_INTEGER_AND: {
      if (Error E = requireOperands(Inst, 3))
        return E;
      auto Destination = operand(Inst, 0);
      auto Left = operand(Inst, 1);
      auto Right = operand(Inst, 2);
      if (!Destination || !Left || !Right)
        return joinErrors(Destination.takeError(),
                          joinErrors(Left.takeError(), Right.takeError()));
      if (!HasIntegerType(*Destination) || *Left >= RegisterTypes.size() ||
          *Right >= RegisterTypes.size() ||
          RegisterTypes[*Destination] != RegisterTypes[*Left] ||
          RegisterTypes[*Destination] != RegisterTypes[*Right])
        return fail("invalid IntegerAnd register types");
      Payload = static_cast<uint32_t>(*Destination | (UINT64_C(3) << 5) |
                                      (*Left << 9) | (*Right << 14));
      break;
    }
    case S2_BRANCH: {
      if (Error E = requireOperands(Inst, 1))
        return E;
      auto Target = operand(Inst, 0);
      if (!Target)
        return Target.takeError();
      if (*Target >= Instructions.size())
        return fail("Branch target is outside the operation table");
      Payload = static_cast<uint32_t>(*Target);
      break;
    }
    case S2_BRANCH_IF: {
      if (Error E = requireOperands(Inst, 3))
        return E;
      auto Condition = operand(Inst, 0);
      auto TrueTarget = operand(Inst, 1);
      auto FalseTarget = operand(Inst, 2);
      if (!Condition || !TrueTarget || !FalseTarget)
        return joinErrors(
            Condition.takeError(),
            joinErrors(TrueTarget.takeError(), FalseTarget.takeError()));
      if (!HasIntegerType(*Condition) || *TrueTarget >= Instructions.size() ||
          *FalseTarget >= Instructions.size())
        return fail("invalid BranchIf operands");
      Payload = static_cast<uint32_t>(*Condition | (*TrueTarget << 5) |
                                      (*FalseTarget << 12));
      break;
    }
    case S2_RETURN:
      if (Error E = requireOperands(Inst, 0))
        return E;
      break;
    case S2_PHYSICAL_ADDRESS: {
      if (Error E = requireOperands(Inst, 2))
        return E;
      auto Destination = operand(Inst, 0);
      auto Address = operand(Inst, 1);
      if (!Destination || !Address)
        return joinErrors(Destination.takeError(), Address.takeError());
      const unsigned LiteralIndex = literalIndex(Literals, {true, *Address});
      if (LiteralIndex >= 4096)
        return fail("PhysicalAddress literal index overflow");
      Payload = static_cast<uint32_t>(*Destination | (LiteralIndex << 5));
      break;
    }
    case S2_PHYSICAL_LOAD: {
      if (Error E = requireOperands(Inst, 3))
        return E;
      auto Destination = operand(Inst, 0);
      auto Width = operand(Inst, 1);
      auto Address = operand(Inst, 2);
      if (!Destination || !Width || !Address)
        return joinErrors(Destination.takeError(),
                          joinErrors(Width.takeError(), Address.takeError()));
      const uint64_t ExpectedType = *Width == U8 ? I8 : I32;
      if ((*Width != U8 && *Width != U32) ||
          !HasType(*Destination, ExpectedType) || !HasType(*Address, PADDR))
        return fail("invalid PhysicalLoad operands");
      Payload =
          static_cast<uint32_t>(*Destination | (*Width << 5) | (*Address << 6));
      break;
    }
    case S2_PHYSICAL_STORE: {
      if (Error E = requireOperands(Inst, 3))
        return E;
      auto Width = operand(Inst, 0);
      auto Address = operand(Inst, 1);
      auto Source = operand(Inst, 2);
      if (!Width || !Address || !Source)
        return joinErrors(Width.takeError(),
                          joinErrors(Address.takeError(), Source.takeError()));
      const uint64_t ExpectedType = *Width == U8 ? I8 : I32;
      if ((*Width != U8 && *Width != U32) || !HasType(*Address, PADDR) ||
          !HasType(*Source, ExpectedType))
        return fail("invalid PhysicalStore operands");
      Payload =
          static_cast<uint32_t>(*Width | (*Address << 1) | (*Source << 6));
      break;
    }
    default:
      return fail("unsupported MC opcode");
    }
    auto EncodingOpcode = encodingOpcode(Inst.getOpcode());
    if (!EncodingOpcode)
      return EncodingOpcode.takeError();
    Words.push_back(packWord(*EncodingOpcode, Payload));
  }

  Layout L;
  if (!computeLayout(RegisterTypes.size(), Instructions.size(), Literals.size(),
                     RelocationCount, L))
    return fail("object layout overflow");
  if (L.TotalSize > 1048576)
    return fail("object size limit exceeded");

  std::vector<uint8_t> Bytes(L.TotalSize, 0);
  Bytes[0] = 0x7f;
  Bytes[1] = 'E';
  Bytes[2] = 'L';
  Bytes[3] = 'F';
  Bytes[4] = 2;
  Bytes[5] = 1;
  Bytes[6] = 1;
  Bytes[7] = ExperimentalOSABI;
  storeU16(Bytes, 16, 1);
  storeU16(Bytes, 18, ExperimentalMachine);
  storeU32(Bytes, 20, 1);
  storeU64(Bytes, 40, L.SectionHeadersOffset);
  storeU32(Bytes, 48, ExperimentalFlags);
  storeU16(Bytes, 52, 64);
  storeU16(Bytes, 58, 64);
  storeU16(Bytes, 60, 9);
  storeU16(Bytes, 62, 8);

  storeU32(Bytes, L.TargetOffset, 0);
  storeU32(Bytes, L.TargetOffset + 4, 0);
  storeU32(Bytes, L.TargetOffset + 8, 1);
  storeU32(Bytes, L.TargetOffset + 12, 0);
  storeU32(Bytes, L.TargetOffset + 16, 0);
  storeU32(Bytes, L.TargetOffset + 20, 1);
  storeU32(Bytes, L.TargetOffset + 24, 0);
  storeU32(Bytes, L.TargetOffset + 28, Entry);

  llvm::copy(RegisterTypes, Bytes.begin() + L.TypesOffset);
  for (size_t I = 0; I != Literals.size(); ++I)
    storeU64(Bytes, L.LiteralsOffset + I * LiteralSize,
             Literals[I].IsPhysical ? 0 : Literals[I].Value);
  for (size_t I = 0; I != Words.size(); ++I)
    storeU32(Bytes, L.TextOffset + I * TextWordSize, Words[I]);

  size_t RelocationIndex = 0;
  for (size_t I = 0; I != Literals.size(); ++I) {
    if (!Literals[I].IsPhysical)
      continue;
    auto Addend = relocationAddend(RelocationBase, Literals[I].Value);
    if (!Addend)
      return Addend.takeError();
    const uint64_t Base = L.RelaOffset + RelocationIndex * RelaSize;
    storeU64(Bytes, Base, I * LiteralSize);
    storeU64(Bytes, Base + 8, RelaInfo);
    storeU64(Bytes, Base + 16, static_cast<uint64_t>(*Addend));
    ++RelocationIndex;
  }

  storeU16(Bytes, L.SymtabOffset + SymbolSize + 6, AbsoluteSection);
  storeU64(Bytes, L.SymtabOffset + SymbolSize + 8, RelocationBase);
  std::copy(std::begin(SectionNames), std::end(SectionNames),
            Bytes.begin() + L.ShstrtabOffset);

  const uint64_t SH = L.SectionHeadersOffset;
  storeSection(Bytes, SH + SectionHeaderSize, 1, 1, 0, L.TargetOffset,
               TargetSize, 0, 0, 8, TargetSize);
  storeSection(Bytes, SH + 2 * SectionHeaderSize, 15, 1, 0, L.TypesOffset,
               RegisterTypes.size(), 0, 0, 1, 1);
  storeSection(Bytes, SH + 3 * SectionHeaderSize, 28, 1, 0, L.LiteralsOffset,
               Literals.size() * LiteralSize, 0, 0, 8, LiteralSize);
  storeSection(Bytes, SH + 4 * SectionHeaderSize, 44, 1, 6, L.TextOffset,
               Words.size() * TextWordSize, 0, 0, 4, TextWordSize);
  storeSection(Bytes, SH + 5 * SectionHeaderSize, 56, 4, 0, L.RelaOffset,
               RelocationCount * RelaSize, 6, 3, 8, RelaSize);
  storeSection(Bytes, SH + 6 * SectionHeaderSize, 77, 2, 0, L.SymtabOffset,
               SymbolCount * SymbolSize, 7, 2, 8, SymbolSize);
  storeSection(Bytes, SH + 7 * SectionHeaderSize, 85, 3, 0, L.StrtabOffset, 1,
               0, 0, 1, 0);
  storeSection(Bytes, SH + 8 * SectionHeaderSize, 93, 3, 0, L.ShstrtabOffset,
               sizeof(SectionNames), 0, 0, 1, 0);

  Out.write(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  Out.flush();
  return Error::success();
}

Error S2ObjectWriter::writeDirectCallExact(ArrayRef<S2DirectFunction> Functions,
                                           uint32_t EntryFunction,
                                           uint64_t RelocationBase) {
  if (Mode != S2ObjectMode::DirectCall)
    return fail("direct-call write used with the legacy writer mode");
  if (Functions.size() != 2 || EntryFunction != 0 || !Functions[0].Present ||
      !Functions[1].Present)
    return fail("direct-call writer requires entry function 0 and leaf 1");

  constexpr std::array<uint8_t, 6> RequiredTypes{PADDR, PADDR, I8,
                                                 I8,    I32,   I32};
  if (Functions[0].RegisterTypes != ArrayRef<uint8_t>(RequiredTypes) ||
      Functions[1].RegisterTypes != ArrayRef<uint8_t>(RequiredTypes) ||
      !Functions[0].ParameterSlots.empty() || Functions[0].ResultKind != 0 ||
      Functions[1].ParameterSlots != ArrayRef<uint8_t>({4}) ||
      Functions[1].ResultKind != 2)
    return fail("direct-call functions do not have the exact S3b.5 signature");

  uint64_t RegisterCount = 0;
  uint64_t OperationCount = 0;
  for (const S2DirectFunction &Function : Functions) {
    if (Function.Instructions.empty() || Function.Instructions.size() > 128 ||
        Function.Entry >= Function.Instructions.size())
      return fail("direct-call function operation range is invalid");
    if (!checkedAdd(RegisterCount, Function.RegisterTypes.size(),
                    RegisterCount) ||
        !checkedAdd(OperationCount, Function.Instructions.size(),
                    OperationCount))
      return fail("direct-call resource count overflow");
  }
  if (RegisterCount > 52 || OperationCount > 256)
    return fail("direct-call module resource limit exceeded");

  SmallVector<Literal, 64> Literals;
  for (const S2DirectFunction &Function : Functions) {
    for (const MCInst &Inst : Function.Instructions) {
      switch (Inst.getOpcode()) {
      case S2_CONSTANT: {
        if (Error E = requireOperands(Inst, 3))
          return E;
        auto Kind = operand(Inst, 1);
        auto Value = operand(Inst, 2);
        if (!Kind || !Value)
          return joinErrors(Kind.takeError(), Value.takeError());
        Literals.push_back({false, *Value});
        break;
      }
      case S2_PHYSICAL_ADDRESS: {
        if (Error E = requireOperands(Inst, 2))
          return E;
        auto Address = operand(Inst, 1);
        if (!Address)
          return Address.takeError();
        Literals.push_back({true, *Address});
        break;
      }
      default:
        break;
      }
    }
  }
  llvm::sort(Literals);
  Literals.erase(std::unique(Literals.begin(), Literals.end()), Literals.end());
  if (Literals.size() > 256)
    return fail("direct-call literal limit exceeded");
  const uint64_t RelocationCount =
      llvm::count_if(Literals, [](const Literal &L) { return L.IsPhysical; });
  if (RelocationCount > 256)
    return fail("direct-call relocation limit exceeded");

  std::array<SmallVector<DirectDescriptor, 4>, 2> FunctionDescriptors;
  uint64_t DescriptorCount = 0;
  for (unsigned FunctionIndex = 0; FunctionIndex != Functions.size();
       ++FunctionIndex) {
    const S2DirectFunction &Function = Functions[FunctionIndex];
    for (const MCInst &Inst : Function.Instructions) {
      if (Inst.getOpcode() != S2_DIRECT_CALL)
        continue;
      auto Descriptor = directDescriptor(Inst, Functions, Function);
      if (!Descriptor)
        return Descriptor.takeError();
      FunctionDescriptors[FunctionIndex].push_back(*Descriptor);
    }
    llvm::sort(FunctionDescriptors[FunctionIndex]);
    FunctionDescriptors[FunctionIndex].erase(
        std::unique(FunctionDescriptors[FunctionIndex].begin(),
                    FunctionDescriptors[FunctionIndex].end()),
        FunctionDescriptors[FunctionIndex].end());
    if (FunctionDescriptors[FunctionIndex].size() > 128 ||
        !checkedAdd(DescriptorCount, FunctionDescriptors[FunctionIndex].size(),
                    DescriptorCount))
      return fail("direct-call descriptor limit exceeded");
  }
  if (DescriptorCount != 1)
    return fail("S3b.5 compiler profile requires exactly one descriptor");

  std::array<SmallVector<uint32_t, 128>, 2> FunctionWords;
  unsigned CallCount = 0;
  uint64_t BasicBlockCount = 0;
  uint64_t CFGEdgeCount = 0;
  uint64_t PhysicalMemoryCount = 0;
  for (unsigned FunctionIndex = 0; FunctionIndex != Functions.size();
       ++FunctionIndex) {
    const S2DirectFunction &Function = Functions[FunctionIndex];
    auto HasType = [&](uint64_t Register, uint64_t Type) {
      return Register < Function.RegisterTypes.size() &&
             Function.RegisterTypes[Register] == Type;
    };
    auto HasIntegerType = [&](uint64_t Register) {
      return Register < Function.RegisterTypes.size() &&
             (Function.RegisterTypes[Register] == I8 ||
              Function.RegisterTypes[Register] == I32 ||
              Function.RegisterTypes[Register] == I64);
    };
    const unsigned FunctionSize = Function.Instructions.size();
    if (Function.BlockStarts.empty() || Function.BlockStarts.front() != 0 ||
        Function.Entry != 0 || Function.BlockStarts.back() >= FunctionSize)
      return fail("direct-call block-start range is invalid");
    for (unsigned BlockIndex = 1; BlockIndex != Function.BlockStarts.size();
         ++BlockIndex)
      if (Function.BlockStarts[BlockIndex - 1] >=
          Function.BlockStarts[BlockIndex])
        return fail("direct-call block starts are not strictly increasing");
    auto IsBlockStart = [&](uint64_t OperationIndex) {
      return llvm::binary_search(Function.BlockStarts,
                                 static_cast<uint32_t>(OperationIndex));
    };
    SmallVector<uint32_t, 128> ReadMasks(FunctionSize, 0);
    SmallVector<uint32_t, 128> DefinitionMasks(FunctionSize, 0);
    SmallVector<bool, 128> CallTransfers(FunctionSize, false);
    SmallVector<bool, 128> Terminators(FunctionSize, false);
    SmallVector<SmallVector<unsigned, 2>, 128> Successors(FunctionSize);
    SmallVector<SmallVector<unsigned, 4>, 128> Predecessors(FunctionSize);
    std::optional<unsigned> CallOperation;
    unsigned ReturnCount = 0;

    auto Read = [&](unsigned OperationIndex, uint64_t Register) -> Error {
      if (Register >= Function.RegisterTypes.size() || Register >= 26)
        return fail("direct-call read register is out of range");
      ReadMasks[OperationIndex] |= UINT32_C(1) << Register;
      return Error::success();
    };
    auto Define = [&](unsigned OperationIndex, uint64_t Register) -> Error {
      if (Register >= Function.RegisterTypes.size() || Register >= 26)
        return fail("direct-call definition register is out of range");
      DefinitionMasks[OperationIndex] |= UINT32_C(1) << Register;
      return Error::success();
    };
    auto AddSuccessor = [&](unsigned OperationIndex, uint64_t Target) -> Error {
      if (Target >= FunctionSize)
        return fail("direct-call branch target is outside its function");
      Successors[OperationIndex].push_back(static_cast<unsigned>(Target));
      Predecessors[Target].push_back(OperationIndex);
      return Error::success();
    };

    for (unsigned OperationIndex = 0;
         OperationIndex != Function.Instructions.size(); ++OperationIndex) {
      const MCInst &Inst = Function.Instructions[OperationIndex];
      uint32_t Payload = 0;
      bool Terminates = false;
      switch (Inst.getOpcode()) {
      case S2_CONSTANT: {
        if (Error E = requireOperands(Inst, 3))
          return E;
        auto Destination = operand(Inst, 0);
        auto Kind = operand(Inst, 1);
        auto Value = operand(Inst, 2);
        if (!Destination || !Kind || !Value)
          return joinErrors(Destination.takeError(),
                            joinErrors(Kind.takeError(), Value.takeError()));
        const uint64_t Mask = integerMask(*Kind);
        if (Mask == 0 || !HasType(*Destination, *Kind) || (*Value & ~Mask) != 0)
          return fail("invalid direct-call Constant operands");
        const unsigned LiteralIndex = literalIndex(Literals, {false, *Value});
        if (LiteralIndex >= 4096)
          return fail("direct-call Constant literal index overflow");
        Payload = static_cast<uint32_t>(*Destination | (*Kind << 5) |
                                        (LiteralIndex << 7));
        if (Error E = Define(OperationIndex, *Destination))
          return E;
        break;
      }
      case S2_INTEGER_AND: {
        if (Error E = requireOperands(Inst, 3))
          return E;
        auto Destination = operand(Inst, 0);
        auto Left = operand(Inst, 1);
        auto Right = operand(Inst, 2);
        if (!Destination || !Left || !Right)
          return joinErrors(Destination.takeError(),
                            joinErrors(Left.takeError(), Right.takeError()));
        if (!HasIntegerType(*Destination) ||
            !HasType(*Left, Function.RegisterTypes[*Destination]) ||
            !HasType(*Right, Function.RegisterTypes[*Destination]))
          return fail("invalid direct-call IntegerAnd register types");
        if (Error E = Read(OperationIndex, *Left))
          return E;
        if (Error E = Read(OperationIndex, *Right))
          return E;
        Payload = static_cast<uint32_t>(*Destination | (UINT64_C(3) << 5) |
                                        (*Left << 9) | (*Right << 14));
        if (Error E = Define(OperationIndex, *Destination))
          return E;
        break;
      }
      case S2_DIRECT_CALL: {
        if (FunctionIndex != 0 || ++CallCount != 1)
          return fail("direct call is outside the exact root profile");
        auto Descriptor = directDescriptor(Inst, Functions, Function);
        if (!Descriptor)
          return Descriptor.takeError();
        auto Position =
            llvm::lower_bound(FunctionDescriptors[FunctionIndex], *Descriptor);
        if (Position == FunctionDescriptors[FunctionIndex].end() ||
            *Position != *Descriptor)
          return fail("direct-call descriptor was not canonicalized");
        const unsigned DescriptorIndex = static_cast<unsigned>(
            Position - FunctionDescriptors[FunctionIndex].begin());
        const uint8_t Result = (*Descriptor)[4];
        const uint8_t Argument = (*Descriptor)[8];
        if (Error E = Read(OperationIndex, Argument))
          return E;
        if (Error E = Define(OperationIndex, Result))
          return E;
        CallTransfers[OperationIndex] = true;
        CallOperation = OperationIndex;
        Payload = DescriptorIndex;
        break;
      }
      case S2_BRANCH: {
        if (Error E = requireOperands(Inst, 1))
          return E;
        auto Target = operand(Inst, 0);
        if (!Target)
          return Target.takeError();
        if (!IsBlockStart(*Target))
          return fail("direct-call branch target is not a block start");
        if (Error E = AddSuccessor(OperationIndex, *Target))
          return E;
        Payload = static_cast<uint32_t>(*Target);
        Terminates = true;
        break;
      }
      case S2_BRANCH_IF: {
        if (Error E = requireOperands(Inst, 3))
          return E;
        auto Condition = operand(Inst, 0);
        auto TrueTarget = operand(Inst, 1);
        auto FalseTarget = operand(Inst, 2);
        if (!Condition || !TrueTarget || !FalseTarget)
          return joinErrors(
              Condition.takeError(),
              joinErrors(TrueTarget.takeError(), FalseTarget.takeError()));
        if (!HasIntegerType(*Condition) || *TrueTarget == *FalseTarget)
          return fail("invalid direct-call BranchIf operands");
        if (!IsBlockStart(*TrueTarget) || !IsBlockStart(*FalseTarget))
          return fail("direct-call branch target is not a block start");
        if (Error E = Read(OperationIndex, *Condition))
          return E;
        if (Error E = AddSuccessor(OperationIndex, *TrueTarget))
          return E;
        if (Error E = AddSuccessor(OperationIndex, *FalseTarget))
          return E;
        Payload = static_cast<uint32_t>(*Condition | (*TrueTarget << 5) |
                                        (*FalseTarget << 12));
        Terminates = true;
        break;
      }
      case S2_RETURN:
        if (Error E = requireOperands(Inst, 0))
          return E;
        if (FunctionIndex != 0)
          return fail("void Return is outside the entry terminator");
        ++ReturnCount;
        Terminates = true;
        break;
      case S2_RETURN_VALUE: {
        if (Error E = requireOperands(Inst, 1))
          return E;
        auto Value = operand(Inst, 0);
        if (!Value)
          return Value.takeError();
        if (FunctionIndex != 1 || !HasType(*Value, I32))
          return fail("valued Return is outside the i32 leaf terminator");
        if (Error E = Read(OperationIndex, *Value))
          return E;
        Payload = static_cast<uint32_t>(*Value | (UINT64_C(1) << 5));
        ++ReturnCount;
        Terminates = true;
        break;
      }
      case S2_PHYSICAL_ADDRESS: {
        if (Error E = requireOperands(Inst, 2))
          return E;
        auto Destination = operand(Inst, 0);
        auto Address = operand(Inst, 1);
        if (!Destination || !Address)
          return joinErrors(Destination.takeError(), Address.takeError());
        if (!HasType(*Destination, PADDR))
          return fail("invalid direct-call PhysicalAddress destination");
        const unsigned LiteralIndex = literalIndex(Literals, {true, *Address});
        if (LiteralIndex >= 4096)
          return fail("direct-call PhysicalAddress literal index overflow");
        Payload = static_cast<uint32_t>(*Destination | (LiteralIndex << 5));
        if (Error E = Define(OperationIndex, *Destination))
          return E;
        break;
      }
      case S2_PHYSICAL_LOAD: {
        if (++PhysicalMemoryCount > 64)
          return fail(
              "direct-call module physical memory operation limit exceeded");
        if (Error E = requireOperands(Inst, 3))
          return E;
        auto Destination = operand(Inst, 0);
        auto Width = operand(Inst, 1);
        auto Address = operand(Inst, 2);
        if (!Destination || !Width || !Address)
          return joinErrors(Destination.takeError(),
                            joinErrors(Width.takeError(), Address.takeError()));
        const uint64_t ExpectedType = *Width == U8 ? I8 : I32;
        if ((*Width != U8 && *Width != U32) ||
            !HasType(*Destination, ExpectedType) || !HasType(*Address, PADDR))
          return fail("invalid direct-call PhysicalLoad operands");
        if (Error E = Read(OperationIndex, *Address))
          return E;
        Payload = static_cast<uint32_t>(*Destination | (*Width << 5) |
                                        (*Address << 6));
        if (Error E = Define(OperationIndex, *Destination))
          return E;
        break;
      }
      case S2_PHYSICAL_STORE: {
        if (++PhysicalMemoryCount > 64)
          return fail(
              "direct-call module physical memory operation limit exceeded");
        if (Error E = requireOperands(Inst, 3))
          return E;
        auto Width = operand(Inst, 0);
        auto Address = operand(Inst, 1);
        auto Source = operand(Inst, 2);
        if (!Width || !Address || !Source)
          return joinErrors(Width.takeError(), joinErrors(Address.takeError(),
                                                          Source.takeError()));
        const uint64_t ExpectedType = *Width == U8 ? I8 : I32;
        if ((*Width != U8 && *Width != U32) || !HasType(*Address, PADDR) ||
            !HasType(*Source, ExpectedType))
          return fail("invalid direct-call PhysicalStore operands");
        if (Error E = Read(OperationIndex, *Address))
          return E;
        if (Error E = Read(OperationIndex, *Source))
          return E;
        Payload =
            static_cast<uint32_t>(*Width | (*Address << 1) | (*Source << 6));
        break;
      }
      default:
        return fail("unsupported direct-call MC opcode");
      }
      auto EncodingOpcode = directEncodingOpcode(Inst.getOpcode());
      if (!EncodingOpcode)
        return EncodingOpcode.takeError();
      FunctionWords[FunctionIndex].push_back(
          packWord(*EncodingOpcode, Payload));
      Terminators[OperationIndex] = Terminates;
      if (!Terminates) {
        if (OperationIndex + 1 == FunctionSize)
          return fail("direct-call function falls past its operation range");
        if (Error E = AddSuccessor(OperationIndex, OperationIndex + 1))
          return E;
      }
    }

    if (ReturnCount == 0)
      return fail("direct-call function has no Return");

    // The streamer carries the actual non-debug MachineBasicBlock boundaries
    // as target-private transient metadata.  Independently validate those
    // boundaries against the flattened operations and derive every explicit
    // or implicit-fallthrough block edge before publication.
    const uint64_t FunctionBlockCount = Function.BlockStarts.size();
    uint64_t FunctionEdgeCount = 0;
    for (unsigned BlockIndex = 0; BlockIndex != Function.BlockStarts.size();
         ++BlockIndex) {
      const unsigned Start = Function.BlockStarts[BlockIndex];
      const unsigned End = BlockIndex + 1 == Function.BlockStarts.size()
                               ? FunctionSize
                               : Function.BlockStarts[BlockIndex + 1];
      if (Start == End)
        return fail("direct-call block is empty");
      for (unsigned OperationIndex = Start; OperationIndex + 1 != End;
           ++OperationIndex)
        if (Terminators[OperationIndex])
          return fail("direct-call terminator precedes its block end");
      const unsigned Last = End - 1;
      if (Terminators[Last]) {
        if (!checkedAdd(FunctionEdgeCount, Successors[Last].size(),
                        FunctionEdgeCount))
          return fail("direct-call CFG edge count overflow");
      } else {
        if (End == FunctionSize || Successors[Last].size() != 1 ||
            Successors[Last][0] != End)
          return fail("direct-call block fallthrough is not canonical");
        if (!checkedAdd(FunctionEdgeCount, UINT64_C(1), FunctionEdgeCount))
          return fail("direct-call CFG edge count overflow");
      }
    }
    if (FunctionBlockCount > 4 || FunctionEdgeCount > 8)
      return fail("direct-call function CFG resource limit exceeded");
    if (!checkedAdd(BasicBlockCount, FunctionBlockCount, BasicBlockCount) ||
        !checkedAdd(CFGEdgeCount, FunctionEdgeCount, CFGEdgeCount))
      return fail("direct-call module CFG resource count overflow");
    if (BasicBlockCount > 8 || CFGEdgeCount > 16)
      return fail("direct-call module CFG resource limit exceeded");

    SmallVector<bool, 128> Reachable(FunctionSize, false);
    SmallVector<unsigned, 128> Worklist{Function.Entry};
    while (!Worklist.empty()) {
      const unsigned OperationIndex = Worklist.pop_back_val();
      if (Reachable[OperationIndex])
        continue;
      Reachable[OperationIndex] = true;
      llvm::append_range(Worklist, Successors[OperationIndex]);
    }
    if (llvm::any_of(Reachable, [](bool Value) { return !Value; }))
      return fail("direct-call function contains an unreachable operation");

    const uint32_t AllRegisters =
        (UINT32_C(1) << Function.RegisterTypes.size()) - 1;
    uint32_t EntryState = 0;
    for (uint8_t Parameter : Function.ParameterSlots)
      EntryState |= UINT32_C(1) << Parameter;
    SmallVector<uint32_t, 128> In(FunctionSize, AllRegisters);
    SmallVector<uint32_t, 128> Out(FunctionSize, AllRegisters);
    auto Transfer = [&](unsigned OperationIndex, uint32_t State) {
      if (CallTransfers[OperationIndex])
        return DefinitionMasks[OperationIndex];
      return State | DefinitionMasks[OperationIndex];
    };
    In[Function.Entry] = EntryState;
    Out[Function.Entry] = Transfer(Function.Entry, EntryState);
    bool Changed = true;
    unsigned Iterations = 0;
    while (Changed) {
      if (++Iterations > 512)
        return fail("direct-call definition analysis did not converge");
      Changed = false;
      for (unsigned OperationIndex = 0; OperationIndex != FunctionSize;
           ++OperationIndex) {
        uint32_t NewIn = EntryState;
        if (OperationIndex != Function.Entry) {
          NewIn = AllRegisters;
          if (Predecessors[OperationIndex].empty())
            return fail("direct-call non-entry operation has no predecessor");
          for (unsigned Predecessor : Predecessors[OperationIndex])
            NewIn &= Out[Predecessor];
        }
        const uint32_t NewOut = Transfer(OperationIndex, NewIn);
        if (In[OperationIndex] != NewIn || Out[OperationIndex] != NewOut) {
          In[OperationIndex] = NewIn;
          Out[OperationIndex] = NewOut;
          Changed = true;
        }
      }
    }
    for (unsigned OperationIndex = 0; OperationIndex != FunctionSize;
         ++OperationIndex)
      if ((ReadMasks[OperationIndex] & ~In[OperationIndex]) != 0)
        return fail("direct-call register is read before definition");

    if (FunctionIndex == 0 && CallOperation) {
      SmallVector<bool, 128> BeforeCall(FunctionSize, false);
      Worklist.clear();
      Worklist.push_back(Function.Entry);
      while (!Worklist.empty()) {
        const unsigned OperationIndex = Worklist.pop_back_val();
        if (BeforeCall[OperationIndex])
          continue;
        BeforeCall[OperationIndex] = true;
        if (OperationIndex == *CallOperation)
          continue;
        if (Function.Instructions[OperationIndex].getOpcode() == S2_RETURN)
          return fail("entry can Return before its direct call");
        llvm::append_range(Worklist, Successors[OperationIndex]);
      }
      SmallVector<bool, 128> AfterCall(FunctionSize, false);
      Worklist.clear();
      llvm::append_range(Worklist, Successors[*CallOperation]);
      while (!Worklist.empty()) {
        const unsigned OperationIndex = Worklist.pop_back_val();
        if (OperationIndex == *CallOperation)
          return fail("entry direct call can execute more than once");
        if (AfterCall[OperationIndex])
          continue;
        AfterCall[OperationIndex] = true;
        llvm::append_range(Worklist, Successors[OperationIndex]);
      }

      // Reconstruct the semantic provenance of the returned r4 rather than
      // accepting any later r4 read.  The three-bit union lattice retains all
      // path states at joins: the original result is either unconsumed,
      // consumed, or already overwritten.  Every reachable root Return must
      // see only consumed paths, and no reachable path may overwrite the
      // result before its first read.
      constexpr uint8_t Unconsumed = UINT8_C(1) << 0;
      constexpr uint8_t Consumed = UINT8_C(1) << 1;
      constexpr uint8_t Overwritten = UINT8_C(1) << 2;
      constexpr uint32_t ResultRegister = UINT32_C(1) << 4;
      SmallVector<uint8_t, 128> ResultIn(FunctionSize, 0);
      SmallVector<uint8_t, 128> ResultOut(FunctionSize, 0);
      Changed = true;
      Iterations = 0;
      while (Changed) {
        if (++Iterations > 512)
          return fail(
              "direct-call result provenance analysis did not converge");
        Changed = false;
        for (unsigned OperationIndex = 0; OperationIndex != FunctionSize;
             ++OperationIndex) {
          if (!AfterCall[OperationIndex])
            continue;
          uint8_t NewIn = 0;
          for (unsigned Predecessor : Predecessors[OperationIndex]) {
            if (Predecessor == *CallOperation)
              NewIn |= Unconsumed;
            else if (AfterCall[Predecessor])
              NewIn |= ResultOut[Predecessor];
          }
          uint8_t NewOut = NewIn;
          if ((NewOut & Unconsumed) != 0) {
            if ((ReadMasks[OperationIndex] & ResultRegister) != 0) {
              NewOut = static_cast<uint8_t>((NewOut & ~Unconsumed) | Consumed);
            } else if ((DefinitionMasks[OperationIndex] & ResultRegister) !=
                       0) {
              NewOut =
                  static_cast<uint8_t>((NewOut & ~Unconsumed) | Overwritten);
            }
          }
          if (ResultIn[OperationIndex] != NewIn ||
              ResultOut[OperationIndex] != NewOut) {
            ResultIn[OperationIndex] = NewIn;
            ResultOut[OperationIndex] = NewOut;
            Changed = true;
          }
        }
      }
      for (unsigned OperationIndex = 0; OperationIndex != FunctionSize;
           ++OperationIndex) {
        if (!AfterCall[OperationIndex])
          continue;
        if ((ResultOut[OperationIndex] & Overwritten) != 0)
          return fail("direct-call result is overwritten before consumption");
        if (Function.Instructions[OperationIndex].getOpcode() == S2_RETURN &&
            ResultOut[OperationIndex] != Consumed)
          return fail(
              "direct-call result is not consumed on every root exit path");
      }
    }
  }
  if (CallCount != 1)
    return fail("S3b.5 compiler profile requires exactly one direct call");

  DirectLayout L;
  if (!computeDirectLayout(Functions.size(), RegisterCount, OperationCount,
                           DescriptorCount, Literals.size(), RelocationCount,
                           L))
    return fail("direct-call object layout overflow");
  if (L.TotalSize > 1048576)
    return fail("direct-call object size limit exceeded");

  std::vector<uint8_t> Bytes(L.TotalSize, 0);
  Bytes[0] = 0x7f;
  Bytes[1] = 'E';
  Bytes[2] = 'L';
  Bytes[3] = 'F';
  Bytes[4] = 2;
  Bytes[5] = 1;
  Bytes[6] = 1;
  Bytes[7] = ExperimentalOSABI;
  storeU16(Bytes, 16, 1);
  storeU16(Bytes, 18, ExperimentalMachine);
  storeU32(Bytes, 20, 1);
  storeU64(Bytes, 40, L.SectionHeadersOffset);
  storeU32(Bytes, 48, DirectExperimentalFlags);
  storeU16(Bytes, 52, 64);
  storeU16(Bytes, 58, 64);
  storeU16(Bytes, 60, DirectSectionCount);
  storeU16(Bytes, 62, 10);

  storeU32(Bytes, L.TargetOffset, 0);
  storeU32(Bytes, L.TargetOffset + 4, 0);
  storeU32(Bytes, L.TargetOffset + 8, 1);
  storeU32(Bytes, L.TargetOffset + 12, 0);
  storeU32(Bytes, L.TargetOffset + 16, 0);
  storeU32(Bytes, L.TargetOffset + 20, 1);
  storeU32(Bytes, L.TargetOffset + 24, 0);
  storeU32(Bytes, L.TargetOffset + 28, EntryFunction);

  uint32_t RegisterFirst = 0;
  uint32_t OperationFirst = 0;
  uint32_t DescriptorFirst = 0;
  uint64_t TypeCursor = L.TypesOffset;
  uint64_t TextCursor = L.TextOffset;
  uint64_t DescriptorCursor = L.DescriptorsOffset;
  for (unsigned FunctionIndex = 0; FunctionIndex != Functions.size();
       ++FunctionIndex) {
    const S2DirectFunction &Function = Functions[FunctionIndex];
    const uint64_t Record =
        L.FunctionsOffset + FunctionIndex * DirectFunctionSize;
    storeU32(Bytes, Record, Function.Entry);
    storeU32(Bytes, Record + 4, Function.ResultKind);
    storeU32(Bytes, Record + 8, Function.ParameterSlots.size());
    for (unsigned Parameter = 0; Parameter != 4; ++Parameter)
      storeU32(Bytes, Record + 16 + Parameter * 4,
               Parameter < Function.ParameterSlots.size()
                   ? Function.ParameterSlots[Parameter]
                   : UINT32_MAX);
    storeU32(Bytes, Record + 32, RegisterFirst);
    storeU32(Bytes, Record + 36, Function.RegisterTypes.size());
    storeU32(Bytes, Record + 40, OperationFirst);
    storeU32(Bytes, Record + 44, Function.Instructions.size());
    storeU32(Bytes, Record + 48, DescriptorFirst);
    storeU32(Bytes, Record + 52, FunctionDescriptors[FunctionIndex].size());

    llvm::copy(Function.RegisterTypes, Bytes.begin() + TypeCursor);
    TypeCursor += Function.RegisterTypes.size();
    for (const DirectDescriptor &Descriptor :
         FunctionDescriptors[FunctionIndex]) {
      llvm::copy(Descriptor, Bytes.begin() + DescriptorCursor);
      DescriptorCursor += DirectDescriptorSize;
    }
    for (uint32_t Word : FunctionWords[FunctionIndex]) {
      storeU32(Bytes, TextCursor, Word);
      TextCursor += TextWordSize;
    }

    RegisterFirst += Function.RegisterTypes.size();
    OperationFirst += Function.Instructions.size();
    DescriptorFirst += FunctionDescriptors[FunctionIndex].size();
  }

  for (size_t I = 0; I != Literals.size(); ++I)
    storeU64(Bytes, L.LiteralsOffset + I * LiteralSize,
             Literals[I].IsPhysical ? 0 : Literals[I].Value);

  size_t RelocationIndex = 0;
  for (size_t I = 0; I != Literals.size(); ++I) {
    if (!Literals[I].IsPhysical)
      continue;
    auto Addend = relocationAddend(RelocationBase, Literals[I].Value);
    if (!Addend)
      return Addend.takeError();
    const uint64_t Base = L.RelaOffset + RelocationIndex * RelaSize;
    storeU64(Bytes, Base, I * LiteralSize);
    storeU64(Bytes, Base + 8, RelaInfo);
    storeU64(Bytes, Base + 16, static_cast<uint64_t>(*Addend));
    ++RelocationIndex;
  }

  storeU16(Bytes, L.SymtabOffset + SymbolSize + 6, AbsoluteSection);
  storeU64(Bytes, L.SymtabOffset + SymbolSize + 8, RelocationBase);
  std::copy(std::begin(DirectSectionNames), std::end(DirectSectionNames),
            Bytes.begin() + L.ShstrtabOffset);

  const uint64_t SH = L.SectionHeadersOffset;
  storeSection(Bytes, SH + SectionHeaderSize, 1, 1, 0, L.TargetOffset,
               TargetSize, 0, 0, 8, TargetSize);
  storeSection(Bytes, SH + 2 * SectionHeaderSize, 15, 1, 0, L.FunctionsOffset,
               Functions.size() * DirectFunctionSize, 0, 0, 8,
               DirectFunctionSize);
  storeSection(Bytes, SH + 3 * SectionHeaderSize, 32, 1, 0, L.TypesOffset,
               RegisterCount, 0, 0, 1, 1);
  storeSection(Bytes, SH + 4 * SectionHeaderSize, 45, 1, 0, L.LiteralsOffset,
               Literals.size() * LiteralSize, 0, 0, 8, LiteralSize);
  storeSection(Bytes, SH + 5 * SectionHeaderSize, 61, 1, 0, L.DescriptorsOffset,
               DescriptorCount * DirectDescriptorSize, 0, 0, 8,
               DirectDescriptorSize);
  storeSection(Bytes, SH + 6 * SectionHeaderSize, 80, 1, 6, L.TextOffset,
               OperationCount * TextWordSize, 0, 0, 4, TextWordSize);
  storeSection(Bytes, SH + 7 * SectionHeaderSize, 92, 4, 0, L.RelaOffset,
               RelocationCount * RelaSize, 8, 4, 8, RelaSize);
  storeSection(Bytes, SH + 8 * SectionHeaderSize, 113, 2, 0, L.SymtabOffset,
               SymbolCount * SymbolSize, 9, 2, 8, SymbolSize);
  storeSection(Bytes, SH + 9 * SectionHeaderSize, 121, 3, 0, L.StrtabOffset, 1,
               0, 0, 1, 0);
  storeSection(Bytes, SH + 10 * SectionHeaderSize, 129, 3, 0, L.ShstrtabOffset,
               sizeof(DirectSectionNames), 0, 0, 1, 0);

  Out.write(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
  Out.flush();
  return Error::success();
}

} // namespace llvm::Brace
