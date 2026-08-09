; REQUIRES: brace-registered-target
;
; The opt-in System ABI treats LLVM IR as an untrusted carrier.  Keep each
; rejection independent and prove that no partial object is published.
;
; DEFINE: %{brace-s3-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-leaf-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
; RUN: rm -rf %t && split-file %s %t
; RUN: not --crash %{brace-s3-llc} %t/extra-function.ll \
; RUN:   -o %t/extra-function.o 2>&1 | FileCheck %s --check-prefix=FUNCTIONS
; RUN: test ! -s %t/extra-function.o
; RUN: not --crash %{brace-s3-llc} %t/argument.ll \
; RUN:   -o %t/argument.o 2>&1 | FileCheck %s --check-prefix=SIGNATURE
; RUN: test ! -s %t/argument.o
; RUN: not --crash %{brace-s3-llc} %t/global.ll \
; RUN:   -o %t/global.o 2>&1 | FileCheck %s --check-prefix=GLOBAL
; RUN: test ! -s %t/global.o
; RUN: not --crash %{brace-s3-llc} %t/add.ll \
; RUN:   -o %t/add.o 2>&1 | FileCheck %s --check-prefix=ADD
; RUN: test ! -s %t/add.o
; RUN: not --crash %{brace-s3-llc} %t/gep-pointer.ll \
; RUN:   -o %t/gep-pointer.o 2>&1 | FileCheck %s --check-prefix=POINTER
; RUN: test ! -s %t/gep-pointer.o
; RUN: not --crash %{brace-s3-llc} %t/i16-load.ll \
; RUN:   -o %t/i16-load.o 2>&1 | FileCheck %s --check-prefix=WIDTH
; RUN: test ! -s %t/i16-load.o
; RUN: not --crash %{brace-s3-llc} %t/nonvolatile-load.ll \
; RUN:   -o %t/nonvolatile-load.o 2>&1 | FileCheck %s --check-prefix=VOLATILE
; RUN: test ! -s %t/nonvolatile-load.o
; RUN: not --crash %{brace-s3-llc} %t/atomic-load.ll \
; RUN:   -o %t/atomic-load.o 2>&1 | FileCheck %s --check-prefix=VOLATILE
; RUN: test ! -s %t/atomic-load.o
; RUN: not --crash %{brace-s3-llc} %t/alloca.ll \
; RUN:   -o %t/alloca.o 2>&1 | FileCheck %s --check-prefix=INSTRUCTION
; RUN: test ! -s %t/alloca.o
; RUN: not --crash %{brace-s3-llc} %t/inline-asm-call.ll \
; RUN:   -o %t/inline-asm-call.o 2>&1 | FileCheck %s --check-prefix=INSTRUCTION
; RUN: test ! -s %t/inline-asm-call.o
; RUN: not %{brace-s3-llc} %t/wrong-layout.ll \
; RUN:   -o %t/wrong-layout.o 2>&1 | FileCheck %s --check-prefix=LAYOUT
; RUN: test ! -s %t/wrong-layout.o
; RUN: not %{brace-s3-llc} %t/wrong-triple.ll \
; RUN:   -o %t/wrong-triple.o 2>&1 | FileCheck %s --check-prefix=LAYOUT
; RUN: test ! -s %t/wrong-triple.o
; RUN: not --crash %{brace-s3-llc} %t/unreachable.ll \
; RUN:   -o %t/unreachable.o 2>&1 | FileCheck %s --check-prefix=REACHABLE
; RUN: test ! -s %t/unreachable.o
; RUN: not --crash %{brace-s3-llc} %t/five-blocks.ll \
; RUN:   -o %t/five-blocks.o 2>&1 | FileCheck %s --check-prefix=BLOCKS
; RUN: test ! -s %t/five-blocks.o
; RUN: not --crash %{brace-s3-llc} %t/nonvoid.ll -o %t/nonvoid.o
; RUN: test ! -s %t/nonvoid.o
; RUN: not --crash %{brace-s3-llc} %t/vararg.ll -o %t/vararg.o
; RUN: test ! -s %t/vararg.o
; RUN: not --crash %{brace-s3-llc} %t/declaration.ll -o %t/declaration.o
; RUN: test ! -s %t/declaration.o
; RUN: not --crash %{brace-s3-llc} %t/as0-load.ll -o %t/as0-load.o
; RUN: test ! -s %t/as0-load.o
; RUN: not --crash %{brace-s3-llc} %t/addrspacecast.ll \
; RUN:   -o %t/addrspacecast.o
; RUN: test ! -s %t/addrspacecast.o
; RUN: not --crash %{brace-s3-llc} %t/u64-load.ll -o %t/u64-load.o
; RUN: test ! -s %t/u64-load.o
; RUN: not --crash %{brace-s3-llc} %t/misaligned-load.ll \
; RUN:   -o %t/misaligned-load.o
; RUN: test ! -s %t/misaligned-load.o
; RUN: sed 's/add i32/or i32/' %t/add.ll > %t/or.ll
; RUN: not --crash %{brace-s3-llc} %t/or.ll -o %t/or.o
; RUN: test ! -s %t/or.o
; RUN: sed 's/add i32/xor i32/' %t/add.ll > %t/xor.ll
; RUN: not --crash %{brace-s3-llc} %t/xor.ll -o %t/xor.o
; RUN: test ! -s %t/xor.o
; RUN: not --crash %{brace-s3-llc} %t/phi.ll -o %t/phi.o
; RUN: test ! -s %t/phi.o
; RUN: not --crash %{brace-s3-llc} %t/select.ll -o %t/select.o
; RUN: test ! -s %t/select.o
; RUN: not --crash %{brace-s3-llc} %t/switch.ll -o %t/switch.o
; RUN: test ! -s %t/switch.o
; RUN: not --crash %{brace-s3-llc} %t/same-successor.ll \
; RUN:   -o %t/same-successor.o
; RUN: test ! -s %t/same-successor.o
; RUN: not --crash %{brace-s3-llc} %t/undef.ll -o %t/undef.o
; RUN: test ! -s %t/undef.o
; RUN: not --crash %{brace-s3-llc} %t/poison.ll -o %t/poison.o
; RUN: test ! -s %t/poison.o
;
; FUNCTIONS: LLVM ERROR: brace64 S3b.3 leaf ABI: exactly one function is required
; SIGNATURE: LLVM ERROR: brace64 S3b.3 leaf ABI: requires one external C void brace_system_entry(void)
; GLOBAL: LLVM ERROR: brace64 S3b.3 leaf ABI: globals, aliases, ifuncs, and module asm are not admitted
; ADD: LLVM ERROR: brace64 S3b.3 leaf ABI: only i8/i32 integer-and is admitted
; POINTER: LLVM ERROR: brace64 S3b.3 leaf ABI: physical pointer must be a direct i64 addrspace(200) inttoptr
; WIDTH: LLVM ERROR: brace64 S3b.3 leaf ABI: physical memory width must be i8 or i32
; VOLATILE: LLVM ERROR: brace64 S3b.3 leaf ABI: loads must be volatile direct addrspace(200) i8/i32 accesses
; INSTRUCTION: LLVM ERROR: brace64 S3b.3 leaf ABI: instruction is outside the S3b.3 leaf profile
; LAYOUT: error: brace64 S3b.3 input triple or data layout mismatch
; REACHABLE: LLVM ERROR: brace64 S3b.3 leaf ABI: all basic blocks must be reachable from the entry
; BLOCKS: LLVM ERROR: brace64 S3b.3 leaf ABI: basic-block count is outside 1..4

; Common canonical module shape is repeated intentionally so every failure is
; attributable to the single mutation named by its split-file section.

;--- extra-function.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}

