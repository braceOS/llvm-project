; REQUIRES: brace-registered-target
;
; Freeze the nontrivial i32 mask, both diamond arms, one explicit Branch, the
; final two-target BranchIf, typed register allocation, and MIR restart.
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
; SIZE: 1008
; SHA: 22df64c912289e69f11fe916d5702f3b22705743033917608cdeed27cfdb8069
; OBJECT: Sections [
; OBJECT: Name: .brace.text
; OBJECT: Size: 52
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
; ISEL-DAG: class: i32regs
; ISEL: liveins: []
; ISEL: stackSize: 0
; ISEL: hasCalls: false
; ISEL: stack: []
; ISEL-LABEL: body: |
; ISEL: %0:paddrregs = PADDR_IMM 2147483648
; ISEL-NEXT: %1:i32regs = LOAD32 killed %0
; ISEL-NEXT: %2:i32regs = CONST32 -2147483647
; ISEL-NEXT: %3:i32regs = AND32 %1, killed %2
; ISEL-NEXT: BRCOND32 killed %3, %bb.2, 1
; ISEL-NEXT: BR %bb.1
; ISEL: %6:i32regs = CONST32 1515847680
; ISEL-NEXT: %7:paddrregs = PADDR_IMM 2147483652
; ISEL-NEXT: STORE32 killed %7, killed %6
; ISEL-NEXT: BR %bb.3
; ISEL: %4:i32regs = CONST32 -1515913215
; ISEL-NEXT: %5:paddrregs = PADDR_IMM 2147483656
; ISEL-NEXT: STORE32 killed %5, killed %4
; ISEL: bb.3.if.end:
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
; RA: $r0 = PADDR_IMM 2147483648
; RA-NEXT: $r4 = LOAD32 killed $r0
; RA-NEXT: $r5 = CONST32 -2147483647
; RA-NEXT: $r4 = AND32 killed $r4, killed $r5
; RA-NEXT: BRCOND32 killed $r4, %bb.2, 1
; RA-NEXT: BR %bb.1
; RA: $r4 = CONST32 1515847680
; RA-NEXT: $r0 = PADDR_IMM 2147483652
; RA-NEXT: STORE32 killed $r0, killed $r4
; RA-NEXT: BR %bb.3
; RA: $r4 = CONST32 -1515913215
; RA-NEXT: $r0 = PADDR_IMM 2147483656
; RA-NEXT: STORE32 killed $r0, killed $r4
; RA: bb.3.if.end:
; RA-NEXT: RET
;
; FINAL-LABEL: name: brace_system_entry
; FINAL: noVRegs: true
; FINAL: stackSize: 0
; FINAL: hasCalls: false
; FINAL: stack: []
; FINAL-LABEL: body: |
; FINAL: $r4 = AND32 killed $r4, killed $r5
; FINAL-NEXT: BR_IF32 killed $r4, %bb.2, %bb.1
; FINAL: bb.1.if.then:
; FINAL: BR %bb.3
; FINAL: bb.2.if.else:
; FINAL-NOT: BR %bb.3
; FINAL: bb.3.if.end:
; FINAL-NEXT: RET
;
source_filename = "s3b3-load-and-diamond.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %masked = and i32 %input, -2147483647
  %zero = icmp eq i32 %masked, 0
  br i1 %zero, label %if.then, label %if.else

if.then:
  store volatile i32 1515847680, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  br label %if.end

if.else:
  store volatile i32 -1515913215, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  br label %if.end

if.end:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
