; REQUIRES: brace-registered-target
;
; The S3b.2 checkpoint is deliberately narrower than a general backend.  The
; sole accepted IR shape must produce the already accepted S2 object identity,
; and every unsupported TargetMachine configuration must fail without bytes.
;
; RUN: llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj %s -o %t.o
; RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.o | FileCheck %s --check-prefix=ELF
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.o | FileCheck %s --check-prefix=COUNTS
; RUN: llc -mtriple=brace64-unknown-none-elf -O1 -compile-twice -filetype=obj %s -o %t.twice.o
; RUN: cmp %t.o %t.twice.o
;
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -relocation-model=pic -filetype=obj %s -o %t.pic.o
; RUN: test ! -s %t.pic.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -code-model=large -filetype=obj %s -o %t.large.o
; RUN: test ! -s %t.large.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -mcpu=brace-nongeneric -filetype=obj %s -o %t.cpu.o
; RUN: test ! -s %t.cpu.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -mattr=+brace-nonempty -filetype=obj %s -o %t.attr.o
; RUN: test ! -s %t.attr.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -disable-verify -filetype=obj %s -o %t.noverify.o
; RUN: test ! -s %t.noverify.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -filetype=asm %s -o %t.s
; RUN: test ! -s %t.s
; RUN: not llc -mtriple=brace64-unknown-none-elf -O0 -filetype=obj %s -o %t.o0.o
; RUN: test ! -s %t.o0.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O2 -filetype=obj %s -o %t.o2.o
; RUN: test ! -s %t.o2.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O3 -filetype=obj %s -o %t.o3.o
; RUN: test ! -s %t.o3.o
; RUN: sed 's/nofree //' %s > %t.missing-attr.ll
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj %t.missing-attr.ll -o %t.missing-attr.o
; RUN: test ! -s %t.missing-attr.o
; RUN: sed 's/, target_mem0: none, target_mem1: none//' %s > %t.memory-attr.ll
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj %t.memory-attr.ll -o %t.memory-attr.o
; RUN: test ! -s %t.memory-attr.o
; RUN: sed 's/ local_unnamed_addr//' %s > %t.unnamed-addr.ll
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj %t.unnamed-addr.ll -o %t.unnamed-addr.o
; RUN: test ! -s %t.unnamed-addr.o
;
; SIZE: 2272
; SHA: c0be5323fceadba8847d844144053e861a4fb2a5b010d72b5e5f54d850bdabba
;
; ELF: Sections [
; ELF: Name: .brace.target
; ELF: Name: .brace.types
; ELF: Name: .brace.literals
; ELF: Name: .brace.text
; ELF: Name: .rela.brace.literals
; ELF: Name: .symtab
; ELF: Name: .strtab
; ELF: Name: .shstrtab
; ELF: Relocations [
; ELF: Section (5) .rela.brace.literals {
; ELF: Symbols [
;
; COUNTS: Sections [
; COUNTS-COUNT-9: Section {
; COUNTS: Relocations [
; COUNTS: Section (5) .rela.brace.literals {
; COUNTS-COUNT-29: Type: Unknown (1)
; COUNTS: Symbols [
; COUNTS-COUNT-2: Symbol {
;
; ModuleID = 'brace-s2-exact-object'
source_filename = "brace-s2-exact-object.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  store volatile i32 1819043144, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  store volatile i32 1867980911, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  store volatile i32 543452274, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  store volatile i32 1836020326, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  store volatile i32 1634877984, ptr addrspace(200) inttoptr (i64 2147483664 to ptr addrspace(200)), align 16
  store volatile i32 1327523171, ptr addrspace(200) inttoptr (i64 2147483668 to ptr addrspace(200)), align 4
  store volatile i32 2643, ptr addrspace(200) inttoptr (i64 2147483672 to ptr addrspace(200)), align 8
  br label %while.cond

while.cond:
  %0 = load volatile i8, ptr addrspace(200) inttoptr (i64 268435461 to ptr addrspace(200)), align 1
  %1 = and i8 %0, 32
  %cmp = icmp eq i8 %1, 0
  br i1 %cmp, label %while.cond, label %do.body, !llvm.loop !1

do.body:
  %2 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  store volatile i8 %2, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %3 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483649 to ptr addrspace(200)), align 1
  store volatile i8 %3, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %4 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483650 to ptr addrspace(200)), align 2
  store volatile i8 %4, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %5 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483651 to ptr addrspace(200)), align 1
  store volatile i8 %5, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %6 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  store volatile i8 %6, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %7 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483653 to ptr addrspace(200)), align 1
  store volatile i8 %7, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %8 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483654 to ptr addrspace(200)), align 2
  store volatile i8 %8, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %9 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483655 to ptr addrspace(200)), align 1
  store volatile i8 %9, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %10 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  store volatile i8 %10, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %11 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483657 to ptr addrspace(200)), align 1
  store volatile i8 %11, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %12 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483658 to ptr addrspace(200)), align 2
  store volatile i8 %12, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %13 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483659 to ptr addrspace(200)), align 1
  store volatile i8 %13, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %14 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  store volatile i8 %14, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %15 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483661 to ptr addrspace(200)), align 1
  store volatile i8 %15, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %16 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483662 to ptr addrspace(200)), align 2
  store volatile i8 %16, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %17 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483663 to ptr addrspace(200)), align 1
  store volatile i8 %17, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %18 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483664 to ptr addrspace(200)), align 16
  store volatile i8 %18, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %19 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483665 to ptr addrspace(200)), align 1
  store volatile i8 %19, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %20 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483666 to ptr addrspace(200)), align 2
  store volatile i8 %20, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %21 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483667 to ptr addrspace(200)), align 1
  store volatile i8 %21, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %22 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483668 to ptr addrspace(200)), align 4
  store volatile i8 %22, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %23 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483669 to ptr addrspace(200)), align 1
  store volatile i8 %23, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %24 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483670 to ptr addrspace(200)), align 2
  store volatile i8 %24, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %25 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483671 to ptr addrspace(200)), align 1
  store volatile i8 %25, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %26 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483672 to ptr addrspace(200)), align 8
  store volatile i8 %26, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  %27 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483673 to ptr addrspace(200)), align 1
  store volatile i8 %27, ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
  store volatile i32 21845, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = distinct !{!1, !2, !3}
!2 = !{!"llvm.loop.mustprogress"}
!3 = !{!"llvm.loop.unroll.disable"}
