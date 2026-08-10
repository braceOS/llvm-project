// RUN: %clang_cc1 -triple brace64-unknown-none-elf \
// RUN:   -target-abi brace-system-s2-leaf-r0 -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefix=LEAF-ABI %s
// RUN: %clang_cc1 -triple brace64-unknown-none-elf \
// RUN:   -target-abi brace-system-s2-leaf-home-r0 -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefix=HOME-ABI %s
// RUN: %clang_cc1 -triple brace64-unknown-none-elf -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefix=DEFAULT-ABI %s
// RUN: not %clang_cc1 -triple brace64-unknown-none-elf \
// RUN:   -target-abi not-brace -emit-llvm -o /dev/null %s 2>&1 | \
// RUN:   FileCheck --check-prefix=BAD-ABI %s

// LEAF-ABI: !llvm.module.flags = !{
// LEAF-ABI-DAG: !{{[0-9]+}} = !{i32 1, !"target-abi", !"brace-system-s2-leaf-r0"}

// HOME-ABI: !llvm.module.flags = !{
// HOME-ABI-DAG: !{{[0-9]+}} = !{i32 1, !"target-abi", !"brace-system-s2-leaf-home-r0"}

// DEFAULT-ABI-NOT: !"target-abi"

// BAD-ABI: error: unknown target ABI 'not-brace'

void brace_system_entry(void) {}
