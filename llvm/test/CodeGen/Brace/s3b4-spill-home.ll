; REQUIRES: brace-registered-target
;
; Exercise the exact S3b.4 seam: LLVM register allocation creates ordinary
; private spill slots, BraceFinalizeSpillHomes converts the surviving typed
; slots to immediate S2 home ordinals, and neither FI nor an MMO-backed spill
; operation reaches publication.
;
; DEFINE: %{brace-s3b4-llc} = llc -mtriple=brace64-unknown-none-elf \
; DEFINE:   -target-abi=brace-system-s2-leaf-home-r0 -O1 \
; DEFINE:   -verify-machineinstrs -filetype=obj
; RUN: rm -rf %t && split-file %s %t
; RUN: %python %t/check-alias-guards.py \
; RUN:   %llvm_src_root/lib/Target/Brace/BraceInstrInfo.cpp \
; RUN:   %llvm_src_root/lib/Target/Brace/BraceFinalizeSpillHomes.cpp \
; RUN:   %llvm_src_root/lib/Target/Brace/BraceFinalizeBranches.cpp \
; RUN:   %llvm_src_root/lib/Target/Brace/BraceFrameLowering.cpp
;
; RUN: %{brace-s3b4-llc} -stop-after=stack-slot-coloring %t/i32.ll \
; RUN:   -o %t/i32.pre.mir
; RUN: FileCheck %s --check-prefix=I32-PRE < %t/i32.pre.mir
; RUN: %{brace-s3b4-llc} -stop-after=brace-finalize-spill-homes %t/i32.ll \
; RUN:   -o %t/i32.post.mir
; RUN: FileCheck %s --check-prefix=I32-POST < %t/i32.post.mir
; RUN: %{brace-s3b4-llc} %t/i32.ll -o %t/i32.o
; The registered pre-home restart explicitly disables the global CLI verifier
; policy, including EXPENSIVE_CHECKS defaults.  Its four verifiers must
; therefore come from the target pipeline itself.
; RUN: llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-home-r0 -O1 -filetype=obj \
; RUN:   -verify-machineinstrs=false -start-after=stack-slot-coloring \
; RUN:   -debug-pass=Structure \
; RUN:   %t/i32.pre.mir -o %t/i32.target-owned.o \
; RUN:   2> %t/target-owned-pipeline.txt
; RUN: cmp %t/i32.o %t/i32.target-owned.o
; RUN: %python %t/check-target-owned-verifiers.py \
; RUN:   %llvm_src_root/lib/Target/Brace/BraceTargetMachine.cpp \
; RUN:   %t/target-owned-pipeline.txt
; RUN: wc -c < %t/i32.o | FileCheck %s --check-prefix=OBJECT-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t/i32.o | FileCheck %s --check-prefix=I32-SHA
; RUN: llvm-readobj --sections --relocations --expand-relocs \
; RUN:   --hex-dump=.brace.types --hex-dump=.brace.text %t/i32.o | \
; RUN:   FileCheck %s --check-prefixes=OBJECT,I32-OBJECT
;
; Both restart seams must recover the exact direct object without a hidden
; MachineFunctionInfo spill map.
; RUN: %{brace-s3b4-llc} -start-after=stack-slot-coloring %t/i32.pre.mir \
; RUN:   -o %t/i32.pre-restart.o
; RUN: %{brace-s3b4-llc} -start-after=brace-finalize-spill-homes \
; RUN:   %t/i32.post.mir -o %t/i32.post-restart.o
; RUN: cmp %t/i32.o %t/i32.pre-restart.o
; RUN: cmp %t/i32.o %t/i32.post-restart.o
;
; RUN: %{brace-s3b4-llc} -stop-after=stack-slot-coloring %t/i8.ll \
; RUN:   -o %t/i8.pre.mir
; RUN: FileCheck %s --check-prefix=I8-PRE < %t/i8.pre.mir
; RUN: %{brace-s3b4-llc} -stop-after=brace-finalize-spill-homes %t/i8.ll \
; RUN:   -o %t/i8.post.mir
; RUN: FileCheck %s --check-prefix=I8-POST < %t/i8.post.mir
; RUN: %{brace-s3b4-llc} %t/i8.ll -o %t/i8.o
; RUN: wc -c < %t/i8.o | FileCheck %s --check-prefix=OBJECT-SIZE
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t/i8.o | FileCheck %s --check-prefix=I8-SHA
; RUN: llvm-readobj --sections --relocations --expand-relocs \
; RUN:   --hex-dump=.brace.types %t/i8.o | \
; RUN:   FileCheck %s --check-prefixes=OBJECT,I8-OBJECT
;
; Three direct constant paddr values remain live across intervening volatile
; accesses before Greedy RA.  S3b.4 must resolve that pressure by exact paddr
; rematerialization instead of asking either spill callback for a paddr home.
; RUN: %{brace-s3b4-llc} -stop-before=greedy %t/paddr-remat.ll \
; RUN:   -o %t/paddr-remat.before.mir
; RUN: FileCheck %s --check-prefix=PADDR-BEFORE \
; RUN:   < %t/paddr-remat.before.mir
; RUN: %{brace-s3b4-llc} -stop-after=virtregrewriter %t/paddr-remat.ll \
; RUN:   -o %t/paddr-remat.after.mir
; RUN: FileCheck %s --check-prefix=PADDR-AFTER \
; RUN:   < %t/paddr-remat.after.mir
; RUN: %python %t/check-paddr-remat.py %t/paddr-remat.before.mir \
; RUN:   %t/paddr-remat.after.mir
; RUN: %{brace-s3b4-llc} %t/paddr-remat.ll -o %t/paddr-remat.o
; RUN: test -s %t/paddr-remat.o
;
; Conversely, replace the three long-lived values with deliberately
; non-rematerializable COPY results at the registered pre-RA seam.  Greedy RA
; must reach the target paddr spill callback, which fails closed for S3b.4.
; RUN: %python %t/make-paddr-spill.py %t/paddr-remat.before.mir \
; RUN:   %t/paddr-spill.mir
; RUN: not --crash %{brace-s3b4-llc} -start-before=greedy \
; RUN:   %t/paddr-spill.mir -o %t/paddr-spill.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=PADDR-SPILL
; RUN: test ! -s %t/paddr-spill.o
;
; The ABI-local hook must not expand the frozen S3b.3 selector: the identical
; pressure remains a target spill-callback rejection there.
; RUN: sed 's/brace-system-s2-leaf-home-r0/brace-system-s2-leaf-r0/' \
; RUN:   %t/paddr-remat.ll > %t/paddr-remat.old.ll
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %t/paddr-remat.old.ll -o %t/paddr-remat.old.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=OLD-PADDR-PRESSURE
; RUN: test ! -s %t/paddr-remat.old.o
;
; The old selector retains the S3b.3 fatal-on-pressure contract.
; RUN: sed 's/brace-system-s2-leaf-home-r0/brace-system-s2-leaf-r0/' \
; RUN:   %t/i32.ll > %t/i32.old.ll
; RUN: not --crash llc -mtriple=brace64-unknown-none-elf \
; RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
; RUN:   -filetype=obj %t/i32.old.ll -o %t/i32.old.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=OLD-PRESSURE
; RUN: test ! -s %t/i32.old.o
;
; The registered pre-home seam is immediately after stack-slot coloring, not
; immediately after virtual-register rewriting or immediately before the
; target-owned finalizer.  Two disjoint pressure regions create four initial
; spill objects and coloring reuses them as two homes.  Restarting there must
; both retain the direct object's exact identity and replay the target-owned
; verifier immediately before home finalization.
; RUN: %{brace-s3b4-llc} -stop-after=virtregrewriter %t/nonoverlap.ll \
; RUN:   -o %t/nonoverlap.uncolored.mir
; RUN: grep -c 'type: spill-slot' %t/nonoverlap.uncolored.mir | \
; RUN:   FileCheck %s --check-prefix=NONOVERLAP-UNCOLOR-SLOTS
; RUN: grep -c SPILL_STORE32 %t/nonoverlap.uncolored.mir | \
; RUN:   FileCheck %s --check-prefix=NONOVERLAP-UNCOLOR-STORES
; RUN: grep -c SPILL_LOAD32 %t/nonoverlap.uncolored.mir | \
; RUN:   FileCheck %s --check-prefix=NONOVERLAP-UNCOLOR-LOADS
; RUN: %{brace-s3b4-llc} -stop-after=stack-slot-coloring %t/nonoverlap.ll \
; RUN:   -o %t/nonoverlap.pre.mir
; RUN: grep -c 'type: spill-slot' %t/nonoverlap.pre.mir | \
; RUN:   FileCheck %s --check-prefix=NONOVERLAP-COLOR-SLOTS
; RUN: grep -c SPILL_STORE32 %t/nonoverlap.pre.mir | \
; RUN:   FileCheck %s --check-prefix=NONOVERLAP-COLOR-STORES
; RUN: grep -c SPILL_LOAD32 %t/nonoverlap.pre.mir | \
; RUN:   FileCheck %s --check-prefix=NONOVERLAP-COLOR-LOADS
; RUN: %{brace-s3b4-llc} %t/nonoverlap.ll -o %t/nonoverlap.o
; RUN: %{brace-s3b4-llc} -start-after=stack-slot-coloring \
; RUN:   %t/nonoverlap.pre.mir -o %t/nonoverlap.restart.o
; RUN: cmp %t/nonoverlap.o %t/nonoverlap.restart.o
; RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t/nonoverlap.o | FileCheck %s --check-prefix=NONOVERLAP-SHA
;
; A post-home restart is still untrusted: missing all-path initialization,
; a declaration hole, a type conflict, and an out-of-range ordinal all fail
; before object publication.
; RUN: sed '/HOME_SAVE32 1/d' %t/i32.post.mir > %t/no-save.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/no-save.mir \
; RUN:   -o %t/no-save.o 2>&1 | FileCheck %s --check-prefix=NO-SAVE
; RUN: test ! -s %t/no-save.o
; RUN: sed 's/\$r4 = HOME_RESTORE32 1/\$r4 = CONST32 0/' \
; RUN:   %t/i32.post.mir > %t/no-restore.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/no-restore.mir \
; RUN:   -o %t/no-restore.o 2>&1 | FileCheck %s --check-prefix=NO-RESTORE
; RUN: test ! -s %t/no-restore.o
; RUN: sed -e 's/HOME_SAVE32 0/HOME_SAVE32 2/' \
; RUN:   -e 's/HOME_RESTORE32 0/HOME_RESTORE32 2/' \
; RUN:   %t/i32.post.mir > %t/hole.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/hole.mir \
; RUN:   -o %t/hole.o 2>&1 | FileCheck %s --check-prefix=HOLE
; RUN: test ! -s %t/hole.o
; RUN: awk '/HOME_SAVE32 1/ { print "    $r2 = CONST8 1"; \
; RUN:   print "    HOME_SAVE8 1, killed $r2" } { print }' \
; RUN:   %t/i32.post.mir > %t/type-conflict.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/type-conflict.mir \
; RUN:   -o %t/type-conflict.o 2>&1 | FileCheck %s --check-prefix=TYPE
; RUN: test ! -s %t/type-conflict.o
; RUN: sed 's/HOME_SAVE32 1/HOME_SAVE32 20/' \
; RUN:   %t/i32.post.mir > %t/out-of-range.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/out-of-range.mir \
; RUN:   -o %t/out-of-range.o 2>&1 | FileCheck %s --check-prefix=RANGE
; RUN: test ! -s %t/out-of-range.o
;
; Skipping home finalization cannot launder a surviving FI/MMO.  Likewise a
; pre-home non-spill frame object and an embedded ABI mismatch fail closed.
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/i32.pre.mir \
; RUN:   -o %t/skipped.o
; RUN: test ! -s %t/skipped.o
; RUN: sed 's/type: spill-slot/type: default/' \
; RUN:   %t/i32.pre.mir > %t/nonspill.mir
; RUN: not --crash %{brace-s3b4-llc} -start-before=brace-finalize-spill-homes \
; RUN:   %t/nonspill.mir -o %t/nonspill.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=NONSPILL
; RUN: test ! -s %t/nonspill.o
; An explicit alias bit is not part of the ordinary stack-object MIR schema.
; Freeze that parser rejection separately from the internal MachineFrameInfo
; guards audited above; neither path claims serialized creator provenance.
; RUN: sed '1,/type: spill-slot/s/type: spill-slot/type: spill-slot, isAliased: true/' \
; RUN:   %t/i32.pre.mir > %t/aliased.mir
; RUN: not %{brace-s3b4-llc} -start-before=brace-finalize-spill-homes \
; RUN:   %t/aliased.mir -o %t/aliased.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=ALIASED-PARSER
; RUN: test ! -s %t/aliased.o
; RUN: sed 's/isCalleeSavedInfoValid: false/isCalleeSavedInfoValid: true/' \
; RUN:   %t/i32.pre.mir > %t/callee-saved-pre.mir
; RUN: not --crash %{brace-s3b4-llc} -start-before=brace-finalize-spill-homes \
; RUN:   %t/callee-saved-pre.mir -o %t/callee-saved-pre.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=CALLEE-SAVED-PRE
; RUN: test ! -s %t/callee-saved-pre.o
; RUN: sed 's/isCalleeSavedInfoValid: false/isCalleeSavedInfoValid: true/' \
; RUN:   %t/i32.post.mir > %t/callee-saved-post.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/callee-saved-post.mir \
; RUN:   -o %t/callee-saved-post.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=CALLEE-SAVED-POST
; RUN: test ! -s %t/callee-saved-post.o
; RUN: sed 's/maxCallFrameSize: 4294967295/maxCallFrameSize: 1/' \
; RUN:   %t/i32.pre.mir > %t/call-frame-pre.mir
; RUN: not --crash %{brace-s3b4-llc} -start-before=brace-finalize-spill-homes \
; RUN:   %t/call-frame-pre.mir -o %t/call-frame-pre.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=CALL-FRAME-PRE
; RUN: test ! -s %t/call-frame-pre.o
; RUN: sed 's/maxCallFrameSize: 4294967295/maxCallFrameSize: 1/' \
; RUN:   %t/i32.post.mir > %t/call-frame-post.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/call-frame-post.mir \
; RUN:   -o %t/call-frame-post.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=CALL-FRAME-POST
; RUN: test ! -s %t/call-frame-post.o
; RUN: sed 's/brace-system-s2-leaf-home-r0/brace-system-s2-leaf-r0/' \
; RUN:   %t/i32.post.mir > %t/abi-mismatch.mir
; RUN: not --crash %{brace-s3b4-llc} \
; RUN:   -start-after=brace-finalize-spill-homes %t/abi-mismatch.mir \
; RUN:   -o %t/abi-mismatch.o
; RUN: test ! -s %t/abi-mismatch.o
;
; This constructed-MIR unit boundary exercises the finalizer's exact 20-home
; local limit and one-more rejection.  It does not claim the separately
; required source-to-real-RA 20/21-home acceptance evidence.
; RUN: %python %t/make-extra-homes.py %t/i32.pre.mir %t/max-homes.mir 18
; RUN: %{brace-s3b4-llc} -start-before=brace-finalize-spill-homes \
; RUN:   -stop-after=brace-finalize-spill-homes %t/max-homes.mir \
; RUN:   -o %t/max-homes.post.mir
; RUN: FileCheck %s --check-prefix=MAX-HOMES < %t/max-homes.post.mir
; RUN: %{brace-s3b4-llc} -start-after=brace-finalize-spill-homes \
; RUN:   %t/max-homes.post.mir -o %t/max-homes.o
; RUN: llvm-readobj --sections %t/max-homes.o | \
; RUN:   FileCheck %s --check-prefix=MAX-OBJECT
; RUN: %python %t/make-extra-homes.py %t/i32.pre.mir %t/too-many.mir 19
; RUN: not --crash %{brace-s3b4-llc} -start-before=brace-finalize-spill-homes \
; RUN:   %t/too-many.mir -o %t/too-many.o 2>&1 | \
; RUN:   FileCheck %s --check-prefix=TOO-MANY
; RUN: test ! -s %t/too-many.o
;
; I32-PRE: maxAlignment:    4
; I32-PRE: stack:
; I32-PRE-COUNT-2: type: spill-slot
; I32-PRE-COUNT-2: SPILL_STORE32
; I32-PRE-COUNT-2: SPILL_LOAD32
; I32-PRE-NOT: HOME_
;
; I32-POST: maxAlignment:    4
; I32-POST: stack:           []
; I32-POST-NOT: %stack.
; I32-POST: HOME_SAVE32 1, killed $r4
; I32-POST: HOME_SAVE32 0, killed $r4
; I32-POST: $r4 = HOME_RESTORE32 1
; I32-POST: $r4 = HOME_RESTORE32 0
; I32-POST-NOT: SPILL_
;
; I8-PRE: maxAlignment:    1
; I8-PRE: stack:
; I8-PRE-COUNT-2: type: spill-slot
; I8-PRE-COUNT-2: SPILL_STORE8
; I8-PRE-COUNT-2: SPILL_LOAD8
; I8-PRE-NOT: HOME_
;
; I8-POST: maxAlignment:    1
; I8-POST: stack:           []
; I8-POST-NOT: %stack.
; I8-POST: HOME_SAVE8 1, killed $r2
; I8-POST: HOME_SAVE8 0, killed $r2
; I8-POST: $r2 = HOME_RESTORE8 1
; I8-POST: $r2 = HOME_RESTORE8 0
; I8-POST-NOT: SPILL_
;
; NONOVERLAP-UNCOLOR-SLOTS: 4
; NONOVERLAP-UNCOLOR-STORES: 4
; NONOVERLAP-UNCOLOR-LOADS: 4
; NONOVERLAP-COLOR-SLOTS: 2
; NONOVERLAP-COLOR-STORES: 4
; NONOVERLAP-COLOR-LOADS: 4
; NONOVERLAP-SHA: 9645bcd2312a6335cdc3b36866ba1fabf1f15d37711dc4485ecd91ec83b29a45
;
; PADDR-BEFORE: %[[A:[0-9]+]]:paddrregs = PADDR_IMM 2147484160
; PADDR-BEFORE: LOAD32 %[[A]]
; PADDR-BEFORE: %[[B:[0-9]+]]:paddrregs = PADDR_IMM 2147484164
; PADDR-BEFORE: LOAD32 %[[B]]
; PADDR-BEFORE: %[[C:[0-9]+]]:paddrregs = PADDR_IMM 2147484168
; PADDR-BEFORE: LOAD32 %[[C]]
; PADDR-BEFORE: LOAD32 %[[A]]
; PADDR-BEFORE: LOAD32 %[[B]]
; PADDR-BEFORE: LOAD32 %[[C]]
;
; PADDR-AFTER: stack:           []
; PADDR-AFTER: PADDR_IMM 2147484160
; PADDR-AFTER: PADDR_IMM 2147484164
; PADDR-AFTER: PADDR_IMM 2147484168
; PADDR-AFTER: PADDR_IMM 2147484160
; PADDR-AFTER: PADDR_IMM 2147484164
; PADDR-AFTER-NOT: SPILL_
; PADDR-AFTER-NOT: HOME_
;
; OBJECT-SIZE: 1096
; I32-SHA: 733dab9e0b7549349b88ecfb64b2975350df2c76428b71bfc2a936148d04b618
; I8-SHA: 36b9f4a31bdbc5d2a0ebf4e8a4799167d7e379361638dbb8be2e68d847065383
; OBJECT: Name: .brace.types
; OBJECT: Size: 8
; OBJECT: Name: .brace.literals
; OBJECT: Size: 48
; OBJECT: Name: .brace.text
; OBJECT: Size: 68
; OBJECT-COUNT-6: Relocation {
; I32-OBJECT: 07070000 02020202
; I8-OBJECT: 07070000 02020000
;
; OLD-PRESSURE: LLVM ERROR: brace64 S3b.3 leaf ABI register pressure would spill
; OLD-PADDR-PRESSURE: LLVM ERROR: brace64 S3b.3 leaf ABI register pressure would spill
; PADDR-SPILL: LLVM ERROR: brace64 S3b.4 spill-home ABI only admits i8/i32 spills
; NO-SAVE: LLVM ERROR: brace64 S3b.3 post-RA verifier: semantic home is restored before a save on every path
; NO-RESTORE: LLVM ERROR: brace64 S3b.3 post-RA verifier: every published semantic home requires both save and restore
; HOLE: LLVM ERROR: brace64 S3b.3 post-RA verifier: semantic-home declaration contains a hole
; TYPE: LLVM ERROR: brace64 S3b.3 post-RA verifier: semantic home is reused across value types
; RANGE: LLVM ERROR: brace64 S3b.3 post-RA verifier: semantic-home ordinal is outside 0..19
; NONSPILL: LLVM ERROR: brace64 S3b.4 spill-home finalizer: spill pseudo refers to a noncanonical spill frame index
; ALIASED-PARSER: error: YAML:
; ALIASED-PARSER-SAME: unknown key 'isAliased'
; CALLEE-SAVED-PRE: LLVM ERROR: brace64 S3b.4 spill-home finalizer: non-spill stack, frame, or call state is forbidden
; CALLEE-SAVED-POST: LLVM ERROR: brace64 S3b.4 post-home frame verifier: noncanonical pre-PEI frame state is forbidden
; CALL-FRAME-PRE: LLVM ERROR: brace64 S3b.4 spill-home finalizer: non-spill stack, frame, or call state is forbidden
; CALL-FRAME-POST: LLVM ERROR: brace64 S3b.4 post-home frame verifier: noncanonical pre-PEI frame state is forbidden
; MAX-HOMES: stack:           []
; MAX-HOMES: HOME_SAVE32 19, killed $r4
; MAX-HOMES: $r4 = HOME_RESTORE32 19
; MAX-HOMES-NOT: %stack.
; MAX-OBJECT: Name: .brace.types
; MAX-OBJECT-NEXT: Type: SHT_PROGBITS
; MAX-OBJECT: Size: 26
; TOO-MANY: LLVM ERROR: brace64 S3b.4 spill-home finalizer: typed spill-home count exceeds r6..r25

;--- i32.ll
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
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-home-r0"}

;--- nonoverlap.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %a0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483904 to ptr addrspace(200)), align 256
  %a1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483908 to ptr addrspace(200)), align 4
  %a2 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483912 to ptr addrspace(200)), align 8
  store volatile i32 %a0, ptr addrspace(200) inttoptr (i64 2147483920 to ptr addrspace(200)), align 16
  store volatile i32 %a1, ptr addrspace(200) inttoptr (i64 2147483924 to ptr addrspace(200)), align 4
  store volatile i32 %a2, ptr addrspace(200) inttoptr (i64 2147483928 to ptr addrspace(200)), align 8
  %b0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483936 to ptr addrspace(200)), align 32
  %b1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483940 to ptr addrspace(200)), align 4
  %b2 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483944 to ptr addrspace(200)), align 8
  store volatile i32 %b0, ptr addrspace(200) inttoptr (i64 2147483952 to ptr addrspace(200)), align 16
  store volatile i32 %b1, ptr addrspace(200) inttoptr (i64 2147483956 to ptr addrspace(200)), align 4
  store volatile i32 %b2, ptr addrspace(200) inttoptr (i64 2147483960 to ptr addrspace(200)), align 8
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-home-r0"}

