// REQUIRES: brace-registered-target
//
// DEFINE: %{brace-s3b4-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -nostdinc -Wall -Wextra -Wpedantic -Werror
//
// RUN: %{brace-s3b4-cc} -mabi=brace-system-s2-leaf-home-r0 \
// RUN:   -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR
// RUN: %{brace-s3b4-cc} -mabi=brace-system-s2-leaf-home-r0 \
// RUN:   -c %s -o %t.direct.o
// RUN: %{brace-s3b4-cc} -mabi=brace-system-s2-leaf-home-r0 \
// RUN:   -c %s -o %t.direct-again.o
// RUN: %{brace-s3b4-cc} -mabi=brace-system-s2-leaf-home-r0 \
// RUN:   -c -emit-llvm %s -o %t.bc
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-leaf-home-r0 -O1 \
// RUN:   -verify-machineinstrs -filetype=obj %t.bc -o %t.llc.o
// RUN: cmp %t.direct.o %t.direct-again.o
// RUN: cmp %t.direct.o %t.llc.o
// RUN: wc -c < %t.direct.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.direct.o | FileCheck %s --check-prefix=SHA
//
// The old ABI continues to reject this source at the real RA spill callback.
// RUN: not %{brace-s3b4-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -c %s -o %t.old.o
// RUN: test ! -s %t.old.o

// IR-LABEL: define dso_local void @brace_system_entry() local_unnamed_addr
// IR: load volatile i32, ptr addrspace(200) inttoptr (i64 2147483648
// IR: load volatile i32, ptr addrspace(200) inttoptr (i64 2147483652
// IR: load volatile i32, ptr addrspace(200) inttoptr (i64 2147483656
// IR: store volatile i32 %{{.*}}, ptr addrspace(200) inttoptr (i64 2147483660
// IR: store volatile i32 %{{.*}}, ptr addrspace(200) inttoptr (i64 2147483664
// IR: store volatile i32 %{{.*}}, ptr addrspace(200) inttoptr (i64 2147483668
// IR: !{i32 1, !"target-abi", !"brace-system-s2-leaf-home-r0"}
// SIZE: 1096
// SHA: 733dab9e0b7549349b88ecfb64b2975350df2c76428b71bfc2a936148d04b618

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))

void brace_system_entry(void) {
  const brace_u32 a = PHYSICAL_U32((brace_u64)0x80000000ULL);
  const brace_u32 b = PHYSICAL_U32((brace_u64)0x80000004ULL);
  const brace_u32 c = PHYSICAL_U32((brace_u64)0x80000008ULL);
  PHYSICAL_U32((brace_u64)0x8000000cULL) = a;
  PHYSICAL_U32((brace_u64)0x80000010ULL) = b;
  PHYSICAL_U32((brace_u64)0x80000014ULL) = c;
}
