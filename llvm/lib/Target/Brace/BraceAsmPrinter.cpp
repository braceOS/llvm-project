//===-- BraceAsmPrinter.cpp - Publish verified Brace MI as S2 ------------===//

#include "Brace.h"
#include "BraceTargetMachine.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "TargetInfo/BraceTargetInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

using namespace llvm;

#define DEBUG_TYPE "brace-asm-printer"

namespace {

class BraceAsmPrinter final : public AsmPrinter {
  DenseMap<const MachineBasicBlock *, uint32_t> BlockStarts;

public:
  static char ID;

  BraceAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override {
    return "Brace S3b.3 S2 Assembly Printer";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void emitInstruction(const MachineInstr *) override {
    llvm_unreachable("BraceAsmPrinter uses its bounded whole-function path");
  }

private:
  uint64_t slot(Register Reg) const;
  uint32_t blockStart(const MachineBasicBlock *MBB) const;
  void emitS2(const MachineInstr &MI);
  void emitMC(unsigned Opcode, std::initializer_list<uint64_t> Operands);
};

} // namespace

char BraceAsmPrinter::ID = 0;

uint64_t BraceAsmPrinter::slot(Register Reg) const {
  switch (Reg) {
  case Brace::R0:
    return 0;
  case Brace::R1:
    return 1;
  case Brace::R2:
    return 2;
  case Brace::R3:
    return 3;
  case Brace::R4:
    return 4;
  case Brace::R5:
    return 5;
  default:
    OutContext.reportError(SMLoc(),
                           "brace64 S3b.3 encountered an unknown register");
    return 0;
  }
}

uint32_t BraceAsmPrinter::blockStart(const MachineBasicBlock *MBB) const {
  auto Found = BlockStarts.find(MBB);
  if (Found == BlockStarts.end()) {
    OutContext.reportError(SMLoc(),
                           "brace64 S3b.3 branch target is unpublished");
    return 0;
  }
  return Found->second;
}

void BraceAsmPrinter::emitMC(unsigned Opcode,
                             std::initializer_list<uint64_t> Operands) {
  MCInst Inst;
  Inst.setOpcode(Opcode);
  for (uint64_t Operand : Operands)
    Inst.addOperand(MCOperand::createImm(static_cast<int64_t>(Operand)));
  OutStreamer->emitInstruction(Inst, getSubtargetInfo());
}

void BraceAsmPrinter::emitS2(const MachineInstr &MI) {
  auto Reg = [&](unsigned Index) {
    return slot(MI.getOperand(Index).getReg());
  };
  auto Imm = [&](unsigned Index) {
    return static_cast<uint64_t>(MI.getOperand(Index).getImm());
  };
  auto Target = [&](unsigned Index) {
    return static_cast<uint64_t>(blockStart(MI.getOperand(Index).getMBB()));
  };

  switch (MI.getOpcode()) {
  case Brace::CONST8:
    emitMC(Brace::S2_CONSTANT, {Reg(0), Brace::I8, Imm(1) & UINT64_C(0xff)});
    return;
  case Brace::CONST32:
    emitMC(Brace::S2_CONSTANT,
           {Reg(0), Brace::I32, Imm(1) & UINT64_C(0xffffffff)});
    return;
  case Brace::PADDR_IMM:
    emitMC(Brace::S2_PHYSICAL_ADDRESS, {Reg(0), Imm(1)});
    return;
  case Brace::AND8:
  case Brace::AND32:
    emitMC(Brace::S2_INTEGER_AND, {Reg(0), Reg(1), Reg(2)});
    return;
  case Brace::MOV8:
  case Brace::MOV32:
    emitMC(Brace::S2_INTEGER_AND, {Reg(0), Reg(1), Reg(1)});
    return;
  case Brace::LOAD8:
    emitMC(Brace::S2_PHYSICAL_LOAD, {Reg(0), Brace::U8, Reg(1)});
    return;
  case Brace::LOAD32:
    emitMC(Brace::S2_PHYSICAL_LOAD, {Reg(0), Brace::U32, Reg(1)});
    return;
  case Brace::STORE8:
    emitMC(Brace::S2_PHYSICAL_STORE, {Brace::U8, Reg(0), Reg(1)});
    return;
  case Brace::STORE32:
    emitMC(Brace::S2_PHYSICAL_STORE, {Brace::U32, Reg(0), Reg(1)});
    return;
  case Brace::BR:
    emitMC(Brace::S2_BRANCH, {Target(0)});
    return;
  case Brace::BR_IF8:
  case Brace::BR_IF32:
    emitMC(Brace::S2_BRANCH_IF, {Reg(0), Target(1), Target(2)});
    return;
  case Brace::RET:
    emitMC(Brace::S2_RETURN, {});
    return;
  default:
    OutContext.reportError(
        SMLoc(), "brace64 S3b.3 AsmPrinter received an unknown MachineInstr");
  }
}

bool BraceAsmPrinter::runOnMachineFunction(MachineFunction &Function) {
  SetupMachineFunction(Function);
  BlockStarts.clear();

  uint32_t Operation = 0;
  for (const MachineBasicBlock &MBB : Function) {
    BlockStarts.try_emplace(&MBB, Operation);
    for (const MachineInstr &MI : MBB)
      if (!MI.isDebugInstr())
        ++Operation;
  }
  if (Operation == 0 || Operation > 128) {
    OutContext.reportError(SMLoc(),
                           "brace64 S3b.3 operation count is outside 1..128");
    return false;
  }

  auto *TargetStreamer =
      static_cast<Brace::S2TargetStreamer *>(OutStreamer->getTargetStreamer());
  if (!TargetStreamer) {
    OutContext.reportError(SMLoc(), "brace64 S3b.3 target streamer is missing");
    return false;
  }
  const uint8_t RegisterTypes[] = {
      Brace::PADDR, Brace::PADDR, Brace::I8, Brace::I8, Brace::I32, Brace::I32,
  };
  if (Error E = TargetStreamer->setHeader(RegisterTypes,
                                          blockStart(&Function.front()),
                                          Brace::S2RelocationBase)) {
    OutContext.reportError(SMLoc(), toString(std::move(E)));
    return false;
  }

  for (const MachineBasicBlock &MBB : Function)
    for (const MachineInstr &MI : MBB)
      if (!MI.isDebugInstr())
        emitS2(MI);
  return false;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeBraceAsmPrinter() {
  RegisterAsmPrinter<BraceAsmPrinter> X(getTheBraceTarget());
}
