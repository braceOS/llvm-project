// REQUIRES: brace-registered-target
//
// The canonical C seed must reach the target-local S2 writer directly.  The
// explicit frame-pointer choice is part of this constrained frontend profile;
// removing it adds IR state and must therefore fail without publishing bytes.
//
// RUN: %clang --target=brace64-unknown-none-elf -std=c11 -O1 -ffreestanding \
// RUN:   -fno-builtin -fno-common -fno-stack-protector -fno-unwind-tables \
// RUN:   -fno-asynchronous-unwind-tables -fno-ident -fno-strict-aliasing \
// RUN:   -fno-vectorize -fno-slp-vectorize -fno-unroll-loops -fno-pic \
// RUN:   -fno-pie -fomit-frame-pointer -nostdinc -Wall -Wextra -Wpedantic \
// RUN:   -Werror -c %s -o %t.o
// RUN: wc -c < %t.o | FileCheck %s --check-prefix=SIZE
// RUN: %python -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" %t.o | FileCheck %s --check-prefix=SHA
// RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.o | FileCheck %s --check-prefix=ELF
// RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.o | FileCheck %s --check-prefix=COUNTS
//
// RUN: not %clang --target=brace64-unknown-none-elf -std=c11 -O1 \
// RUN:   -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
// RUN:   -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
// RUN:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// RUN:   -fno-unroll-loops -fno-pic -fno-pie -nostdinc -Wall -Wextra \
// RUN:   -Wpedantic -Werror -c %s -o %t.no-fomit.o
// RUN: test ! -s %t.no-fomit.o
//
// RUN: not %clang --target=brace64-unknown-none-elf -std=c11 -O0 \
// RUN:   -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
// RUN:   -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
// RUN:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// RUN:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// RUN:   -nostdinc -Werror -c %s -o %t.o0.o
// RUN: test ! -s %t.o0.o
// RUN: not %clang --target=brace64-unknown-none-elf -std=c11 -O2 \
// RUN:   -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
// RUN:   -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
// RUN:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// RUN:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// RUN:   -nostdinc -Werror -c %s -o %t.o2.o
// RUN: test ! -s %t.o2.o
// RUN: not %clang --target=brace64-unknown-none-elf -std=c11 -O3 \
// RUN:   -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
// RUN:   -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident \
// RUN:   -fno-strict-aliasing -fno-vectorize -fno-slp-vectorize \
// RUN:   -fno-unroll-loops -fno-pic -fno-pie -fomit-frame-pointer \
// RUN:   -nostdinc -Werror -c %s -o %t.o3.o
// RUN: test ! -s %t.o3.o
//
// SIZE: 2272
// SHA: c0be5323fceadba8847d844144053e861a4fb2a5b010d72b5e5f54d850bdabba
// ELF: Sections [
// ELF: Relocations [
// ELF: Section (5) .rela.brace.literals {
// ELF: Symbols [
// COUNTS: Sections [
// COUNTS-COUNT-9: Section {
// COUNTS: Relocations [
// COUNTS: Section (5) .rela.brace.literals {
// COUNTS-COUNT-29: Type: Unknown (1)
// COUNTS: Symbols [
// COUNTS-COUNT-2: Symbol {

typedef __UINT8_TYPE__ brace_u8;
typedef __UINT32_TYPE__ brace_u32;
typedef __UINT64_TYPE__ brace_u64;
typedef __UINTPTR_TYPE__ brace_uptr;
typedef volatile brace_u8 __attribute__((address_space(200))) brace_paddr_u8;
typedef volatile brace_u32 __attribute__((address_space(200))) brace_paddr_u32;

#define RAM_BASE ((brace_u64)0x80000000ULL)
#define UART_TX ((brace_u64)0x10000000ULL)
#define UART_LINE_STATUS ((brace_u64)0x10000005ULL)
#define TEST_FINISHER ((brace_u64)0x00100000ULL)
#define PHYSICAL_U8(address) (*(brace_paddr_u8 *)(brace_uptr)(address))
#define PHYSICAL_U32(address) (*(brace_paddr_u32 *)(brace_uptr)(address))
#define EMIT_RAM_BYTE(offset)                                                \
  do {                                                                       \
    PHYSICAL_U8(UART_TX) = PHYSICAL_U8(RAM_BASE + (brace_u64)(offset));      \
  } while (0)

void brace_system_entry(void) {
  PHYSICAL_U32(RAM_BASE + (brace_u64)0) = (brace_u32)0x6c6c6548U;
  PHYSICAL_U32(RAM_BASE + (brace_u64)4) = (brace_u32)0x6f57206fU;
  PHYSICAL_U32(RAM_BASE + (brace_u64)8) = (brace_u32)0x20646c72U;
  PHYSICAL_U32(RAM_BASE + (brace_u64)12) = (brace_u32)0x6d6f7266U;
  PHYSICAL_U32(RAM_BASE + (brace_u64)16) = (brace_u32)0x61724220U;
  PHYSICAL_U32(RAM_BASE + (brace_u64)20) = (brace_u32)0x4f206563U;
  PHYSICAL_U32(RAM_BASE + (brace_u64)24) = (brace_u32)0x00000a53U;

  while ((PHYSICAL_U8(UART_LINE_STATUS) & (brace_u8)0x20U) == (brace_u8)0) {
  }

  EMIT_RAM_BYTE(0);
  EMIT_RAM_BYTE(1);
  EMIT_RAM_BYTE(2);
  EMIT_RAM_BYTE(3);
  EMIT_RAM_BYTE(4);
  EMIT_RAM_BYTE(5);
  EMIT_RAM_BYTE(6);
  EMIT_RAM_BYTE(7);
  EMIT_RAM_BYTE(8);
  EMIT_RAM_BYTE(9);
  EMIT_RAM_BYTE(10);
  EMIT_RAM_BYTE(11);
  EMIT_RAM_BYTE(12);
  EMIT_RAM_BYTE(13);
  EMIT_RAM_BYTE(14);
  EMIT_RAM_BYTE(15);
  EMIT_RAM_BYTE(16);
  EMIT_RAM_BYTE(17);
  EMIT_RAM_BYTE(18);
  EMIT_RAM_BYTE(19);
  EMIT_RAM_BYTE(20);
  EMIT_RAM_BYTE(21);
  EMIT_RAM_BYTE(22);
  EMIT_RAM_BYTE(23);
  EMIT_RAM_BYTE(24);
  EMIT_RAM_BYTE(25);

  PHYSICAL_U32(TEST_FINISHER) = (brace_u32)0x00005555U;
  return;
}
