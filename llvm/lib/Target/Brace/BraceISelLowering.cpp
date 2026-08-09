//===-- BraceISelLowering.cpp - Brace SelectionDAG lowering --------------===//

#include "BraceISelLowering.h"
#include "Brace.h"
#include "BraceSubtarget.h"
#include "BraceTargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

BraceTargetLowering::BraceTargetLowering(const BraceTargetMachine &TM,
                                         const BraceSubtarget &STI)
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i8, &Brace::I8RegsRegClass);
  addRegisterClass(MVT::i32, &Brace::I32RegsRegClass);
  addRegisterClass(MVT::i64, &Brace::PAddrRegsRegClass);
  computeRegisterProperties(STI.getRegisterInfo());

  setBooleanContents(ZeroOrOneBooleanContent);
  setOperationAction(ISD::BR_CC, MVT::i8, Custom);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::SETCC, MVT::i8, Expand);
  setOperationAction(ISD::SETCC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT, MVT::i8, Expand);
  setOperationAction(ISD::SELECT, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i8, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setMaxAtomicSizeInBitsSupported(0);
  setMinFunctionAlignment(Align(1));
  setPrefFunctionAlignment(Align(1));
}

SDValue BraceTargetLowering::LowerOperation(SDValue Op,
                                            SelectionDAG &DAG) const {
  if (Op.getOpcode() == ISD::BR_CC)
    return lowerBRCC(Op, DAG);
  llvm_unreachable("unexpected Brace custom-lowered SelectionDAG node");
}

SDValue BraceTargetLowering::lowerBRCC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  const auto CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  const auto *RHS = dyn_cast<ConstantSDNode>(Op.getOperand(3));
  SDValue Dest = Op.getOperand(4);
  if (!RHS || !RHS->isZero() || (CC != ISD::SETEQ && CC != ISD::SETNE) ||
      (LHS.getValueType() != MVT::i8 && LHS.getValueType() != MVT::i32))
    report_fatal_error(
        "brace64 S3b.3 leaf ABI only admits i8/i32 eq/ne-zero branches");

  SDLoc DL(Op);
  SDValue Nonzero =
      DAG.getTargetConstant(CC == ISD::SETNE ? 1 : 0, DL, MVT::i8);
  return DAG.getNode(BraceISD::BR_CC, DL, MVT::Other, Chain, LHS, Dest,
                     Nonzero);
}

SDValue BraceTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &, SelectionDAG &,
    SmallVectorImpl<SDValue> &) const {
  if (IsVarArg || !Ins.empty())
    report_fatal_error("brace64 S3b.3 leaf ABI requires a zero-argument entry");
  return Chain;
}

bool BraceTargetLowering::CanLowerReturn(
    CallingConv::ID, MachineFunction &, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &,
    const Type *) const {
  return !IsVarArg && Outs.empty();
}

SDValue
BraceTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID, bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  if (IsVarArg || !Outs.empty() || !OutVals.empty())
    report_fatal_error("brace64 S3b.3 leaf ABI requires a void return");
  return DAG.getNode(BraceISD::RET, DL, MVT::Other, Chain);
}
