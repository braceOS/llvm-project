; REQUIRES: brace-registered-target
;
; Mutate one direct-call IR contract field at a time.  Every verifier-valid
; carrier reaches the Brace pre-isel boundary, fails closed, and publishes no
; partial object.  In particular the call-site attribute cases guard against
; assertion-prone CallBase attribute queries on untrusted input.
;
; DEFINE: %{brace-s3b5-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
; RUN: rm -rf %t.dir && split-file %s %t.dir
;
; RUN: sed 's/%result = call fastcc/%result = tail call fastcc/' \
; RUN:   %t.dir/seed.ll > %t.dir/tail.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/tail.ll \
; RUN:   -o %t.dir/tail.o 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: test ! -s %t.dir/tail.o
;
; RUN: awk '/%result = call fastcc/ { print; \
; RUN:   print "  %again = call fastcc i32 @brace_system_call_leaf(i32 noundef %result) #2"; \
; RUN:   next } { print }' %t.dir/seed.ll > %t.dir/second-call.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/second-call.ll \
; RUN:   -o %t.dir/second-call.o 2>&1 | FileCheck %s --check-prefix=CALL
; RUN: test ! -s %t.dir/second-call.o
;
; RUN: sed 's/store volatile i32 %result/store volatile i32 0/' \
; RUN:   %t.dir/seed.ll > %t.dir/unused-result.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/unused-result.ll \
; RUN:   -o %t.dir/unused-result.o 2>&1 | FileCheck %s --check-prefix=UNUSED
; RUN: test ! -s %t.dir/unused-result.o
;
; RUN: awk '/%result = call fastcc/ { print; \
; RUN:   print "  %combined = and i32 %result, %input"; next } \
; RUN:   { gsub("store volatile i32 %result", \
; RUN:     "store volatile i32 %combined"); print }' \
; RUN:   %t.dir/seed.ll > %t.dir/live-through.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/live-through.ll \
; RUN:   -o %t.dir/live-through.o 2>&1 | FileCheck %s --check-prefix=LIVE
; RUN: test ! -s %t.dir/live-through.o
;
; RUN: sed 's/brace_system_call_leaf/not_brace_system_call_leaf/g' \
; RUN:   %t.dir/seed.ll > %t.dir/wrong-helper.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/wrong-helper.ll \
; RUN:   -o %t.dir/wrong-helper.o 2>&1 | FileCheck %s --check-prefix=IDENTITY
; RUN: test ! -s %t.dir/wrong-helper.o
;
; RUN: sed 's/define internal fastcc i32/define internal i32/' \
; RUN:   %t.dir/seed.ll > %t.dir/wrong-helper-cc.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/wrong-helper-cc.ll \
; RUN:   -o %t.dir/wrong-helper-cc.o 2>&1 | FileCheck %s --check-prefix=HELPER-CC
; RUN: test ! -s %t.dir/wrong-helper-cc.o
;
; RUN: sed 's/attributes #0 = {/attributes #0 = { mustprogress/' \
; RUN:   %t.dir/seed.ll > %t.dir/entry-attribute.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/entry-attribute.ll \
; RUN:   -o %t.dir/entry-attribute.o 2>&1 | FileCheck %s --check-prefix=FUNCTION-ATTR
; RUN: test ! -s %t.dir/entry-attribute.o
;
; RUN: sed 's/attributes #2 = { nobuiltin/attributes #2 = { nounwind nobuiltin/' \
; RUN:   %t.dir/seed.ll > %t.dir/callsite-extra.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/callsite-extra.ll \
; RUN:   -o %t.dir/callsite-extra.o 2>&1 | FileCheck %s --check-prefix=CALL-ATTR
; RUN: test ! -s %t.dir/callsite-extra.o
;
; RUN: sed 's/{ nobuiltin "no-builtins" }/{ "no-builtins" }/' \
; RUN:   %t.dir/seed.ll > %t.dir/callsite-missing-nobuiltin.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/callsite-missing-nobuiltin.ll \
; RUN:   -o %t.dir/callsite-missing-nobuiltin.o 2>&1 | FileCheck %s --check-prefix=CALL-ATTR
; RUN: test ! -s %t.dir/callsite-missing-nobuiltin.o
;
; RUN: sed 's/range(i32 0, 16)/range(i32 0, 32)/' \
; RUN:   %t.dir/pure.ll > %t.dir/range-payload.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/range-payload.ll \
; RUN:   -o %t.dir/range-payload.o 2>&1 | FileCheck %s --check-prefix=RETURN-ATTR
; RUN: test ! -s %t.dir/range-payload.o
; RUN: sed 's/i32 noundef %value/i32 noundef range(i32 0, 32) %value/' \
; RUN:   %t.dir/pure.ll > %t.dir/parameter-range.ll
; RUN: not --crash %{brace-s3b5-llc} %t.dir/parameter-range.ll \
; RUN:   -o %t.dir/parameter-range.o 2>&1 | FileCheck %s --check-prefix=PARAMETER-ATTR
; RUN: test ! -s %t.dir/parameter-range.o
;
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -filetype=obj \
; RUN:   %t.dir/seed.ll -o %t.dir/old-selector.o
; RUN: test ! -s %t.dir/old-selector.o
; RUN: not llc -mtriple=brace64-unknown-none-elf -O1 -filetype=obj \
; RUN:   %t.dir/seed.ll -o %t.dir/no-selector.o
; RUN: test ! -s %t.dir/no-selector.o
; RUN: not %{brace-s3b5-llc} -code-model=small %t.dir/seed.ll \
; RUN:   -o %t.dir/code-model-small.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=CODE-MODEL
; RUN: test ! -s %t.dir/code-model-small.o
;
; The table-driven structural matrix below uses independently materialized IR
; files and invokes llc once per case.  Each mutation must be observed, fail
; nonzero, and leave no nonempty object.  Together with the fourteen focused
; cases above this freezes 56 IR/invocation negative identities.
; STRUCTURAL-CASE-COUNT: 56
; RUN: %python %t.dir/ir-contract-matrix.py %t.dir/seed.ll %t.dir | \
; RUN:   FileCheck %s --check-prefix=MATRIX
;
; CALL: LLVM ERROR: brace64 S3b.5 direct-call ABI: requires one non-tail private i32(i32) direct call
; UNUSED: LLVM ERROR: brace64 S3b.5 direct-call ABI: entry must consume the direct-call result
; LIVE: LLVM ERROR: brace64 S3b.5 direct-call ABI: caller SSA value remains live across the call
; IDENTITY: LLVM ERROR: brace64 S3b.5 direct-call ABI: required entry or helper identity is missing
; HELPER-CC: LLVM ERROR: brace64 S3b.5 direct-call ABI: requires one private fastcc i32 brace_system_call_leaf(i32)
; FUNCTION-ATTR: LLVM ERROR: brace64 S3b.5 direct-call ABI: required function attributes are missing
; CALL-ATTR: LLVM ERROR: brace64 S3b.5 direct-call ABI: call-site attributes are outside the direct-call profile
; RETURN-ATTR: LLVM ERROR: brace64 S3b.5 direct-call ABI: return attributes are outside the direct-call profile
; PARAMETER-ATTR: LLVM ERROR: brace64 S3b.5 direct-call ABI: parameter attributes are outside the direct-call profile
; CODE-MODEL: llc: error: target does not support generation of this file type
; MATRIX: s3b5-ir-contract-matrix: 42/42 rejected with zero object bytes
;
;--- seed.ll
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

;--- ir-contract-matrix.py
import pathlib
import re
import shutil
import subprocess
import sys

seed_path = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2]) / "ir-contract-matrix"
out.mkdir(parents=True, exist_ok=True)
llc = shutil.which("llc")
if not llc:
    raise SystemExit("llc is not on the lit PATH")
