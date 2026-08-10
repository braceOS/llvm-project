// REQUIRES: brace-registered-target
//
// DEFINE: %{brace-s3b5-base-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -fno-optimize-sibling-calls \
// DEFINE:   -nostdinc -Wall -Wextra -Wpedantic -Werror
// DEFINE: %{brace-s3b5-cc} = %{brace-s3b5-base-cc} \
// DEFINE:   -mabi=brace-system-s2-direct-call-r0
//
// RUN: %{brace-s3b5-cc} -S -emit-llvm %s -o - | \
// RUN:   FileCheck %s --check-prefix=IR
// RUN: %{brace-s3b5-cc} -c %s -o %t.direct.o
// RUN: %{brace-s3b5-cc} -c %s -o %t.direct-again.o
// RUN: %{brace-s3b5-cc} -c -emit-llvm %s -o %t.bc
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
// RUN:   -verify-machineinstrs -filetype=obj %t.bc -o %t.llc.o
// RUN: llc -mtriple=brace64-unknown-none-elf \
// RUN:   -target-abi=brace-system-s2-direct-call-r0 -O1 \
// RUN:   -verify-machineinstrs -filetype=obj %t.bc -o %t.llc-again.o
// RUN: cmp %t.direct.o %t.direct-again.o
// RUN: cmp %t.direct.o %t.llc.o
// RUN: cmp %t.direct.o %t.llc-again.o
// RUN: rm -f %t.lto.o
// RUN: not %{brace-s3b5-cc} -flto -c %s -o %t.lto.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=LTO-REJECT
// RUN: test ! -e %t.lto.o
// RUN: rm -f %t.mllvm.o
// RUN: not %{brace-s3b5-cc} -mllvm -verify-machineinstrs -c %s \
// RUN:   -o %t.mllvm.o 2>&1 | FileCheck %s --check-prefix=MLLVM-REJECT
// RUN: test ! -e %t.mllvm.o
// RUN: rm -f %t.exceptions.o
// RUN: not %{brace-s3b5-cc} -fexceptions -c %s -o %t.exceptions.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=EXCEPTIONS-REJECT
// RUN: test ! -e %t.exceptions.o
// RUN: rm -f %t.xclang-missing.o
// RUN: not %{brace-s3b5-base-cc} -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 -c %s \
// RUN:   -o %t.xclang-missing.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-missing.o
// RUN: rm -f %t.xclang-old.o
// RUN: not %{brace-s3b5-base-cc} -mabi=brace-system-s2-leaf-r0 \
// RUN:   -Xclang -target-abi -Xclang brace-system-s2-direct-call-r0 \
// RUN:   -c %s -o %t.xclang-old.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-old.o
// RUN: rm -f %t.xclang-direct-override.o
// RUN: not %{brace-s3b5-cc} -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-leaf-r0 -c %s \
// RUN:   -o %t.xclang-direct-override.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-direct-override.o
// RUN: rm -f %t.xclang-direct-duplicate.o
// RUN: not %{brace-s3b5-cc} -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 -c %s \
// RUN:   -o %t.xclang-direct-duplicate.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-direct-duplicate.o
// RUN: rm -f %t.xclang-mllvm.o
// RUN: not %{brace-s3b5-base-cc} -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 \
// RUN:   -mllvm -verify-machineinstrs -c %s -o %t.xclang-mllvm.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-mllvm.o
// RUN: rm -f %t.xclang-exceptions.o
// RUN: not %{brace-s3b5-base-cc} -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 -fexceptions -c %s \
// RUN:   -o %t.xclang-exceptions.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-exceptions.o
// RUN: %python -c "import sys; open(sys.argv[1], 'w').write('-Xclang\\n-target-abi\\n-Xclang\\nbrace-system-s2-direct-call-r0\\n')" %t.xclang.rsp
// RUN: rm -f %t.xclang-response.o
// RUN: not %{brace-s3b5-base-cc} @%t.xclang.rsp -c %s \
// RUN:   -o %t.xclang-response.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=XCLANG-TARGET-ABI-REJECT
// RUN: test ! -e %t.xclang-response.o
// RUN: wc -c < %t.direct.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.direct.o | FileCheck %s --check-prefix=SHA
// RUN: llvm-readobj --file-headers --sections --symbols --relocations \
// RUN:   --expand-relocs %t.direct.o | FileCheck %s --check-prefix=OBJECT
// RUN: llvm-readobj --sections %t.direct.o | \
// RUN:   FileCheck %s --check-prefix=SCNT
//
// IR-LABEL: define dso_local void @brace_system_entry()
// IR: call fastcc i32 @brace_system_call_leaf
// IR: store volatile i32
// IR: ret void
// IR-LABEL: define internal fastcc i32 @brace_system_call_leaf(i32 noundef
// IR: load volatile i32
// IR: and i32
// IR: ret i32
// IR: !{i32 1, !"target-abi", !"brace-system-s2-direct-call-r0"}
//
// SIZE: 1304
// SHA: 638333df950316ee0992d17cc787fc65a90b2803cccbbfcca1d47dc4a535ba9d
// LTO-REJECT: error: unsupported option '-flto' for target 'brace64-unknown-none-elf'
// MLLVM-REJECT: error: unsupported option '-mllvm -verify-machineinstrs' for target 'brace64-unknown-none-elf'
// EXCEPTIONS-REJECT: error: unsupported option '-fexceptions' for target 'brace64-unknown-none-elf'
// XCLANG-TARGET-ABI-REJECT: error: unsupported option '-Xclang -target-abi' for target 'brace64-unknown-none-elf'
// OBJECT: OS/ABI: Standalone (0xFF)
// OBJECT: Type: Relocatable (0x1)
// OBJECT: Machine: 0xFFB0
// OBJECT: Flags [ (0x42520200)
// OBJECT: SectionHeaderCount: 11
// OBJECT: StringTableSectionIndex: 10
// OBJECT: Sections [
// OBJECT: Name: .brace.functions
// OBJECT: Size: 128
// OBJECT: Name: .brace.types
// OBJECT: Size: 12
// OBJECT: Name: .brace.descriptors
// OBJECT: Size: 24
// OBJECT: Name: .brace.text
// OBJECT: Size: 44
// OBJECT: Relocations [
// OBJECT: Section (7) .rela.brace.literals {
// OBJECT-COUNT-3: Type: Unknown (1)
// OBJECT: Symbols [
// OBJECT-COUNT-2: Symbol {
// SCNT: Sections [
// SCNT-COUNT-11: Section {

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
  PHYSICAL_U32((brace_u64)0x80000004ULL) = result;
}
