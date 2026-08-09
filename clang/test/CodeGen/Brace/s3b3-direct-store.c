// REQUIRES: brace-registered-target
//
// The same source is compiled with both registered finisher values.  This
// fixture deliberately contains only the one physical store required by the
// direct-store corpus contract.
//
// DEFINE: %{brace-s3-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -nostdinc -Wall -Wextra -Wpedantic -Werror
//
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -S -emit-llvm %s -o - | \
// RUN:   FileCheck %s --check-prefixes=IR,IR-PASS
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -DBRACE_FINISH_VALUE=0x00013333U -S -emit-llvm %s -o - | \
// RUN:   FileCheck %s --check-prefixes=IR,IR-FAIL
//
// The driver's direct object path and the explicit bitcode-to-llc path must
// produce the same S2 object for each registered finisher result.
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c %s -o %t.pass.direct.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c %s -o %t.pass.direct-again.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c -emit-llvm %s -o %t.pass.bc
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
// RUN:   -filetype=obj %t.pass.bc -o %t.pass.llc.o
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
// RUN:   -filetype=obj %t.pass.bc -o %t.pass.llc-again.o
// RUN: cmp %t.pass.direct.o %t.pass.direct-again.o
// RUN: cmp %t.pass.direct.o %t.pass.llc.o
// RUN: cmp %t.pass.direct.o %t.pass.llc-again.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -DBRACE_FINISH_VALUE=0x00013333U -c %s -o %t.fail.direct.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -DBRACE_FINISH_VALUE=0x00013333U -c %s \
// RUN:   -o %t.fail.direct-again.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -DBRACE_FINISH_VALUE=0x00013333U -c -emit-llvm %s -o %t.fail.bc
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
// RUN:   -filetype=obj %t.fail.bc -o %t.fail.llc.o
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
// RUN:   -filetype=obj %t.fail.bc -o %t.fail.llc-again.o
// RUN: cmp %t.fail.direct.o %t.fail.direct-again.o
// RUN: cmp %t.fail.direct.o %t.fail.llc.o
// RUN: cmp %t.fail.direct.o %t.fail.llc-again.o
// RUN: not cmp %t.pass.direct.o %t.fail.direct.o
//
// Omitting the opt-in ABI must fail closed and publish no object.
// RUN: not %{brace-s3-cc} -c %s -o %t.default.o
// RUN: test ! -s %t.default.o

// IR-LABEL: define dso_local void @brace_system_entry() local_unnamed_addr
// IR-PASS: store volatile i32 21845, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
// IR-FAIL: store volatile i32 78643, ptr addrspace(200) inttoptr (i64 1048576 to ptr addrspace(200)), align 1048576
// IR-NEXT: ret void
// IR: !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define TEST_FINISHER ((brace_u64)0x00100000ULL)
#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))

#ifndef BRACE_FINISH_VALUE
#define BRACE_FINISH_VALUE 0x00005555U
#endif

void brace_system_entry(void) {
  PHYSICAL_U32(TEST_FINISHER) = (brace_u32)BRACE_FINISH_VALUE;
}
