//===-- BraceInstrInfo.cpp - Brace instruction information --------------===//

#include "BraceInstrInfo.h"
#include "Brace.h"
#include "BraceSubtarget.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "BraceGenInstrInfo.inc"

using namespace llvm;

BraceInstrInfo::BraceInstrInfo(const BraceSubtarget &STI)
    : BraceGenInstrInfo(STI, RI) {}

void BraceInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, Register DestReg,
                                 Register SrcReg, bool KillSrc, bool,
                                 bool) const {
  unsigned Opcode = 0;
  if (Brace::I8RegsRegClass.contains(DestReg) &&
      Brace::I8RegsRegClass.contains(SrcReg))
    Opcode = Brace::MOV8;
  else if (Brace::I32RegsRegClass.contains(DestReg) &&
           Brace::I32RegsRegClass.contains(SrcReg))
    Opcode = Brace::MOV32;
  else
    report_fatal_error(
        "brace64 S3b.3 leaf ABI cannot copy this physical register");

  BuildMI(MBB, I, DL, get(Opcode), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void BraceInstrInfo::storeRegToStackSlot(MachineBasicBlock &,
                                         MachineBasicBlock::iterator, Register,
                                         bool, int, const TargetRegisterClass *,
                                         Register, MachineInstr::MIFlag) const {
  report_fatal_error("brace64 S3b.3 leaf ABI register pressure would spill");
}

void BraceInstrInfo::loadRegFromStackSlot(MachineBasicBlock &,
                                          MachineBasicBlock::iterator, Register,
                                          int, const TargetRegisterClass *,
                                          Register, unsigned,
                                          MachineInstr::MIFlag) const {
  report_fatal_error("brace64 S3b.3 leaf ABI register pressure would reload");
}
