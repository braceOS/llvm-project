// REQUIRES: brace-registered-target
//
// The entry load and continuation store stay inside the registered RAM window,
// while the helper's load deliberately names an unmapped physical address.
// This gives the independent runtimes a real compiler-produced callee-fault
// object without changing the call shape or relying on a malformed image.
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
// RUN: FileCheck %s --check-prefix=POSTRA < %t.ra.mir
// RUN: %{brace-s3b5-llc} -start-after=virtregrewriter %t.ra.mir \
// RUN:   -o %t.ra.o
// RUN: cmp %t.o %t.ra.o
// RUN: %{brace-s3b5-llc} -stop-after=brace-finalize-branches %t.bc \
// RUN:   -o %t.final.mir
// RUN: FileCheck %s --check-prefix=POSTRA < %t.final.mir
// RUN: %{brace-s3b5-llc} -start-after=brace-finalize-branches %t.final.mir \
// RUN:   -o %t.final.o
// RUN: cmp %t.o %t.final.o
// RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
// RUN: llvm-readobj --file-headers --sections --relocations %t.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
//
// IR-LABEL: define dso_local void @brace_system_entry()
// IR: load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200))
// IR: call fastcc i32 @brace_system_call_leaf
// IR: store volatile i32
// IR-LABEL: define internal fastcc i32 @brace_system_call_leaf(i32 noundef
// IR: load volatile i32, ptr addrspace(200) inttoptr (i64 2415919104 to ptr addrspace(200))
// IR: ret i32
// ISEL-LABEL: name: brace_system_entry
// ISEL: noVRegs: false
// ISEL: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// ISEL-LABEL: name: brace_system_call_leaf
// ISEL: noVRegs: false
// ISEL: liveins: $r4
// ISEL: PADDR_IMM 2415919104
// ISEL: RET_I32 $r4
// POSTRA-LABEL: name: brace_system_entry
// POSTRA: noVRegs: true
// POSTRA: CALL_I32 @brace_system_call_leaf, csr_noregs, implicit-def $r4, implicit $r4
// POSTRA-LABEL: name: brace_system_call_leaf
// POSTRA: noVRegs: true
// POSTRA: liveins: $r4
// POSTRA: $r0 = PADDR_IMM 2415919104
// POSTRA: RET_I32 $r4
// SIZE: 1304
// SHA: a7e482504a419e3887b68454ff1aea5aebf4f82531192e6cba3423b922273a28
// OBJECT: Flags [ (0x42520200)
// OBJECT: SectionHeaderCount: 11
// OBJECT: Name: .brace.functions
// OBJECT: Size: 128
// OBJECT: Name: .brace.descriptors
// OBJECT: Size: 24

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))

static __attribute__((noinline)) brace_u32
brace_system_call_leaf(brace_u32 value) {
  return value & PHYSICAL_U32((brace_u64)0x90000000ULL);
}

void brace_system_entry(void) {
  brace_u32 input = PHYSICAL_U32((brace_u64)0x80000000ULL);
  brace_u32 result = brace_system_call_leaf(input);
  PHYSICAL_U32((brace_u64)0x80000004ULL) = result;
}
