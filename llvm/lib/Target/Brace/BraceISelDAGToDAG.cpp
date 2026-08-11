//===-- BraceISelDAGToDAG.cpp - Brace DAG instruction selection ---------===//

#include "Brace.h"
#include "BraceISelLowering.h"
#include "BraceTargetMachine.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "brace-isel"

namespace {

class BraceDAGToDAGISel final : public SelectionDAGISel {
  bool FixedLocal = false;

public:
  explicit BraceDAGToDAGISel(BraceTargetMachine &TM)
      : SelectionDAGISel(TM),
        FixedLocal(TM.usesSdagDirectCallByteFrameFixedLocalABI()) {}

  void Select(SDNode *Node) override;

#include "BraceGenDAGISel.inc"
};

class BraceDAGToDAGISelLegacy final : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit BraceDAGToDAGISelLegacy(BraceTargetMachine &TM)
      : SelectionDAGISelLegacy(ID, std::make_unique<BraceDAGToDAGISel>(TM)) {}
};

} // namespace

char BraceDAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(BraceDAGToDAGISelLegacy, DEBUG_TYPE,
                "Brace SelectionDAG instruction selection", false, false)

static void requirePhysicalMemory(const MemSDNode &Node) {
  if (Node.getAddressSpace() != 200 || !Node.isVolatile() || Node.isAtomic())
    report_fatal_error(
        "brace64 S3b.3 leaf ABI requires volatile unindexed addrspace(200) "
        "memory");
}

static void requireFixedLocalMemory(const MemSDNode &Node) {
  if (Node.getAddressSpace() != 0 || !Node.isVolatile() || Node.isAtomic())
    report_fatal_error(
        "brace64 S3b.8 fixed-local ABI requires volatile unindexed AS0 "
        "local memory");
}

