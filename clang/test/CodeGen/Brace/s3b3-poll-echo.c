// REQUIRES: brace-registered-target
//
// The conformance machine reports UART ready on its first observation, but
// the object must still encode the status-poll loop backedge.
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
// RUN:   -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c %s -o %t.direct.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c %s -o %t.direct-again.o
// RUN: %{brace-s3-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c -emit-llvm %s -o %t.bc
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
// RUN:   -filetype=obj %t.bc -o %t.llc.o
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-r0 -O1 -verify-machineinstrs \
// RUN:   -filetype=obj %t.bc -o %t.llc-again.o
// RUN: cmp %t.direct.o %t.direct-again.o
// RUN: cmp %t.direct.o %t.llc.o
// RUN: cmp %t.direct.o %t.llc-again.o
// RUN: not %{brace-s3-cc} -c %s -o %t.default.o
// RUN: test ! -s %t.default.o

// IR-LABEL: define dso_local void @brace_system_entry() local_unnamed_addr
// IR: entry:
// IR-NEXT: br label %while.cond
// IR: while.cond:
// IR: [[STATUS:%.*]] = load volatile i8, ptr addrspace(200) inttoptr (i64 268435461 to ptr addrspace(200)), align 1
// IR-NEXT: [[MASKED:%.*]] = and i8 [[STATUS]], 32
// IR-NEXT: [[WAIT:%.*]] = icmp eq i8 [[MASKED]], 0
// IR-NEXT: br i1 [[WAIT]], label %while.cond, label %while.end
// IR: while.end:
// IR: [[BYTE:%.*]] = load volatile i8, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
// IR-NEXT: store volatile i8 [[BYTE]], ptr addrspace(200) inttoptr (i64 268435456 to ptr addrspace(200)), align 268435456
// IR-NEXT: ret void
// IR: !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

typedef __UINT8_TYPE__ brace_u8;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u8 __attribute__((address_space(200))) brace_paddr_u8;

#define RAM_INPUT ((brace_u64)0x80000000ULL)
#define UART_TX ((brace_u64)0x10000000ULL)
#define UART_LINE_STATUS ((brace_u64)0x10000005ULL)
#define PHYSICAL_U8(address) (*(brace_paddr_u8 *)(brace_uptr)(address))

void brace_system_entry(void) {
  while ((PHYSICAL_U8(UART_LINE_STATUS) & (brace_u8)0x20U) ==
         (brace_u8)0U) {
  }
  PHYSICAL_U8(UART_TX) = PHYSICAL_U8(RAM_INPUT);
}
