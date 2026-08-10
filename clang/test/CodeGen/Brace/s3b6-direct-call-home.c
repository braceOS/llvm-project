// REQUIRES: brace-registered-target
//
// Compile one C value that is genuinely live across the private direct call.
// Greedy RA must spill it through the S3b.6 activation home; this checkpoint
// deliberately does not claim a Guest stack pointer or byte-addressed frame.
//
// DEFINE: %{brace-s3b6-base-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -fno-optimize-sibling-calls \
// DEFINE:   -nostdinc -Wall -Wextra -Wpedantic -Werror
// DEFINE: %{brace-s3b6-cc} = %{brace-s3b6-base-cc} \
// DEFINE:   -mabi=brace-system-s2-direct-call-home-r0
//
// RUN: %{brace-s3b6-cc} -S -emit-llvm %s -o - | \
// RUN:   FileCheck %s --check-prefix=IR
// RUN: %{brace-s3b6-cc} -c %s -o %t.home.o
// RUN: %{brace-s3b6-cc} -c %s -o %t.home-again.o
// RUN: cmp %t.home.o %t.home-again.o
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.home.o | FileCheck %s --check-prefix=SHA
// RUN: %{brace-s3b6-cc} -c -emit-llvm %s -o %t.bc
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-direct-call-home-r0 -O1 \
// RUN:   -verify-machineinstrs -filetype=obj %t.bc -o %t.llc.o
// RUN: cmp %t.home.o %t.llc.o
// RUN: wc -c < %t.home.o | FileCheck %s --check-prefix=SIZE
// RUN: llvm-readobj --file-headers --sections %t.home.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
//
// The frozen no-home selector must continue to reject this same C-generated
// module before it publishes an object.
// RUN: rm -f %t.old.o
// RUN: not %{brace-s3b6-base-cc} \
// RUN:   -mabi=brace-system-s2-direct-call-r0 -c %s -o %t.old.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=OLD-LIVE
// RUN: test ! -s %t.old.o
//
// The new direct-call sibling inherits the fail-closed driver surface.
// RUN: rm -f %t.lto.o
// RUN: not %{brace-s3b6-cc} -flto -c %s -o %t.lto.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=LTO-REJECT
// RUN: test ! -e %t.lto.o
// RUN: rm -f %t.mllvm.o
// RUN: not %{brace-s3b6-cc} -mllvm -verify-machineinstrs -c %s \
// RUN:   -o %t.mllvm.o 2>&1 | FileCheck %s --check-prefix=MLLVM-REJECT
// RUN: test ! -e %t.mllvm.o
//
// IR-LABEL: define dso_local void @brace_system_entry()
// IR: %[[INPUT:[-a-zA-Z$._0-9]+]] = load volatile i32
// IR: %[[RESULT:[-a-zA-Z$._0-9]+]] = call fastcc i32 @brace_system_call_leaf(i32 noundef %[[INPUT]])
// IR: and i32 %[[RESULT]], %[[INPUT]]
// IR: store volatile i32
// IR: ret void
// IR-LABEL: define internal fastcc i32 @brace_system_call_leaf(i32 noundef
// IR: load volatile i32
// IR: and i32
// IR: ret i32
// IR: !{i32 1, !"target-abi", !"brace-system-s2-direct-call-home-r0"}
//
// SIZE: 1312
// SHA: b59866691d4a81ea304b3d8ba5c3a549c33b0022861937806024e5e9fca1d59a
// OBJECT: Flags [ (0x42520300)
// OBJECT: SectionHeaderCount: 11
// OBJECT: StringTableSectionIndex: 10
// OBJECT: Name: .brace.functions
// OBJECT: Size: 128
// OBJECT: Name: .brace.types
// OBJECT: Size: 13
// OBJECT: Name: .brace.descriptors
// OBJECT: Size: 24
// OBJECT: Name: .brace.text
// OBJECT: Size: 56
// OLD-LIVE: brace64 S3b.5 direct-call ABI: caller SSA value remains live across the call
// LTO-REJECT: error: unsupported option '-flto' for target 'brace64-unknown-none-elf'
// MLLVM-REJECT: error: unsupported option '-mllvm -verify-machineinstrs' for target 'brace64-unknown-none-elf'

typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u32 __attribute__((address_space(200)))
    brace_paddr_u32;

#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))

static __attribute__((noinline)) brace_u32
brace_system_call_leaf(brace_u32 value) {
  return value & PHYSICAL_U32((brace_u64)0x80000008ULL);
}

void brace_system_entry(void) {
  brace_u32 input = PHYSICAL_U32((brace_u64)0x80000000ULL);
  brace_u32 result = brace_system_call_leaf(input);
  PHYSICAL_U32((brace_u64)0x80000004ULL) = result & input;
}
