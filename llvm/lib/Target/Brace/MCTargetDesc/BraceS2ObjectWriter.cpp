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

} // namespace

Error S2TargetStreamer::setHeader(ArrayRef<uint8_t> Types, uint32_t EntryBlock,
                                  uint64_t Base) {
  if (HasHeader)
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

Error S2TargetStreamer::emitInstruction(const MCInst &Inst) {
  if (!HasHeader)
    return fail("instruction emitted before header");
  if (Instructions.size() == 128)
    return fail("operation limit exceeded");
  Instructions.push_back(Inst);
  return Error::success();
}

S2ObjectWriter::S2ObjectWriter(raw_pwrite_stream &Out) : Out(Out) {}

uint64_t S2ObjectWriter::writeObject() {
  llvm_unreachable(
      "generic MCAssembler emission cannot produce the exact Brace S2 object");
}

std::unique_ptr<S2ObjectWriter> createS2ObjectWriter(raw_pwrite_stream &Out) {
  return std::make_unique<S2ObjectWriter>(Out);
}

Error S2TargetStreamer::writeObject(S2ObjectWriter &Writer) const {
  if (!HasHeader)
    return fail("missing header");
  return Writer.writeExact(RegisterTypes, Instructions, Entry, RelocationBase);
}

Error S2ObjectWriter::writeExact(ArrayRef<uint8_t> RegisterTypes,
                                 ArrayRef<MCInst> Instructions, uint32_t Entry,
                                 uint64_t RelocationBase) {
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

} // namespace llvm::Brace
