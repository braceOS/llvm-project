; REQUIRES: brace-registered-target
;
; This registered normalized-IR shape keeps two valued Return blocks.  The
; canonical C frontend merges multiple C returns through a phi at O1, and phi
; is deliberately outside this r0 contract, so the CFG checkpoint enters at
; the independently validated IR seam rather than weakening that boundary.
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
; RUN: llvm-readobj --file-headers --sections --relocations %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJECT
;
; SIZE: 1360
; SHA: 2f6d5463a7f1295eb31dc08e3c8a277ed5576e42d13cdc0366b5d47738991b16
; OBJECT: Flags [ (0x42520200)
; OBJECT: SectionHeaderCount: 11
; OBJECT: Name: .brace.functions
; OBJECT: Size: 128
; OBJECT: Name: .brace.descriptors
; OBJECT: Size: 24

source_filename = "s3b5-direct-call-helper-cfg.ll"
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
  %masked = and i32 %value, 1
  %zero = icmp eq i32 %masked, 0
  br i1 %zero, label %if.then, label %if.else

if.then:
  %even = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  ret i32 %even

if.else:
  %odd = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483664 to ptr addrspace(200)), align 16
  ret i32 %odd
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nofree noinline norecurse nounwind willreturn memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-r0"}
