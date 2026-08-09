//===-- BraceFinalizeBranches.cpp - Canonical S3b.3 MI publication ------===//

#include "Brace.h"
#include "BraceInstrInfo.h"
#include "BraceSubtarget.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <array>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "brace-finalize-branches"

namespace {

[[noreturn]] void reject(const Twine &Message) {
  report_fatal_error("brace64 S3b.3 post-RA verifier: " + Message);
}

bool isInteger8(Register Reg) { return Brace::I8RegsRegClass.contains(Reg); }

bool isInteger32(Register Reg) { return Brace::I32RegsRegClass.contains(Reg); }

bool isPAddr(Register Reg) { return Brace::PAddrRegsRegClass.contains(Reg); }

std::optional<uint64_t> directMemoryAddress(const MachineMemOperand &Memory) {
  const auto *Expression = dyn_cast_or_null<ConstantExpr>(Memory.getValue());
  const auto *Address =
      Expression && Expression->getOpcode() == Instruction::IntToPtr &&
              Expression->getNumOperands() == 1 &&
              Expression->getType()->getPointerAddressSpace() == 200
          ? dyn_cast<ConstantInt>(Expression->getOperand(0))
          : nullptr;
  if (!Address || Address->getBitWidth() != 64 || Memory.getOffset() != 0)
    return std::nullopt;
  return Address->getZExtValue();
}

MachineBasicBlock::iterator nextReal(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) {
  while (I != MBB.end() && I->isDebugInstr())
    ++I;
  return I;
}

void verifyOperandEnvelope(const MachineOperand &Operand) {
  if (!Operand.isReg()) {
    if (Operand.getTargetFlags() != 0)
      reject("target operand flags are forbidden");
    return;
  }

  Register Reg = Operand.getReg();
  if (!Reg || !Reg.isPhysical() ||
      (!isPAddr(Reg) && !isInteger8(Reg) && !isInteger32(Reg)))
    reject("virtual, null, or untyped register survived publication");
  if (Operand.getSubReg() != 0 || Operand.isUndef() ||
      Operand.isInternalRead() || Operand.isEarlyClobber() ||
      Operand.isRenamable() || Operand.isDebug())
    reject("noncanonical physical-register operand flags survived publication");
}

void verifyInstructionEnvelope(const MachineInstr &MI) {
  if (MI.getNumOperands() != MI.getNumExplicitOperands())
    reject("implicit register, regmask, or live-out operands are forbidden");
  if (MI.getFlags() != MachineInstr::NoFlags || MI.getAsmPrinterFlags() != 0 ||
      MI.getDebugLoc() || MI.getPreInstrSymbol() || MI.getPostInstrSymbol() ||
      MI.getHeapAllocMarker() || MI.getPCSections() || MI.getMMRAMetadata() ||
      MI.getDeactivationSymbol() || MI.getCFIType() != 0)
    reject("unconsumed MachineInstr publication metadata is forbidden");
  for (const MachineOperand &Operand : MI.operands())
    verifyOperandEnvelope(Operand);
}

void verifyBlockEnvelope(const MachineBasicBlock &MBB) {
  if ((MBB.isEntryBlock() && !MBB.livein_empty()) ||
      MBB.getCallFrameSize() != 0 || MBB.getAlignment() != Align(1) ||
      MBB.getMaxBytesForAlignment() != 0 || MBB.isEHPad() ||
      MBB.hasAddressTaken() || MBB.hasLabelMustBeEmitted() ||
      MBB.isEHScopeEntry() || MBB.isEHContTarget() || MBB.isEHFuncletEntry() ||
      MBB.isCleanupFuncletEntry() || MBB.isBeginSection() ||
      MBB.isEndSection() || MBB.getBBID() ||
      MBB.getSectionID() != MBBSectionID(0) ||
      MBB.isInlineAsmBrIndirectTarget() ||
      !MBB.getPrefetchTargetCallsiteIndexes().empty() ||
      MBB.getIrrLoopHeaderWeight())
    reject("noncanonical machine-basic-block state survived publication");
  for (const MachineBasicBlock::RegisterMaskPair &LiveIn : MBB.liveins())
    if ((!isPAddr(LiveIn.PhysReg) && !isInteger8(LiveIn.PhysReg) &&
         !isInteger32(LiveIn.PhysReg)) ||
        LiveIn.LaneMask != LaneBitmask::getAll())
      reject("internal block live-in is outside the typed register envelope");
}

void finalizeConditionalBranch(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const BraceInstrInfo &TII) {
  MachineInstr &Branch = *I;
  verifyInstructionEnvelope(Branch);
  if (Branch.getNumOperands() != 3 || Branch.getNumExplicitOperands() != 3 ||
      !Branch.getOperand(0).isReg() || !Branch.getOperand(1).isMBB() ||
      !Branch.getOperand(2).isImm() || Branch.getOperand(0).isDef() ||
      Branch.getOperand(0).isTied())
    reject("malformed one-target conditional branch");

  Register Condition = Branch.getOperand(0).getReg();
  MachineBasicBlock *Taken = Branch.getOperand(1).getMBB();
  const int64_t Nonzero = Branch.getOperand(2).getImm();
  if (Nonzero != 0 && Nonzero != 1)
    reject("conditional branch sense is not boolean");

  auto Following = nextReal(MBB, std::next(I));
  MachineBasicBlock *Other = nullptr;
  MachineInstr *TrailingBranch = nullptr;
  if (Following != MBB.end()) {
    verifyInstructionEnvelope(*Following);
    if (Following->getOpcode() != Brace::BR ||
        Following->getNumOperands() != 1 ||
        Following->getNumExplicitOperands() != 1 ||
        !Following->getOperand(0).isMBB() ||
        nextReal(MBB, std::next(Following)) != MBB.end())
      reject("conditional branch is followed by a noncanonical terminator");
    TrailingBranch = &*Following;
    Other = Following->getOperand(0).getMBB();
  } else {
    Other = MBB.getNextNode();
  }
  if (!Other || !MBB.isSuccessor(Taken) || !MBB.isSuccessor(Other))
    reject("conditional branch targets do not match the final CFG");

  MachineBasicBlock *TrueTarget = Nonzero ? Taken : Other;
  MachineBasicBlock *FalseTarget = Nonzero ? Other : Taken;
  const unsigned Opcode =
      Branch.getOpcode() == Brace::BRCOND8 ? Brace::BR_IF8 : Brace::BR_IF32;
  BuildMI(MBB, I, Branch.getDebugLoc(), TII.get(Opcode))
      .addReg(Condition, getKillRegState(Branch.getOperand(0).isKill()))
      .addMBB(TrueTarget)
      .addMBB(FalseTarget);
  Branch.eraseFromParent();
  if (TrailingBranch)
    TrailingBranch->eraseFromParent();
}

void verifyInstruction(const MachineInstr &MI) {
  verifyInstructionEnvelope(MI);

  auto requireReg = [&](unsigned Index, bool (*Predicate)(Register), bool IsDef,
                        bool IsTied = false) {
    if (Index >= MI.getNumExplicitOperands() || !MI.getOperand(Index).isReg() ||
        !Predicate(MI.getOperand(Index).getReg()) ||
        MI.getOperand(Index).isDef() != IsDef ||
        MI.getOperand(Index).isTied() != IsTied)
      reject("instruction has a wrong typed register operand");
  };
  auto requireMBB = [&](unsigned Index) {
    if (Index >= MI.getNumExplicitOperands() || !MI.getOperand(Index).isMBB())
      reject("instruction has a malformed block operand");
  };
  auto requireImm = [&](unsigned Index) {
    if (Index >= MI.getNumExplicitOperands() || !MI.getOperand(Index).isImm())
      reject("instruction has a malformed immediate operand");
  };
  auto requireCount = [&](unsigned Count) {
    if (MI.getNumExplicitOperands() != Count)
      reject("instruction has a noncanonical explicit operand count");
  };
  auto requireMemory = [&](bool IsLoad, uint64_t Bytes) {
    if (std::distance(MI.memoperands_begin(), MI.memoperands_end()) != 1)
      reject("memory instruction must carry exactly one memory operand");
    const MachineMemOperand &Memory = **MI.memoperands_begin();
    const std::optional<uint64_t> Address = directMemoryAddress(Memory);
    const MachineMemOperand::Flags ExpectedFlags =
        MachineMemOperand::MOVolatile |
        (IsLoad ? MachineMemOperand::MOLoad : MachineMemOperand::MOStore);
    if (Memory.getFlags() != ExpectedFlags || Memory.getAddrSpace() != 200 ||
        Memory.getSize() != LocationSize::precise(Bytes) ||
        Memory.getOffset() != 0 || Memory.getAlign().value() < Bytes ||
        !Address || *Address % Memory.getAlign().value() != 0 ||
        Memory.getAAInfo() || Memory.getRanges() ||
        Memory.getSyncScopeID() != SyncScope::System ||
        Memory.getSuccessOrdering() != AtomicOrdering::NotAtomic ||
        Memory.getFailureOrdering() != AtomicOrdering::NotAtomic)
      reject("memory instruction lost its exact AS200 effect");
  };

  if (!MI.mayLoad() && !MI.mayStore() && !MI.memoperands_empty())
    reject("non-memory instruction carries an unconsumed memory operand");

  switch (MI.getOpcode()) {
  case Brace::CONST8:
    requireCount(2);
    requireReg(0, isInteger8, /*IsDef=*/true);
    requireImm(1);
    if (!isInt<8>(MI.getOperand(1).getImm()))
      reject("i8 constant is not canonically sign extended");
    break;
  case Brace::CONST32:
    requireCount(2);
    requireReg(0, isInteger32, /*IsDef=*/true);
    requireImm(1);
    if (!isInt<32>(MI.getOperand(1).getImm()))
      reject("i32 constant is not canonically sign extended");
    break;
  case Brace::PADDR_IMM:
    requireCount(2);
    requireReg(0, isPAddr, /*IsDef=*/true);
    requireImm(1);
    break;
  case Brace::AND8:
    requireCount(3);
    requireReg(0, isInteger8, /*IsDef=*/true, /*IsTied=*/true);
    requireReg(1, isInteger8, /*IsDef=*/false, /*IsTied=*/true);
    requireReg(2, isInteger8, /*IsDef=*/false);
    break;
  case Brace::AND32:
    requireCount(3);
    requireReg(0, isInteger32, /*IsDef=*/true, /*IsTied=*/true);
    requireReg(1, isInteger32, /*IsDef=*/false, /*IsTied=*/true);
    requireReg(2, isInteger32, /*IsDef=*/false);
    break;
  case Brace::MOV8:
    requireCount(2);
    requireReg(0, isInteger8, /*IsDef=*/true);
    requireReg(1, isInteger8, /*IsDef=*/false);
    break;
  case Brace::MOV32:
    requireCount(2);
    requireReg(0, isInteger32, /*IsDef=*/true);
    requireReg(1, isInteger32, /*IsDef=*/false);
    break;
  case Brace::LOAD8:
    requireCount(2);
    requireReg(0, isInteger8, /*IsDef=*/true);
    requireReg(1, isPAddr, /*IsDef=*/false);
    requireMemory(/*IsLoad=*/true, 1);
    break;
  case Brace::LOAD32:
    requireCount(2);
    requireReg(0, isInteger32, /*IsDef=*/true);
    requireReg(1, isPAddr, /*IsDef=*/false);
    requireMemory(/*IsLoad=*/true, 4);
    break;
  case Brace::STORE8:
    requireCount(2);
    requireReg(0, isPAddr, /*IsDef=*/false);
    requireReg(1, isInteger8, /*IsDef=*/false);
    requireMemory(/*IsLoad=*/false, 1);
    break;
  case Brace::STORE32:
    requireCount(2);
    requireReg(0, isPAddr, /*IsDef=*/false);
    requireReg(1, isInteger32, /*IsDef=*/false);
    requireMemory(/*IsLoad=*/false, 4);
    break;
  case Brace::BR:
    requireCount(1);
    requireMBB(0);
    break;
  case Brace::BR_IF8:
    requireCount(3);
    requireReg(0, isInteger8, /*IsDef=*/false);
    requireMBB(1);
    requireMBB(2);
    break;
  case Brace::BR_IF32:
    requireCount(3);
    requireReg(0, isInteger32, /*IsDef=*/false);
    requireMBB(1);
    requireMBB(2);
    break;
  case Brace::RET:
    requireCount(0);
    break;
  default:
    reject("unknown or pre-publication MachineInstr survived");
  }
}

struct PAddrState {
  bool Reachable = false;
  std::array<std::optional<uint64_t>, 2> Values{};

