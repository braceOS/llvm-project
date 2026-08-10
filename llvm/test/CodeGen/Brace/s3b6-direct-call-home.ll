; REQUIRES: brace-registered-target
;
; Freeze the first real Greedy-RA call-live i32 path.  The new sibling keeps
; r0..r5 call-clobbered, transports the one live value through an activation
; home, and still has zero Guest stack/frame storage.
;
; DEFINE: %{brace-s3b6-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-direct-call-home-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
;
; RUN: %{brace-s3b6-llc} %s -o %t.o
; RUN: %{brace-s3b6-llc} %s -o %t.again.o
; RUN: cmp %t.o %t.again.o
; RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
; RUN: llvm-readobj --file-headers --sections %t.o | \
; RUN:   FileCheck %s --check-prefix=OBJECT
; RUN: llvm-readobj --sections %t.o | FileCheck %s --check-prefix=SCNT
;
; The accepted S3b.5 object remains byte-identical, while its selector rejects
; this source-level live-through before publishing any bytes.
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %S/s3b5-direct-call.ll \
; RUN:   -o %t.s3b5.o
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.s3b5.o | FileCheck %s --check-prefix=OLD-SHA
; H0 is a legal control in the new sibling.  It retains the complete old S3b.5
; payload and differs only in the independently allocated e_flags word.
; RUN: sed 's/brace-system-s2-direct-call-r0/brace-system-s2-direct-call-home-r0/' \
; RUN:   %S/s3b5-direct-call.ll > %t.h0.ll
; RUN: %{brace-s3b6-llc} %t.h0.ll -o %t.h0.o
; RUN: %{brace-s3b6-llc} %t.h0.ll -o %t.h0-again.o
; RUN: cmp %t.h0.o %t.h0-again.o
; RUN: wc -c < %t.h0.o | FileCheck %s --check-prefix=H0-SIZE
; RUN: llvm-readobj --file-headers --sections %t.h0.o | \
; RUN:   FileCheck %s --check-prefix=H0-OBJECT
; RUN: %python -c "import sys; old=bytearray(open(sys.argv[1],'rb').read()); new=bytearray(open(sys.argv[2],'rb').read()); assert old[48:52] == bytes.fromhex('00025242'); assert new[48:52] == bytes.fromhex('00035242'); old[48:52] = new[48:52]; assert old == new" %t.s3b5.o %t.h0.o
; RUN: sed 's/brace-system-s2-direct-call-home-r0/brace-system-s2-direct-call-r0/' \
; RUN:   %s > %t.old-compatible.ll
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
; RUN:   -verify-machineinstrs -filetype=obj %t.old-compatible.ll \
; RUN:   -o %t.old-reject.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=OLD-LIVE
; RUN: test ! -s %t.old-reject.o
;
; RUN: %{brace-s3b6-llc} -stop-after=finalize-isel %s -o %t.isel.mir
; RUN: FileCheck %s --check-prefix=ISEL < %t.isel.mir
; RUN: %{brace-s3b6-llc} -start-after=finalize-isel %t.isel.mir \
; RUN:   -o %t.from-isel.o
; RUN: cmp %t.o %t.from-isel.o
;
; RUN: %{brace-s3b6-llc} -stop-after=virtregrewriter %s -o %t.ra.mir
; RUN: FileCheck %s --check-prefix=RA < %t.ra.mir
; RUN: %{brace-s3b6-llc} -start-after=virtregrewriter %t.ra.mir \
; RUN:   -o %t.from-ra.o
; RUN: cmp %t.o %t.from-ra.o
;
; RUN: %{brace-s3b6-llc} -stop-after=stack-slot-coloring %s \
; RUN:   -o %t.color.mir
; RUN: FileCheck %s --check-prefix=COLOR < %t.color.mir
; RUN: %{brace-s3b6-llc} -start-after=stack-slot-coloring %t.color.mir \
; RUN:   -o %t.from-color.o
; RUN: cmp %t.o %t.from-color.o
;
; RUN: %{brace-s3b6-llc} -stop-after=brace-finalize-spill-homes %s \
; RUN:   -o %t.home.mir
; RUN: FileCheck %s --check-prefix=HOME < %t.home.mir
; RUN: %{brace-s3b6-llc} -start-after=brace-finalize-spill-homes \
; RUN:   %t.home.mir -o %t.from-home.o
; RUN: cmp %t.o %t.from-home.o
; A post-home restart is untrusted.  Moving the save across Call must not be
; accepted merely because generic home definite-initialization still holds.
; RUN: awk '/HOME_SAVE32 0/ { next } { print } /CALL_I32/ { print "    HOME_SAVE32 0, $r4" }' \
; RUN:   %t.home.mir > %t.save-after-call.mir
; RUN: not --crash %{brace-s3b6-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t.save-after-call.mir \
; RUN:   -o %t.save-after-call.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=SAVE-AFTER-CALL
; RUN: test ! -s %t.save-after-call.o
;
; RUN: %{brace-s3b6-llc} -stop-after=brace-finalize-branches %s \
; RUN:   -o %t.final.mir
; RUN: FileCheck %s --check-prefix=FINAL < %t.final.mir
; RUN: %{brace-s3b6-llc} -start-after=brace-finalize-branches \
; RUN:   %t.final.mir -o %t.from-final.o
; RUN: cmp %t.o %t.from-final.o
; The final restart is untrusted too.  A constant cannot impersonate the
; registered non-rematerializable physical-load origin, and a syntactically
; present restore cannot be killed before reaching the physical Store.
; RUN: sed 's/    \$r4 = LOAD32.*$/    \$r4 = CONST32 7/' \
; RUN:   %t.final.mir > %t.forged-origin.mir
; RUN: not --crash %{brace-s3b6-llc} \
; RUN:   -start-after=brace-finalize-branches %t.forged-origin.mir \
; RUN:   -o %t.forged-origin.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=FORGED-ORIGIN
; RUN: test ! -s %t.forged-origin.o
; RUN: awk '{ print } /\$r5 = HOME_RESTORE32 0/ { \
; RUN:   print "    $r5 = CONST32 0" }' %t.final.mir \
; RUN:   > %t.dead-restore.mir
; RUN: not --crash %{brace-s3b6-llc} \
; RUN:   -start-after=brace-finalize-branches %t.dead-restore.mir \
; RUN:   -o %t.dead-restore.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=DEAD-RESTORE
; RUN: test ! -s %t.dead-restore.o
; The final-MIR verifier must reconstruct the Call lifecycle independently of
; the MC writer.  A returned path may neither re-enter Call nor discard its r4
; result while preserving an otherwise observable restored home.
; RUN: awk 'BEGIN { root=0; body=0; changed=0 } \
; RUN:   /^name:            brace_system_entry$/ { root=1 } \
; RUN:   /^name:            brace_system_call_leaf$/ { root=0 } \
; RUN:   root && /^body:/ { body=1 } \
; RUN:   root && body && /^  bb\.0\.entry:$/ { print; \
; RUN:     print "    successors: %bb.0(0x40000000), %bb.1(0x40000000)"; \
; RUN:     next } \
; RUN:   root && body && /STORE32 killed \$r0, killed \$r4/ { \
; RUN:     sub("killed \\$r4", "$r4") } \
; RUN:   root && body && !changed && /^    RET$/ { \
; RUN:     print "    BR_IF32 $r4, %bb.0, %bb.1"; print ""; \
; RUN:     print "  bb.1:"; print "    RET"; changed=1; next } \
; RUN:   { print }' %t.final.mir > %t.call-cycle.mir
; RUN: not --crash %{brace-s3b6-llc} \
; RUN:   -start-after=brace-finalize-branches %t.call-cycle.mir \
; RUN:   -o %t.call-cycle.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=CALL-CYCLE
; RUN: test ! -s %t.call-cycle.o
; RUN: sed -e '/    \$r4 = AND32 killed \$r4, killed \$r5/d' \
; RUN:   -e 's/STORE32 killed \$r0, killed \$r4/STORE32 killed \$r0, killed \$r5/' \
; RUN:   %t.final.mir > %t.dead-call-result.mir
; RUN: not --crash %{brace-s3b6-llc} \
; RUN:   -start-after=brace-finalize-branches %t.dead-call-result.mir \
; RUN:   -o %t.dead-call-result.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=DEAD-CALL-RESULT
; RUN: test ! -s %t.dead-call-result.o
;
; SIZE: 1312
; SHA: b59866691d4a81ea304b3d8ba5c3a549c33b0022861937806024e5e9fca1d59a
; OLD-SHA: 638333df950316ee0992d17cc787fc65a90b2803cccbbfcca1d47dc4a535ba9d
; H0-SIZE: 1304
; OLD-LIVE: brace64 S3b.5 direct-call ABI: caller SSA value remains live across the call
; SAVE-AFTER-CALL: LLVM ERROR: brace64 S3b.6 post-home verifier: H1 requires one i32 h0 save immediately before Call and restore immediately after Return
; FORGED-ORIGIN: LLVM ERROR: brace64 S3b.6 post-home verifier: saved h0 does not originate from a physical i32 load
; DEAD-RESTORE: LLVM ERROR: brace64 S3b.6 post-home verifier: restored h0 does not reach a physical store on every exit
; CALL-CYCLE: LLVM ERROR: brace64 S3b.6 post-home verifier: entry direct call can execute more than once
; DEAD-CALL-RESULT: LLVM ERROR: brace64 S3b.6 post-home verifier: direct-call result is not consumed on every root exit path
; OBJECT: Flags [ (0x42520300)
; OBJECT: SectionHeaderCount: 11
; OBJECT: StringTableSectionIndex: 10
; OBJECT: Name: .brace.functions
; OBJECT: Size: 128
; OBJECT: Name: .brace.types
; OBJECT: Size: 13
; OBJECT: Name: .brace.descriptors
; OBJECT: Size: 24
; OBJECT: Name: .brace.text
; OBJECT: Size: 56
; H0-OBJECT: Flags [ (0x42520300)
; H0-OBJECT: SectionHeaderCount: 11
; H0-OBJECT: Name: .brace.types
; H0-OBJECT: Size: 12
; H0-OBJECT: Name: .brace.text
; H0-OBJECT: Size: 44
; SCNT: Sections [
; SCNT-COUNT-11: Section {
;
; ISEL-LABEL: name: brace_system_entry
; ISEL: stackSize: 0
; ISEL: hasCalls: true
; ISEL: stack: []
; ISEL: CALL_I32 @brace_system_call_leaf
; ISEL-SAME: csr_noregs
; ISEL-SAME: implicit-def $r4
; ISEL-SAME: implicit $r4
;
; RA-LABEL: name: brace_system_entry
; RA: stackSize: 0
; RA: hasCalls: true
; RA: type: spill-slot
; RA: size: 4
; RA: alignment: 4
; RA: SPILL_STORE32
; RA: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
; RA: SPILL_LOAD32
; RA-LABEL: name: brace_system_call_leaf
; RA: stack: []
;
; COLOR-LABEL: name: brace_system_entry
; COLOR: stackSize: 0
; COLOR: hasCalls: true
; COLOR: type: spill-slot
; COLOR: size: 4
; COLOR: alignment: 4
; COLOR: SPILL_STORE32
; COLOR: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
; COLOR: SPILL_LOAD32
; COLOR-LABEL: name: brace_system_call_leaf
; COLOR: stack: []
;
; HOME-LABEL: name: brace_system_entry
; HOME: stackSize: 0
; HOME: HOME_SAVE32 0
; HOME: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
; HOME: HOME_RESTORE32 0
; HOME-NOT: SPILL_
; HOME-LABEL: name: brace_system_call_leaf
; HOME: stack: []
;
; FINAL-LABEL: name: brace_system_entry
; FINAL: stackSize: 0
; FINAL: HOME_SAVE32 0
; FINAL: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
; FINAL: HOME_RESTORE32 0
; FINAL-NOT: SPILL_
; FINAL-LABEL: name: brace_system_call_leaf
; FINAL: stack: []

source_filename = "s3b6-direct-call-home.c"
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
!1 = !{i32 1, !"target-abi", !"brace-system-s2-direct-call-home-r0"}
