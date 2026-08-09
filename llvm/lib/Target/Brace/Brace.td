//===-- Brace.td - Brace experimental target definition ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

let Namespace = "Brace" in {
def S2DummyReg : Register<"s2-dummy">;
def S2DummyRegClass : RegisterClass<"Brace", [i64], 64, (add S2DummyReg)>;
}

defm : RemapAllTargetPseudoPointerOperands<S2DummyRegClass>;

def S2Imm : Operand<i64>;

class S2MCInst<string mnemonic, dag operands> : Instruction {
  let Namespace = "Brace";
  dag OutOperandList = (outs);
  dag InOperandList = operands;
  let AsmString = mnemonic;
  let Pattern = [];
  let isPseudo = 1;
  let hasSideEffects = 1;
}

def S2_CONSTANT : S2MCInst<"s2.constant",
    (ins S2Imm:$destination, S2Imm:$kind, S2Imm:$value)>;
def S2_INTEGER_AND : S2MCInst<"s2.integer-and",
    (ins S2Imm:$destination, S2Imm:$left, S2Imm:$right)>;
def S2_BRANCH : S2MCInst<"s2.branch", (ins S2Imm:$target)>;
def S2_BRANCH_IF : S2MCInst<"s2.branch-if",
    (ins S2Imm:$condition, S2Imm:$true_target, S2Imm:$false_target)>;
def S2_RETURN : S2MCInst<"s2.return", (ins)>;
def S2_PHYSICAL_ADDRESS : S2MCInst<"s2.physical-address",
    (ins S2Imm:$destination, S2Imm:$address)>;
def S2_PHYSICAL_LOAD : S2MCInst<"s2.physical-load",
    (ins S2Imm:$destination, S2Imm:$width, S2Imm:$address)>;
def S2_PHYSICAL_STORE : S2MCInst<"s2.physical-store",
    (ins S2Imm:$width, S2Imm:$address, S2Imm:$source)>;

def BraceInstrInfo : InstrInfo;

def : ProcessorModel<"generic", NoSchedModel, []>;

def Brace : Target {
  let InstructionSet = BraceInstrInfo;
}
