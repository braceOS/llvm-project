; REQUIRES: brace-registered-target
;
; Freeze the four-operation direct-store path, both registered finisher
; results, the virtual/physical typed banks, and both MIR restart seams.
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %s -o %t.pass.o
; RUN: wc -c < %t.pass.o | FileCheck %s --check-prefix=PASS-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.pass.o | FileCheck %s --check-prefix=PASS-SHA
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs \
; RUN:   %t.pass.o | FileCheck %s --check-prefix=OBJECT
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs \
; RUN:   %t.pass.o | FileCheck %s --check-prefix=COUNTS
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %s -o %t.pass-again.o
; RUN: cmp %t.pass.o %t.pass-again.o
;
; RUN: sed 's/store volatile i32 21845/store volatile i32 78643/' %s > %t.fail.ll
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %t.fail.ll -o %t.fail.o
; RUN: wc -c < %t.fail.o | FileCheck %s --check-prefix=FAIL-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.fail.o | FileCheck %s --check-prefix=FAIL-SHA
; RUN: not cmp %t.pass.o %t.fail.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -stop-after=finalize-isel %s -o %t.isel.mir
; RUN: FileCheck %s --check-prefix=ISEL < %t.isel.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -start-after=finalize-isel %t.isel.mir \
; RUN:   -o %t.from-isel.o
; RUN: cmp %t.pass.o %t.from-isel.o
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -stop-after=virtregrewriter %s -o %t.ra.mir
; RUN: FileCheck %s --check-prefix=RA < %t.ra.mir
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj -start-after=virtregrewriter %t.ra.mir \
; RUN:   -o %t.from-ra.o
; RUN: cmp %t.pass.o %t.from-ra.o
;
; The S3b.3 corpus is not accepted by the ABI-less S3b.2 profile.
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj %s \
; RUN:   -o %t.default-reject.o
; RUN: test ! -s %t.default-reject.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -target-abi=not-brace \
; RUN:   -O1 -filetype=obj %s -o %t.unknown-abi.o
; RUN: test ! -s %t.unknown-abi.o
; RUN: sed 's/brace-system-s2-leaf-r0/not-brace/' %s > %t.mismatch.ll
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -filetype=obj \
; RUN:   %t.mismatch.ll -o %t.mismatch.o
; RUN: test ! -s %t.mismatch.o
;
; Every noncanonical TargetMachine selector must fail before publishing bytes.
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -relocation-model=pic \
; RUN:   -filetype=obj %s -o %t.pic.o
; RUN: test ! -s %t.pic.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -code-model=large \
; RUN:   -filetype=obj %s -o %t.large.o
; RUN: test ! -s %t.large.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -mcpu=brace-nongeneric \
; RUN:   -filetype=obj %s -o %t.cpu.o
; RUN: test ! -s %t.cpu.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -mattr=+brace-nonempty \
; RUN:   -filetype=obj %s -o %t.feature.o
; RUN: test ! -s %t.feature.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -disable-verify \
; RUN:   -filetype=obj %s -o %t.noverify.o
; RUN: test ! -s %t.noverify.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -filetype=asm \
; RUN:   %s -o %t.s
; RUN: test ! -s %t.s
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O0 -filetype=obj \
; RUN:   %s -o %t.o0.o
; RUN: test ! -s %t.o0.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O2 -filetype=obj \
; RUN:   %s -o %t.o2.o
; RUN: test ! -s %t.o2.o
; RUN: not llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O3 -filetype=obj \
; RUN:   %s -o %t.o3.o
; RUN: test ! -s %t.o3.o
;
; S3b.3 must not change the old default profile's exact object identity.
; RUN: llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj \
; RUN:   %S/s2-exact-object.ll -o %t.legacy.o
; RUN: wc -c < %t.legacy.o | FileCheck %s --check-prefix=LEGACY-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.legacy.o | FileCheck %s --check-prefix=LEGACY-SHA
;
; PASS-SIZE: 888
; PASS-SHA: 38ff300acc0c2b61b38730a4b3c2f17ca3b1e8a975f60765f72a5b5cc91f507b
; FAIL-SIZE: 888
; FAIL-SHA: cde7723fe00acf799805dfd9b82e9c9cf22f30fc6f8b260b46753edfb9b2d880
; LEGACY-SIZE: 2272
; LEGACY-SHA: c0be5323fceadba8847d844144053e861a4fb2a5b010d72b5e5f54d850bdabba
;
; OBJECT: Sections [
; OBJECT: Name: .brace.text
; OBJECT: Size: 16
; OBJECT: Relocations [
; OBJECT: Section (5) .rela.brace.literals {
; OBJECT: Symbols [
; COUNTS: Sections [
; COUNTS-COUNT-9: Section {
; COUNTS: Relocations [
; COUNTS: Section (5) .rela.brace.literals {
; COUNTS-COUNT-1: Type: Unknown (1)
; COUNTS: Symbols [
; COUNTS-COUNT-2: Symbol {
;
; ISEL-LABEL: name: brace_system_entry
; ISEL: noVRegs: false
; ISEL: registers:
; ISEL-DAG: class: i32regs
; ISEL-DAG: class: paddrregs
; ISEL: liveins: []
; ISEL: stackSize: 0
; ISEL: hasCalls: false
; ISEL: fixedStack: []
; ISEL-NEXT: stack: []
; ISEL-LABEL: body: |
; ISEL: bb.0.entry:
; ISEL-NEXT: %0:i32regs = CONST32 21845
; ISEL-NEXT: %1:paddrregs = PADDR_IMM 1048576
; ISEL-NEXT: STORE32 killed %1, killed %0
; ISEL-NEXT: RET
;
; RA-LABEL: name: brace_system_entry
; RA: noVRegs: true
; RA: registers: []
; RA: liveins: []
; RA: stackSize: 0
; RA: hasCalls: false
; RA: fixedStack: []
; RA-NEXT: stack: []
; RA-LABEL: body: |
; RA: bb.0.entry:
; RA-NEXT: $r4 = CONST32 21845
; RA-NEXT: $r0 = PADDR_IMM 1048576
; RA-NEXT: STORE32 killed $r0, killed $r4
; RA-NEXT: RET
;
source_filename = "s3b3-direct-store.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  store volatile i32 21845, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
