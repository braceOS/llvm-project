; REQUIRES: brace-registered-target
;
; Exercise the S3b.5 input-local limits and the reachable maxima derived from
; their conjunction.  The generator emits real verifier-valid IR; it does not
; pass count tokens to the target.  Cases that are exact at the pre-isel IR
; boundary but cannot fit the narrower physical operation envelope must fail
; later at the independently checked post-RA boundary.
;
; DEFINE: %{brace-s3b5-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
; RUN: rm -rf %t.dir && split-file %s %t.dir
; RUN: %python %t.dir/make-ir.py %t.dir
;
; The joint maximum has 4+4 blocks, 64+64 tracked values, 32+32
; physical stores, and 100+99=199 IR instructions.  Dead integer nodes are
; still checked input values but disappear only after the pre-isel verifier.
; RUN: %{brace-s3b5-llc} %t.dir/joint-max.ll -o %t.dir/joint-max.o
; RUN: %{brace-s3b5-llc} %t.dir/joint-max.ll -o %t.dir/joint-max-again.o
; RUN: cmp %t.dir/joint-max.o %t.dir/joint-max-again.o
; RUN: llvm-readobj --file-headers --sections %t.dir/joint-max.o | \
; RUN:   FileCheck %s --check-prefix=JOINT-MAX
;
; Six edges per function and twelve per module are the reachable maxima for
; four blocks with at least one Return.  This irreducible but phi-free CFG is
; a real accepted object, not a count-only probe.
; RUN: %{brace-s3b5-llc} %t.dir/edge-max.ll -o %t.dir/edge-max.o
; RUN: llvm-readobj --file-headers --sections %t.dir/edge-max.o | \
; RUN:   FileCheck %s --check-prefix=EDGE-MAX
;
; One extra block is simultaneously the only way to exceed the derived edge
; maximum.  The frozen first error is therefore the input-local block cap.
; RUN: not --crash %{brace-s3b5-llc} %t.dir/block-plus.ll \
; RUN:   -o %t.dir/block-plus.o 2>&1 | FileCheck %s --check-prefix=BLOCK-PLUS
; RUN: test ! -s %t.dir/block-plus.o
;
; One extra per-function tracked value also makes the derived module total 129.
; The per-function 65-value error is deliberately first and no partial object
; is published.
; RUN: not --crash %{brace-s3b5-llc} %t.dir/function-value-plus.ll \
; RUN:   -o %t.dir/function-value-plus.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=FUNCTION-VALUE-PLUS
; RUN: test ! -s %t.dir/function-value-plus.o
;
; 65 module physical operations and 200 module IR instructions are the same
; combined one-more fixture for the jointly reachable 64/199 maxima.
; RUN: not --crash %{brace-s3b5-llc} %t.dir/module-plus.ll \
; RUN:   -o %t.dir/module-plus.o 2>&1 | FileCheck %s --check-prefix=MODULE-PLUS
; RUN: test ! -s %t.dir/module-plus.o
;
; A single function with exactly 128 IR instructions remains publishable.
; RUN: %{brace-s3b5-llc} %t.dir/function-instruction-max.ll \
; RUN:   -o %t.dir/function-instruction-max.o
; RUN: llvm-readobj --file-headers %t.dir/function-instruction-max.o | \
; RUN:   FileCheck %s --check-prefix=FUNCTION-INSTRUCTION-MAX
; RUN: not --crash %{brace-s3b5-llc} \
; RUN:   %t.dir/function-instruction-plus.ll \
; RUN:   -o %t.dir/function-instruction-plus.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=FUNCTION-INSTRUCTION-PLUS
; RUN: test ! -s %t.dir/function-instruction-plus.o
;
; 64 physical stores in one function are admitted by the pre-isel boundary,
; then rejected by the separately smaller 128-operation publication limit
; because every store needs PADDR+STORE plus a valued Return.  Store 65 fails
; at the input-local memory boundary instead.
; RUN: not --crash %{brace-s3b5-llc} %t.dir/function-memory-max.ll \
; RUN:   -o %t.dir/function-memory-max.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=FUNCTION-MEMORY-MAX
; RUN: test ! -s %t.dir/function-memory-max.o
; RUN: not --crash %{brace-s3b5-llc} %t.dir/function-memory-plus.ll \
; RUN:   -o %t.dir/function-memory-plus.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=FUNCTION-MEMORY-PLUS
; RUN: test ! -s %t.dir/function-memory-plus.o
;
; Construct final MIR from a registered positive object.  Repeated physical
; constants are genuine target operations and make both functions exactly
; 128 operations (256 module operations) without adding another descriptor.
; RUN: %{brace-s3b5-llc} -stop-after=brace-finalize-branches \
; RUN:   %S/s3b5-direct-call.ll -o %t.dir/base-final.mir
; RUN: %python %t.dir/make-mir.py %t.dir/base-final.mir \
; RUN:   %t.dir/operation-max.mir 122 123 keep
; RUN: %{brace-s3b5-llc} -start-after=brace-finalize-branches \
; RUN:   %t.dir/operation-max.mir -o %t.dir/operation-max.o
; RUN: llvm-readobj --sections %t.dir/operation-max.o | \
; RUN:   FileCheck %s --check-prefix=OPERATION-MAX
; RUN: %python %t.dir/make-mir.py %t.dir/base-final.mir \
; RUN:   %t.dir/operation-plus.mir 123 123 keep
; RUN: not --crash %{brace-s3b5-llc} \
; RUN:   -start-after=brace-finalize-branches %t.dir/operation-plus.mir \
; RUN:   -o %t.dir/operation-plus.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=OPERATION-PLUS
; RUN: test ! -s %t.dir/operation-plus.o
;
; Six register types and one canonical descriptor are fixed shapes rather
; than ranges.  The positive objects above prove the exact writer shape; these
; final-MIR mutations prove missing/duplicate calls are stopped before MC and
; an undeclared seventh register fails with zero object bytes.
; RUN: %python %t.dir/make-mir.py %t.dir/base-final.mir \
; RUN:   %t.dir/missing-call.mir 0 0 remove-call
; RUN: not --crash %{brace-s3b5-llc} \
; RUN:   -start-after=brace-finalize-branches %t.dir/missing-call.mir \
; RUN:   -o %t.dir/missing-call.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=MISSING-CALL
; RUN: test ! -s %t.dir/missing-call.o
; RUN: %python %t.dir/make-mir.py %t.dir/base-final.mir \
; RUN:   %t.dir/second-call.mir 0 0 duplicate-call
; RUN: not --crash %{brace-s3b5-llc} \
; RUN:   -start-after=brace-finalize-branches %t.dir/second-call.mir \
; RUN:   -o %t.dir/second-call.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=SECOND-CALL
; RUN: test ! -s %t.dir/second-call.o
; RUN: sed 's/\$r5 = LOAD32/\$r6 = LOAD32/' %t.dir/base-final.mir \
; RUN:   > %t.dir/register-seven.mir
; RUN: not %{brace-s3b5-llc} -start-after=brace-finalize-branches \
; RUN:   %t.dir/register-seven.mir -o %t.dir/register-seven.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=REGISTER-SEVEN
; RUN: test ! -s %t.dir/register-seven.o
;
; JOINT-MAX: Flags [ (0x42520200)
; JOINT-MAX: SectionHeaderCount: 11
; EDGE-MAX: Flags [ (0x42520200)
; EDGE-MAX: SectionHeaderCount: 11
; BLOCK-PLUS: LLVM ERROR: brace64 S3b.5 direct-call ABI: basic-block count is outside 1..4
; FUNCTION-VALUE-PLUS: LLVM ERROR: brace64 S3b.5 direct-call ABI: per-function tracked non-void value count exceeds 64
; MODULE-PLUS: LLVM ERROR: brace64 S3b.5 direct-call ABI: module resource limit exceeded
; FUNCTION-INSTRUCTION-MAX: Flags [ (0x42520200)
; FUNCTION-INSTRUCTION-PLUS: LLVM ERROR: brace64 S3b.5 direct-call ABI: IR instruction count exceeds 128
; FUNCTION-MEMORY-MAX: LLVM ERROR: brace64 S3b.5 post-RA verifier: published operation count exceeds 128
; FUNCTION-MEMORY-PLUS: LLVM ERROR: brace64 S3b.5 direct-call ABI: physical memory operation count exceeds 64
; OPERATION-MAX: Name: .brace.text
; OPERATION-MAX: Size: 1024
; OPERATION-PLUS: LLVM ERROR: brace64 S3b.5 post-RA verifier: published operation count exceeds 128
; MISSING-CALL: LLVM ERROR: brace64 S3b.5 post-RA verifier: function Call/Return profile is not exact
; SECOND-CALL: LLVM ERROR: brace64 S3b.5 post-RA verifier: function Call/Return profile is not exact
; REGISTER-SEVEN: unknown register name 'r6'
;
;--- make-ir.py
import pathlib
import sys

out = pathlib.Path(sys.argv[1])

header = '''target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

'''

footer = '''
attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { nofree noinline norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { mustprogress nofree noinline norecurse nounwind willreturn memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-r0"}
'''

def dead(prefix, count):
    return ["  %%%s%d = and i32 0, %d" % (prefix, i, (i % 15) + 1)
            for i in range(count)]

def stores(count, value, address):
    result = []
    for i in range(count):
        stored = value if i == 0 and value else str((i * 17) & 0xffffffff)
        result.append(
            "  store volatile i32 %s, ptr addrspace(200) inttoptr "
            "(i64 %d to ptr addrspace(200)), align 4" % (stored, address))
    return result

def chain_module(root_dead, helper_dead, root_stores, helper_stores):
    root = ["define dso_local void @brace_system_entry() local_unnamed_addr #0 {", "entry:"]
    root += dead("rdead", root_dead)
    root += ["  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef 7) #2",
             "  br label %root.one", "", "root.one:"]
    root += stores(root_stores // 2, "%result", 2147483680)
    root += ["  br label %root.two", "", "root.two:"]
    root += stores(root_stores - root_stores // 2, None, 2147483680)
    root += ["  br label %root.return", "", "root.return:", "  ret void", "}", ""]

    helper = ["define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #1 {",
              "entry:"]
    helper += dead("hdead", helper_dead)
    helper += ["  br label %helper.one", "", "helper.one:"]
    helper += stores(helper_stores // 2, None, 2147483684)
    helper += ["  br label %helper.two", "", "helper.two:"]
    helper += stores(helper_stores - helper_stores // 2, None, 2147483684)
    helper += ["  br label %helper.return", "", "helper.return:",
               "  %returned = and i32 %value, 15", "  ret i32 %returned", "}", ""]
    return header + "\n".join(root + helper) + footer

def edge_module():
    def body(name, source, is_root):
        call = (["  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef 7) #2"]
                if is_root else [])
        value = "%result" if is_root else "%value"
        result = (["define dso_local void @brace_system_entry() local_unnamed_addr #0 {"] if is_root else
                  ["define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #1 {"])
        result += ["entry:"] + call + [
            "  %%%s0 = icmp eq i32 %s, 0" % (name, value),
            "  br i1 %%%s0, label %%%s.one, label %%%s.two" % (name, name, name),
            "", "%s.one:" % name,
            "  %%%s1 = icmp eq i32 %s, 0" % (name, value),
            "  br i1 %%%s1, label %%%s.two, label %%%s.return" % (name, name, name),
            "", "%s.two:" % name,
            "  %%%s2 = icmp ne i32 %s, 0" % (name, value),
            "  br i1 %%%s2, label %%%s.one, label %%%s.return" % (name, name, name),
            "", "%s.return:" % name]
        if is_root:
            result += stores(1, "%result", 2147483680) + ["  ret void", "}", ""]
        else:
            result += stores(1, None, 2147483684) + ["  ret i32 %value", "}", ""]
        return result
    return header + "\n".join(body("root", None, True) +
                                body("helper", None, False)) + footer

def block_plus():
    root = '''define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef 7) #2
  br label %one
one:
  br label %two
two:
  br label %three
three:
  br label %four
four:
  store volatile i32 %result, ptr addrspace(200) inttoptr (i64 2147483680 to ptr addrspace(200)), align 4
  ret void
}

'''
    helper = '''define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #3 {
entry:
  store volatile i32 0, ptr addrspace(200) inttoptr (i64 2147483684 to ptr addrspace(200)), align 4
  ret i32 %value
}
'''
    return header + root + helper + footer

def function_instruction(stores_count):
    root = '''define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef 7) #2
  store volatile i32 %result, ptr addrspace(200) inttoptr (i64 2147483680 to ptr addrspace(200)), align 4
  ret void
}

'''
    helper = ["define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #1 {",
              "entry:"] + dead("idead", 62) + ["  br label %one", "", "one:"]
    helper += stores(stores_count // 2, None, 2147483684)
    helper += ["  br label %two", "", "two:"]
    helper += stores(stores_count - stores_count // 2, None, 2147483684)
    helper += ["  br label %return", "", "return:",
               "  %returned = and i32 %value, 15", "  ret i32 %returned", "}", ""]
    return header + root + "\n".join(helper) + footer

def function_memory(count):
    root = '''define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef 7) #2
  %used = and i32 %result, 1
  ret void
}

'''
    helper = ["define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #3 {",
              "entry:"]
    helper += stores(count, None, 2147483684)
    helper += ["  ret i32 %value", "}", ""]
    return header + root + "\n".join(helper) + footer

cases = {
    "joint-max.ll": chain_module(63, 62, 32, 32),
    "edge-max.ll": edge_module(),
    "block-plus.ll": block_plus(),
    "function-value-plus.ll": chain_module(64, 62, 32, 32),
    "module-plus.ll": chain_module(63, 62, 33, 32),
    "function-instruction-max.ll": function_instruction(61),
    "function-instruction-plus.ll": function_instruction(62),
    "function-memory-max.ll": function_memory(64),
    "function-memory-plus.ll": function_memory(65),
}
for name, contents in cases.items():
    (out / name).write_text(contents)

;--- make-mir.py
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
output = pathlib.Path(sys.argv[2])
root_extra = int(sys.argv[3])
helper_extra = int(sys.argv[4])
mode = sys.argv[5]

if source.count("CALL_I32 @brace_system_call_leaf") != 1:
    raise SystemExit("unexpected base direct-call count")

if mode == "remove-call":
    source = "\n".join(line for line in source.splitlines()
                       if "CALL_I32 @brace_system_call_leaf" not in line) + "\n"
elif mode == "duplicate-call":
    needle = next(line for line in source.splitlines()
                  if "CALL_I32 @brace_system_call_leaf" in line)
    source = source.replace(needle, needle + "\n" + needle, 1)
elif mode != "keep":
    raise SystemExit("unknown MIR mutation mode")

current = None
result = []
for line in source.splitlines():
    if line == "name:            brace_system_entry":
        current = "root"
    elif line == "name:            brace_system_call_leaf":
        current = "helper"
    if line == "    RET" and current == "root":
        result.extend("    $r5 = CONST32 0" for _ in range(root_extra))
    if line == "    RET_I32 $r4" and current == "helper":
        result.extend("    $r5 = CONST32 0" for _ in range(helper_extra))
    result.append(line)
output.write_text("\n".join(result) + "\n")
