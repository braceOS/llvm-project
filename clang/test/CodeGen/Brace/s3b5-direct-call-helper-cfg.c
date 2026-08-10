// REQUIRES: brace-registered-target
//
// Both helper arms perform different-width volatile stores and converge on a
// common physical result cell and valued Return. This keeps normalized
// frontend IR phi-free while making even/odd inputs return distinct values.
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
// RUN: %{brace-s3b5-cc} -S -emit-llvm %s -o - | \
// RUN:   FileCheck %s --check-prefix=IR
// RUN: %{brace-s3b5-cc} -c %s -o %t.o
// RUN: %{brace-s3b5-cc} -c %s -o %t.again.o
// RUN: %{brace-s3b5-cc} -c -emit-llvm %s -o %t.bc
// RUN: %{brace-s3b5-llc} %t.bc -o %t.llc.o
// RUN: %{brace-s3b5-llc} %t.bc -o %t.llc-again.o
// RUN: cmp %t.o %t.again.o
// RUN: cmp %t.o %t.llc.o
// RUN: cmp %t.o %t.llc-again.o
// RUN: %{brace-s3b5-llc} -stop-after=finalize-isel %t.bc -o %t.isel.mir
// RUN: FileCheck %s --check-prefix=ISEL < %t.isel.mir
// RUN: %{brace-s3b5-llc} -start-after=finalize-isel %t.isel.mir \
// RUN:   -o %t.isel.o
// RUN: cmp %t.o %t.isel.o
// RUN: %{brace-s3b5-llc} -stop-after=virtregrewriter %t.bc -o %t.ra.mir
// RUN: FileCheck %s --check-prefix=RA < %t.ra.mir
// RUN: %{brace-s3b5-llc} -start-after=virtregrewriter %t.ra.mir \
// RUN:   -o %t.ra.o
// RUN: cmp %t.o %t.ra.o
// RUN: %{brace-s3b5-llc} -stop-after=brace-finalize-branches %t.bc \
// RUN:   -o %t.final.mir
// RUN: FileCheck %s --check-prefix=FINAL < %t.final.mir
// RUN: %{brace-s3b5-llc} -start-after=brace-finalize-branches %t.final.mir \
// RUN:   -o %t.final.o
// RUN: cmp %t.o %t.final.o
// RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
// RUN: llvm-readobj --file-headers --sections --relocations %t.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
//
// IR-LABEL: define internal fastcc i32 @brace_system_call_leaf(i32 noundef
// IR: and i32
// IR: icmp eq i32
// IR: br i1
// IR: store volatile i32 287454020
// IR: store volatile i8 -35
// IR: load volatile i32
// IR: ret i32
// ISEL-LABEL: name: brace_system_entry
// ISEL: noVRegs: false
// ISEL: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// ISEL-LABEL: name: brace_system_call_leaf
// ISEL: noVRegs: false
// ISEL: successors: %bb.1(0x40000000), %bb.2(0x40000000)
// ISEL: liveins: $r4
// ISEL: BRCOND32 killed %2, %bb.2, 1
// ISEL: STORE32
// ISEL: STORE8
// ISEL: LOAD32
// ISEL: RET_I32 $r4
// RA-LABEL: name: brace_system_entry
// RA: noVRegs: true
// RA: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// RA-LABEL: name: brace_system_call_leaf
// RA: noVRegs: true
// RA: liveins: $r4
// RA: BRCOND32 killed $r4, %bb.2, 1
// RA: $r4 = CONST32 287454020
// RA: STORE32
// RA: $r2 = CONST8 -35
// RA: STORE8
// RA: $r4 = LOAD32
// RA: RET_I32 $r4
// FINAL-LABEL: name: brace_system_entry
// FINAL: noVRegs: true
// FINAL: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// FINAL-LABEL: name: brace_system_call_leaf
// FINAL: noVRegs: true
// FINAL: liveins: $r4
// FINAL: BR_IF32 killed $r4, %bb.2, %bb.1
// FINAL: $r4 = CONST32 287454020
// FINAL: STORE32
// FINAL: $r2 = CONST8 -35
// FINAL: STORE8
// FINAL: $r4 = LOAD32
// FINAL: RET_I32 $r4
// SIZE: 1360
// SHA: 28b01a5aedb7ad1baae190ce955ddbf98871fc02faa1a7ae0416405855ede67a
// OBJECT: Flags [ (0x42520200)
// OBJECT: SectionHeaderCount: 11
// OBJECT: Name: .brace.functions
// OBJECT: Size: 128
// OBJECT: Name: .brace.descriptors
// OBJECT: Size: 24

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef __UINT8_TYPE__ brace_u8;
typedef volatile brace_u8 __attribute__((address_space(200))) brace_paddr_u8;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define PHYSICAL_U8(address) (*(brace_paddr_u8 *)(brace_uptr)(address))
#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))

static __attribute__((noinline)) brace_u32
brace_system_call_leaf(brace_u32 value) {
  if ((value & 1U) == 0U)
    PHYSICAL_U32((brace_u64)0x80000014ULL) = 0x11223344U;
  else
    PHYSICAL_U8((brace_u64)0x80000014ULL) = 0xddU;
  return PHYSICAL_U32((brace_u64)0x80000014ULL);
}

void brace_system_entry(void) {
  brace_u32 input = PHYSICAL_U32((brace_u64)0x80000000ULL);
  brace_u32 result = brace_system_call_leaf(input);
  PHYSICAL_U32((brace_u64)0x80000004ULL) = result;
}
