//===-- Brace.td - Brace experimental target definition ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

let Namespace = "Brace" in {
  class BraceReg<bits<16> encoding, string name> : Register<name> {
    let HWEncoding = encoding;
  }

  def R0 : BraceReg<0, "r0">, DwarfRegNum<[0]>;
  def R1 : BraceReg<1, "r1">, DwarfRegNum<[1]>;
  def R2 : BraceReg<2, "r2">, DwarfRegNum<[2]>;
  def R3 : BraceReg<3, "r3">, DwarfRegNum<[3]>;
  def R4 : BraceReg<4, "r4">, DwarfRegNum<[4]>;
  def R5 : BraceReg<5, "r5">, DwarfRegNum<[5]>;

  // S3b.3 deliberately uses disjoint typed banks.  An S2 physical slot has one
  // type for the whole program, so a conventional overlapping GPR class would
  // allow register allocation to silently change the object semantics.
  def PAddrRegs : RegisterClass<"Brace", [i64], 64, (add R0, R1)>;
  def I8Regs : RegisterClass<"Brace", [i8], 8, (add R2, R3)>;
  def I32Regs : RegisterClass<"Brace", [i32], 32, (add R4, R5)>;
}

defm : RemapAllTargetPseudoPointerOperands<PAddrRegs>;

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
def S2_RETURN_VALUE : S2MCInst<"s2.return-value",
    (ins S2Imm:$value)>;
def S2_DIRECT_CALL : S2MCInst<"s2.direct-call",
    (ins S2Imm:$target, S2Imm:$result, S2Imm:$argument)>;
def S2_PHYSICAL_ADDRESS : S2MCInst<"s2.physical-address",
    (ins S2Imm:$destination, S2Imm:$address)>;
def S2_PHYSICAL_LOAD : S2MCInst<"s2.physical-load",
    (ins S2Imm:$destination, S2Imm:$width, S2Imm:$address)>;
def S2_PHYSICAL_STORE : S2MCInst<"s2.physical-store",
    (ins S2Imm:$width, S2Imm:$address, S2Imm:$source)>;
def S2_FRAME_ENTER : S2MCInst<"s2.frame-enter", (ins)>;
def S2_FRAME_LOAD : S2MCInst<"s2.frame-load",
    (ins S2Imm:$destination, S2Imm:$offset, S2Imm:$width)>;
def S2_FRAME_STORE : S2MCInst<"s2.frame-store",
    (ins S2Imm:$offset, S2Imm:$source, S2Imm:$width)>;
def S2_FRAME_LEAVE : S2MCInst<"s2.frame-leave", (ins)>;

def BraceImm8 : Operand<i8>;
def BraceImm32 : Operand<i32>;
def BraceImm64 : Operand<i64>;
def BraceMBB : Operand<OtherVT>;
def BraceFrameIndex : Operand<i64>;
def BraceHome : Operand<i32>;
def BraceFrameOffset : Operand<i32>;
def BraceFrameSize : Operand<i32>;
def BraceCallTarget : Operand<i64>;

class BraceMI<dag outs, dag ins, string mnemonic> : Instruction {
  let Namespace = "Brace";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = mnemonic;
  let Pattern = [];
  let isCodeGenOnly = 1;
}

def CONST8 : BraceMI<(outs I8Regs:$dst), (ins BraceImm8:$value),
                     "const8 $dst, $value">;
def CONST32 : BraceMI<(outs I32Regs:$dst), (ins BraceImm32:$value),
                      "const32 $dst, $value">;
// The descriptor advertises rematerialization so LLVM's allocator can ask the
// target about it.  BraceInstrInfo keeps the answer ABI-local: this is a pure,
// duplicable constant only for the S3b.4 spill-home profile, while the frozen
// S3b.3 profile retains its old register-pressure behavior and instruction
// ordering.
let isReMaterializable = 1 in
def PADDR_IMM : BraceMI<(outs PAddrRegs:$dst), (ins BraceImm64:$address),
                        "paddr $dst, $address">;

let Constraints = "$dst = $left" in {
  def AND8 : BraceMI<(outs I8Regs:$dst), (ins I8Regs:$left, I8Regs:$right),
                     "and8 $dst, $left, $right">;
  def AND32 : BraceMI<(outs I32Regs:$dst), (ins I32Regs:$left, I32Regs:$right),
                      "and32 $dst, $left, $right">;
}

def MOV8 : BraceMI<(outs I8Regs:$dst), (ins I8Regs:$src), "mov8 $dst, $src">;
def MOV32
    : BraceMI<(outs I32Regs:$dst), (ins I32Regs:$src), "mov32 $dst, $src">;

// S3b.4 deliberately lets LLVM model register-allocation pressure with its
// ordinary private frame-index machinery.  These four pseudos exist only
// between physical-register rewrite and BraceFinalizeSpillHomes.  They are
// never publishable S2 operations and never denote Guest memory.
let isPseudo = 1, mayStore = 1, hasSideEffects = 0 in {
  def SPILL_STORE8
      : BraceMI<(outs), (ins I8Regs:$src, BraceFrameIndex:$slot),
                "spill_store8 $src, $slot">;
  def SPILL_STORE32
      : BraceMI<(outs), (ins I32Regs:$src, BraceFrameIndex:$slot),
                "spill_store32 $src, $slot">;
}

