//===-- BraceISelLowering.cpp - Brace SelectionDAG lowering --------------===//

#include "BraceISelLowering.h"
#include "Brace.h"
#include "BraceSubtarget.h"
#include "BraceTargetMachine.h"
#include "MCTargetDesc/BraceMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

BraceTargetLowering::BraceTargetLowering(const BraceTargetMachine &TM,
                                         const BraceSubtarget &STI)
    : TargetLowering(TM, STI),
      DirectCallABI(
          TM.Options.MCOptions.getABIName() == BraceSdagDirectCallABIName ||
          TM.Options.MCOptions.getABIName() == BraceSdagDirectCallHomeABIName) {
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

MVT BraceTargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                       CallingConv::ID CallConv,
                                                       EVT VT) const {
  if (DirectCallABI && VT == MVT::i32 &&
      (CallConv == CallingConv::C || CallConv == CallingConv::Fast))
    return MVT::i32;
  return TargetLowering::getRegisterTypeForCallingConv(Context, CallConv, VT);
}

unsigned BraceTargetLowering::getNumRegistersForCallingConv(
    LLVMContext &Context, CallingConv::ID CallConv, EVT VT) const {
  if (DirectCallABI && VT == MVT::i32 &&
      (CallConv == CallingConv::C || CallConv == CallingConv::Fast))
    return 1;
  return TargetLowering::getNumRegistersForCallingConv(Context, CallConv, VT);
}

SDValue BraceTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  const MachineFunction &MF = DAG.getMachineFunction();
  if (!DirectCallABI) {
    if (IsVarArg || !Ins.empty())
      report_fatal_error(
          "brace64 S3b.3 leaf ABI requires a zero-argument entry");
    return Chain;
  }

  if (IsVarArg || (CallConv != CallingConv::C && CallConv != CallingConv::Fast))
    report_fatal_error("brace64 S3b.5 rejects this formal calling convention");
  if (MF.getName() == "brace_system_entry") {
    if (!Ins.empty())
      report_fatal_error("brace64 S3b.5 entry requires zero formal arguments");
    return Chain;
  }
  if (MF.getName() != "brace_system_call_leaf" || Ins.size() != 1 ||
      Ins[0].VT != MVT::i32)
    report_fatal_error("brace64 S3b.5 helper requires one i32 formal argument");

  MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
  Register Incoming = MRI.createVirtualRegister(&Brace::I32RegsRegClass);
  MRI.addLiveIn(Brace::R4, Incoming);
  SDValue Value = DAG.getCopyFromReg(Chain, DL, Incoming, MVT::i32);
  InVals.push_back(Value);
  return Value.getValue(1);
}

SDValue BraceTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  if (!DirectCallABI)
    report_fatal_error("brace64 S3b.3 leaf ABI does not admit calls");
  if (CLI.IsVarArg)
    report_fatal_error("brace64 S3b.5 direct call may not be variadic");
  if (CLI.IsTailCall)
    report_fatal_error("brace64 S3b.5 direct call may not be a tail call");
  if (CLI.CallConv != CallingConv::C && CLI.CallConv != CallingConv::Fast)
    report_fatal_error("brace64 S3b.5 direct call has a wrong convention");
  if (CLI.Outs.size() != 1 || CLI.OutVals.size() != 1)
    report_fatal_error(Twine("brace64 S3b.5 direct call argument count is ") +
                       Twine(CLI.Outs.size()) + "/" +
                       Twine(CLI.OutVals.size()));
  if (CLI.Outs[0].VT != MVT::i32)
    report_fatal_error("brace64 S3b.5 direct call argument VT is not i32");
  if (CLI.OutVals[0].getValueType() != MVT::i32)
    report_fatal_error("brace64 S3b.5 direct call argument value is not i32");
  if (CLI.Ins.size() != 1 || CLI.Ins[0].VT != MVT::i32)
    report_fatal_error("brace64 S3b.5 direct call has a wrong result shape");

  const auto *Address = dyn_cast<GlobalAddressSDNode>(CLI.Callee);
  const Function *Callee =
      Address ? dyn_cast<Function>(Address->getGlobal()) : nullptr;
  if (!Callee || Address->getOffset() != 0 ||
      Callee->getName() != "brace_system_call_leaf" ||
      Callee->hasExternalLinkage())
    report_fatal_error(
        "brace64 S3b.5 requires the private direct helper target");

  SelectionDAG &DAG = CLI.DAG;
  SDValue Chain =
      DAG.getCopyToReg(CLI.Chain, CLI.DL, Brace::R4, CLI.OutVals[0], SDValue());
  SDValue Glue = Chain.getValue(1);
  SDValue Target = DAG.getTargetGlobalAddress(
      Callee, CLI.DL, getPointerTy(DAG.getDataLayout()));
  const TargetRegisterInfo *TRI =
      DAG.getMachineFunction().getSubtarget().getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CLI.CallConv);
  if (!Mask)
    report_fatal_error("brace64 S3b.5 call-preserved mask is missing");

  SmallVector<SDValue, 4> Operands{Chain, Target, DAG.getRegisterMask(Mask),
                                   Glue};
  Chain = DAG.getNode(BraceISD::CALL, CLI.DL,
                      DAG.getVTList(MVT::Other, MVT::Glue), Operands);
  Glue = Chain.getValue(1);
  SDValue Result = DAG.getCopyFromReg(Chain, CLI.DL, Brace::R4, MVT::i32, Glue);
  InVals.push_back(Result);
  return Result.getValue(1);
}

bool BraceTargetLowering::CanLowerReturn(
    CallingConv::ID, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &,
    const Type *RetTy) const {
  if (DirectCallABI) {
    if (IsVarArg)
      return false;
    if (RetTy && RetTy->isVoidTy())
      return Outs.empty();
    return RetTy && RetTy->isIntegerTy(32) && Outs.size() == 1 &&
           Outs[0].VT == MVT::i32 &&
           (MF.getName() == "brace_system_entry" ||
            MF.getName() == "brace_system_call_leaf");
  }
  return !IsVarArg && Outs.empty();
}

SDValue
BraceTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID, bool IsVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &DL, SelectionDAG &DAG) const {
  const MachineFunction &MF = DAG.getMachineFunction();
  if (DirectCallABI && MF.getName() == "brace_system_call_leaf") {
    if (IsVarArg || Outs.size() != 1 || OutVals.size() != 1 ||
        Outs[0].VT != MVT::i32 || OutVals[0].getValueType() != MVT::i32)
      report_fatal_error("brace64 S3b.5 helper requires one i32 return value");
    Chain = DAG.getCopyToReg(Chain, DL, Brace::R4, OutVals[0], SDValue());
    return DAG.getNode(BraceISD::RET_VALUE, DL, MVT::Other, Chain,
                       DAG.getRegister(Brace::R4, MVT::i32), Chain.getValue(1));
  }
  if (IsVarArg || !Outs.empty() || !OutVals.empty())
    report_fatal_error(DirectCallABI
                           ? "brace64 S3b.5 entry requires a void return"
                           : "brace64 S3b.3 leaf ABI requires a void return");
  return DAG.getNode(BraceISD::RET, DL, MVT::Other, Chain);
}