seed = seed_path.read_text()

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise AssertionError("mutation source is not unique: " + old[:72])
    return text.replace(old, new, 1)

def replace_helper(text, signature, body):
    start = text.index("define internal fastcc i32 @brace_system_call_leaf")
    end = text.index("\n}\n\nattributes #0", start) + 3
    return text[:start] + signature + " {\n" + body + "\n}\n" + text[end:]

def declaration_helper(text):
    start = text.index("define internal fastcc i32 @brace_system_call_leaf")
    end = text.index("\n}\n\nattributes #0", start) + 3
    declaration = (
        "declare fastcc i32 @brace_system_call_leaf(i32 noundef) #1\n")
    return text[:start] + declaration + text[end:]

def entry_call(text, replacement):
    old = ("  %result = call fastcc i32 @brace_system_call_leaf"
           "(i32 noundef %input) #2")
    return replace_once(text, old, replacement)

cases = []
def case(name, text, diagnostic="brace64 S3b.5 direct-call ABI:"):
    if text == seed:
        raise AssertionError("no-op mutation: " + name)
    cases.append((name, text, diagnostic))

case("extra-function", seed + "\ndeclare void @brace_extra()\n",
     "exactly two functions are required")
case("helper-declaration", declaration_helper(seed),
     "function declaration or signature envelope mismatch")