  bool operator==(const PAddrState &Other) const {
    return Reachable == Other.Reachable && Values == Other.Values;
  }
};

unsigned paddrIndex(Register Reg) {
  if (Reg == Brace::R0)
    return 0;
  if (Reg == Brace::R1)
    return 1;
  reject("physical-address operand is outside r0..r1");
}

unsigned registerIndex(Register Reg) {
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
    reject("physical register is outside r0..r5");
  }
}

uint8_t registerBit(Register Reg) {
  // Most calls occur after verifyInstruction has established one of the six
  // physical registers.  Keep the actual switch in registerIndex so malformed
  // live-in input still fails closed in release builds.
  return static_cast<uint8_t>(1U << registerIndex(Reg));
}

uint8_t blockDefinitionMask(const MachineBasicBlock &MBB) {
  uint8_t Mask = 0;
  for (const MachineInstr &MI : MBB)
    for (const MachineOperand &Operand : MI.operands())
      if (Operand.isReg() && Operand.isDef())
        Mask |= registerBit(Operand.getReg());
  return Mask;
}

uint8_t blockUpwardUseMask(const MachineBasicBlock &MBB) {
  uint8_t Defined = 0;
  uint8_t UsedBeforeDefinition = 0;
  for (const MachineInstr &MI : MBB) {
    // Reads must be observed before defs so a tied two-address instruction
    // cannot manufacture its own incoming value.
    for (const MachineOperand &Operand : MI.operands()) {
      if (!Operand.isReg() || !Operand.readsReg())
        continue;
      const uint8_t Bit = registerBit(Operand.getReg());
      if ((Defined & Bit) == 0)
        UsedBeforeDefinition |= Bit;
    }
    for (const MachineOperand &Operand : MI.operands())
      if (Operand.isReg() && Operand.isDef())
        Defined |= registerBit(Operand.getReg());
  }
  return UsedBeforeDefinition;
}