let isPseudo = 1, mayLoad = 1, hasSideEffects = 0 in {
  def SPILL_LOAD8
      : BraceMI<(outs I8Regs:$dst), (ins BraceFrameIndex:$slot),
                "spill_load8 $dst, $slot">;
  def SPILL_LOAD32
      : BraceMI<(outs I32Regs:$dst), (ins BraceFrameIndex:$slot),
                "spill_load32 $dst, $slot">;
}

// FI-free semantic-home transfers are the only S3b.4 publication form.  A
// home ordinal is the zero-based compiler-local h in 0..19, not an LLVM
// physical register or an S2 slot number.  AsmPrinter alone projects h to
// S2 r(6+h).  Keep both directions side-effecting so post-RA sinking cannot
// change architectural fuel/location order.
let isPseudo = 1, hasSideEffects = 1 in {
  def HOME_SAVE8
      : BraceMI<(outs), (ins BraceHome:$home, I8Regs:$src),
                "home_save8 $home, $src">;
  def HOME_SAVE32
      : BraceMI<(outs), (ins BraceHome:$home, I32Regs:$src),
                "home_save32 $home, $src">;
  def HOME_RESTORE8
      : BraceMI<(outs I8Regs:$dst), (ins BraceHome:$home),
                "home_restore8 $dst, $home">;
  def HOME_RESTORE32
      : BraceMI<(outs I32Regs:$dst), (ins BraceHome:$home),
                "home_restore32 $dst, $home">;
}

// S3b.7c finalizer-only semantic Guest byte-frame operations.  FRAME_ENTER's
// size is the target-owned carrier for the serialized function-record
// frame_size_bytes field.  FRAME_ENTER and FRAME_LEAVE each publish one
// semantic operation/tick word whose opcode payload remains zero.  No
// FrameIndex or fixed-stack MMO survives into this publication form.
let isPseudo = 1, hasSideEffects = 1 in {
  def FRAME_ENTER
      : BraceMI<(outs), (ins BraceFrameSize:$size), "frame_enter $size">;
  def FRAME_LEAVE : BraceMI<(outs), (ins), "frame_leave">;
}

let isPseudo = 1, mayLoad = 1, hasSideEffects = 1 in {
  def FRAME_LOAD32
      : BraceMI<(outs I32Regs:$dst), (ins BraceFrameOffset:$offset),
                "frame_load32 $dst, $offset">;
}

let isPseudo = 1, mayStore = 1, hasSideEffects = 1 in {
  def FRAME_STORE32
      : BraceMI<(outs), (ins BraceFrameOffset:$offset, I32Regs:$src),
                "frame_store32 $offset, $src">;
}

let mayLoad = 1, hasSideEffects = 1 in {
  def LOAD8 : BraceMI<(outs I8Regs:$dst), (ins PAddrRegs:$address),
                      "load8 $dst, $address">;
  def LOAD32 : BraceMI<(outs I32Regs:$dst), (ins PAddrRegs:$address),
                       "load32 $dst, $address">;
}

let mayStore = 1, hasSideEffects = 1 in {
  def STORE8 : BraceMI<(outs), (ins PAddrRegs:$address, I8Regs:$source),
                       "store8 $address, $source">;
  def STORE32 : BraceMI<(outs), (ins PAddrRegs:$address, I32Regs:$source),
                        "store32 $address, $source">;
}

let isBranch = 1, isTerminator = 1 in {
  let isBarrier =
      1 in def BR : BraceMI<(outs), (ins BraceMBB:$target), "br $target">;

  // BRCOND is an isel-only one-target branch.  BraceFinalizeBranches replaces
  // it with the explicit two-target BR_IF required by S2 before publication.
  def BRCOND8
      : BraceMI<(outs),
                (ins I8Regs:$condition, BraceMBB:$target, BraceImm8:$nonzero),
                "brcond8 $condition, $target, $nonzero">;
  def BRCOND32
      : BraceMI<(outs),
                (ins I32Regs:$condition, BraceMBB:$target, BraceImm8:$nonzero),
                "brcond32 $condition, $target, $nonzero">;

  let isBarrier = 1 in {
    def BR_IF8 : BraceMI<(outs),
                         (ins I8Regs:$condition, BraceMBB:$true_target,
                             BraceMBB:$false_target),
                         "br_if8 $condition, $true_target, $false_target">;
    def BR_IF32 : BraceMI<(outs),
                          (ins I32Regs:$condition, BraceMBB:$true_target,
                              BraceMBB:$false_target),
                          "br_if32 $condition, $true_target, $false_target">;
  }
}

let isReturn = 1, isTerminator = 1,
    isBarrier = 1 in def RET : BraceMI<(outs), (ins), "ret">;

let isCall = 1, hasSideEffects = 1, Defs = [R4],
    Uses = [R4] in def CALL_I32
    : BraceMI<(outs), (ins BraceCallTarget:$target, variable_ops),
              "call_i32 $target">;

let isReturn = 1, isTerminator = 1,
    isBarrier = 1 in def RET_I32
    : BraceMI<(outs), (ins I32Regs:$value), "ret_i32 $value">;

def CSR_NoRegs : CalleeSavedRegs<(add)>;

def BraceInstrInfo : InstrInfo;

def : ProcessorModel<"generic", NoSchedModel, []>;

def Brace : Target {
  let InstructionSet = BraceInstrInfo;
}
