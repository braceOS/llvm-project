// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm %s 2>&1 | \
// RUN:   FileCheck -check-prefix=DEFAULT-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-s2-leaf-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=LEAF-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=not-brace %s 2>&1 | \
// RUN:   FileCheck -check-prefix=BAD-ABI %s
// RUN: %clang --target=brace64-unknown-none-elf -ffreestanding -nostdinc \
// RUN:   -S -emit-llvm -o - %s | FileCheck -check-prefix=IR %s
// RUN: not %clang --target=brace-unknown-none-elf -fsyntax-only %s \
// RUN:   2>&1 | FileCheck -check-prefix=NO-ALIAS %s

// DEFAULT-ABI: "-cc1"
// DEFAULT-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// DEFAULT-ABI-SAME: "-emit-llvm-bc"
// DEFAULT-ABI-NOT: "-target-abi"

// LEAF-ABI: "-cc1"
// LEAF-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// LEAF-ABI-SAME: "-target-abi" "brace-system-s2-leaf-r0"

// BAD-ABI: error: unsupported argument 'not-brace' to option '-mabi='

// IR: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
// IR-NEXT: target triple = "brace64-unknown-none-elf"
// IR: define{{.*}} i64 @brace_identity(i64

// NO-ALIAS: error: unknown target triple 'brace-unknown-none-elf'

unsigned long brace_identity(unsigned long Value) { return Value + 1; }