case("wrong-entry-name",
     replace_once(seed, "@brace_system_entry", "@not_brace_system_entry"),
     "required entry or helper identity is missing")
case("entry-internal-linkage",
     replace_once(seed, "define dso_local void @brace_system_entry",
                  "define internal void @brace_system_entry"),
     "requires one external C void brace_system_entry(void)")
case("entry-parameter",
     replace_once(seed, "@brace_system_entry()", "@brace_system_entry(i32 %x)"),
     "requires one external C void brace_system_entry(void)")
case("helper-external-linkage",
     replace_once(seed, "define internal fastcc i32 @brace_system_call_leaf",
                  "define dso_local fastcc i32 @brace_system_call_leaf"),
     "requires one private fastcc i32 brace_system_call_leaf(i32)")
case("helper-vararg",
     replace_once(seed, "(i32 noundef %value) unnamed_addr #1",
                  "(i32 noundef %value, ...) unnamed_addr #1"),
     "error:")

two_args = replace_once(
    seed, "(i32 noundef %value) unnamed_addr #1",
    "(i32 noundef %value, i32 noundef %other) unnamed_addr #1")
two_args = entry_call(
    two_args, "  %result = call fastcc i32 @brace_system_call_leaf"
              "(i32 noundef %input, i32 noundef 0) #2")
case("helper-two-arguments", two_args,
     "requires one private fastcc i32 brace_system_call_leaf(i32)")

i8_arg = replace_helper(
    seed,
    "define internal fastcc i32 @brace_system_call_leaf"
    "(i8 noundef %value) unnamed_addr #1",
    "entry:\n  ret i32 0")
i8_arg = entry_call(
    i8_arg, "  %result = call fastcc i32 @brace_system_call_leaf"
            "(i8 noundef 7) #2")
case("helper-i8-argument", i8_arg,
     "requires one private fastcc i32 brace_system_call_leaf(i32)")

i8_result = replace_helper(
    seed,
    "define internal fastcc i8 @brace_system_call_leaf"
    "(i32 noundef %value) unnamed_addr #1",
    "entry:\n  ret i8 0")
i8_result = entry_call(
    i8_result, "  %result = call fastcc i8 @brace_system_call_leaf"
               "(i32 noundef %input) #2")
i8_result = replace_once(i8_result, "store volatile i32 %result",
                         "store volatile i8 %result")
case("helper-i8-result", i8_result,
     "requires one private fastcc i32 brace_system_call_leaf(i32)")

void_result = replace_helper(
    seed,
    "define internal fastcc void @brace_system_call_leaf"
    "(i32 noundef %value) unnamed_addr #1",
    "entry:\n  ret void")
void_result = entry_call(
    void_result, "  call fastcc void @brace_system_call_leaf"
                 "(i32 noundef %input) #2")
void_result = replace_once(void_result, "store volatile i32 %result",
                           "store volatile i32 0")
case("helper-void-result", void_result,
     "requires one private fastcc i32 brace_system_call_leaf(i32)")

