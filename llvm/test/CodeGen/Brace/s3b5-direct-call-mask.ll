; REQUIRES: brace-registered-target
;
; The pure registered helper freezes the exact return range emitted for an
; i32 mask of 15.  The negative suite mutates this payload independently.
;
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %s -o %t.o
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %s -o %t.again.o
; RUN: cmp %t.o %t.again.o
; RUN: llvm-readobj --file-headers --sections %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJECT
;
; OBJECT: Flags [ (0x42520200)
; OBJECT: SectionHeaderCount: 11
; OBJECT: Name: .brace.functions
; OBJECT: Size: 128
; OBJECT: Name: .brace.descriptors
; OBJECT: Size: 24

source_filename = "s3b5-direct-call-mask.ll"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #2
  store volatile i32 %result, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  ret void
}

define internal fastcc noundef range(i32 0, 16) i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #1 {
entry:
  %masked = and i32 %value, 15
  ret i32 %masked
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nofree noinline norecurse nosync nounwind willreturn memory(none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-r0"}
