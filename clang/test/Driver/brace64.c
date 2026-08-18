// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm %s 2>&1 | \
// RUN:   FileCheck -check-prefix=DEFAULT-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -E -dM -mabi=brace-system-llvm-reference-as1-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=REFERENCE-AS1-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -M -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -MM -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -fsyntax-only \
// RUN:   -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -S -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-NATIVE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-NATIVE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-llvm-reference-as1-r0 \
// RUN:   -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-llvm-reference-as1-r0 \
// RUN:   -mabi=brace-system-s2-leaf-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-s2-leaf-r0 \
// RUN:   -mabi=brace-system-llvm-reference-as1-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-llvm-reference-as1-r0 \
// RUN:   -flto %s 2>&1 | FileCheck -check-prefix=REFERENCE-AS1-LTO %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-llvm-reference-as1-r0 \
// RUN:   -mllvm -verify-machineinstrs %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-MLLVM %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-llvm-reference-as1-r0 \
// RUN:   -fexceptions %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-EXCEPTIONS %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-llvm-reference-as1-r0 \
// RUN:   -Xclang -disable-llvm-passes %s 2>&1 | \
// RUN:   FileCheck -check-prefix=REFERENCE-AS1-XCLANG %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-s2-leaf-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=LEAF-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-s2-leaf-home-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=HOME-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=brace-system-s2-direct-call-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=CALL-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-home-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=CALL-HOME-ABI %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=CALL-BYTE-FRAME-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=BYTE-FRAME-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 \
// RUN:   -mabi=brace-system-s2-direct-call-home-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=BYTE-FRAME-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 \
// RUN:   -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-byte-frame-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=XCLANG-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm -mabi=not-brace %s 2>&1 | \
// RUN:   FileCheck -check-prefix=BAD-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=XCLANG-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -mabi=brace-system-s2-leaf-r0 \
// RUN:   -Xclang -target-abi -Xclang brace-system-s2-direct-call-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=XCLANG-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -mabi=brace-system-s2-direct-call-r0 \
// RUN:   -Xclang -target-abi -Xclang brace-system-s2-leaf-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=XCLANG-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -mabi=brace-system-s2-direct-call-r0 \
// RUN:   -Xclang -target-abi -Xclang brace-system-s2-direct-call-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=XCLANG-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 -mllvm -verify-machineinstrs \
// RUN:   %s 2>&1 | FileCheck -check-prefix=XCLANG-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-r0 -fexceptions %s 2>&1 | \
// RUN:   FileCheck -check-prefix=XCLANG-ABI %s
// RUN: %clang --target=brace64-unknown-none-elf -ffreestanding -nostdinc \
// RUN:   -S -emit-llvm -o - %s | FileCheck -check-prefix=IR %s
// RUN: not %clang --target=brace-unknown-none-elf -fsyntax-only %s \
// RUN:   2>&1 | FileCheck -check-prefix=NO-ALIAS %s
// RUN: %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=CALL-BYTE-FRAME-FIXED-LOCAL-ABI %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=FIXED-LOCAL-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 %s 2>&1 | \
// RUN:   FileCheck -check-prefix=BYTE-FRAME-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -emit-llvm \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-r0 \
// RUN:   -mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=BYTE-FRAME-DUPLICATE %s
// RUN: not %clang -### --target=brace64-unknown-none-elf -ffreestanding \
// RUN:   -nostdinc -c -Xclang -target-abi \
// RUN:   -Xclang brace-system-s2-direct-call-byte-frame-fixed-local-r0 %s \
// RUN:   2>&1 | FileCheck -check-prefix=XCLANG-ABI %s

// DEFAULT-ABI: "-cc1"
// DEFAULT-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// DEFAULT-ABI-SAME: "-emit-llvm-bc"
// DEFAULT-ABI-NOT: "-target-abi"

// REFERENCE-AS1-ABI: "-cc1"
// REFERENCE-AS1-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// REFERENCE-AS1-ABI-SAME: "-target-abi" "brace-system-llvm-reference-as1-r0"

// REFERENCE-AS1-NATIVE: error: unsupported option '-mabi=brace-system-llvm-reference-as1-r0' for target 'brace64-unknown-none-elf'
// REFERENCE-AS1-DUPLICATE: error: unsupported option '-mabi=brace-system-llvm-reference-as1-r0' for target 'brace64-unknown-none-elf'
// REFERENCE-AS1-LTO: error: unsupported option '-flto' for target 'brace64-unknown-none-elf'
// REFERENCE-AS1-MLLVM: error: unsupported option '-mllvm -verify-machineinstrs' for target 'brace64-unknown-none-elf'
// REFERENCE-AS1-EXCEPTIONS: error: unsupported option '-fexceptions' for target 'brace64-unknown-none-elf'
// REFERENCE-AS1-XCLANG: error: unsupported option '-Xclang -disable-llvm-passes' for target 'brace64-unknown-none-elf'

// LEAF-ABI: "-cc1"
// LEAF-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// LEAF-ABI-SAME: "-target-abi" "brace-system-s2-leaf-r0"

// HOME-ABI: "-cc1"
// HOME-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// HOME-ABI-SAME: "-target-abi" "brace-system-s2-leaf-home-r0"

// CALL-ABI: "-cc1"
// CALL-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// CALL-ABI-SAME: "-target-abi" "brace-system-s2-direct-call-r0"

// CALL-HOME-ABI: "-cc1"
// CALL-HOME-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// CALL-HOME-ABI-SAME: "-target-abi" "brace-system-s2-direct-call-home-r0"

// CALL-BYTE-FRAME-ABI: "-cc1"
// CALL-BYTE-FRAME-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// CALL-BYTE-FRAME-ABI-SAME: "-target-abi" "brace-system-s2-direct-call-byte-frame-r0"

// BAD-ABI: error: unsupported argument 'not-brace' to option '-mabi='

// BYTE-FRAME-DUPLICATE: error: unsupported option '-mabi=brace-system-s2-direct-call-byte-frame-r0' for target 'brace64-unknown-none-elf'

// XCLANG-ABI: error: unsupported option '-Xclang -target-abi' for target 'brace64-unknown-none-elf'

// IR: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
// IR-NEXT: target triple = "brace64-unknown-none-elf"
// IR: define{{.*}} i64 @brace_identity(i64

// NO-ALIAS: error: unknown target triple 'brace-unknown-none-elf'

// CALL-BYTE-FRAME-FIXED-LOCAL-ABI: "-cc1"
// CALL-BYTE-FRAME-FIXED-LOCAL-ABI-SAME: "-triple" "brace64-unknown-none-elf"
// CALL-BYTE-FRAME-FIXED-LOCAL-ABI-SAME: "-target-abi" "brace-system-s2-direct-call-byte-frame-fixed-local-r0"

// FIXED-LOCAL-DUPLICATE: error: unsupported option '-mabi=brace-system-s2-direct-call-byte-frame-fixed-local-r0' for target 'brace64-unknown-none-elf'

unsigned long brace_identity(unsigned long Value) { return Value + 1; }
