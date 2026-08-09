// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm %s 2>&1 | \
// RUN:   FileCheck -check-prefix=DRIVER %s
// RUN: %clang --target=brace64-unknown-none-elf -ffreestanding -nostdinc \
// RUN:   -S -emit-llvm -o - %s | FileCheck -check-prefix=IR %s
// RUN: not %clang --target=brace-unknown-none-elf -fsyntax-only %s \
// RUN:   2>&1 | FileCheck -check-prefix=NO-ALIAS %s

// DRIVER: "-cc1"
// DRIVER-SAME: "-triple" "brace64-unknown-none-elf"
// DRIVER-SAME: "-emit-llvm-bc"

// IR: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
// IR-NEXT: target triple = "brace64-unknown-none-elf"
// IR: define{{.*}} i64 @brace_identity(i64

// NO-ALIAS: error: unknown target triple 'brace-unknown-none-elf'

unsigned long brace_identity(unsigned long Value) { return Value + 1; }