void verifyRegisterDefinitions(MachineFunction &MF) {
  constexpr uint8_t AllRegisters = UINT8_C(0x3f);
  DenseMap<const MachineBasicBlock *, uint8_t> In;
  DenseMap<const MachineBasicBlock *, uint8_t> Out;

  for (const MachineBasicBlock &MBB : MF) {
    const uint8_t Initial = &MBB == &MF.front() ? 0 : AllRegisters;
    In[&MBB] = Initial;
    Out[&MBB] = Initial | blockDefinitionMask(MBB);
  }

  bool Changed = true;
  unsigned Iterations = 0;
  while (Changed) {
    if (++Iterations > 32)
      reject("physical-register definition analysis did not converge");
    Changed = false;
    for (const MachineBasicBlock &MBB : MF) {
      uint8_t NewIn = 0;
      if (&MBB != &MF.front()) {
        NewIn = AllRegisters;
        bool HasPredecessor = false;
        for (const MachineBasicBlock *Predecessor : MBB.predecessors()) {
          NewIn &= Out[Predecessor];
          HasPredecessor = true;
        }
        if (!HasPredecessor)
          reject("non-entry block has no predecessor");
      }
      const uint8_t NewOut = NewIn | blockDefinitionMask(MBB);
      if (In[&MBB] != NewIn || Out[&MBB] != NewOut) {
        In[&MBB] = NewIn;
        Out[&MBB] = NewOut;
        Changed = true;
      }
    }
  }

  // Recompute exact physical-register liveness rather than trusting MIR's
  // block live-in lists.  Unlike local upward-use alone, this retains a value
  // through an intermediate block that does not itself read the value.
  DenseMap<const MachineBasicBlock *, uint8_t> LiveIn;
  DenseMap<const MachineBasicBlock *, uint8_t> LiveOut;
  Changed = true;
  Iterations = 0;
  while (Changed) {
    if (++Iterations > 64)
      reject("physical-register liveness analysis did not converge");
    Changed = false;
    for (const MachineBasicBlock &MBB : MF) {
      uint8_t NewOut = 0;
      for (const MachineBasicBlock *Successor : MBB.successors())
        NewOut |= LiveIn[Successor];
      const uint8_t NewIn =
          blockUpwardUseMask(MBB) |
          (NewOut & static_cast<uint8_t>(~blockDefinitionMask(MBB)));
      if (LiveIn[&MBB] != NewIn || LiveOut[&MBB] != NewOut) {
        LiveIn[&MBB] = NewIn;
        LiveOut[&MBB] = NewOut;
        Changed = true;
      }
    }
  }

  for (const MachineBasicBlock &MBB : MF) {
    uint8_t State = In[&MBB];
    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &Operand : MI.operands()) {
        if (!Operand.isReg() || !Operand.readsReg())
          continue;
        const uint8_t Bit = registerBit(Operand.getReg());
        if ((State & Bit) == 0)
          reject(
              "physical register is read without a definition on every path");
      }
      for (const MachineOperand &Operand : MI.operands()) {
        if (!Operand.isReg() || !Operand.isDef())
          continue;
        const uint8_t Bit = registerBit(Operand.getReg());
        State |= Bit;
      }
    }

    uint8_t DeclaredLiveIns = 0;
    for (const MachineBasicBlock::RegisterMaskPair &LiveInValue :
         MBB.liveins()) {
      const uint8_t Bit = registerBit(LiveInValue.PhysReg);
      if ((DeclaredLiveIns & Bit) != 0)
        reject("duplicate machine-basic-block live-in");
      DeclaredLiveIns |= Bit;
    }
    if (DeclaredLiveIns != LiveIn[&MBB] ||
        (DeclaredLiveIns & static_cast<uint8_t>(~In[&MBB])) != 0)
      reject("machine-basic-block live-ins do not match physical uses");
  }
}

