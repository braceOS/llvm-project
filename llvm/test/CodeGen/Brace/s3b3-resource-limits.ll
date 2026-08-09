; REQUIRES: brace-registered-target
;
; Exercise both sides of every finite S3b.3 compiler resource boundary that
; can be expressed at IR intake, plus exact-type pressure beyond each two-
; register integer bank.  The large one-block cases are generated from the
; registered direct-store IR so their module envelope stays canonical.
;
; DEFINE: %{brace-s3-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-leaf-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
; RUN: rm -rf %t && mkdir -p %t
;
; 128 instructions, 64 tracked values and 63 memory operands are accepted.
; RUN: awk '/^  store volatile i32 21845/ { \
; RUN:   for (i = 0; i < 64; ++i) \
; RUN:     printf "  %cvalue%d = and i32 1, 2\n", 37, i; \
; RUN:   for (i = 0; i < 63; ++i) print; next } { print }' \
; RUN:   %S/s3b3-direct-store.ll > %t/instruction-exact.ll
; RUN: %{brace-s3-llc} %t/instruction-exact.ll \
; RUN:   -o %t/instruction-exact.o
; RUN: test -s %t/instruction-exact.o
; RUN: wc -c < %t/instruction-exact.o | FileCheck %s \
; RUN:   --check-prefix=INSTRUCTION-EXACT-SIZE
;
; The 129th instruction is rejected before publication while memory remains
; at its exact limit.
; RUN: awk '/^  store volatile i32 21845/ { \
; RUN:   for (i = 0; i < 64; ++i) \
; RUN:     printf "  %cvalue%d = and i32 1, 2\n", 37, i; \
; RUN:   for (i = 0; i < 64; ++i) print; next } { print }' \
; RUN:   %S/s3b3-direct-store.ll > %t/instruction-plus-one.ll
; RUN: not --crash %{brace-s3-llc} %t/instruction-plus-one.ll \
; RUN:   -o %t/instruction-plus-one.o 2>&1 | FileCheck %s \
; RUN:   --check-prefix=INSTRUCTION-PLUS-ONE
; RUN: test ! -s %t/instruction-plus-one.o
;
; Exactly 64 physical memory operands are accepted; the 65th is rejected.
; RUN: awk '/^  store volatile i32 21845/ { \
; RUN:   for (i = 0; i < 64; ++i) print; next } { print }' \
; RUN:   %S/s3b3-direct-store.ll > %t/memory-exact.ll
; RUN: %{brace-s3-llc} %t/memory-exact.ll -o %t/memory-exact.o
; RUN: test -s %t/memory-exact.o
; RUN: wc -c < %t/memory-exact.o | FileCheck %s \
; RUN:   --check-prefix=MEMORY-EXACT-SIZE
; RUN: awk '/^  store volatile i32 21845/ { \
; RUN:   for (i = 0; i < 65; ++i) print; next } { print }' \
; RUN:   %S/s3b3-direct-store.ll > %t/memory-plus-one.ll
; RUN: not --crash %{brace-s3-llc} %t/memory-plus-one.ll \
; RUN:   -o %t/memory-plus-one.o 2>&1 | FileCheck %s \
; RUN:   --check-prefix=MEMORY-PLUS-ONE
; RUN: test ! -s %t/memory-plus-one.o
;
; Isolate the tracked-value boundary without relying on memory operations.
; RUN: awk '/^  store volatile i32 21845/ { \
; RUN:   for (i = 0; i < 64; ++i) \
; RUN:     printf "  %cvalue%d = and i32 1, 2\n", 37, i; next } { print }' \
; RUN:   %S/s3b3-direct-store.ll > %t/value-exact.ll
; RUN: %{brace-s3-llc} %t/value-exact.ll -o %t/value-exact.o
; RUN: test -s %t/value-exact.o
; RUN: awk '/^  store volatile i32 21845/ { \
; RUN:   for (i = 0; i < 65; ++i) \
; RUN:     printf "  %cvalue%d = and i32 1, 2\n", 37, i; next } { print }' \
; RUN:   %S/s3b3-direct-store.ll > %t/value-plus-one.ll
; RUN: not --crash %{brace-s3-llc} %t/value-plus-one.ll \
; RUN:   -o %t/value-plus-one.o 2>&1 | FileCheck %s \
; RUN:   --check-prefix=VALUE-PLUS-ONE
; RUN: test ! -s %t/value-plus-one.o
;
; Four blocks and eight CFG edges are accepted.  A four-block module with a
; ninth edge isolates the edge cap before the unsupported switch diagnostic.
; RUN: split-file %s %t/split
; RUN: %{brace-s3-llc} %t/split/edge-exact.ll -o %t/edge-exact.o
; RUN: test -s %t/edge-exact.o
; RUN: not --crash %{brace-s3-llc} %t/split/edge-plus-one.ll \
; RUN:   -o %t/edge-plus-one.o 2>&1 | FileCheck %s \
; RUN:   --check-prefix=EDGE-PLUS-ONE
; RUN: test ! -s %t/edge-plus-one.o
;
; A value may legitimately remain live through an intermediate loop block
; that does not read it before a successor does.  This is internal post-RA
; liveness, not an external MachineFunction ABI live-in.
; RUN: %{brace-s3-llc} %t/split/cross-block-live.ll \
; RUN:   -o %t/cross-block-live.o
; RUN: test -s %t/cross-block-live.o
;
; A third simultaneously live value cannot spill and must fail independently
; in each two-register integer bank.
; RUN: not --crash %{brace-s3-llc} %t/split/i32-pressure.ll \
; RUN:   -o %t/i32-pressure.o
; RUN: test ! -s %t/i32-pressure.o
; RUN: not --crash %{brace-s3-llc} %t/split/i8-pressure.ll \
; RUN:   -o %t/i8-pressure.o
; RUN: test ! -s %t/i8-pressure.o
;
; INSTRUCTION-EXACT-SIZE: 1136
; MEMORY-EXACT-SIZE: 1144
; INSTRUCTION-PLUS-ONE: LLVM ERROR: brace64 S3b.3 leaf ABI: IR instruction count exceeds 128
; MEMORY-PLUS-ONE: LLVM ERROR: brace64 S3b.3 leaf ABI: physical memory operand count exceeds 64
; VALUE-PLUS-ONE: LLVM ERROR: brace64 S3b.3 leaf ABI: tracked non-void value count exceeds 64
; EDGE-PLUS-ONE: LLVM ERROR: brace64 S3b.3 leaf ABI: CFG edge count exceeds 8

; The exact canonical envelope is intentionally repeated in the split inputs:
; every source is independently consumable by llc.

;--- edge-exact.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %v0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %c0 = icmp eq i32 %v0, 0
  br i1 %c0, label %b1, label %b2
b1:
  %v1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  %c1 = icmp ne i32 %v1, 0
  br i1 %c1, label %b2, label %b3
b2:
  %v2 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  %c2 = icmp eq i32 %v2, 0
  br i1 %c2, label %b1, label %b3
b3:
  %v3 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  %c3 = icmp ne i32 %v3, 0
  br i1 %c3, label %b1, label %b2
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- edge-plus-one.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  switch i32 0, label %b1 [
    i32 1, label %b1
    i32 2, label %b2
    i32 3, label %b3
    i32 4, label %b1
    i32 5, label %b2
    i32 6, label %b3
    i32 7, label %b1
    i32 8, label %b2
  ]
b1:
  br label %b2
b2:
  br label %b3
b3:
  br label %b1
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- cross-block-live.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  br label %wait
wait:
  %condition = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  %masked = and i8 %condition, 1
  %ready = icmp ne i8 %masked, 0
  br i1 %ready, label %write, label %wait, !llvm.loop !2
write:
  store volatile i32 %value, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
!2 = distinct !{!2, !3, !4}
!3 = !{!"llvm.loop.mustprogress"}
!4 = !{!"llvm.loop.unroll.disable"}

;--- i32-pressure.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %a = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %b = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  %c = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  store volatile i32 %a, ptr addrspace(200) inttoptr (i64 2147483660 to ptr addrspace(200)), align 4
  store volatile i32 %b, ptr addrspace(200) inttoptr (i64 2147483664 to ptr addrspace(200)), align 16
  store volatile i32 %c, ptr addrspace(200) inttoptr (i64 2147483668 to ptr addrspace(200)), align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- i8-pressure.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %a = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %b = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483649 to ptr addrspace(200)), align 1
  %c = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483650 to ptr addrspace(200)), align 2
  store volatile i8 %a, ptr addrspace(200) inttoptr (i64 2147483651 to ptr addrspace(200)), align 1
  store volatile i8 %b, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  store volatile i8 %c, ptr addrspace(200) inttoptr (i64 2147483653 to ptr addrspace(200)), align 1
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