struct_result = replace_helper(
    seed,
    "define internal fastcc { i32, i32 } @brace_system_call_leaf"
    "(i32 noundef %value) unnamed_addr #1",
    "entry:\n  ret { i32, i32 } { i32 0, i32 0 }")
struct_result = entry_call(
    struct_result,
    "  %result = call fastcc { i32, i32 } @brace_system_call_leaf"
    "(i32 noundef %input) #2")
struct_result = replace_once(struct_result, "store volatile i32 %result",
                             "store volatile i32 0")
case("helper-aggregate-result", struct_result,
     "requires one private fastcc i32 brace_system_call_leaf(i32)")

recursive = replace_once(
    seed, "  ret i32 %masked",
    "  %again = call fastcc i32 @brace_system_call_leaf"
    "(i32 noundef %masked) #2\n  ret i32 %again")
case("recursive-helper-call", recursive,
     "helper address escapes the single direct call")
case("indirect-call",
     entry_call(seed,
                "  %result = call fastcc i32 inttoptr (i64 1 to ptr)"
                "(i32 noundef %input) #2"),
     "requires one non-tail private i32(i32) direct call")

invoke = entry_call(
    seed,
    "  %result = invoke fastcc i32 @brace_system_call_leaf"
    "(i32 noundef %input) #2 to label %after unwind label %unwind")
invoke = replace_once(invoke, "  store volatile i32 %result", "after:\n  store volatile i32 %result")
invoke = replace_once(invoke, "  ret void\n}\n\ndefine internal",
                      "  ret void\nunwind:\n  unreachable\n}\n\ndefine internal")
case("invoke-call", invoke, "error:")

callbr = entry_call(
    seed,
    "  %result = callbr fastcc i32 @brace_system_call_leaf"
    "(i32 noundef %input) #2 to label %after [label %indirect]")
callbr = replace_once(callbr, "  store volatile i32 %result", "after:\n  store volatile i32 %result")
callbr = replace_once(callbr, "  ret void\n}\n\ndefine internal",
                      "  ret void\nindirect:\n  ret void\n}\n\ndefine internal")
case("callbr-call", callbr, "error:")

case("global", seed + "\n@bad_global = global i32 0\n",
     "globals, aliases, ifuncs, and module asm are not admitted")
case("alias", seed +
     "\n@bad_alias = alias void (), ptr @brace_system_entry\n",
     "globals, aliases, ifuncs, and module asm are not admitted")
case("ifunc", seed +
     "\n@bad_ifunc = ifunc i32 (i32), ptr @brace_system_call_leaf\n",
     "error:")
case("thread-local-global", seed +
     "\n@bad_tls = thread_local global i32 0\n",
     "globals, aliases, ifuncs, and module asm are not admitted")
case("module-asm", replace_once(seed, "target triple = \"brace64-unknown-none-elf\"",
                                "target triple = \"brace64-unknown-none-elf\"\nmodule asm \"nop\""),
     "globals, aliases, ifuncs, and module asm are not admitted")

identified = replace_once(seed, "target triple = \"brace64-unknown-none-elf\"",
                          "target triple = \"brace64-unknown-none-elf\"\n%bad_type = type { i32 }")
identified = replace_once(identified, "entry:\n  %input =",
                          "entry:\n  %bad_slot = alloca %bad_type\n  %input =")
case("identified-type", identified,
     "identified types and COMDAT are not admitted")
case("comdat", replace_once(
         replace_once(seed, "target triple = \"brace64-unknown-none-elf\"",
                      "target triple = \"brace64-unknown-none-elf\"\n$bad = comdat any"),
         "local_unnamed_addr #0 {", "local_unnamed_addr #0 comdat($bad) {"),
     "identified types and COMDAT are not admitted")

for name, decoration in [
        ("personality", "personality ptr @brace_system_entry"),
        ("prefix", "prefix i32 0"),
        ("prologue", "prologue i32 0"),
        ("gc", "gc \"statepoint-example\""),
        ("section", "section \".brace.bad\"")]:
    case(name, replace_once(seed, "local_unnamed_addr #0 {",
                            "local_unnamed_addr #0 " + decoration + " {"),
         "function decoration is outside the direct-call profile")