void transferPAddrDefinitions(const MachineBasicBlock &MBB, PAddrState &State) {
  if (!State.Reachable)
    return;
  for (const MachineInstr &MI : MBB)
    if (MI.getOpcode() == Brace::PADDR_IMM)
      State.Values[paddrIndex(MI.getOperand(0).getReg())] =
          static_cast<uint64_t>(MI.getOperand(1).getImm());
}

void verifyPAddrReachingDefinitions(MachineFunction &MF) {
  DenseMap<const MachineBasicBlock *, PAddrState> In;
  DenseMap<const MachineBasicBlock *, PAddrState> Out;

  bool Changed = true;
  unsigned Iterations = 0;
  while (Changed) {
    if (++Iterations > 32)
      reject("physical-address reaching definitions did not converge");
    Changed = false;
    for (const MachineBasicBlock &MBB : MF) {
      PAddrState NewIn;
      if (&MBB == &MF.front()) {
        // The semantic entry has no live-in.  A backedge into the entry can
        // never make either address register known at the first attempt.
        NewIn.Reachable = true;
      } else {
        bool First = true;
        for (const MachineBasicBlock *Predecessor : MBB.predecessors()) {
          const PAddrState &PredOut = Out[Predecessor];
          if (!PredOut.Reachable)
            continue;
          if (First) {
            NewIn = PredOut;
            First = false;
            continue;
          }
          for (unsigned Index = 0; Index != NewIn.Values.size(); ++Index)
            if (!NewIn.Values[Index] || !PredOut.Values[Index] ||
                NewIn.Values[Index] != PredOut.Values[Index])
              NewIn.Values[Index].reset();
        }
      }

      PAddrState NewOut = NewIn;
      transferPAddrDefinitions(MBB, NewOut);
      if (!(In[&MBB] == NewIn) || !(Out[&MBB] == NewOut)) {
        In[&MBB] = NewIn;
        Out[&MBB] = NewOut;
        Changed = true;
      }
    }
  }

  for (const MachineBasicBlock &MBB : MF) {
    PAddrState State = In[&MBB];
    if (!State.Reachable)
      reject("machine block has no physical-address entry state");
    for (const MachineInstr &MI : MBB) {
      if (MI.getOpcode() == Brace::PADDR_IMM) {
        State.Values[paddrIndex(MI.getOperand(0).getReg())] =
            static_cast<uint64_t>(MI.getOperand(1).getImm());
        continue;
      }
      unsigned AddressOperand = 0;
      switch (MI.getOpcode()) {
      case Brace::LOAD8:
      case Brace::LOAD32:
        AddressOperand = 1;
        break;
      case Brace::STORE8:
      case Brace::STORE32:
        AddressOperand = 0;
        break;
      default:
        continue;
      }
      const unsigned Index = paddrIndex(MI.getOperand(AddressOperand).getReg());
      const MachineMemOperand &Memory = **MI.memoperands_begin();
      const std::optional<uint64_t> Expected = directMemoryAddress(Memory);
      if (!Expected || !State.Values[Index] || State.Values[Index] != Expected)
        reject("physical-address reaching definition disagrees with MMO");
    }
  }
}

