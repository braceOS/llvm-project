; REQUIRES: brace-registered-target
;
; H0 and one i32/one-use H1 are the only S3b.6 caller-home shapes.  Reject a
; second live value and a differently typed live value at the IR trust
; boundary, before Greedy RA or object publication.
;
; RUN: rm -rf %t && split-file %s %t
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-home-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %t/h2.ll -o %t/h2.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=H2
; RUN: test ! -s %t/h2.o
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-home-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %t/i8.ll -o %t/i8.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=I8
; RUN: test ! -s %t/i8.o
;
; H2: LLVM ERROR: brace64 S3b.6 direct-call-home ABI: root permits H0 or exactly one i32 SSA value with one post-call use
; I8: LLVM ERROR: brace64 S3b.6 direct-call-home ABI: call-live home must originate from one i32 physical load

;--- h2.ll
source_filename = "s3b6-h2.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %input1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input0) #2
  %combined0 = and i32 %result, %input0
  %combined1 = and i32 %combined0, %input1
  store volatile i32 %combined1, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
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
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-home-r0"}

;--- i8.ll
source_filename = "s3b6-i8.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %live8 = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #2
  store volatile i32 %result, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  store volatile i8 %live8, ptr addrspace(200) inttoptr (i64 2147483661 to ptr addrspace(200)), align 1
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
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-home-r0"}