declare void @extra()

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- argument.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry(i32 %argument) local_unnamed_addr #0 {
entry:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- global.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

@global = global i32 0

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- add.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = add i32 1, 2
  store volatile i32 %value, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- gep-pointer.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i32, ptr addrspace(200) getelementptr (i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), i64 1), align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- i16-load.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i16, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- nonvolatile-load.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- atomic-load.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load atomic volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)) monotonic, align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- alloca.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %slot = alloca i32, align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- inline-asm-call.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  call void asm sideeffect "", ""()
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- wrong-layout.ll
target datalayout = "e-p:64:64"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- wrong-triple.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-linux-gnu"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- unreachable.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void

dead:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- five-blocks.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  br label %one
one:
  br label %two
two:
  br label %three
three:
  br label %four
four:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- nonvoid.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local i32 @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret i32 0
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- vararg.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry(...) local_unnamed_addr #0 {
entry:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- declaration.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

declare void @brace_system_entry()

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- as0-load.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i32, ptr inttoptr (i64 2147483648 to ptr), align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- addrspacecast.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %pointer = addrspacecast ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)) to ptr
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- u64-load.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i64, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 8
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- misaligned-load.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483649 to ptr addrspace(200)), align 1
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- phi.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  br label %join
join:
  %value = phi i32 [ 0, %entry ]
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- select.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = select i1 true, i32 0, i32 1
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- switch.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  switch i32 0, label %exit [ i32 1, label %exit ]
exit:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- same-successor.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %zero = icmp eq i32 %value, 0
  br i1 %zero, label %done, label %done
done:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- undef.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = and i32 0, undef
  store volatile i32 %value, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- poison.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = and i32 0, poison
  store volatile i32 %value, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
