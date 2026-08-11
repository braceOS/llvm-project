//===-- BraceMCTargetDesc.h - Brace MC layer --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BRACE_MCTARGETDESC_BRACEMCTARGETDESC_H
#define LLVM_LIB_TARGET_BRACE_MCTARGETDESC_BRACEMCTARGETDESC_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCSubtargetInfo;
class raw_pwrite_stream;
class Triple;

namespace Brace {

enum ValueKind : unsigned {
  I8 = 0,
  I32 = 2,
  I64 = 3,
  PADDR = 7,
};

enum PhysicalWidth : unsigned {
  U8 = 0,
  U32 = 1,
};

constexpr uint64_t S2RelocationBase = UINT64_C(0x40000000);

enum class S2ObjectMode : uint8_t {
  Legacy,
  DirectCall,
  DirectCallHome,
  DirectCallByteFrame,
};

struct S2DirectFunction final {
  SmallVector<uint8_t, 26> RegisterTypes;
  SmallVector<MCInst, 128> Instructions;
  SmallVector<uint8_t, 4> ParameterSlots;
  SmallVector<uint32_t, 4> BlockStarts;
  uint32_t Entry = 0;
  uint32_t ResultKind = 0;
  uint32_t FrameSizeBytes = 0;
  bool Present = false;
};

class S2ObjectWriter final : public MCObjectWriter {
  raw_pwrite_stream &Out;
  S2ObjectMode Mode;

public:
  explicit S2ObjectWriter(raw_pwrite_stream &Out, S2ObjectMode Mode);

  Error writeExact(ArrayRef<uint8_t> RegisterTypes,
                   ArrayRef<MCInst> Instructions, uint32_t Entry,
                   uint64_t RelocationBase);
  Error writeDirectCallExact(ArrayRef<S2DirectFunction> Functions,
                             uint32_t EntryFunction, uint64_t RelocationBase);

  S2ObjectMode getMode() const { return Mode; }

  uint64_t writeObject() override;
};

class S2TargetStreamer final : public MCTargetStreamer {
  SmallVector<uint8_t, 26> RegisterTypes;
  SmallVector<MCInst, 128> Instructions;
  uint32_t Entry = 0;
  uint64_t RelocationBase = S2RelocationBase;
  bool HasHeader = false;
  SmallVector<S2DirectFunction, 2> DirectFunctions;
  std::optional<unsigned> CurrentDirectFunction;

public:
  explicit S2TargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

  Error setHeader(ArrayRef<uint8_t> Types, uint32_t EntryBlock, uint64_t Base);
  Error beginDirectFunction(uint32_t FunctionIndex, ArrayRef<uint8_t> Types,
                            uint32_t EntryOperation, uint32_t ResultKind,
                            uint32_t FrameSizeBytes,
                            ArrayRef<uint8_t> ParameterSlots,
                            ArrayRef<uint32_t> BlockStarts, uint64_t Base);
  Error emitInstruction(const MCInst &Inst);
  Error writeObject(S2ObjectWriter &Writer) const;
};

std::unique_ptr<S2ObjectWriter> createS2ObjectWriter(raw_pwrite_stream &Out,
                                                     S2ObjectMode Mode);

MCStreamer *createS2ELFStreamer(const Triple &TT, MCContext &Context,
                                std::unique_ptr<MCAsmBackend> &&Backend,
                                std::unique_ptr<MCObjectWriter> &&Writer,
                                std::unique_ptr<MCCodeEmitter> &&Emitter);

MCTargetStreamer *createS2ObjectTargetStreamer(MCStreamer &Streamer,
                                               const MCSubtargetInfo &STI);

} // namespace Brace
} // namespace llvm

#define GET_REGINFO_ENUM
#include "BraceGenRegisterInfo.inc"

#define GET_INSTRINFO_ENUM
#include "BraceGenInstrInfo.inc"

#endif // LLVM_LIB_TARGET_BRACE_MCTARGETDESC_BRACEMCTARGETDESC_H