class BraceFinalizeBranchesLegacy final : public MachineFunctionPass {
public:
  static char ID;
  BraceFinalizeBranchesLegacy() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    // MIR restart seams skip the ordinary IR pass pipeline.  Recheck the
    // complete embedded module here so a resumed compilation cannot pair an
    // exact command-line ABI with a stale or forged triple/layout/ABI body.
    verifyBraceS3LateModuleEnvelope(*MF.getFunction().getParent());
    if (MF.getName() != "brace_system_entry")
      reject("unexpected MachineFunction identity");
    const MachineFunctionProperties &Properties = MF.getProperties();
    if (MF.getAlignment() != Align(1) || MF.exposesReturnsTwice() ||
        MF.hasInlineAsm() || MF.hasWinCFI() || MF.callsEHReturn() ||
        MF.callsUnwindInit() || MF.hasEHContTarget() || MF.hasEHScopes() ||
        MF.hasEHFunclets() || MF.hasFakeUses() || MF.isOutlined() ||
        MF.useDebugInstrRef() || MF.hasDebugValueSubstitutions() ||
        MF.hasDebugPHIPositions() || !MF.getVariableDbgInfo().empty() ||
        !MF.getCallSitesInfo().empty() || !MF.getCalledGlobals().empty() ||
        !MF.getLongjmpTargets().empty() || !MF.getEHContTargets().empty() ||
        !MF.getLandingPads().empty() || MF.hasAnyCallSiteLandingPad() ||
        MF.hasAnyCallSiteLabel() || !MF.getCodeViewAnnotations().empty() ||
        !MF.getTypeInfos().empty() || !MF.getFilterIds().empty() ||
        MF.getWasmEHFuncInfo() || MF.getWinEHFuncInfo() ||
        (MF.getJumpTableInfo() && !MF.getJumpTableInfo()->isEmpty()) ||
        (MF.getConstantPool() && !MF.getConstantPool()->isEmpty()) ||
        Properties.hasFailedISel() || Properties.hasLegalized() ||
        Properties.hasRegBankSelected() || Properties.hasSelected() ||
        Properties.hasFailsVerification() || Properties.hasFailedRegAlloc())
      reject("MachineFunction state is outside the finite leaf envelope");
    MachineFrameInfo &Frame = MF.getFrameInfo();
    if (Frame.getStackSize() != 0 || Frame.getNumObjects() != 0 ||
        Frame.getNumFixedObjects() != 0 || Frame.hasVarSizedObjects() ||
        Frame.hasStackProtectorIndex() || Frame.hasFunctionContextIndex() ||
        Frame.getOffsetAdjustment() != 0 || Frame.getMaxAlign() != Align(1) ||
        Frame.getMaxCallFrameSize() != 0 ||
        Frame.getCVBytesOfCalleeSavedRegisters() != 0 ||
        !Frame.getCalleeSavedInfo().empty() ||
        Frame.getLocalFrameObjectCount() != 0 ||
        Frame.getLocalFrameSize() != 0 ||
        Frame.getLocalFrameMaxAlign() != Align(1) ||
        Frame.getUseLocalStackAllocationBlock() ||
        !Frame.getSavePoints().empty() || !Frame.getRestorePoints().empty() ||
        Frame.getUnsafeStackSize() != 0 || Frame.hasCalls() ||
        Frame.isFrameAddressTaken() || Frame.isReturnAddressTaken() ||
        Frame.hasStackMap() || Frame.hasPatchPoint() || Frame.adjustsStack() ||
        Frame.hasOpaqueSPAdjustment() || Frame.hasVAStart() ||
        Frame.hasCopyImplyingStackAdjustment() ||
        Frame.hasMustTailInVarArgFunc() || Frame.hasTailCall())
      reject("stack, frame, return-address, and call state are forbidden");
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    const MCPhysReg *CalleeSaved = MRI.getCalleeSavedRegs();
    if (!MRI.tracksLiveness() || !MRI.livein_empty() ||
        MRI.getNumVirtRegs() != 0 || (CalleeSaved && *CalleeSaved != 0) ||
        !MF.getProperties().hasNoVRegs())
      reject("live-in or virtual-register state survived physical rewrite");
    if (MF.size() == 0 || MF.size() > 4)
      reject("machine basic-block count is outside 1..4");

