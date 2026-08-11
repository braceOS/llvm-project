; REQUIRES: brace-registered-target
;
; Freeze the independent S3b.7c compiler projection.  LLVM's one i32 spill
; remains a private FI/MMO through Greedy RA, then the target maps it to Guest
; byte offset 4 in a 16-byte semantic frame.  No FI ordinal or MFI offset is
; serialized.
;
; DEFINE: %{brace-s3b7c-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-direct-call-byte-frame-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
;
; RUN: %{brace-s3b7c-llc} %s -o %t.o
; RUN: %{brace-s3b7c-llc} %s -o %t.again.o
; RUN: cmp %t.o %t.again.o
; RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
; RUN: llvm-readobj --file-headers --sections %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJECT
; RUN: %python -c "import struct,sys; d=open(sys.argv[1],'rb').read(); assert struct.unpack_from('<I',d,108)[0]==16; assert struct.unpack_from('<I',d,172)[0]==0; w=struct.unpack_from('<16I',d,0x120); assert [((x>>2)&63) for x in w]==[25,22,23,27,16,26,4,22,24,28,20,22,23,4,4,20]; assert w[0]==0x267 and w[3]==0x4266f and w[5]==0x4966b and w[9]==0x273" %t.o
;
; BF0 retains the complete S3b.5 payload and zero frame records.  Only the
; independently allocated exact e_flags identity changes.
; RUN: sed 's/brace-system-s2-direct-call-r0/brace-system-s2-direct-call-byte-frame-r0/g' %S/s3b5-direct-call.ll > %t.bf0.ll
; RUN: %{brace-s3b7c-llc} %t.bf0.ll -o %t.bf0.o
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %S/s3b5-direct-call.ll \
; RUN:   -o %t.old.o
; RUN: wc -c < %t.bf0.o | FileCheck %s --check-prefix=BF0-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.bf0.o | FileCheck %s --check-prefix=BF0-SHA
; RUN: %python -c "import struct,sys; old=bytearray(open(sys.argv[1],'rb').read()); new=bytearray(open(sys.argv[2],'rb').read()); assert old[48:52]==bytes.fromhex('00025242') and new[48:52]==bytes.fromhex('00045242'); assert struct.unpack_from('<I',new,108)[0]==0 and struct.unpack_from('<I',new,172)[0]==0; old[48:52]=new[48:52]; assert old==new" %t.old.o %t.bf0.o
;
; Five independently restartable seams cover normalized IR through final MIR.
; RUN: %{brace-s3b7c-llc} -stop-after=finalize-isel %s -o %t.isel.mir
; RUN: FileCheck %s --check-prefix=ISEL < %t.isel.mir
; RUN: %{brace-s3b7c-llc} -start-after=finalize-isel %t.isel.mir -o %t.from-isel.o
; RUN: cmp %t.o %t.from-isel.o
; RUN: %{brace-s3b7c-llc} -stop-after=virtregrewriter %s -o %t.ra.mir
; RUN: FileCheck %s --check-prefix=RA < %t.ra.mir
; RUN: %{brace-s3b7c-llc} -start-after=virtregrewriter %t.ra.mir -o %t.from-ra.o
; RUN: cmp %t.o %t.from-ra.o
; RUN: %{brace-s3b7c-llc} -stop-after=stack-slot-coloring %s -o %t.color.mir
; RUN: FileCheck %s --check-prefix=COLOR < %t.color.mir
; RUN: %{brace-s3b7c-llc} -start-after=stack-slot-coloring %t.color.mir -o %t.from-color.o
; RUN: cmp %t.o %t.from-color.o
; RUN: %{brace-s3b7c-llc} -stop-after=brace-finalize-byte-frame %s -o %t.frame.mir
; RUN: FileCheck %s --check-prefix=FRAME < %t.frame.mir
; RUN: %{brace-s3b7c-llc} -start-after=brace-finalize-byte-frame %t.frame.mir -o %t.from-frame.o
; RUN: cmp %t.o %t.from-frame.o
; RUN: %{brace-s3b7c-llc} -stop-after=brace-finalize-branches %s -o %t.final.mir
; RUN: FileCheck %s --check-prefix=FINAL < %t.final.mir
; RUN: %{brace-s3b7c-llc} -start-after=brace-finalize-branches %t.final.mir -o %t.from-final.o
; RUN: cmp %t.o %t.from-final.o
;
; IR intake admits only BF0 or one i32/one-use call-live value, and this new
; selector explicitly contracts both functions to exactly one Return.
; RUN: rm -rf %t.reject && split-file %S/s3b6-ir-reject.ll %t.reject
; RUN: sed 's/brace-system-s2-direct-call-home-r0/brace-system-s2-direct-call-byte-frame-r0/g' %t.reject/h2.ll > %t.h2.ll
; RUN: not --crash %{brace-s3b7c-llc} %t.h2.ll -o %t.h2.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=H2-REJECT
; RUN: test ! -s %t.h2.o
; RUN: sed 's/brace-system-s2-direct-call-home-r0/brace-system-s2-direct-call-byte-frame-r0/g' %t.reject/i8.ll > %t.i8.ll
; RUN: not --crash %{brace-s3b7c-llc} %t.i8.ll -o %t.i8.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=I8-REJECT
; RUN: test ! -s %t.i8.o
; RUN: sed 's/brace-system-s2-direct-call-r0/brace-system-s2-direct-call-byte-frame-r0/g' %S/s3b5-direct-call-helper-cfg.ll > %t.two-helper-returns.ll
; RUN: not --crash %{brace-s3b7c-llc} %t.two-helper-returns.ll \
; RUN:   -o %t.two-helper-returns.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=TWO-RET-REJECT
; RUN: test ! -s %t.two-helper-returns.o
;
; SIZE: 1320
; SHA: cb09ac307141381c709d1504bab2f74b274a0b50e42b20cad593f9bf2846fb55
; BF0-SIZE: 1304
; BF0-SHA: 406fe1d3e7443777b53ce5bbca54172e88ba2b5cd50071f03c022e0bfd3327cf
; H2-REJECT: LLVM ERROR: brace64 S3b.7c direct-call-byte-frame ABI: root permits BF0 or exactly one i32 SSA value with one post-call use
; I8-REJECT: LLVM ERROR: brace64 S3b.7c direct-call-byte-frame ABI: call-live frame value must originate from one i32 physical load
; TWO-RET-REJECT: LLVM ERROR: brace64 S3b.7c direct-call-byte-frame ABI: each function requires exactly one signature-matching Return
; OBJECT: Flags [ (0x42520400)
; OBJECT: SectionHeaderCount: 11
; OBJECT: StringTableSectionIndex: 10
; OBJECT: Name: .brace.functions
; OBJECT: Size: 128
; OBJECT: Name: .brace.types
; OBJECT: Size: 12
; OBJECT: Name: .brace.descriptors
; OBJECT: Size: 24
; OBJECT: Name: .brace.text
; OBJECT: Size: 64
;
; ISEL-LABEL: name: brace_system_entry
; ISEL: stackSize: 0
; ISEL: hasCalls: true
; ISEL: stack: []
; ISEL: CALL_I32 @brace_system_call_leaf
;
; RA-LABEL: name: brace_system_entry
; RA: type: spill-slot
; RA: size: 4
; RA: alignment: 4
; RA: SPILL_STORE32 $r4, %stack.0 :: (store (s32) into %stack.0)
; RA-NEXT: CALL_I32 @brace_system_call_leaf
; RA-NEXT: $r5 = SPILL_LOAD32 %stack.0 :: (load (s32) from %stack.0)
; RA-LABEL: name: brace_system_call_leaf
; RA: stack: []
;
; COLOR-LABEL: name: brace_system_entry
; COLOR: type: spill-slot
; COLOR: size: 4
; COLOR: alignment: 4
; COLOR: SPILL_STORE32 $r4, %stack.0 :: (store (s32) into %stack.0)
; COLOR-NEXT: CALL_I32 @brace_system_call_leaf
; COLOR-NEXT: $r5 = SPILL_LOAD32 %stack.0 :: (load (s32) from %stack.0)
;
; FRAME-LABEL: name: brace_system_entry
; FRAME: stack: []
; FRAME: bb.0.entry:
; FRAME-NEXT: FRAME_ENTER 16
; FRAME: FRAME_STORE32 4, $r4
; FRAME-NEXT: CALL_I32 @brace_system_call_leaf
; FRAME-NEXT: $r5 = FRAME_LOAD32 4
; FRAME: FRAME_LEAVE
; FRAME-NEXT: RET
; FRAME-NOT: SPILL_
; FRAME-NOT: HOME_
; FRAME-NOT: %stack.
; FRAME-LABEL: name: brace_system_call_leaf
; FRAME: stack: []
; FRAME-NOT: FRAME_
;
; FINAL-LABEL: name: brace_system_entry
; FINAL: stack: []
; FINAL: bb.0.entry:
; FINAL-NEXT: FRAME_ENTER 16
; FINAL: FRAME_STORE32 4, $r4
; FINAL-NEXT: CALL_I32 @brace_system_call_leaf
; FINAL-NEXT: $r5 = FRAME_LOAD32 4
; FINAL: $r4 = AND32 killed $r4, killed $r5
; FINAL: STORE32 killed $r0, killed $r4
; FINAL-NEXT: FRAME_LEAVE
; FINAL-NEXT: RET
; FINAL-NOT: SPILL_
; FINAL-NOT: HOME_
; FINAL-NOT: %stack.
; FINAL-LABEL: name: brace_system_call_leaf
; FINAL: stack: []
; FINAL-NOT: FRAME_

source_filename = "s3b7c-direct-call-byte-frame.c"
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %input = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
  %result = call fastcc i32 @brace_system_call_leaf(i32 noundef %input) #2
  %combined = and i32 %result, %input
  store volatile i32 %combined, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
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
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-byte-frame-r0"}
