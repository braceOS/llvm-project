//===-- BraceInstrInfo.cpp - Brace instruction information --------------===//

#include "BraceInstrInfo.h"
#include "Brace.h"
#include "BraceSubtarget.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "BraceGenInstrInfo.inc"

using namespace llvm;

BraceInstrInfo::BraceInstrInfo(const BraceSubtarget &STI)
    : BraceGenInstrInfo(STI, RI) {}

namespace {

bool usesSpillHomes(const MachineFunction &MF) {
  return MF.getTarget().Options.MCOptions.getABIName() ==
         BraceSdagLeafHomeABIName;
}

unsigned spillStoreOpcode(const TargetRegisterClass *RC) {
  if (RC == &Brace::I8RegsRegClass)
    return Brace::SPILL_STORE8;
  if (RC == &Brace::I32RegsRegClass)
    return Brace::SPILL_STORE32;
  return 0;
}

unsigned spillLoadOpcode(const TargetRegisterClass *RC) {
  if (RC == &Brace::I8RegsRegClass)
    return Brace::SPILL_LOAD8;
  if (RC == &Brace::I32RegsRegClass)
    return Brace::SPILL_LOAD32;
  return 0;
}

void verifySpillFrameIndex(const MachineFunction &MF, int FrameIndex,
                           const TargetRegisterClass *RC) {
  const MachineFrameInfo &Frame = MF.getFrameInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  if (FrameIndex < 0 || FrameIndex >= Frame.getObjectIndexEnd() ||
      Frame.isDeadObjectIndex(FrameIndex) ||
      !Frame.isSpillSlotObjectIndex(FrameIndex) ||
      Frame.getObjectAllocation(FrameIndex) ||
      Frame.isAliasedObjectIndex(FrameIndex) ||
      Frame.getObjectSize(FrameIndex) != TRI.getSpillSize(*RC) ||
      Frame.getObjectAlign(FrameIndex) != TRI.getSpillAlign(*RC))
    report_fatal_error(
        "brace64 S3b.4 spill-home ABI received a noncanonical spill object");
}

} // namespace

Register BraceInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {
  if ((MI.getOpcode() != Brace::SPILL_LOAD8 &&
       MI.getOpcode() != Brace::SPILL_LOAD32) ||
      MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(0).isDef() || !MI.getOperand(1).isFI())
    return Register();
  FrameIndex = MI.getOperand(1).getIndex();
  return MI.getOperand(0).getReg();
}

Register BraceInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  if ((MI.getOpcode() != Brace::SPILL_STORE8 &&
       MI.getOpcode() != Brace::SPILL_STORE32) ||
      MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isReg() ||
      MI.getOperand(0).isDef() || !MI.getOperand(1).isFI())
    return Register();
  FrameIndex = MI.getOperand(1).getIndex();
  return MI.getOperand(0).getReg();
}

bool BraceInstrInfo::isReMaterializableImpl(const MachineInstr &MI) const {
  if (MI.getOpcode() != Brace::PADDR_IMM)
    return TargetInstrInfo::isReMaterializableImpl(MI);

  const MachineFunction *MF = MI.getMF();
  if (!MF || !usesSpillHomes(*MF) ||
      MI.getNumOperands() != MI.getNumExplicitOperands() ||
      MI.getNumExplicitOperands() != 2 || !MI.getOperand(0).isReg() ||
      !MI.getOperand(0).isDef() || !MI.getOperand(1).isImm() ||
      MI.getOperand(1).getTargetFlags() != 0 || !MI.memoperands_empty() ||
      MI.getDebugLoc() || MI.getFlags() != MachineInstr::NoFlags ||
      MI.getAsmPrinterFlags() != 0)
    return false;

  const Register Destination = MI.getOperand(0).getReg();
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  return Destination && Destination.isVirtual() &&
         MRI.getRegClass(Destination) == &Brace::PAddrRegsRegClass &&
         MI.getOperand(0).getSubReg() == 0 && !MI.getOperand(0).isUndef() &&
         !MI.getOperand(0).isInternalRead() &&
         !MI.getOperand(0).isEarlyClobber() && !MI.getOperand(0).isDebug();
}

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

void BraceInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register SrcReg,
    bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register,
    MachineInstr::MIFlag Flags) const {
  MachineFunction &MF = *MBB.getParent();
  if (!usesSpillHomes(MF))
    report_fatal_error("brace64 S3b.3 leaf ABI register pressure would spill");
  const unsigned Opcode = spillStoreOpcode(RC);
  if (!Opcode)
    report_fatal_error(
        "brace64 S3b.4 spill-home ABI only admits i8/i32 spills");
  verifySpillFrameIndex(MF, FrameIndex, RC);

  const MachineFrameInfo &Frame = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, Frame.getObjectSize(FrameIndex),
      Frame.getObjectAlign(FrameIndex));
  const DebugLoc DL = I == MBB.end() ? DebugLoc() : I->getDebugLoc();
  BuildMI(MBB, I, DL, get(Opcode))
      .addReg(SrcReg, getKillRegState(IsKill))
      .addFrameIndex(FrameIndex)
      .addMemOperand(MMO)
      .setMIFlags(Flags);
}

void BraceInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          Register DestReg, int FrameIndex,
                                          const TargetRegisterClass *RC,
                                          Register, unsigned SubReg,
                                          MachineInstr::MIFlag Flags) const {
  MachineFunction &MF = *MBB.getParent();
  if (!usesSpillHomes(MF))
    report_fatal_error("brace64 S3b.3 leaf ABI register pressure would reload");
  const unsigned Opcode = spillLoadOpcode(RC);
  if (!Opcode || SubReg != 0)
    report_fatal_error(
        "brace64 S3b.4 spill-home ABI only admits whole i8/i32 reloads");
  verifySpillFrameIndex(MF, FrameIndex, RC);

  const MachineFrameInfo &Frame = MF.getFrameInfo();
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, Frame.getObjectSize(FrameIndex),
      Frame.getObjectAlign(FrameIndex));
  const DebugLoc DL = I == MBB.end() ? DebugLoc() : I->getDebugLoc();
  BuildMI(MBB, I, DL, get(Opcode), DestReg)
      .addFrameIndex(FrameIndex)
      .addMemOperand(MMO)
      .setMIFlags(Flags);
}