    // Validate every block before the only structural normalization below.
    // In particular, an instruction-free lexical entry must not be able to
    // launder live-in, EH, section, alignment, or address-taken state by being
    // erased before the publication trust boundary observes it.
    for (const MachineBasicBlock &MBB : MF)
      verifyBlockEnvelope(MBB);

    const auto &TII = *MF.getSubtarget<BraceSubtarget>().getInstrInfo();
    bool Changed = false;

    // SelectionDAG can preserve an instruction-free IR entry block when its
    // only edge is the lexical fallthrough into a loop header.  It carries no
    // S2 location and must not become a second name for operation zero.  Drop
    // that uniquely safe shape before branch targets are finalized; every
    // other non-emitting block remains a publication error below.
    while (MF.size() > 1) {
      MachineBasicBlock &Entry = MF.front();
      if (nextReal(Entry, Entry.begin()) != Entry.end())
        break;
      MachineBasicBlock *Next = Entry.getNextNode();
      if (!Entry.pred_empty() || Entry.succ_size() != 1 ||
          *Entry.succ_begin() != Next)
        reject("empty entry block is not a pure lexical fallthrough");
      Entry.removeSuccessor(Next);
      Entry.eraseFromParent();
      Changed = true;
    }

    for (MachineBasicBlock &MBB : MF) {
      for (auto I = MBB.begin(); I != MBB.end();) {
        auto Current = I++;
        if (Current->getOpcode() == Brace::BRCOND8 ||
            Current->getOpcode() == Brace::BRCOND32) {
          finalizeConditionalBranch(MBB, Current, TII);
          Changed = true;
          break;
        }
      }
    }

