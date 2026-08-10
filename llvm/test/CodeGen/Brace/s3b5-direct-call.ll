; REQUIRES: brace-registered-target
;
; Freeze the first private fastcc i32(i32) direct-call path from normalized IR
; through both serializable MIR seams and the exact eleven-section object.
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %s -o %t.again.o
; RUN: cmp %t.o %t.again.o
; RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
; RUN: llvm-readobj --file-headers --sections --symbols --relocations \
; RUN:   --expand-relocs %t.o | FileCheck %s --check-prefix=OBJECT
; RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=SCNT
;
; SIZE: 1304
; SHA: 638333df950316ee0992d17cc787fc65a90b2803cccbbfcca1d47dc4a535ba9d
; OBJECT: OS/ABI: Standalone (0xFF)
; OBJECT: Type: Relocatable (0x1)
; OBJECT: Machine: 0xFFB0
; OBJECT: Flags [ (0x42520200)
; OBJECT: SectionHeaderCount: 11
; OBJECT: StringTableSectionIndex: 10
; OBJECT: Sections [
; OBJECT: Name: .brace.target
; OBJECT: Size: 32
; OBJECT: Name: .brace.functions
; OBJECT: Size: 128
; OBJECT: Name: .brace.types
; OBJECT: Size: 12
; OBJECT: Name: .brace.literals
; OBJECT: Size: 24
; OBJECT: Name: .brace.descriptors
; OBJECT: Size: 24
; OBJECT: Name: .brace.text
; OBJECT: Size: 44
; OBJECT: Relocations [
; OBJECT: Section (7) .rela.brace.literals {
; OBJECT-COUNT-3: Type: Unknown (1)
; OBJECT: Symbols [
; OBJECT-COUNT-2: Symbol {
; SCNT: Sections [
; SCNT-COUNT-11: Section {
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj -stop-after=finalize-isel \
; RUN:   %s -o %t.isel.mir
; RUN: FileCheck %s --check-prefix=ISEL < %t.isel.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj -start-after=finalize-isel \
; RUN:   %t.isel.mir -o %t.from-isel.o
; RUN: cmp %t.o %t.from-isel.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj -stop-after=virtregrewriter \
; RUN:   %s -o %t.ra.mir
; RUN: FileCheck %s --check-prefix=RA < %t.ra.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj -start-after=virtregrewriter \
; RUN:   %t.ra.mir -o %t.from-ra.o
; RUN: cmp %t.o %t.from-ra.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj \
; RUN:   -stop-after=brace-finalize-branches %s -o %t.final.mir
; RUN: FileCheck %s --check-prefix=FINAL < %t.final.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj \
; RUN:   -start-after=brace-finalize-branches %t.final.mir \
; RUN:   -o %t.from-final.o
; RUN: cmp %t.o %t.from-final.o
;
; ISEL-LABEL: name: brace_system_entry
; ISEL: noVRegs: false
; ISEL: liveins: []
; ISEL: stackSize: 0
; ISEL: hasCalls: true
; ISEL: stack: []
; ISEL-LABEL: body: |
; ISEL: %0:paddrregs = PADDR_IMM 2147483648
; ISEL-NEXT: %1:i32regs = LOAD32 killed %0
; ISEL: CALL_I32 @brace_system_call_leaf
; ISEL-SAME: csr_noregs
; ISEL-SAME: implicit-def $r4
; ISEL-SAME: implicit $r4
; ISEL: RET
; ISEL-LABEL: name: brace_system_call_leaf
; ISEL: noVRegs: false
; ISEL: liveins:
; ISEL: reg: '$r4'
; ISEL: stackSize: 0
; ISEL: hasCalls: false
; ISEL: stack: []
; ISEL-LABEL: body: |
; ISEL: liveins: $r4
; ISEL: %0:i32regs = COPY $r4
; ISEL: RET_I32 $r4
;
; RA-LABEL: name: brace_system_entry
; RA: noVRegs: true
; RA: registers: []
; RA: liveins: []
; RA: stackSize: 0
; RA: hasCalls: true
; RA: stack: []
; RA-LABEL: body: |
; RA: $r0 = PADDR_IMM 2147483648
; RA-NEXT: $r4 = LOAD32 killed $r0
; RA-NEXT: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
; RA-NEXT: $r0 = PADDR_IMM 2147483652
; RA-NEXT: STORE32 killed $r0, killed $r4
; RA-NEXT: RET
; RA-LABEL: name: brace_system_call_leaf
; RA: noVRegs: true
; RA: registers: []
; RA: liveins:
; RA: reg: '$r4'
; RA: stackSize: 0
; RA: hasCalls: false
; RA: stack: []
; RA-LABEL: body: |
; RA: liveins: $r4
; RA: $r0 = PADDR_IMM 2147483656
; RA-NEXT: $r5 = LOAD32 killed $r0
; RA-NEXT: $r5 = AND32 killed $r5, killed $r4
; RA-NEXT: $r4 = COPY killed $r5
; RA-NEXT: RET_I32 $r4
;
; FINAL-LABEL: name: brace_system_entry
; FINAL: noVRegs: true
; FINAL: liveins: []
; FINAL: stackSize: 0
; FINAL: hasCalls: true
; FINAL: stack: []
; FINAL-LABEL: body: |
; FINAL: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
; FINAL-NEXT: $r0 = PADDR_IMM 2147483652
; FINAL-NEXT: STORE32 killed $r0, killed $r4
; FINAL-NEXT: RET
; FINAL-LABEL: name: brace_system_call_leaf
; FINAL: noVRegs: true
; FINAL: liveins:
; FINAL: reg: '$r4'
; FINAL: stackSize: 0
; FINAL: hasCalls: false
; FINAL: stack: []
; FINAL-LABEL: body: |
; FINAL: liveins: $r4
; FINAL: $r4 = MOV32 killed $r5
; FINAL-NEXT: RET_I32 $r4

source_filename = "s3b5-direct-call.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #2
  store volatile i32 %result, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  ret void
}

define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #1 {
entry:
  %mask = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  %masked = and i32 %mask, %value
  ret i32 %masked
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nofree noinline norecurse nounwind willreturn memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-r0"}
