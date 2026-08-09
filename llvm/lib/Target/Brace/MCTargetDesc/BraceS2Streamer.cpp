//===-- BraceS2Streamer.cpp - Exact experimental S2 MC streamer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// S2 instruction words refer to a module-wide, sorted literal table.  They
// therefore cannot be finalized independently by LLVM's ordinary per-MCInst
// code-emitter path.  This target-local object streamer retains the bounded MC
// instruction sequence and asks the exact S2 writer to publish it at finish().
// No generic LLVM ELF writer participates in the object identity.
//
//===----------------------------------------------------------------------===//

#include "BraceMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Error.h"
#include <memory>

using namespace llvm;

namespace llvm::Brace {
namespace {

class S2MCStreamer final : public MCStreamer {
  std::unique_ptr<S2ObjectWriter> Writer;
  bool Finished = false;

  S2TargetStreamer *targetStreamer() {
    return static_cast<S2TargetStreamer *>(getTargetStreamer());
  }

public:
  S2MCStreamer(MCContext &Context, std::unique_ptr<S2ObjectWriter> Writer)
      : MCStreamer(Context), Writer(std::move(Writer)) {
    IsObj = true;
  }

  bool emitSymbolAttribute(MCSymbol *, MCSymbolAttr) override { return false; }

  void emitCommonSymbol(MCSymbol *, uint64_t, Align) override {
    getContext().reportError(SMLoc(),
                             "brace64 S2 does not admit common symbols");
  }

  void emitSubsectionsViaSymbols() override {
    getContext().reportError(
        SMLoc(), "brace64 S2 does not admit subsections-via-symbols");
  }

  void beginCOFFSymbolDef(const MCSymbol *) override {
    getContext().reportError(SMLoc(),
                             "brace64 S2 does not admit COFF directives");
  }
  void emitCOFFSymbolStorageClass(int) override {}
  void emitCOFFSymbolType(int) override {}
  void endCOFFSymbolDef() override {}

  void emitXCOFFSymbolLinkageWithVisibility(MCSymbol *, MCSymbolAttr,
                                            MCSymbolAttr) override {
    getContext().reportError(SMLoc(),
                             "brace64 S2 does not admit XCOFF directives");
  }

  void emitInstruction(const MCInst &Inst,
                       const MCSubtargetInfo &STI) override {
    MCStreamer::emitInstruction(Inst, STI);
    S2TargetStreamer *TargetStreamer = targetStreamer();
    if (!TargetStreamer) {
      getContext().reportError(SMLoc(),
                               "brace64 S2 target streamer is missing");
      return;
    }
    if (Error E = TargetStreamer->emitInstruction(Inst))
      getContext().reportError(SMLoc(), toString(std::move(E)));
  }

  void finishImpl() override {
    if (Finished) {
      getContext().reportError(SMLoc(), "brace64 S2 streamer finished twice");
      return;
    }
    Finished = true;
    if (getContext().hadError())
      return;
    if (!Writer) {
      getContext().reportError(SMLoc(), "brace64 exact S2 writer is missing");
      return;
    }
    S2TargetStreamer *TargetStreamer = targetStreamer();
    if (!TargetStreamer) {
      getContext().reportError(SMLoc(),
                               "brace64 S2 target streamer is missing");
      return;
    }
    if (Error E = TargetStreamer->writeObject(*Writer))
      getContext().reportError(SMLoc(), toString(std::move(E)));
  }
};

} // namespace

MCStreamer *createS2ELFStreamer(const Triple &, MCContext &Context,
                                std::unique_ptr<MCAsmBackend> &&Backend,
                                std::unique_ptr<MCObjectWriter> &&Writer,
                                std::unique_ptr<MCCodeEmitter> &&Emitter) {
  if (Backend || Emitter || !Writer) {
    Context.reportError(
        SMLoc(),
        "brace64 exact S2 streamer requires only its target-local writer");
  }

  // This constructor is registered only for Brace and addPassesToEmitFile
  // always supplies createS2ObjectWriter().  Keeping the conversion here makes
  // the generic MC registry the dispatch point without admitting the generic
  // ELF object writer into the S2 identity.
  auto *ExactWriter = static_cast<S2ObjectWriter *>(Writer.release());
  return new S2MCStreamer(Context,
                          std::unique_ptr<S2ObjectWriter>(ExactWriter));
}

MCTargetStreamer *createS2ObjectTargetStreamer(MCStreamer &Streamer,
                                               const MCSubtargetInfo &) {
  return new S2TargetStreamer(Streamer);
}

} // namespace llvm::Brace
