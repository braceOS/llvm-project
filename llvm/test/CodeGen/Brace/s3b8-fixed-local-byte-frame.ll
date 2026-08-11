; REQUIRES: brace-registered-target
;
; This is the independent normalized-IR producer for S3b.8 FL1.  It freezes
; the exact IR/local/lifetime envelope, all five restart seams, FL0 isolation,
; and IR negatives that cannot be represented honestly after SelectionDAG.
;
; DEFINE: %{brace-s3b8-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 \
; DEFINE:   -O1 -verify-machineinstrs -filetype=obj
; DEFINE: %{brace-s3b7c-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-direct-call-byte-frame-r0 \
; DEFINE:   -O1 -verify-machineinstrs -filetype=obj
;
; RUN: rm -rf %t.dir && split-file %s %t.dir
; RUN: llvm-as %t.dir/fl1.ll -o %t.dir/fl1.bc
; RUN: llvm-as %t.dir/fl1.ll -o %t.dir/fl1.again.bc
; RUN: cmp %t.dir/fl1.bc %t.dir/fl1.again.bc
; RUN: llvm-dis -o %t.dir/fl1.dis.ll < %t.dir/fl1.bc
; RUN: FileCheck %s --check-prefix=IR < %t.dir/fl1.dis.ll
; RUN: wc -c < %t.dir/fl1.bc | FileCheck %s --check-prefix=CANONICAL-BC-SIZE
;
; LLVM's intrinsic registry canonicalizes five distinct raw declaration
; spellings to the same six-attribute bitcode identity.  These are positive
; assembler/reader KATs, not target-verifier negatives.
; RUN: %python %t.dir/mutate.py declaration-no-mustprogress %t.dir/fl1.ll %t.dir/declaration-no-mustprogress.ll
; RUN: llvm-as %t.dir/declaration-no-mustprogress.ll -o %t.dir/declaration-no-mustprogress.bc
; RUN: llvm-dis -o %t.dir/declaration-no-mustprogress.dis.ll < %t.dir/declaration-no-mustprogress.bc
; RUN: cmp %t.dir/fl1.bc %t.dir/declaration-no-mustprogress.bc
; RUN: cmp %t.dir/fl1.dis.ll %t.dir/declaration-no-mustprogress.dis.ll
; RUN: %python %t.dir/mutate.py declaration-cold %t.dir/fl1.ll %t.dir/declaration-cold.ll
; RUN: llvm-as %t.dir/declaration-cold.ll -o %t.dir/declaration-cold.bc
; RUN: llvm-dis -o %t.dir/declaration-cold.dis.ll < %t.dir/declaration-cold.bc
; RUN: cmp %t.dir/fl1.bc %t.dir/declaration-cold.bc
; RUN: cmp %t.dir/fl1.dis.ll %t.dir/declaration-cold.dis.ll
; RUN: %python %t.dir/mutate.py declaration-string %t.dir/fl1.ll %t.dir/declaration-string.ll
; RUN: llvm-as %t.dir/declaration-string.ll -o %t.dir/declaration-string.bc
; RUN: llvm-dis -o %t.dir/declaration-string.dis.ll < %t.dir/declaration-string.bc
; RUN: cmp %t.dir/fl1.bc %t.dir/declaration-string.bc
; RUN: cmp %t.dir/fl1.dis.ll %t.dir/declaration-string.dis.ll
; RUN: %python %t.dir/mutate.py declaration-no-nocallback %t.dir/fl1.ll %t.dir/declaration-no-nocallback.ll
; RUN: llvm-as %t.dir/declaration-no-nocallback.ll -o %t.dir/declaration-no-nocallback.bc
; RUN: llvm-dis -o %t.dir/declaration-no-nocallback.dis.ll < %t.dir/declaration-no-nocallback.bc
; RUN: cmp %t.dir/fl1.bc %t.dir/declaration-no-nocallback.bc
; RUN: cmp %t.dir/fl1.dis.ll %t.dir/declaration-no-nocallback.dis.ll
; RUN: %python %t.dir/mutate.py declaration-memory-none %t.dir/fl1.ll %t.dir/declaration-memory-none.ll
; RUN: llvm-as %t.dir/declaration-memory-none.ll -o %t.dir/declaration-memory-none.bc
; RUN: llvm-dis -o %t.dir/declaration-memory-none.dis.ll < %t.dir/declaration-memory-none.bc
; RUN: cmp %t.dir/fl1.bc %t.dir/declaration-memory-none.bc
; RUN: cmp %t.dir/fl1.dis.ll %t.dir/declaration-memory-none.dis.ll
; RUN: %{brace-s3b8-llc} %t.dir/fl1.bc -o %t.dir/fl1.o
; RUN: %{brace-s3b8-llc} %t.dir/fl1.bc -o %t.dir/fl1.again.o
; RUN: cmp %t.dir/fl1.o %t.dir/fl1.again.o
; RUN: wc -c < %t.dir/fl1.o | FileCheck %s --check-prefix=FL1-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.dir/fl1.o | FileCheck %s --check-prefix=FL1-SHA
; RUN: llvm-readobj --file-headers --sections %t.dir/fl1.o | \
; RUN:   FileCheck %s --check-prefix=FL1-OBJECT
; RUN: %python -c "import struct,sys; d=open(sys.argv[1],'rb').read(); assert struct.unpack_from('<I',d,108)[0]==16 and struct.unpack_from('<I',d,172)[0]==0; assert list(struct.unpack_from('<16I',d,0x120))==[0x267,0x25b,0x925f,0x8266f,0x243,0x8966b,0x4299613,0x825b,0x50663,0x273,0x253,0x1025b,0x965f,0x4299613,0x5299213,0x9253]" %t.dir/fl1.o
;
; The normalized producer independently crosses all five registered seams.
; RUN: %{brace-s3b8-llc} -stop-after=finalize-isel %t.dir/fl1.bc \
; RUN:   -o %t.dir/isel.mir
; RUN: FileCheck %s --check-prefix=PRE-ISEL < %t.dir/isel.mir
; RUN: %{brace-s3b8-llc} -start-after=finalize-isel %t.dir/isel.mir \
; RUN:   -o %t.dir/from-isel.o
; RUN: cmp %t.dir/fl1.o %t.dir/from-isel.o
; RUN: %{brace-s3b8-llc} -stop-after=virtregrewriter %t.dir/fl1.bc \
; RUN:   -o %t.dir/ra.mir
; RUN: FileCheck %s --check-prefix=PRE-RA < %t.dir/ra.mir
; RUN: %{brace-s3b8-llc} -start-after=virtregrewriter %t.dir/ra.mir \
; RUN:   -o %t.dir/from-ra.o
; RUN: cmp %t.dir/fl1.o %t.dir/from-ra.o
; RUN: %{brace-s3b8-llc} -stop-after=stack-slot-coloring %t.dir/fl1.bc \
; RUN:   -o %t.dir/color.mir
; RUN: FileCheck %s --check-prefix=PRE-RA < %t.dir/color.mir
; RUN: %{brace-s3b8-llc} -start-after=stack-slot-coloring %t.dir/color.mir \
; RUN:   -o %t.dir/from-color.o
; RUN: cmp %t.dir/fl1.o %t.dir/from-color.o
; RUN: %{brace-s3b8-llc} \
; RUN:   -stop-after=brace-finalize-fixed-local-byte-frame %t.dir/fl1.bc \
; RUN:   -o %t.dir/frame.mir
; RUN: FileCheck %s --check-prefix=POST-FRAME < %t.dir/frame.mir
; RUN: %{brace-s3b8-llc} \
; RUN:   -start-after=brace-finalize-fixed-local-byte-frame %t.dir/frame.mir \
; RUN:   -o %t.dir/from-frame.o
; RUN: cmp %t.dir/fl1.o %t.dir/from-frame.o
; RUN: %{brace-s3b8-llc} -stop-after=brace-finalize-branches %t.dir/fl1.bc \
; RUN:   -o %t.dir/final.mir
; RUN: FileCheck %s --check-prefix=POST-FINAL < %t.dir/final.mir
; RUN: %{brace-s3b8-llc} -start-after=brace-finalize-branches \
; RUN:   %t.dir/final.mir -o %t.dir/from-final.o
; RUN: cmp %t.dir/fl1.o %t.dir/from-final.o
;
; FL0 is the complete second positive class.  It has no alloca/lifetime/MFI/
; LOCAL/FRAME state and remains byte-identical to predecessor BF0.
; RUN: sed 's/brace-system-s2-direct-call-r0/brace-system-s2-direct-call-byte-frame-fixed-local-r0/g' %S/s3b5-direct-call.ll > %t.dir/fl0.ll
; RUN: sed 's/brace-system-s2-direct-call-r0/brace-system-s2-direct-call-byte-frame-r0/g' %S/s3b5-direct-call.ll > %t.dir/bf0.ll
; RUN: %{brace-s3b8-llc} %t.dir/fl0.ll -o %t.dir/fl0.o
; RUN: %{brace-s3b7c-llc} %t.dir/bf0.ll -o %t.dir/bf0.o
; RUN: cmp %t.dir/bf0.o %t.dir/fl0.o
; RUN: wc -c < %t.dir/fl0.o | FileCheck %s --check-prefix=FL0-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.dir/fl0.o | FileCheck %s --check-prefix=FL0-SHA
;
; Mutate one retained-IR identity at a time.  Every failure is target-owned,
; stable, nonzero, and leaves no nonempty object.
; RUN: %python %t.dir/mutate.py missing-start %t.dir/fl1.ll %t.dir/missing-start.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/missing-start.ll -o %t.dir/missing-start.o 2>&1 | FileCheck %s --check-prefix=COUNT-REJECT
; RUN: test ! -s %t.dir/missing-start.o
; RUN: %python %t.dir/mutate.py duplicate-start %t.dir/fl1.ll %t.dir/duplicate-start.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/duplicate-start.ll -o %t.dir/duplicate-start.o 2>&1 | FileCheck %s --check-prefix=COUNT-REJECT
; RUN: test ! -s %t.dir/duplicate-start.o
; RUN: %python %t.dir/mutate.py reordered-end %t.dir/fl1.ll %t.dir/reordered-end.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/reordered-end.ll -o %t.dir/reordered-end.o 2>&1 | FileCheck %s --check-prefix=ORDER-REJECT
; RUN: test ! -s %t.dir/reordered-end.o
; RUN: %python %t.dir/mutate.py tail-start %t.dir/fl1.ll %t.dir/tail-start.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/tail-start.ll -o %t.dir/tail-start.o 2>&1 | FileCheck %s --check-prefix=CALLSITE-REJECT
; RUN: test ! -s %t.dir/tail-start.o
; RUN: %python %t.dir/mutate.py bundle-start %t.dir/fl1.ll %t.dir/bundle-start.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/bundle-start.ll -o %t.dir/bundle-start.o 2>&1 | FileCheck %s --check-prefix=CALLSITE-REJECT
; RUN: test ! -s %t.dir/bundle-start.o
; RUN: %python %t.dir/mutate.py declaration-attrs %t.dir/fl1.ll %t.dir/declaration-attrs.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/declaration-attrs.ll -o %t.dir/declaration-attrs.o 2>&1 | FileCheck %s --check-prefix=DECL-REJECT
; RUN: test ! -s %t.dir/declaration-attrs.o
; RUN: %python %t.dir/mutate.py callsite-attrs %t.dir/fl1.ll %t.dir/callsite-attrs.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/callsite-attrs.ll -o %t.dir/callsite-attrs.o 2>&1 | FileCheck %s --check-prefix=CALLSITE-REJECT
; RUN: test ! -s %t.dir/callsite-attrs.o
; RUN: %python %t.dir/mutate.py callsite-cold %t.dir/fl1.ll %t.dir/callsite-cold.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/callsite-cold.ll -o %t.dir/callsite-cold.o 2>&1 | FileCheck %s --check-prefix=CALLSITE-REJECT
; RUN: test ! -s %t.dir/callsite-cold.o
; RUN: %python %t.dir/mutate.py callsite-mustprogress %t.dir/fl1.ll %t.dir/callsite-mustprogress.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/callsite-mustprogress.ll -o %t.dir/callsite-mustprogress.o 2>&1 | FileCheck %s --check-prefix=CALLSITE-REJECT
; RUN: test ! -s %t.dir/callsite-mustprogress.o
; RUN: %python %t.dir/mutate.py callsite-string %t.dir/fl1.ll %t.dir/callsite-string.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/callsite-string.ll -o %t.dir/callsite-string.o 2>&1 | FileCheck %s --check-prefix=CALLSITE-REJECT
; RUN: test ! -s %t.dir/callsite-string.o
; RUN: %python %t.dir/mutate.py inalloca %t.dir/fl1.ll %t.dir/inalloca.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/inalloca.ll -o %t.dir/inalloca.o 2>&1 | FileCheck %s --check-prefix=ALLOCA-SHAPE
; RUN: test ! -s %t.dir/inalloca.o
; RUN: %python %t.dir/mutate.py swifterror %t.dir/fl1.ll %t.dir/swifterror.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/swifterror.ll -o %t.dir/swifterror.o 2>&1 | FileCheck %s --check-prefix=ALLOCA-SHAPE
; RUN: test ! -s %t.dir/swifterror.o
; RUN: %python %t.dir/mutate.py nonvolatile-store %t.dir/fl1.ll %t.dir/nonvolatile-store.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/nonvolatile-store.ll -o %t.dir/nonvolatile-store.o 2>&1 | FileCheck %s --check-prefix=LOCAL-STORE
; RUN: test ! -s %t.dir/nonvolatile-store.o
; RUN: %python %t.dir/mutate.py pointer-escape %t.dir/fl1.ll %t.dir/pointer-escape.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/pointer-escape.ll -o %t.dir/pointer-escape.o 2>&1 | FileCheck %s --check-prefix=OUTSIDE-PROFILE
; RUN: test ! -s %t.dir/pointer-escape.o
; RUN: %python %t.dir/mutate.py second-local %t.dir/fl1.ll %t.dir/second-local.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/second-local.ll -o %t.dir/second-local.o 2>&1 | FileCheck %s --check-prefix=COUNT-REJECT
; RUN: test ! -s %t.dir/second-local.o
; RUN: %python %t.dir/mutate.py helper-local %t.dir/fl1.ll %t.dir/helper-local.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/helper-local.ll -o %t.dir/helper-local.o 2>&1 | FileCheck %s --check-prefix=OUTSIDE-PROFILE
; RUN: test ! -s %t.dir/helper-local.o
; RUN: %python %t.dir/mutate.py leaf-local %t.dir/fl1.ll %t.dir/leaf-local.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/leaf-local.ll -o %t.dir/leaf-local.o 2>&1 | FileCheck %s --check-prefix=COUNT-REJECT
; RUN: test ! -s %t.dir/leaf-local.o
; RUN: not --crash %{brace-s3b8-llc} %t.dir/hybrid-fl0.ll -o %t.dir/hybrid-fl0.o 2>&1 | FileCheck %s --check-prefix=HYBRID-FL0
; RUN: test ! -s %t.dir/hybrid-fl0.o
; RUN: sed 's/brace-system-s2-direct-call-byte-frame-r0/brace-system-s2-direct-call-byte-frame-fixed-local-r0/g' %S/s3b7c-direct-call-byte-frame.ll > %t.dir/old-bf1.ll
; RUN: not --crash %{brace-s3b8-llc} %t.dir/old-bf1.ll -o %t.dir/old-bf1.o 2>&1 | FileCheck %s --check-prefix=OLD-BF1
; RUN: test ! -s %t.dir/old-bf1.o
; RUN: not --crash %{brace-s3b7c-llc} %t.dir/fl1.bc -o %t.dir/new-fl1-old-selector.o 2>&1 | FileCheck %s --check-prefix=OLD-SELECTOR
; RUN: test ! -s %t.dir/new-fl1-old-selector.o
;
; FL1-SIZE: 1320
; FL1-SHA: abd3121acb5c21474aaa7deabfc7309d9687880600bc4f765828c65228f26148
; CANONICAL-BC-SIZE: 2924
; FL0-SIZE: 1304
; FL0-SHA: 406fe1d3e7443777b53ce5bbca54172e88ba2b5cd50071f03c022e0bfd3327cf
; FL1-OBJECT: Flags [ (0x42520400)
; FL1-OBJECT: SectionHeaderCount: 11
; FL1-OBJECT: Name: .brace.functions
; FL1-OBJECT: Size: 128
; FL1-OBJECT: Name: .brace.text
; FL1-OBJECT: Size: 64
;
; IR-LABEL: define dso_local void @brace_system_entry()
; IR: %local = alloca i32, align 4
; IR-NEXT: %input = load volatile i32, ptr addrspace(200)
; IR-NEXT: call void @llvm.lifetime.start.p0(ptr nonnull %local)
; IR-NEXT: store volatile i32 %input, ptr %local, align 4
; IR-NEXT: %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input)
; IR-NEXT: %recovered = load volatile i32, ptr %local, align 4
; IR-NEXT: %combined = and i32 %recovered, %result
; IR-NEXT: store volatile i32 %combined, ptr addrspace(200)
; IR-NEXT: call void @llvm.lifetime.end.p0(ptr nonnull %local)
; IR-NEXT: ret void
; IR: declare void @llvm.lifetime.start.p0(ptr captures(none))
; IR-LABEL: define internal fastcc i32 @brace_system_call_leaf
; IR: declare void @llvm.lifetime.end.p0(ptr captures(none))
; IR: attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
; IR: !{i32 1, !"target-abi", !"brace-system-s2-direct-call-byte-frame-fixed-local-r0"}
;
; PRE-ISEL-LABEL: name: brace_system_entry
; PRE-ISEL: stack:
; PRE-ISEL-NEXT: - { id: 0, name: local, type: default, offset: 0, size: 4, alignment: 4,
; PRE-ISEL: LIFETIME_START %stack.0.local
; PRE-ISEL-NEXT: LOCAL_STORE32
; PRE-ISEL: LOCAL_LOAD32
; PRE-ISEL: LIFETIME_END %stack.0.local
; PRE-RA-LABEL: name: brace_system_entry
; PRE-RA: stack:
; PRE-RA-NEXT: - { id: 0, name: local, type: default, offset: 0, size: 4, alignment: 4,
; PRE-RA: LIFETIME_START %stack.0.local
; PRE-RA-NEXT: LOCAL_STORE32 $r4, %stack.0.local
; PRE-RA-NEXT: CALL_I32 @brace_system_call_leaf
; PRE-RA-NEXT: $r5 = LOCAL_LOAD32 %stack.0.local
; PRE-RA: LIFETIME_END %stack.0.local
; POST-FRAME-LABEL: name: brace_system_entry
; POST-FRAME: stack: []
; POST-FRAME: machineFunctionInfo: {}
; POST-FRAME: FRAME_ENTER 16
; POST-FRAME: FRAME_STORE32 8, $r4
; POST-FRAME-NEXT: CALL_I32 @brace_system_call_leaf
; POST-FRAME-NEXT: $r5 = FRAME_LOAD32 8
; POST-FRAME: FRAME_LEAVE
; POST-FRAME-NEXT: RET
; POST-FRAME-NOT: LOCAL_
; POST-FRAME-NOT: LIFETIME_
; POST-FRAME-NOT: %stack.
; POST-FINAL-LABEL: name: brace_system_entry
; POST-FINAL: stack: []
; POST-FINAL: machineFunctionInfo: {}
; POST-FINAL: FRAME_ENTER 16
; POST-FINAL: FRAME_STORE32 8, $r4
; POST-FINAL-NEXT: CALL_I32 @brace_system_call_leaf
; POST-FINAL-NEXT: $r5 = FRAME_LOAD32 8
; POST-FINAL: FRAME_LEAVE
; POST-FINAL-NEXT: RET
; POST-FINAL-NOT: LOCAL_
; POST-FINAL-NOT: LIFETIME_
; POST-FINAL-NOT: %stack.
;
; COUNT-REJECT: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: FL1 exact function, instruction, alloca, lifetime, or local-access count mismatch
; ORDER-REJECT: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: FL1 entry instruction order is not exact
; CALLSITE-REJECT: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: lifetime callsite spelling or nonnull operand is not exact
; DECL-REJECT: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: lifetime declaration signature or attributes are not exact
; ALLOCA-SHAPE: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: local allocation is not one exact entry i32 alloca
; LOCAL-STORE: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: local store is not one exact volatile aligned i32 alloca access
; OUTSIDE-PROFILE: LLVM ERROR: brace64 S3b.5 direct-call ABI: instruction is outside the S3b.5 direct-call profile
; HYBRID-FL0: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: FL0 requires no alloca, local access, lifetime, or declaration
; OLD-BF1: LLVM ERROR: brace64 S3b.8 direct-call-byte-frame-fixed-local ABI: non-local SSA value remains live across the direct call
; OLD-SELECTOR: LLVM ERROR: brace64 S3b.5 direct-call ABI: exactly two functions are required

;--- fl1.ll
source_filename = "s3b8-normalized-fixed-local-fl1.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %local = alloca i32, align 4
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  call void @llvm.lifetime.start.p0(ptr nonnull %local)
  store volatile i32 %input, ptr %local, align 4
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #3
  %recovered = load volatile i32, ptr %local, align 4
  %combined = and i32 %recovered, %result
  store volatile i32 %combined, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %local)
  ret void
}

declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #2 {
entry:
  %mask = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  %masked = and i32 %mask, %value
  ret i32 %masked
}

declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nofree noinline norecurse nounwind willreturn memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-byte-frame-fixed-local-r0"}

;--- hybrid-fl0.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #3
  store volatile i32 %result, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
  ret void
}

declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

define internal fastcc i32 @brace_system_call_leaf(i32 noundef %value) unnamed_addr #2 {
entry:
  %mask = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
  %masked = and i32 %mask, %value
  ret i32 %masked
}

declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nofree noinline norecurse nounwind willreturn memory(readwrite, target_mem0: none, target_mem1: none) "disable-tail-calls"="true" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nobuiltin "no-builtins" }

!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-byte-frame-fixed-local-r0"}

;--- mutate.py
from pathlib import Path
import sys

assert len(sys.argv) == 4
mode, source_path, output_path = sys.argv[1:]
text = Path(source_path).read_text()

def once(old, new):
    global text
    assert text.count(old) == 1, (mode, old, text.count(old))
    text = text.replace(old, new, 1)

start = "  call void @llvm.lifetime.start.p0(ptr nonnull %local)\n"
end = "  call void @llvm.lifetime.end.p0(ptr nonnull %local)\n"
load = "  %recovered = load volatile i32, ptr %local, align 4\n"
call = "  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #3\n"

if mode == "missing-start":
    once(start, "")
elif mode == "duplicate-start":
    once(start, start + start)
elif mode == "reordered-end":
    once(end, "")
    once(load, end + load)
elif mode == "tail-start":
    once(start, start.replace("call void", "tail call void"))
