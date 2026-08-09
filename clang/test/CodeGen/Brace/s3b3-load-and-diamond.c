// REQUIRES: brace-registered-target
//
// Distinct addresses in the two arms prevent LLVM from replacing the diamond
// with one unconditional store plus a selected value.
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
// IR: [[INPUT:%.*]] = load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648 to ptr addrspace(200)), align 2147483648
// IR-NEXT: [[MASKED:%.*]] = and i32 [[INPUT]], -2147483647
// IR-NEXT: [[ZERO:%.*]] = icmp eq i32 [[MASKED]], 0
// IR-NEXT: br i1 [[ZERO]], label %if.then, label %if.else
// IR: if.then:
// IR: store volatile i32 1515847680, ptr addrspace(200) inttoptr (i64 2147483652 to ptr addrspace(200)), align 4
// IR-NEXT: br label %if.end
// IR: if.else:
// IR: store volatile i32 -1515913215, ptr addrspace(200) inttoptr (i64 2147483656 to ptr addrspace(200)), align 8
// IR-NEXT: br label %if.end
// IR: if.end:
// IR-NEXT: ret void
// IR: !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define RAM_INPUT ((brace_u64)0x80000000ULL)
#define RAM_ZERO_MARKER ((brace_u64)0x80000004ULL)
#define RAM_NONZERO_MARKER ((brace_u64)0x80000008ULL)
#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))

void brace_system_entry(void) {
  brace_u32 masked =
      PHYSICAL_U32(RAM_INPUT) & (brace_u32)0x80000001U;
  if (masked == (brace_u32)0U)
    PHYSICAL_U32(RAM_ZERO_MARKER) = (brace_u32)0x5a5a0000U;
  else
    PHYSICAL_U32(RAM_NONZERO_MARKER) = (brace_u32)0xa5a50001U;
}
