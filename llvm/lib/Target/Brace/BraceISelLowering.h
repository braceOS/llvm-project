//===-- BraceISelLowering.h - Brace SelectionDAG lowering ------*- C++ -*-===//

#ifndef LLVM_LIB_TARGET_BRACE_BRACEISELLOWERING_H
#define LLVM_LIB_TARGET_BRACE_BRACEISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class BraceSubtarget;
class BraceTargetMachine;

namespace BraceISD {
enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,
  RET,
  RET_VALUE,
  CALL,
  BR_CC,
};
} // namespace BraceISD

class BraceTargetLowering final : public TargetLowering {
public:
  BraceTargetLowering(const BraceTargetMachine &TM, const BraceSubtarget &STI);

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;
  SDValue LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv,
                               bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) const override;
  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;
  MVT getRegisterTypeForCallingConv(LLVMContext &Context,
                                    CallingConv::ID CallConv,
                                    EVT VT) const override;
  unsigned getNumRegistersForCallingConv(LLVMContext &Context,
                                         CallingConv::ID CallConv,
                                         EVT VT) const override;
  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;
  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;

private:
  bool DirectCallABI;
  SDValue lowerBRCC(SDValue Op, SelectionDAG &DAG) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_BRACE_BRACEISELLOWERING_H