function_md = replace_once(seed, "local_unnamed_addr #0 {",
                           "local_unnamed_addr #0 !brace.bad !2 {")
function_md += "\n!2 = !{i32 1}\n"
case("function-metadata", function_md,
     "function decoration is outside the direct-call profile")
instruction_md = replace_once(seed, "align 2147483648",
                              "align 2147483648, !brace.bad !2")
instruction_md += "\n!2 = !{i32 1}\n"
case("instruction-metadata", instruction_md,
     "instruction metadata is outside the finite direct-call envelope")
case("unwind-function-attribute",
     replace_once(seed, "attributes #0 = {", "attributes #0 = { uwtable"),
     "unsupported enum or integer function attribute")

case("stack-alloca", replace_once(seed, "entry:\n  %input =",
                                  "entry:\n  %stack = alloca i32, align 4\n  %input ="),
     "instruction is outside the S3b.5 direct-call profile")
atomic = replace_once(
    seed,
    "load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648",
    "load atomic volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)) monotonic, align 2147483648")
case("atomic-load", atomic,
     "loads must be volatile direct addrspace(200) i8/i32 accesses")
case("nonvolatile-load", replace_once(
         seed,
         "load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648",
         "load i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648"),
     "loads must be volatile direct addrspace(200) i8/i32 accesses")
wrong_as = replace_once(
    seed,
    "ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200))",
    "ptr inttoptr (i64 2147483648 to ptr)")
case("wrong-address-space", wrong_as,
     "physical pointer must be a direct i64 addrspace(200) inttoptr")
case("misaligned-address", replace_once(seed, "i64 2147483648", "i64 2147483649"),
     "physical memory access is not naturally aligned")
i16 = replace_once(seed, "entry:\n  %input =",
                   "entry:\n  %bad_width = load volatile i16, ptr addrspace(200) inttoptr (i64 2147483664 to ptr addrspace(200)), align 2\n  %input =")
case("i16-load", i16, "physical memory width must be i8 or i32")
case("integer-add", replace_once(seed, "  %masked = and i32 %mask, %value",
                                 "  %masked = add i32 %mask, %value"),
     "only i8/i32 integer-and is admitted")
extra_md = seed + "\n!brace.extra = !{!2}\n!2 = !{i32 1}\n"
case("extra-named-metadata", extra_md,
     "noncanonical named metadata or module flags")
case("wrong-module-flag", replace_once(seed, "!\"wchar_size\", i32 4",
                                       "!\"wchar_size\", i32 8"),
     "noncanonical named metadata or module flags")
case("wrong-triple", replace_once(seed, "brace64-unknown-none-elf",
                                  "brace64-unknown-linux-elf"),
     "brace64 S3b.5 input triple or data layout mismatch")
case("wrong-data-layout", replace_once(seed, "-S128", "-S64"),
     "brace64 S3b.5 input triple or data layout mismatch")

base_args = [
    llc,
    "-mtriple=brace64-unknown-none-elf",
    "-target-abi=brace-system-s2-direct-call-r0",
    "-O1",
    "-verify-machineinstrs",
    "-filetype=obj",
]
for index, (name, contents, diagnostic) in enumerate(cases):
    source = out / ("%02d-%s.ll" % (index, name))
    obj = out / ("%02d-%s.o" % (index, name))
    source.write_text(contents)
    obj.unlink(missing_ok=True)
    completed = subprocess.run(base_args + [str(source), "-o", str(obj)],
                               stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT,
                               text=True,
                               timeout=30)
    if completed.returncode == 0:
        raise AssertionError(name + ": unexpectedly accepted")
    if obj.exists() and obj.stat().st_size != 0:
        raise AssertionError(name + ": published object bytes")
    if diagnostic not in completed.stdout:
        raise AssertionError(name + ": missing diagnostic %r:\n%s" %
                             (diagnostic, completed.stdout))

print("s3b5-ir-contract-matrix: %d/%d rejected with zero object bytes" %
      (len(cases), len(cases)))

;--- pure.ll
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