elif mode == "bundle-start":
    once(start, start.rstrip("\n") + ' [ "brace.test"() ]\n')
elif mode == "declaration-attrs":
    once("declare void @llvm.lifetime.start.p0",
         "declare fastcc void @llvm.lifetime.start.p0")
    once("declare void @llvm.lifetime.end.p0",
         "declare fastcc void @llvm.lifetime.end.p0")
    once(start, start.replace("call void", "call fastcc void"))
    once(end, end.replace("call void", "call fastcc void"))
elif mode in {"callsite-attrs", "callsite-cold", "callsite-mustprogress",
             "callsite-string"}:
    callsite_attribute = {
        "callsite-attrs": "nounwind",
        "callsite-cold": "cold",
        "callsite-mustprogress": "mustprogress",
        "callsite-string": '"brace.probe"="x"',
    }[mode]
    once(start, start.rstrip("\n") + " #4\n")
    once("attributes #3 = { nobuiltin \"no-builtins\" }\n",
         "attributes #3 = { nobuiltin \"no-builtins\" }\n"
         f"attributes #4 = {{ {callsite_attribute} }}\n")
elif mode in {"declaration-no-mustprogress", "declaration-cold",
             "declaration-string", "declaration-no-nocallback",
             "declaration-memory-none"}:
    canonical = ("attributes #1 = { mustprogress nocallback nofree nosync "
                 "nounwind willreturn memory(argmem: readwrite) }\n")
    declaration_attribute = {
        "declaration-no-mustprogress": canonical.replace("mustprogress ", ""),
        "declaration-cold": canonical.replace("mustprogress ",
                                               "mustprogress cold "),
        "declaration-string": canonical.replace(
            " memory(argmem: readwrite)",
            ' memory(argmem: readwrite) "brace.probe"="x"'),
        "declaration-no-nocallback": canonical.replace("nocallback ", ""),
        "declaration-memory-none": canonical.replace(
            "memory(argmem: readwrite)", "memory(none)"),
    }[mode]
    once(canonical, declaration_attribute)
