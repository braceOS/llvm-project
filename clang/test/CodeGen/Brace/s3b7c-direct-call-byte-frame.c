// REQUIRES: brace-registered-target
//
// Compile the canonical BF1 C source directly through Clang and prove that
// its emitted bitcode and direct object are identical to the llc path.
//
// DEFINE: %{brace-s3b7c-base-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -fno-optimize-sibling-calls -nostdinc \
// DEFINE:   -Wall -Wextra -Wpedantic -Werror
// DEFINE: %{brace-s3b7c-cc} = %{brace-s3b7c-base-cc} \
// DEFINE:   -mabi=brace-system-s2-direct-call-byte-frame-r0
// DEFINE: %{brace-s3b7c-llc} = llc -mtriple=brace64-unknown-none-elf \
// DEFINE:   -target-abi=brace-system-s2-direct-call-byte-frame-r0 -O1 \
// DEFINE:   -verify-machineinstrs -filetype=obj
//
// RUN: %{brace-s3b7c-cc} -S -emit-llvm %s -o - | \
// RUN:   FileCheck %s --check-prefix=IR
// RUN: %{brace-s3b7c-cc} -c %s -o %t.o
// RUN: %{brace-s3b7c-cc} -c %s -o %t.again.o
// RUN: cmp %t.o %t.again.o
// RUN: %{brace-s3b7c-cc} -c -emit-llvm %s -o %t.bc
// RUN: %{brace-s3b7c-llc} %t.bc -o %t.llc.o
// RUN: cmp %t.o %t.llc.o
// RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
// RUN: llvm-readobj --file-headers --sections %t.o | \
// RUN:   FileCheck %s --check-prefix=OBJECT
// RUN: %python -c "import struct,sys; d=open(sys.argv[1],'rb').read(); assert struct.unpack_from('<I',d,108)[0]==16 and struct.unpack_from('<I',d,172)[0]==0; w=struct.unpack_from('<16I',d,0x120); assert [((x>>2)&63) for x in w]==[25,22,23,27,16,26,4,22,24,28,20,22,23,4,4,20]" %t.o
//
// The independent Clang producer's bitcode must survive all five llc restart
// seams and reproduce the direct-Clang bytes.
// RUN: %{brace-s3b7c-llc} -stop-after=finalize-isel %t.bc -o %t.c-isel.mir
// RUN: FileCheck %s --check-prefix=C-ISEL < %t.c-isel.mir
// RUN: %{brace-s3b7c-llc} -start-after=finalize-isel %t.c-isel.mir -o %t.c-from-isel.o
// RUN: cmp %t.o %t.c-from-isel.o
// RUN: %{brace-s3b7c-llc} -stop-after=virtregrewriter %t.bc -o %t.c-ra.mir
// RUN: FileCheck %s --check-prefix=C-RA < %t.c-ra.mir
// RUN: %{brace-s3b7c-llc} -start-after=virtregrewriter %t.c-ra.mir -o %t.c-from-ra.o
// RUN: cmp %t.o %t.c-from-ra.o
// RUN: %{brace-s3b7c-llc} -stop-after=stack-slot-coloring %t.bc -o %t.c-color.mir
// RUN: FileCheck %s --check-prefix=C-COLOR < %t.c-color.mir
// RUN: %{brace-s3b7c-llc} -start-after=stack-slot-coloring %t.c-color.mir -o %t.c-from-color.o
// RUN: cmp %t.o %t.c-from-color.o
// RUN: %{brace-s3b7c-llc} -stop-after=brace-finalize-byte-frame %t.bc -o %t.c-frame.mir
// RUN: FileCheck %s --check-prefix=C-FRAME < %t.c-frame.mir
// RUN: %{brace-s3b7c-llc} -start-after=brace-finalize-byte-frame %t.c-frame.mir -o %t.c-from-frame.o
// RUN: cmp %t.o %t.c-from-frame.o
// RUN: %{brace-s3b7c-llc} -stop-after=brace-finalize-branches %t.bc -o %t.c-final.mir
// RUN: FileCheck %s --check-prefix=C-FINAL < %t.c-final.mir
// RUN: %{brace-s3b7c-llc} -start-after=brace-finalize-branches %t.c-final.mir -o %t.c-from-final.o
// RUN: cmp %t.o %t.c-from-final.o
//
// The sibling inherits the direct-profile fail-closed driver surface.
// RUN: rm -f %t.lto.o
// RUN: not %{brace-s3b7c-cc} -flto -c %s -o %t.lto.o 2>&1 | \
// RUN:   FileCheck %s --check-prefix=LTO-REJECT
// RUN: test ! -e %t.lto.o
// RUN: rm -f %t.mllvm.o
// RUN: not %{brace-s3b7c-cc} -mllvm -verify-machineinstrs -c %s \
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
// IR: !{i32 1, !"target-abi", !"brace-system-s2-direct-call-byte-frame-r0"}
//
// SIZE: 1320
// SHA: cb09ac307141381c709d1504bab2f74b274a0b50e42b20cad593f9bf2846fb55
// OBJECT: Flags [ (0x42520400)
// OBJECT: SectionHeaderCount: 11
// OBJECT: Name: .brace.functions
// OBJECT: Size: 128
// OBJECT: Name: .brace.text
// OBJECT: Size: 64
// LTO-REJECT: error: unsupported option '-flto' for target 'brace64-unknown-none-elf'
// MLLVM-REJECT: error: unsupported option '-mllvm -verify-machineinstrs' for target 'brace64-unknown-none-elf'
//
// C-ISEL-LABEL: name: brace_system_entry
// C-ISEL: stack: []
// C-ISEL: CALL_I32 @brace_system_call_leaf
// C-RA-LABEL: name: brace_system_entry
// C-RA: type: spill-slot
// C-RA: SPILL_STORE32 $r4, %stack.0 :: (store (s32) into %stack.0)
// C-RA-NEXT: CALL_I32 @brace_system_call_leaf
// C-RA-NEXT: $r5 = SPILL_LOAD32 %stack.0 :: (load (s32) from %stack.0)
// C-COLOR-LABEL: name: brace_system_entry
// C-COLOR: type: spill-slot
// C-COLOR: SPILL_STORE32 $r4, %stack.0 :: (store (s32) into %stack.0)
// C-COLOR-NEXT: CALL_I32 @brace_system_call_leaf
// C-COLOR-NEXT: $r5 = SPILL_LOAD32 %stack.0 :: (load (s32) from %stack.0)
// C-FRAME-LABEL: name: brace_system_entry
// C-FRAME: stack: []
// C-FRAME: FRAME_ENTER 16
// C-FRAME: FRAME_STORE32 4, $r4
// C-FRAME-NEXT: CALL_I32 @brace_system_call_leaf
// C-FRAME-NEXT: $r5 = FRAME_LOAD32 4
// C-FRAME: FRAME_LEAVE
// C-FRAME-NEXT: RET
// C-FINAL-LABEL: name: brace_system_entry
// C-FINAL: stack: []
// C-FINAL: FRAME_ENTER 16
// C-FINAL: FRAME_STORE32 4, $r4
// C-FINAL-NEXT: CALL_I32 @brace_system_call_leaf
// C-FINAL-NEXT: $r5 = FRAME_LOAD32 4
// C-FINAL: FRAME_LEAVE
// C-FINAL-NEXT: RET

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