void BraceDAGToDAGISel::Select(SDNode *Node) {
  if (Node->isMachineOpcode()) {
    Node->setNodeId(-1);
    return;
  }

  const SDLoc DL(Node);
  switch (Node->getOpcode()) {
  case ISD::Constant: {
    const auto *Value = cast<ConstantSDNode>(Node);
    const EVT VT = Node->getValueType(0);
    unsigned Opcode = 0;
    if (VT == MVT::i8)
      Opcode = Brace::CONST8;
    else if (VT == MVT::i32)
      Opcode = Brace::CONST32;
    else if (VT == MVT::i64)
      Opcode = Brace::PADDR_IMM;
    else
      report_fatal_error(
          "brace64 S3b.3 leaf ABI encountered an illegal constant type");
    SDValue Immediate =
        CurDAG->getTargetConstant(Value->getZExtValue(), DL, VT);
    CurDAG->SelectNodeTo(Node, Opcode, VT, Immediate);
    return;
  }
  case ISD::AND: {
    const EVT VT = Node->getValueType(0);
    unsigned Opcode =
        VT == MVT::i8 ? Brace::AND8 : (VT == MVT::i32 ? Brace::AND32 : 0);
    if (!Opcode)
      report_fatal_error(
          "brace64 S3b.3 leaf ABI only admits i8/i32 integer-and");
    CurDAG->SelectNodeTo(Node, Opcode, VT, Node->getOperand(0),
                         Node->getOperand(1));
    return;
  }
  case ISD::LOAD: {
    auto *Load = cast<LoadSDNode>(Node);
    if (FixedLocal && Load->getAddressSpace() == 0) {
      requireFixedLocalMemory(*Load);
      const EVT VT = Load->getMemoryVT();
      if (VT != MVT::i32 || VT != Load->getValueType(0) ||
          Load->getAddressingMode() != ISD::UNINDEXED ||
          !isa<FrameIndexSDNode>(Load->getBasePtr()))
        report_fatal_error(
            "brace64 S3b.8 fixed-local ABI only admits one exact i32 local "
            "load from a FrameIndex");
      const auto *FI = cast<FrameIndexSDNode>(Load->getBasePtr());
      SDValue TargetFI = CurDAG->getTargetFrameIndex(
          FI->getIndex(), Load->getBasePtr().getValueType());
      SDNode *Selected =
          CurDAG->SelectNodeTo(Node, Brace::LOCAL_LOAD32, VT, MVT::Other,
                               TargetFI, Load->getChain());
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected),
                             {Load->getMemOperand()});
      return;
    }
    requirePhysicalMemory(*Load);
    EVT VT = Load->getMemoryVT();
    if (VT != Load->getValueType(0) || (VT != MVT::i8 && VT != MVT::i32))
      report_fatal_error(
          "brace64 S3b.3 leaf ABI only admits exact i8/i32 loads");
    unsigned Opcode = VT == MVT::i8 ? Brace::LOAD8 : Brace::LOAD32;
    SDNode *Selected = CurDAG->SelectNodeTo(
        Node, Opcode, VT, MVT::Other, Load->getBasePtr(), Load->getChain());
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected),
                           {Load->getMemOperand()});
    return;
  }
  case ISD::STORE: {
    auto *Store = cast<StoreSDNode>(Node);
    if (FixedLocal && Store->getAddressSpace() == 0) {
      requireFixedLocalMemory(*Store);
      const EVT VT = Store->getMemoryVT();
      if (VT != MVT::i32 || VT != Store->getValue().getValueType() ||
          Store->getAddressingMode() != ISD::UNINDEXED ||
          !isa<FrameIndexSDNode>(Store->getBasePtr()))
        report_fatal_error(
            "brace64 S3b.8 fixed-local ABI only admits one exact i32 local "
            "store to a FrameIndex");
      const auto *FI = cast<FrameIndexSDNode>(Store->getBasePtr());
      SDValue TargetFI = CurDAG->getTargetFrameIndex(
          FI->getIndex(), Store->getBasePtr().getValueType());
      SDNode *Selected =
          CurDAG->SelectNodeTo(Node, Brace::LOCAL_STORE32, MVT::Other,
                               Store->getValue(), TargetFI, Store->getChain());
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected),
                             {Store->getMemOperand()});
      return;
    }
    requirePhysicalMemory(*Store);
    EVT VT = Store->getMemoryVT();
    if (VT != Store->getValue().getValueType() ||
        (VT != MVT::i8 && VT != MVT::i32))
      report_fatal_error(
          "brace64 S3b.3 leaf ABI only admits exact i8/i32 stores");
    unsigned Opcode = VT == MVT::i8 ? Brace::STORE8 : Brace::STORE32;
    SDNode *Selected =
        CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, Store->getBasePtr(),
                             Store->getValue(), Store->getChain());
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(Selected),
                           {Store->getMemOperand()});
    return;
  }
  case ISD::BR:
    CurDAG->SelectNodeTo(Node, Brace::BR, MVT::Other, Node->getOperand(1),
                         Node->getOperand(0));
    return;
  case BraceISD::BR_CC: {
    EVT VT = Node->getOperand(1).getValueType();
    unsigned Opcode =
        VT == MVT::i8 ? Brace::BRCOND8 : (VT == MVT::i32 ? Brace::BRCOND32 : 0);
    if (!Opcode)
      report_fatal_error(
          "brace64 S3b.3 leaf ABI encountered an illegal branch type");
    SmallVector<SDValue, 4> Operands{Node->getOperand(1), Node->getOperand(2),
                                     Node->getOperand(3), Node->getOperand(0)};
    CurDAG->SelectNodeTo(Node, Opcode, MVT::Other, Operands);
    return;
  }
  case BraceISD::RET:
    CurDAG->SelectNodeTo(Node, Brace::RET, MVT::Other, Node->getOperand(0));
    return;
  case BraceISD::RET_VALUE: {
    SmallVector<SDValue, 3> Operands{Node->getOperand(1), Node->getOperand(0),
                                     Node->getOperand(2)};
    CurDAG->SelectNodeTo(Node, Brace::RET_I32, MVT::Other, Operands);
    return;
  }
  case BraceISD::CALL: {
    SmallVector<SDValue, 4> Operands{Node->getOperand(1), Node->getOperand(2),
                                     Node->getOperand(0), Node->getOperand(3)};
    CurDAG->SelectNodeTo(Node, Brace::CALL_I32,
                         CurDAG->getVTList(MVT::Other, MVT::Glue), Operands);
    return;
  }
  default:
    break;
  }

  SelectCode(Node);
}

FunctionPass *llvm::createBraceISelDag(BraceTargetMachine &TM) {
  return new BraceDAGToDAGISelLegacy(TM);
}
