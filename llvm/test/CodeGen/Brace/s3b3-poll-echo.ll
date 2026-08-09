; REQUIRES: brace-registered-target
;
; Freeze the poll backedge, u8/paddr typed banks, canonical removal of the
; empty source entry block, final two-target BranchIf, and MIR restart.
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %s -o %t.o
; RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJECT
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.o | \
; RUN:   FileCheck %s --check-prefix=COUNTS
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %s -o %t.again.o
; RUN: cmp %t.o %t.again.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -stop-after=finalize-isel %s -o %t.isel.mir
; RUN: FileCheck %s --check-prefix=ISEL < %t.isel.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -start-after=finalize-isel %t.isel.mir \
; RUN:   -o %t.from-isel.o
; RUN: cmp %t.o %t.from-isel.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -stop-after=virtregrewriter %s -o %t.ra.mir
; RUN: FileCheck %s --check-prefix=RA < %t.ra.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -start-after=virtregrewriter %t.ra.mir \
; RUN:   -o %t.from-ra.o
; RUN: cmp %t.o %t.from-ra.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -stop-after=brace-finalize-branches %s \
; RUN:   -o %t.final.mir
; RUN: FileCheck %s --check-prefix=FINAL < %t.final.mir
;
; SIZE: 976
; SHA: 9fceb5d9ca34d4d74740193c8cb83cfe71da847594770d59b9ef48931391eb93
; OBJECT: Sections [
; OBJECT: Name: .brace.text
; OBJECT: Size: 40
; OBJECT: Relocations [
; OBJECT: Section (5) .rela.brace.literals {
; OBJECT: Symbols [
; COUNTS: Sections [
; COUNTS-COUNT-9: Section {
; COUNTS: Relocations [
; COUNTS: Section (5) .rela.brace.literals {
; COUNTS-COUNT-3: Type: Unknown (1)
; COUNTS: Symbols [
; COUNTS-COUNT-2: Symbol {
;
; ISEL-LABEL: name: brace_system_entry
; ISEL: noVRegs: false
; ISEL-DAG: class: paddrregs
; ISEL-DAG: class: i8regs
; ISEL: liveins: []
; ISEL: stackSize: 0
; ISEL: hasCalls: false
; ISEL: stack: []
; ISEL-LABEL: body: |
; ISEL: bb.0.entry:
; ISEL-NEXT: successors: %bb.1
; ISEL: bb.1.while.cond:
; ISEL: %0:paddrregs = PADDR_IMM 268435461
; ISEL-NEXT: %1:i8regs = LOAD8 killed %0
; ISEL-NEXT: %2:i8regs = CONST8 32
; ISEL-NEXT: %3:i8regs = AND8 %1, killed %2
; ISEL-NEXT: BRCOND8 killed %3, %bb.1, 0
; ISEL-NEXT: BR %bb.2
; ISEL: bb.2.while.end:
; ISEL: %4:paddrregs = PADDR_IMM 2147483648
; ISEL-NEXT: %5:i8regs = LOAD8 killed %4
; ISEL-NEXT: %6:paddrregs = PADDR_IMM 268435456
; ISEL-NEXT: STORE8 killed %6, killed %5
; ISEL-NEXT: RET
;
; RA-LABEL: name: brace_system_entry
; RA: noVRegs: true
; RA: registers: []
; RA: liveins: []
; RA: stackSize: 0
; RA: hasCalls: false
; RA: stack: []
; RA-LABEL: body: |
; RA: bb.0.entry:
; RA: bb.1.while.cond:
; RA: $r0 = PADDR_IMM 268435461
; RA-NEXT: $r2 = LOAD8 killed $r0
; RA-NEXT: $r3 = CONST8 32
; RA-NEXT: $r2 = AND8 killed $r2, killed $r3
; RA-NEXT: BRCOND8 killed $r2, %bb.1, 0
; RA-NEXT: BR %bb.2
; RA: bb.2.while.end:
; RA: $r0 = PADDR_IMM 2147483648
; RA-NEXT: $r2 = LOAD8 killed $r0
; RA-NEXT: $r0 = PADDR_IMM 268435456
; RA-NEXT: STORE8 killed $r0, killed $r2
; RA-NEXT: RET
;
; FINAL-LABEL: name: brace_system_entry
; FINAL: noVRegs: true
; FINAL: stackSize: 0
; FINAL: hasCalls: false
; FINAL: stack: []
; FINAL-LABEL: body: |
; FINAL-NOT: bb.0.entry:
; FINAL: bb.1.while.cond:
; FINAL: $r2 = AND8 killed $r2, killed $r3
; FINAL-NEXT: BR_IF8 killed $r2, %bb.2, %bb.1
; FINAL: bb.2.while.end:
; FINAL: STORE8 killed $r0, killed $r2
; FINAL-NEXT: RET
;
source_filename = "s3b3-poll-echo.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  br label %while.cond

while.cond:
  %status = load volatile i8, ptr addrspace(200) inttoptr (i64 268435461 to ptr addrspace(200)), align 1
  %masked = and i8 %status, 32
  %wait = icmp eq i8 %masked, 0
  br i1 %wait, label %while.cond, label %while.end, !llvm.loop !2

while.end:
  %byte = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  store volatile i8 %byte, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
!2 = distinct !{!2, !3, !4}
!3 = !{!"llvm.loop.mustprogress"}
!4 = !{!"llvm.loop.unroll.disable"}