;--- paddr-remat.ll
target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "brace64-unknown-none-elf"

define dso_local void @brace_system_entry() local_unnamed_addr #0 {
entry:
  %a0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147484160 to ptr addrspace(200)), align 512
  store volatile i32 %a0, ptr addrspace(200) inttoptr (i64 2147484176 to ptr addrspace(200)), align 16
  %b0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147484164 to ptr addrspace(200)), align 4
  store volatile i32 %b0, ptr addrspace(200) inttoptr (i64 2147484180 to ptr addrspace(200)), align 4
  %c0 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147484168 to ptr addrspace(200)), align 8
  store volatile i32 %c0, ptr addrspace(200) inttoptr (i64 2147484184 to ptr addrspace(200)), align 8
  %a1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147484160 to ptr addrspace(200)), align 512
  store volatile i32 %a1, ptr addrspace(200) inttoptr (i64 2147484188 to ptr addrspace(200)), align 4
  %b1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147484164 to ptr addrspace(200)), align 4
  store volatile i32 %b1, ptr addrspace(200) inttoptr (i64 2147484192 to ptr addrspace(200)), align 32
  %c1 = load volatile i32, ptr addrspace(200) inttoptr (i64 2147484168 to ptr addrspace(200)), align 8
  store volatile i32 %c1, ptr addrspace(200) inttoptr (i64 2147484196 to ptr addrspace(200)), align 4
  ret void
}