elif mode == "inalloca":
    once("  %local = alloca i32, align 4\n",
         "  %local = alloca inalloca i32, align 4\n")
elif mode == "swifterror":
    once("  %local = alloca i32, align 4\n",
         "  %local = alloca swifterror ptr, align 4\n")
    once(start, start.replace("ptr nonnull", "ptr swifterror nonnull"))
    once(end, end.replace("ptr nonnull", "ptr swifterror nonnull"))
elif mode == "nonvolatile-store":
    once("  store volatile i32 %input, ptr %local, align 4\n",
         "  store i32 %input, ptr %local, align 4\n")
elif mode == "pointer-escape":
    once("  %local = alloca i32, align 4\n",
         "  %local = alloca i32, align 4\n"
         "  %escaped = ptrtoint ptr %local to i64\n")
elif mode == "second-local":
    once("  %local = alloca i32, align 4\n",
         "  %local = alloca i32, align 4\n"
         "  %local2 = alloca i32, align 4\n")
elif mode == "helper-local":
    once("entry:\n  %mask = load volatile i32, ptr addrspace(200)",
         "entry:\n  %helper.local = alloca i32, align 4\n"
         "  %mask = load volatile i32, ptr addrspace(200)")
elif mode == "leaf-local":
    once(call, "  %call.local = load volatile i32, ptr %local, align 4\n" +
         call.replace("%input) #3", "%call.local) #3"))
else:
    raise AssertionError(mode)

Path(output_path).write_text(text)
