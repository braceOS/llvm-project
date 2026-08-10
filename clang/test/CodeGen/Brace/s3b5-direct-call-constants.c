// REQUIRES: brace-registered-target
//
// Compile-time argument and mask variants must remain distinct while using the
// same private fastcc i32(i32) call ABI.  The volatile read keeps the entry's
// exact readwrite attribute tuple; the returned value is stored through the
// post-call rematerialized physical address.
//
// DEFINE: %{brace-s3b5-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -fno-optimize-sibling-calls \
// DEFINE:   -nostdinc -Wall -Wextra -Wpedantic -Werror \
// DEFINE:   -mabi=brace-system-s2-direct-call-r0
// DEFINE: %{brace-s3b5-llc} = llc -mtriple=brace64-unknown-none-elf \
// DEFINE:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
// DEFINE:   -verify-machineinstrs -filetype=obj
//
// RUN: %{brace-s3b5-cc} -S -emit-llvm %s -o %t.base.ll
// RUN: FileCheck %s --check-prefix=BASE-IR < %t.base.ll
// RUN: %{brace-s3b5-cc} -c %s -o %t.base.o
// RUN: %{brace-s3b5-cc} -c %s -o %t.base-again.o
// RUN: %{brace-s3b5-cc} -c -emit-llvm %s -o %t.base.bc
// RUN: %{brace-s3b5-llc} %t.base.bc -o %t.base-llc.o
// RUN: %{brace-s3b5-llc} %t.base.bc -o %t.base-llc-again.o
// RUN: cmp %t.base.o %t.base-again.o
// RUN: cmp %t.base.o %t.base-llc.o
// RUN: cmp %t.base.o %t.base-llc-again.o
//
// RUN: %{brace-s3b5-cc} -DBRACE_ARGUMENT_MASK=16777215U -S -emit-llvm \
// RUN:   %s -o %t.argument.ll
// RUN: FileCheck %s --check-prefix=ARGUMENT-IR < %t.argument.ll
// RUN: %{brace-s3b5-cc} -DBRACE_ARGUMENT_MASK=16777215U -c %s \
// RUN:   -o %t.argument.o
// RUN: %{brace-s3b5-cc} -DBRACE_ARGUMENT_MASK=16777215U -c %s \
// RUN:   -o %t.argument-again.o
// RUN: %{brace-s3b5-cc} -DBRACE_ARGUMENT_MASK=16777215U -c -emit-llvm %s \
// RUN:   -o %t.argument.bc
// RUN: %{brace-s3b5-llc} %t.argument.bc -o %t.argument-llc.o
// RUN: %{brace-s3b5-llc} %t.argument.bc -o %t.argument-llc-again.o
// RUN: cmp %t.argument.o %t.argument-again.o
// RUN: cmp %t.argument.o %t.argument-llc.o
// RUN: cmp %t.argument.o %t.argument-llc-again.o
//
// RUN: %{brace-s3b5-cc} -DBRACE_MASK=13U -S -emit-llvm %s \
// RUN:   -o %t.mask.ll
// RUN: FileCheck %s --check-prefix=MASK-IR < %t.mask.ll
// RUN: %{brace-s3b5-cc} -DBRACE_MASK=13U -c %s -o %t.mask.o
// RUN: %{brace-s3b5-cc} -DBRACE_MASK=13U -c %s -o %t.mask-again.o
// RUN: %{brace-s3b5-cc} -DBRACE_MASK=13U -c -emit-llvm %s -o %t.mask.bc
// RUN: %{brace-s3b5-llc} %t.mask.bc -o %t.mask-llc.o
// RUN: %{brace-s3b5-llc} %t.mask.bc -o %t.mask-llc-again.o
// RUN: cmp %t.mask.o %t.mask-again.o
// RUN: cmp %t.mask.o %t.mask-llc.o
// RUN: cmp %t.mask.o %t.mask-llc-again.o
// RUN: not cmp %t.base.o %t.argument.o
// RUN: not cmp %t.base.o %t.mask.o
// RUN: not cmp %t.argument.o %t.mask.o
// RUN: %{brace-s3b5-llc} -stop-after=finalize-isel %t.base.bc \
// RUN:   -o %t.base.isel.mir
// RUN: FileCheck %s --check-prefix=BASE-ISEL < %t.base.isel.mir
// RUN: %{brace-s3b5-llc} -start-after=finalize-isel %t.base.isel.mir \
// RUN:   -o %t.base.isel.o
// RUN: cmp %t.base.o %t.base.isel.o
// RUN: %{brace-s3b5-llc} -stop-after=virtregrewriter %t.base.bc \
// RUN:   -o %t.base.ra.mir
// RUN: FileCheck %s --check-prefix=BASE-POSTRA < %t.base.ra.mir
// RUN: %{brace-s3b5-llc} -start-after=virtregrewriter %t.base.ra.mir \
// RUN:   -o %t.base.ra.o
// RUN: cmp %t.base.o %t.base.ra.o
// RUN: %{brace-s3b5-llc} -stop-after=brace-finalize-branches %t.base.bc \
// RUN:   -o %t.base.final.mir
// RUN: FileCheck %s --check-prefix=BASE-POSTRA < %t.base.final.mir
// RUN: %{brace-s3b5-llc} -start-after=brace-finalize-branches \
// RUN:   %t.base.final.mir -o %t.base.final.o
// RUN: cmp %t.base.o %t.base.final.o
//
// RUN: %{brace-s3b5-llc} -stop-after=finalize-isel %t.argument.bc \
// RUN:   -o %t.argument.isel.mir
// RUN: FileCheck %s --check-prefix=ARGUMENT-ISEL < %t.argument.isel.mir
// RUN: %{brace-s3b5-llc} -start-after=finalize-isel \
// RUN:   %t.argument.isel.mir -o %t.argument.isel.o
// RUN: cmp %t.argument.o %t.argument.isel.o
// RUN: %{brace-s3b5-llc} -stop-after=virtregrewriter %t.argument.bc \
// RUN:   -o %t.argument.ra.mir
// RUN: FileCheck %s --check-prefix=ARGUMENT-POSTRA < %t.argument.ra.mir
// RUN: %{brace-s3b5-llc} -start-after=virtregrewriter \
// RUN:   %t.argument.ra.mir -o %t.argument.ra.o
// RUN: cmp %t.argument.o %t.argument.ra.o
// RUN: %{brace-s3b5-llc} -stop-after=brace-finalize-branches \
// RUN:   %t.argument.bc -o %t.argument.final.mir
// RUN: FileCheck %s --check-prefix=ARGUMENT-POSTRA < %t.argument.final.mir
// RUN: %{brace-s3b5-llc} -start-after=brace-finalize-branches \
// RUN:   %t.argument.final.mir -o %t.argument.final.o
// RUN: cmp %t.argument.o %t.argument.final.o
//
// RUN: %{brace-s3b5-llc} -stop-after=finalize-isel %t.mask.bc \
// RUN:   -o %t.mask.isel.mir
// RUN: FileCheck %s --check-prefix=MASK-ISEL < %t.mask.isel.mir
// RUN: %{brace-s3b5-llc} -start-after=finalize-isel %t.mask.isel.mir \
// RUN:   -o %t.mask.isel.o
// RUN: cmp %t.mask.o %t.mask.isel.o
// RUN: %{brace-s3b5-llc} -stop-after=virtregrewriter %t.mask.bc \
// RUN:   -o %t.mask.ra.mir
// RUN: FileCheck %s --check-prefix=MASK-POSTRA < %t.mask.ra.mir
// RUN: %{brace-s3b5-llc} -start-after=virtregrewriter %t.mask.ra.mir \
// RUN:   -o %t.mask.ra.o
// RUN: cmp %t.mask.o %t.mask.ra.o
// RUN: %{brace-s3b5-llc} -stop-after=brace-finalize-branches %t.mask.bc \
// RUN:   -o %t.mask.final.mir
// RUN: FileCheck %s --check-prefix=MASK-POSTRA < %t.mask.final.mir
// RUN: %{brace-s3b5-llc} -start-after=brace-finalize-branches \
// RUN:   %t.mask.final.mir -o %t.mask.final.o
// RUN: cmp %t.mask.o %t.mask.final.o
// RUN: wc -c < %t.base.o | FileCheck %s --check-prefix=SIZE
// RUN: wc -c < %t.argument.o | FileCheck %s --check-prefix=SIZE
// RUN: wc -c < %t.mask.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.base.o | FileCheck %s --check-prefix=BASE-SHA
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.argument.o | FileCheck %s --check-prefix=ARGUMENT-SHA
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.mask.o | FileCheck %s --check-prefix=MASK-SHA
// RUN: llvm-readobj --file-headers --sections %t.base.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
// RUN: llvm-readobj --file-headers --sections %t.argument.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
// RUN: llvm-readobj --file-headers --sections %t.mask.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
//
// SIZE: 1328
// BASE-SHA: 831cee82f5f7affaf82febf5a25d0bd8011bcdaa5cbbe50cf961507993377254
// ARGUMENT-SHA: cdd65c72d78ec522eaf0ed8ec9fedf57ee5673a22a6f959617a68d222a3053b2
// MASK-SHA: 5ac8e800919cd550c30fd36cf3768a94668748724f86e4763d87ef1a622f6951
// OBJECT: Flags [ (0x42520200)
// OBJECT: SectionHeaderCount: 11
//
// BASE-IR-LABEL: define dso_local void @brace_system_entry()
// BASE-IR: and i32 %{{.*}}, 268435455
// BASE-IR: call fastcc i32 @brace_system_call_leaf
// BASE-IR-LABEL: define internal fastcc range(i32 0, 16) i32 @brace_system_call_leaf(i32 noundef range(i32 0, 268435456)
// BASE-IR: and i32 %{{.*}}, 15
// BASE-IR: ret i32
// ARGUMENT-IR-LABEL: define dso_local void @brace_system_entry()
// ARGUMENT-IR: and i32 %{{.*}}, 16777215
// ARGUMENT-IR: call fastcc i32 @brace_system_call_leaf
// ARGUMENT-IR-LABEL: define internal fastcc range(i32 0, 16) i32 @brace_system_call_leaf(i32 noundef range(i32 0, 16777216)
// ARGUMENT-IR: and i32 %{{.*}}, 15
// ARGUMENT-IR: ret i32
// MASK-IR-LABEL: define dso_local void @brace_system_entry()
// MASK-IR: and i32 %{{.*}}, 268435455
// MASK-IR: call fastcc i32 @brace_system_call_leaf
// MASK-IR-LABEL: define internal fastcc range(i32 0, 14) i32 @brace_system_call_leaf(i32 noundef range(i32 0, 268435456)
// MASK-IR: and i32 %{{.*}}, 13
// MASK-IR: ret i32
//
// BASE-ISEL-LABEL: name: brace_system_entry
// BASE-ISEL: noVRegs: false
// BASE-ISEL: %2:i32regs = CONST32 268435455
// BASE-ISEL: $r4 = COPY %3
// BASE-ISEL-NEXT: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// BASE-ISEL-LABEL: name: brace_system_call_leaf
// BASE-ISEL: noVRegs: false
// BASE-ISEL: liveins: $r4
// BASE-ISEL: %4:i32regs = CONST32 15
// BASE-ISEL: RET_I32 $r4
// BASE-POSTRA-LABEL: name: brace_system_entry
// BASE-POSTRA: noVRegs: true
// BASE-POSTRA: $r5 = CONST32 268435455
// BASE-POSTRA: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// BASE-POSTRA-LABEL: name: brace_system_call_leaf
// BASE-POSTRA: noVRegs: true
// BASE-POSTRA: liveins: $r4
// BASE-POSTRA: $r5 = CONST32 15
// BASE-POSTRA: RET_I32 $r4
//
// ARGUMENT-ISEL-LABEL: name: brace_system_entry
// ARGUMENT-ISEL: noVRegs: false
// ARGUMENT-ISEL: %2:i32regs = CONST32 16777215
// ARGUMENT-ISEL: $r4 = COPY %3
// ARGUMENT-ISEL-NEXT: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// ARGUMENT-ISEL-LABEL: name: brace_system_call_leaf
// ARGUMENT-ISEL: noVRegs: false
// ARGUMENT-ISEL: liveins: $r4
// ARGUMENT-ISEL: %4:i32regs = CONST32 15
// ARGUMENT-ISEL: RET_I32 $r4
// ARGUMENT-POSTRA-LABEL: name: brace_system_entry
// ARGUMENT-POSTRA: noVRegs: true
// ARGUMENT-POSTRA: $r5 = CONST32 16777215
// ARGUMENT-POSTRA: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// ARGUMENT-POSTRA-LABEL: name: brace_system_call_leaf
// ARGUMENT-POSTRA: noVRegs: true
// ARGUMENT-POSTRA: liveins: $r4
// ARGUMENT-POSTRA: $r5 = CONST32 15
// ARGUMENT-POSTRA: RET_I32 $r4
//
// MASK-ISEL-LABEL: name: brace_system_entry
// MASK-ISEL: noVRegs: false
// MASK-ISEL: %2:i32regs = CONST32 268435455
// MASK-ISEL: $r4 = COPY %3
// MASK-ISEL-NEXT: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// MASK-ISEL-LABEL: name: brace_system_call_leaf
// MASK-ISEL: noVRegs: false
// MASK-ISEL: liveins: $r4
// MASK-ISEL: %4:i32regs = CONST32 13
// MASK-ISEL: RET_I32 $r4
// MASK-POSTRA-LABEL: name: brace_system_entry
// MASK-POSTRA: noVRegs: true
// MASK-POSTRA: $r5 = CONST32 268435455
// MASK-POSTRA: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// MASK-POSTRA-LABEL: name: brace_system_call_leaf
// MASK-POSTRA: noVRegs: true
// MASK-POSTRA: liveins: $r4
// MASK-POSTRA: $r5 = CONST32 13
// MASK-POSTRA: RET_I32 $r4

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))
#ifndef BRACE_ARGUMENT_MASK
#define BRACE_ARGUMENT_MASK 268435455U
#endif
#ifndef BRACE_MASK
#define BRACE_MASK 15U
#endif

static __attribute__((noinline)) brace_u32
brace_system_call_leaf(brace_u32 value) {
  return (value & BRACE_MASK) &
         PHYSICAL_U32((brace_u64)0x80000008ULL);
}

void brace_system_entry(void) {
  brace_u32 input = PHYSICAL_U32((brace_u64)0x80000000ULL);
  brace_u32 result = brace_system_call_leaf(input & BRACE_ARGUMENT_MASK);
  PHYSICAL_U32((brace_u64)0x80000004ULL) = result;
}