attributes #0 = { nofree norecurse nounwind memory(readwrite, target_mem0: none, target_mem1: none) "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
!llvm.module.flags = !{!0, !1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-home-r0"}

;--- make-paddr-spill.py
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
output = pathlib.Path(sys.argv[2])

if "%15" in source or "liveins:         []" not in source:
    raise SystemExit("unexpected pre-RA paddr MIR shape")

registers = "".join(
    "  - { id: %d, class: paddrregs, preferred-register: '', "
    "flags: [  ] }\n" % index
    for index in range(15, 18)
)
source = source.replace("liveins:         []\n", registers +
                        "liveins:         []\n", 1)

for old, new, address in ((0, 15, 2147484160),
                          (3, 16, 2147484164),
                          (6, 17, 2147484168)):
    original = "    %%%d:paddrregs = PADDR_IMM %d\n" % (old, address)
    replacement = ("    %%%d:paddrregs = PADDR_IMM %d\n" % (new, address) +
                   "    %%%d:paddrregs = COPY %%%d\n" % (old, new))
    if source.count(original) != 1:
        raise SystemExit("unexpected long-lived paddr definition")
    source = source.replace(original, replacement, 1)

output.write_text(source)

;--- check-paddr-remat.py
import pathlib
import sys

before = pathlib.Path(sys.argv[1]).read_text()
after = pathlib.Path(sys.argv[2]).read_text()

expected = {
    2147484160: (1, 2),
    2147484164: (1, 2),
    2147484168: (1, 1),
}
for address, counts in expected.items():
    needle = "PADDR_IMM %d" % address
    if (before.count(needle) != counts[0] or
            after.count(needle) != counts[1]):
        raise SystemExit("paddr rematerialization count mismatch")

for forbidden in ("type: spill-slot", "%stack.", "SPILL_", "HOME_"):
    if forbidden in after:
        raise SystemExit("paddr rematerialization left spill/home state")
if "stack:           []" not in after:
    raise SystemExit("paddr rematerialization did not retain an empty frame")

;--- make-extra-homes.py
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
output = pathlib.Path(sys.argv[2])
count = int(sys.argv[3])

objects = []
operations = []
for index in range(2, 2 + count):
    objects.append(
        "  - { id: %d, name: '', type: spill-slot, offset: 0, size: 4, "
        "alignment: 4,\n"
        "      stack-id: default, callee-saved-register: '', "
        "callee-saved-restored: true,\n"
        "      debug-info-variable: '', debug-info-expression: '', "
        "debug-info-location: '' }\n" % index
    )
    operations.append("    $r4 = CONST32 0\n")
    operations.append(
        "    SPILL_STORE32 killed $r4, %%stack.%d :: "
        "(store (s32) into %%stack.%d)\n" % (index, index)
    )
    operations.append(
        "    $r4 = SPILL_LOAD32 %%stack.%d :: "
        "(load (s32) from %%stack.%d)\n" % (index, index)
    )

source = source.replace("entry_values:    []\n", "".join(objects) +
                        "entry_values:    []\n")
source = source.replace("    RET\n...\n", "".join(operations) +
                        "    RET\n...\n")
output.write_text(source)

;--- check-alias-guards.py
import pathlib
import sys

instr_info = pathlib.Path(sys.argv[1]).read_text()
finalizer = pathlib.Path(sys.argv[2]).read_text()
publication = pathlib.Path(sys.argv[3]).read_text()
frame_lowering = pathlib.Path(sys.argv[4]).read_text()

expected = (
    (instr_info, "Frame.isAliasedObjectIndex(FrameIndex)", 1,
     "spill callback"),
    (finalizer, "Frame.isAliasedObjectIndex(FrameIndex)", 1,
     "spill transfer"),
    (finalizer, "Frame.isAliasedObjectIndex(FI)", 2,
     "pre/post object sweeps"),
    (publication, "Frame.isAliasedObjectIndex(FI)", 1,
     "late publication sweep"),
    (frame_lowering, "Frame.isAliasedObjectIndex(FI)", 1,
     "frame-lowering dead-object sweep"),
)
for source, needle, count, role in expected:
    if source.count(needle) != count:
        raise SystemExit("missing exact internal alias guard: " + role)

;--- check-target-owned-verifiers.py
import pathlib
import sys

target_machine = pathlib.Path(sys.argv[1]).read_text()
pipeline = pathlib.Path(sys.argv[2]).read_text()

source_sequence = (
    '"Before Brace S3b.4 spill-home finalization"',
    "createBraceFinalizeSpillHomesPass()",
    '"After Brace S3b.4 spill-home finalization"',
    "createBraceVerifyPostHomeFramePass()",
    '"Before Brace S3b.4 publication"',
    "createBraceFinalizeBranchesPass(getBraceTargetMachine())",
    '"After Brace S3b.4 publication"',
)
source_offsets = [target_machine.find(token) for token in source_sequence]
if any(offset < 0 for offset in source_offsets) or source_offsets != sorted(source_offsets):
    raise SystemExit("target-owned spill-home verifier source order drifted")
for token in source_sequence:
    if target_machine.count(token) != 1:
        raise SystemExit("target-owned spill-home verifier source count drifted")
if target_machine.count("createMachineVerifierPass(") != 4:
    raise SystemExit("target-owned source MachineVerifier count drifted")

argument_lines = [line for line in pipeline.splitlines()
                  if "Pass Arguments:" in line]
if len(argument_lines) != 1:
    raise SystemExit("missing unique legacy pass-argument sequence")
arguments = argument_lines[0]
argument_tokens = arguments.split()
if argument_tokens.count("-machineverifier") != 4:
    raise SystemExit("target-owned MachineVerifier count drifted")
argument_sequence = (
    "-machineverifier",
    "-brace-finalize-spill-homes",
    "-machineverifier",
    "-brace-verify-post-home-frame",
    "-machineverifier",
    "-brace-finalize-branches",
    "-machineverifier",
)
cursor = 0
for token in argument_sequence:
    while cursor < len(argument_tokens) and argument_tokens[cursor] != token:
        cursor += 1
    if cursor == len(argument_tokens):
        raise SystemExit("target-owned pass-argument order drifted")
    cursor += 1

lines = pipeline.splitlines()
named_sequence = (
    "Verify generated machine code",
    "Brace S3b.4 typed spill-home finalizer",
    "Verify generated machine code",
    "Brace S3b.4 post-home frame verifier",
    "Verify generated machine code",
    "Brace finalize branches and publication verifier",
    "Verify generated machine code",
    "Brace S3b.3 S2 Assembly Printer",
)
cursor = 0
for token in named_sequence:
    while cursor < len(lines) and token not in lines[cursor]:
        cursor += 1
    if cursor == len(lines):
        raise SystemExit("target-owned named pass order drifted")
    cursor += 1
if sum("Verify generated machine code" in line for line in lines) != 4:
    raise SystemExit("target-owned named MachineVerifier count drifted")

;--- i8.ll
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
!1 = !{i32 1, !"target-abi", !"brace-system-s2-leaf-home-r0"}
