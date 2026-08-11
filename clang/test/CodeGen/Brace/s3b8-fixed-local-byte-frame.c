// REQUIRES: brace-registered-target
//
// The source section below is the exact canonical FL1 producer: LF-terminated,
// 19 lines, and 653 bytes.  Keep it isolated from this test's RUN directives.
//
// DEFINE: %{brace-s3b8-base-cc} = env -u CLANG_NO_DEFAULT_CONFIG %clang \
// DEFINE:   --target=brace64-unknown-none-elf --no-default-config \
// DEFINE:   -std=c11 -O1 -ffreestanding -fno-builtin -fno-common \
// DEFINE:   -fno-stack-protector -fno-unwind-tables \
// DEFINE:   -fno-asynchronous-unwind-tables -fno-ident \
// DEFINE:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// DEFINE:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// DEFINE:   -fno-optimize-sibling-calls -nostdinc \
// DEFINE:   -Wall -Wextra -Wpedantic -Werror
// DEFINE: %{brace-s3b8-cc} = %{brace-s3b8-base-cc} \
// DEFINE:   -mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0
// DEFINE: %{brace-s3b8-llc} = llc -mtriple=brace64-unknown-none-elf \
// DEFINE:   -target-abi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 \
// DEFINE:   -O1 -verify-machineinstrs -filetype=obj
//
// RUN: rm -rf %t.dir
// RUN: split-file %s %t.dir
// RUN: wc -l < %t.dir/fl1.c | FileCheck %s --check-prefix=SOURCE-LINES
// RUN: wc -c < %t.dir/fl1.c | FileCheck %s --check-prefix=SOURCE-SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.dir/fl1.c | FileCheck %s --check-prefix=SOURCE-SHA
//
// The direct producer must repeat at every source artifact layer.
// RUN: cd %t.dir && %{brace-s3b8-cc} -S -emit-llvm fl1.c -o fl1.ll
// RUN: cd %t.dir && %{brace-s3b8-cc} -S -emit-llvm fl1.c -o fl1.again.ll
// RUN: cmp %t.dir/fl1.ll %t.dir/fl1.again.ll
// RUN: FileCheck %s --check-prefixes=IR,IR-TEXT < %t.dir/fl1.ll
// RUN: cd %t.dir && %{brace-s3b8-cc} -c -emit-llvm fl1.c -o fl1.bc
// RUN: cd %t.dir && %{brace-s3b8-cc} -c -emit-llvm fl1.c -o fl1.again.bc
// RUN: cmp %t.dir/fl1.bc %t.dir/fl1.again.bc
// RUN: llvm-dis %t.dir/fl1.bc -o %t.dir/fl1.dis.ll
// RUN: llvm-dis %t.dir/fl1.bc -o %t.dir/fl1.dis.again.ll
// RUN: cmp %t.dir/fl1.dis.ll %t.dir/fl1.dis.again.ll
// RUN: FileCheck %s --check-prefixes=IR,IR-BC < %t.dir/fl1.dis.ll
// RUN: cd %t.dir && %{brace-s3b8-cc} -c fl1.c -o fl1.o
// RUN: cd %t.dir && %{brace-s3b8-cc} -c fl1.c -o fl1.again.o
// RUN: cmp %t.dir/fl1.o %t.dir/fl1.again.o
// RUN: cd %t.dir && %{brace-s3b8-llc} fl1.bc -o fl1.llc.o
// RUN: cmp %t.dir/fl1.o %t.dir/fl1.llc.o
// RUN: wc -c < %t.dir/fl1.o | FileCheck %s --check-prefix=OBJECT-SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" %t.dir/fl1.o | FileCheck %s --check-prefix=OBJECT-SHA
// RUN: llvm-readobj --file-headers --sections %t.dir/fl1.o | FileCheck %s --check-prefix=OBJECT
// RUN: %python -c "import struct,sys; d=open(sys.argv[1],'rb').read(); assert struct.unpack_from('<I',d,108)[0]==16 and struct.unpack_from('<I',d,172)[0]==0; w=struct.unpack_from('<16I',d,0x120); assert list(w)==[0x267,0x25b,0x925f,0x8266f,0x243,0x8966b,0x4299613,0x825b,0x50663,0x273,0x253,0x1025b,0x965f,0x4299613,0x5299213,0x9253]" %t.dir/fl1.o
//
// The direct-Clang bitcode producer must survive all five registered seams.
// RUN: cd %t.dir && %{brace-s3b8-llc} -stop-after=finalize-isel fl1.bc -o c-isel.mir
// RUN: FileCheck %s --check-prefix=PRE-ISEL < %t.dir/c-isel.mir
// RUN: cd %t.dir && %{brace-s3b8-llc} -start-after=finalize-isel c-isel.mir -o c-from-isel.o
// RUN: cmp %t.dir/fl1.o %t.dir/c-from-isel.o
// RUN: cd %t.dir && %{brace-s3b8-llc} -stop-after=virtregrewriter fl1.bc -o c-ra.mir
// RUN: FileCheck %s --check-prefix=PRE-RA < %t.dir/c-ra.mir
// RUN: cd %t.dir && %{brace-s3b8-llc} -start-after=virtregrewriter c-ra.mir -o c-from-ra.o
// RUN: cmp %t.dir/fl1.o %t.dir/c-from-ra.o
// RUN: cd %t.dir && %{brace-s3b8-llc} -stop-after=stack-slot-coloring fl1.bc -o c-color.mir
// RUN: FileCheck %s --check-prefix=PRE-RA < %t.dir/c-color.mir
// RUN: cd %t.dir && %{brace-s3b8-llc} -start-after=stack-slot-coloring c-color.mir -o c-from-color.o
// RUN: cmp %t.dir/fl1.o %t.dir/c-from-color.o
// RUN: cd %t.dir && %{brace-s3b8-llc} -stop-after=brace-finalize-fixed-local-byte-frame fl1.bc -o c-frame.mir
// RUN: FileCheck %s --check-prefix=POST-FRAME < %t.dir/c-frame.mir
// RUN: cd %t.dir && %{brace-s3b8-llc} -start-after=brace-finalize-fixed-local-byte-frame c-frame.mir -o c-from-frame.o
// RUN: cmp %t.dir/fl1.o %t.dir/c-from-frame.o
// RUN: cd %t.dir && %{brace-s3b8-llc} -stop-after=brace-finalize-branches fl1.bc -o c-final.mir
// RUN: FileCheck %s --check-prefix=POST-FINAL < %t.dir/c-final.mir
// RUN: cd %t.dir && %{brace-s3b8-llc} -start-after=brace-finalize-branches c-final.mir -o c-from-final.o
// RUN: cmp %t.dir/fl1.o %t.dir/c-from-final.o
//
// SOURCE-LINES: 19
// SOURCE-SIZE: 653
// SOURCE-SHA: dacff813d4a03154d34a0ab2a10fa5c25a886f7e53bf2ad33ce157ab364f8ae0
// OBJECT-SIZE: 1320
// OBJECT-SHA: abd3121acb5c21474aaa7deabfc7309d9687880600bc4f765828c65228f26148
// OBJECT: Flags [ (0x42520400)
// OBJECT: SectionHeaderCount: 11
// OBJECT: Name: .brace.functions
// OBJECT: Size: 128
// OBJECT: Name: .brace.text
// OBJECT: Size: 64
//
// IR-LABEL: define dso_local void @brace_system_entry()
// IR: %local = alloca i32, align 4
// IR-NEXT: %[[INPUT:[-a-zA-Z$._0-9]+]] = load volatile i32, ptr addrspace(200)
// IR-NEXT: call void @llvm.lifetime.start.p0(ptr nonnull %local)
// IR-NEXT: store volatile i32 %[[INPUT]], ptr %local, align 4
// IR-NEXT: %[[RESULT:[-a-zA-Z$._0-9]+]] = call fastcc i32 @brace_system_call_leaf(i32 noundef %[[INPUT]])
// IR-NEXT: %[[RECOVERED:[-a-zA-Z$._0-9]+]] = load volatile i32, ptr %local, align 4
// IR-NEXT: %[[COMBINED:[-a-zA-Z$._0-9]+]] = and i32 %[[RECOVERED]], %[[RESULT]]
// IR-NEXT: store volatile i32 %[[COMBINED]], ptr addrspace(200)
// IR-NEXT: call void @llvm.lifetime.end.p0(ptr nonnull %local)
// IR-NEXT: ret void
// IR: declare void @llvm.lifetime.start.p0(ptr captures(none)) #[[LIFETIME_ATTRS:[0-9]+]]
// IR-LABEL: define internal fastcc i32 @brace_system_call_leaf(i32 noundef
// IR: load volatile i32, ptr addrspace(200)
// IR-NEXT: and i32
// IR-NEXT: ret i32
// IR: declare void @llvm.lifetime.end.p0(ptr captures(none)) #[[LIFETIME_ATTRS]]
// IR-TEXT: attributes #[[LIFETIME_ATTRS]] = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
// IR-BC: attributes #[[LIFETIME_ATTRS]] = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
// IR: !{i32 1, !"target-abi", !"brace-system-s2-direct-call-byte-frame-fixed-local-r0"}
//
// PRE-ISEL-LABEL: name: brace_system_entry
// PRE-ISEL: stack:
// PRE-ISEL-NEXT: - { id: 0, name: local, type: default, offset: 0, size: 4, alignment: 4,
// PRE-ISEL: LIFETIME_START %stack.0.local
// PRE-ISEL: LOCAL_STORE32
// PRE-ISEL: LOCAL_LOAD32
// PRE-ISEL: LIFETIME_END %stack.0.local
// PRE-RA-LABEL: name: brace_system_entry
// PRE-RA: stack:
// PRE-RA-NEXT: - { id: 0, name: local, type: default, offset: 0, size: 4, alignment: 4,
// PRE-RA: LIFETIME_START %stack.0.local
// PRE-RA: LOCAL_STORE32 $r4, %stack.0.local
// PRE-RA-NEXT: CALL_I32 @brace_system_call_leaf
// PRE-RA-NEXT: $r5 = LOCAL_LOAD32 %stack.0.local
// PRE-RA: LIFETIME_END %stack.0.local
// POST-FRAME-LABEL: name: brace_system_entry
// POST-FRAME: stack: []
// POST-FRAME: machineFunctionInfo: {}
// POST-FRAME: FRAME_ENTER 16
// POST-FRAME: FRAME_STORE32 8, $r4
// POST-FRAME-NEXT: CALL_I32 @brace_system_call_leaf
// POST-FRAME-NEXT: $r5 = FRAME_LOAD32 8
// POST-FRAME: FRAME_LEAVE
// POST-FRAME-NEXT: RET
// POST-FRAME-NOT: LOCAL_
// POST-FRAME-NOT: LIFETIME_
// POST-FRAME-NOT: %stack.
// POST-FINAL-LABEL: name: brace_system_entry
// POST-FINAL: stack: []
// POST-FINAL: machineFunctionInfo: {}
// POST-FINAL: FRAME_ENTER 16
// POST-FINAL: FRAME_STORE32 8, $r4
// POST-FINAL-NEXT: CALL_I32 @brace_system_call_leaf
// POST-FINAL-NEXT: $r5 = FRAME_LOAD32 8
// POST-FINAL: FRAME_LEAVE
// POST-FINAL-NEXT: RET
// POST-FINAL-NOT: LOCAL_
// POST-FINAL-NOT: LIFETIME_
// POST-FINAL-NOT: %stack.

//--- fl1.c
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
  volatile brace_u32 local = input;
  brace_u32 result = brace_system_call_leaf(input);
  PHYSICAL_U32((brace_u64)0x80000004ULL) = result & local;
}
