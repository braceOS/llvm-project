; REQUIRES: brace-registered-target
;
; Complete the Section 11 directed matrix with parser- and verifier-valid IR.
; Every mutation must reach the Brace target trust boundary, terminate through
; its fail-closed fatal path, and leave no object bytes behind.
;
; DEFINE: %{brace-s3-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-leaf-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
; RUN: rm -rf %t.dir && split-file %s %t.dir
;
; Calls and function identities are independently outside the zero-call leaf
; ABI.  Direct and tail calls use the sole function recursively so an extra
; declaration cannot become the reason for rejection.
; RUN: awk '$0 == "  ret void" { \
; RUN:   print "  call void @brace_system_entry()" } { print }' \
; RUN:   %t.dir/seed.ll > %t.dir/direct-call.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/direct-call.ll \
; RUN:   -o %t.dir/direct-call.o
; RUN: test ! -s %t.dir/direct-call.o
; RUN: awk '$0 == "  ret void" { \
; RUN:   print "  tail call void @brace_system_entry()" } { print }' \
; RUN:   %t.dir/seed.ll > %t.dir/tail-call.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/tail-call.ll \
; RUN:   -o %t.dir/tail-call.o
; RUN: test ! -s %t.dir/tail-call.o
; RUN: awk '$0 == "  ret void" { \
; RUN:   print "  call void inttoptr (i64 1 to ptr)()" } { print }' \
; RUN:   %t.dir/seed.ll > %t.dir/indirect-call.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/indirect-call.ll \
; RUN:   -o %t.dir/indirect-call.o
; RUN: test ! -s %t.dir/indirect-call.o
; RUN: awk '$0 == "  ret void" { \
; RUN:   print "  %address = ptrtoint ptr @brace_system_entry to i64" } \
; RUN:   { print }' %t.dir/seed.ll > %t.dir/function-address.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/function-address.ll \
; RUN:   -o %t.dir/function-address.o
; RUN: test ! -s %t.dir/function-address.o
; RUN: sed 's/define dso_local/define internal/' %t.dir/seed.ll \
; RUN:   > %t.dir/internal-entry.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/internal-entry.ll \
; RUN:   -o %t.dir/internal-entry.o
; RUN: test ! -s %t.dir/internal-entry.o
; RUN: sed 's/@brace_system_entry/@not_brace_entry/g' %t.dir/seed.ll \
; RUN:   > %t.dir/wrong-entry-name.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/wrong-entry-name.ll \
; RUN:   -o %t.dir/wrong-entry-name.o
; RUN: test ! -s %t.dir/wrong-entry-name.o
;
; Module and function decoration categories are kept separate even though the
; finite envelope rejects all of them before instruction selection.
; RUN: not --crash %{brace-s3-llc} %t.dir/alias.ll -o %t.dir/alias.o
; RUN: test ! -s %t.dir/alias.o
; RUN: not --crash %{brace-s3-llc} %t.dir/ifunc.ll -o %t.dir/ifunc.o
; RUN: test ! -s %t.dir/ifunc.o
; RUN: not --crash %{brace-s3-llc} %t.dir/comdat.ll -o %t.dir/comdat.o
; RUN: test ! -s %t.dir/comdat.o
; RUN: not --crash %{brace-s3-llc} %t.dir/tls.ll -o %t.dir/tls.o
; RUN: test ! -s %t.dir/tls.o
; RUN: sed 's/ #0 {/ #0 personality ptr null {/' %t.dir/seed.ll \
; RUN:   > %t.dir/unwind.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/unwind.ll -o %t.dir/unwind.o
; RUN: test ! -s %t.dir/unwind.o
; RUN: not --crash %{brace-s3-llc} %t.dir/debug.ll -o %t.dir/debug.o
; RUN: test ! -s %t.dir/debug.o
; RUN: sed 's/attributes #0 = {/attributes #0 = { sanitize_address/' \
; RUN:   %t.dir/seed.ll > %t.dir/sanitizer.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/sanitizer.ll \
; RUN:   -o %t.dir/sanitizer.o
; RUN: test ! -s %t.dir/sanitizer.o
;
; Pointer construction, observation and escape are rejected independently.
; RUN: not --crash %{brace-s3-llc} %t.dir/dynamic-as200.ll \
; RUN:   -o %t.dir/dynamic-as200.o
; RUN: test ! -s %t.dir/dynamic-as200.o
; RUN: not --crash %{brace-s3-llc} %t.dir/ptrtoint.ll \
; RUN:   -o %t.dir/ptrtoint.o
; RUN: test ! -s %t.dir/ptrtoint.o
; RUN: not --crash %{brace-s3-llc} %t.dir/pointer-escape.ll \
; RUN:   -o %t.dir/pointer-escape.o
; RUN: test ! -s %t.dir/pointer-escape.o
; RUN: not --crash %{brace-s3-llc} %t.dir/pointer-compare.ll \
; RUN:   -o %t.dir/pointer-compare.o
; RUN: test ! -s %t.dir/pointer-compare.o
;
; Every ordinary arithmetic family missing from the compact base matrix is a
; separate valid IR mutation of the same integer/store graph.
; RUN: sed 's/add i32/sub i32/' %t.dir/arithmetic.ll > %t.dir/sub.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/sub.ll -o %t.dir/sub.o
; RUN: test ! -s %t.dir/sub.o
; RUN: sed 's/add i32/mul i32/' %t.dir/arithmetic.ll > %t.dir/mul.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/mul.ll -o %t.dir/mul.o
; RUN: test ! -s %t.dir/mul.o
; RUN: sed 's/add i32/sdiv i32/' %t.dir/arithmetic.ll > %t.dir/sdiv.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/sdiv.ll -o %t.dir/sdiv.o
; RUN: test ! -s %t.dir/sdiv.o
; RUN: sed 's/add i32/udiv i32/' %t.dir/arithmetic.ll > %t.dir/udiv.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/udiv.ll -o %t.dir/udiv.o
; RUN: test ! -s %t.dir/udiv.o
; RUN: sed 's/add i32/srem i32/' %t.dir/arithmetic.ll > %t.dir/srem.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/srem.ll -o %t.dir/srem.o
; RUN: test ! -s %t.dir/srem.o
; RUN: sed 's/add i32/urem i32/' %t.dir/arithmetic.ll > %t.dir/urem.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/urem.ll -o %t.dir/urem.o
; RUN: test ! -s %t.dir/urem.o
; RUN: sed 's/add i32/shl i32/' %t.dir/arithmetic.ll > %t.dir/shl.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/shl.ll -o %t.dir/shl.o
; RUN: test ! -s %t.dir/shl.o
; RUN: sed 's/add i32/lshr i32/' %t.dir/arithmetic.ll > %t.dir/lshr.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/lshr.ll -o %t.dir/lshr.o
; RUN: test ! -s %t.dir/lshr.o
; RUN: sed 's/add i32/ashr i32/' %t.dir/arithmetic.ll > %t.dir/ashr.ll
; RUN: not --crash %{brace-s3-llc} %t.dir/ashr.ll -o %t.dir/ashr.o
; RUN: test ! -s %t.dir/ashr.o
; RUN: not --crash %{brace-s3-llc} %t.dir/general-compare.ll \
; RUN:   -o %t.dir/general-compare.o
; RUN: test ! -s %t.dir/general-compare.o
;
; Indirect control, EH, and the remaining non-scalar operation families all
; parse and verify as LLVM IR before the target's closed switch rejects them.
; RUN: not --crash %{brace-s3-llc} %t.dir/indirectbr.ll \
; RUN:   -o %t.dir/indirectbr.o
; RUN: test ! -s %t.dir/indirectbr.o
; RUN: not --crash %{brace-s3-llc} %t.dir/invoke.ll -o %t.dir/invoke.o
; RUN: test ! -s %t.dir/invoke.o
; RUN: not --crash %{brace-s3-llc} %t.dir/vector.ll -o %t.dir/vector.o
; RUN: test ! -s %t.dir/vector.o
; RUN: not --crash %{brace-s3-llc} %t.dir/fp.ll -o %t.dir/fp.o
; RUN: test ! -s %t.dir/fp.o
; RUN: not --crash %{brace-s3-llc} %t.dir/aggregate.ll \
; RUN:   -o %t.dir/aggregate.o
; RUN: test ! -s %t.dir/aggregate.o
; RUN: not --crash %{brace-s3-llc} %t.dir/fence.ll -o %t.dir/fence.o
; RUN: test ! -s %t.dir/fence.o
; RUN: not --crash %{brace-s3-llc} %t.dir/freeze.ll -o %t.dir/freeze.o
; RUN: test ! -s %t.dir/freeze.o
; RUN: not --crash %{brace-s3-llc} %t.dir/blockaddress.ll \
; RUN:   -o %t.dir/blockaddress.o
; RUN: test ! -s %t.dir/blockaddress.o

;--- seed.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- alias.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
@entry_alias = alias void (), ptr @brace_system_entry
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- ifunc.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
@entry_ifunc = ifunc void (), ptr @entry_resolver
define internal ptr @entry_resolver() {
entry:
  ret ptr @brace_system_entry
}
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- comdat.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
$entry = comdat any
define dso_local void @brace_system_entry() local_unnamed_addr #0 comdat($entry) {
entry:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- tls.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
@tls_state = thread_local global i32 0
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- debug.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 !dbg !6 {
entry:
  ret void, !dbg !9
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!0, !1, !7, !8}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
!2 = distinct !DICompileUnit(language: DW_LANG_C11, file: !3, producer: "brace-s3b3-directed", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !4)
!3 = !DIFile(filename: "directed.c", directory: "/")
!4 = !{}
!5 = !DISubroutineType(types: !4)
!6 = distinct !DISubprogram(name: "brace_system_entry", scope: !3, file: !3, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !2, retainedNodes: !4)
!7 = !{i32 2, !"Dwarf Version", i32 5}
!8 = !{i32 2, !"Debug Info Version", i32 3}
!9 = !DILocation(line: 1, column: 1, scope: !6)

;--- dynamic-as200.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %seed = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %address = zext i32 %seed to i64
  %pointer = inttoptr i64 %address to ptr addrspace(200)
  %value = load volatile i32, ptr addrspace(200) %pointer, align 4
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- ptrtoint.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %address = ptrtoint ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)) to i64
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- pointer-escape.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  store volatile ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- pointer-compare.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %condition = icmp eq ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), null
  br i1 %condition, label %yes, label %no
yes:
  ret void
no:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- arithmetic.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = add i32 17, 3
  store volatile i32 %value, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- general-compare.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %condition = icmp ult i32 %value, 7
  br i1 %condition, label %yes, label %no
yes:
  ret void
no:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- indirectbr.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  indirectbr ptr inttoptr (i64 1 to ptr), [label %target]
target:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- invoke.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 personality ptr null {
entry:
  invoke void @brace_system_entry() to label %done unwind label %cleanup
done:
  ret void
cleanup:
  %landing = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %landing
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- vector.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = and <2 x i32> <i32 1, i32 2>, <i32 3, i32 4>
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- fp.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = fadd float 1.000000e+00, 2.000000e+00
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- aggregate.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = insertvalue { i32, i32 } zeroinitializer, i32 1, 0
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- fence.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  fence seq_cst
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- freeze.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %value = freeze i32 1
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

;--- blockaddress.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"
define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %condition = icmp eq ptr blockaddress(@brace_system_entry, %addressed), null
  br i1 %condition, label %addressed, label %other
addressed:
  ret void
other:
  ret void
}
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}