    unsigned OperationCount = 0;
    unsigned EdgeCount = 0;
    unsigned MemoryCount = 0;
    SmallPtrSet<const MachineBasicBlock *, 4> Reachable;
    SmallVector<const MachineBasicBlock *, 4> Worklist{&MF.front()};
    while (!Worklist.empty()) {
      const MachineBasicBlock *Block = Worklist.pop_back_val();
      if (!Reachable.insert(Block).second)
        continue;
      for (const MachineBasicBlock *Successor : Block->successors())
        Worklist.push_back(Successor);
    }
    if (Reachable.size() != MF.size())
      reject("all machine basic blocks must be CFG-reachable");

    for (MachineBasicBlock &MBB : MF) {
      EdgeCount += MBB.succ_size();
      if (EdgeCount > 8)
        reject("machine CFG edge count exceeds 8");
      unsigned BlockOperations = 0;
      const auto LastIterator = MBB.getLastNonDebugInstr();
      const MachineInstr *Last =
          LastIterator == MBB.end() ? nullptr : &*LastIterator;
      for (const MachineInstr &MI : MBB) {
        if (MI.isDebugInstr())
          reject("debug MachineInstr are not publishable");
        verifyInstruction(MI);
        if (MI.isTerminator() && &MI != Last)
          reject("a terminator may only be the final operation in its block");
        if (MI.mayLoad() || MI.mayStore()) {
          if (++MemoryCount > 64)
            reject("published physical memory operation count exceeds 64");
        }
        ++BlockOperations;
        if (++OperationCount > 128)
          reject("published operation count exceeds 128");
      }
      if (BlockOperations == 0)
        reject("empty machine basic blocks are not publishable");

      const MachineInstr &LastInstruction = *Last;
      auto RequireSuccessor = [&](const MachineBasicBlock *Target) {
        if (!Target || Target->getParent() != &MF || !MBB.isSuccessor(Target))
          reject("branch target is not an emitting CFG successor");
      };
      switch (LastInstruction.getOpcode()) {
      case Brace::BR:
        if (MBB.succ_size() != 1)
          reject("unconditional branch must have one successor");
        RequireSuccessor(LastInstruction.getOperand(0).getMBB());
        break;
      case Brace::BR_IF8:
      case Brace::BR_IF32: {
        const MachineBasicBlock *TrueTarget =
            LastInstruction.getOperand(1).getMBB();
        const MachineBasicBlock *FalseTarget =
            LastInstruction.getOperand(2).getMBB();
        if (MBB.succ_size() != 2 || TrueTarget == FalseTarget)
          reject("conditional branch must have two distinct successors");
        RequireSuccessor(TrueTarget);
        RequireSuccessor(FalseTarget);
        break;
      }
      case Brace::RET:
        if (!MBB.succ_empty())
          reject("return block must have no successor");
        break;
      default:
        break;
      }
      if (!LastInstruction.isTerminator()) {
        if (!MBB.getNextNode() || MBB.succ_size() != 1 ||
            *MBB.succ_begin() != MBB.getNextNode())
          reject("implicit fallthrough does not match final layout");
      }
    }
    verifyRegisterDefinitions(MF);
    verifyPAddrReachingDefinitions(MF);
    return Changed;
  }

  StringRef getPassName() const override {
    return "Brace finalize branches and publication verifier";
  }
};

} // namespace

char BraceFinalizeBranchesLegacy::ID = 0;

INITIALIZE_PASS(BraceFinalizeBranchesLegacy, DEBUG_TYPE,
                "Brace finalize branches and publication verifier", false,
                false)

FunctionPass *llvm::createBraceFinalizeBranchesPass() {
  return new BraceFinalizeBranchesLegacy();
}
