/// Check the experimental Brace64 frontend predefinitions.

// RUN: %clang_cc1 -E -dM -ffreestanding \
// RUN:   -triple=brace64-unknown-none-elf < /dev/null | \
// RUN:   FileCheck -match-full-lines -check-prefix=BRACE64 %s

// BRACE64: #define _LP64 1
// BRACE64: #define __BIGGEST_ALIGNMENT__ 16
// BRACE64: #define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
// BRACE64: #define __INT64_TYPE__ long int
// BRACE64: #define __INTPTR_TYPE__ long int
// BRACE64: #define __LP64__ 1
// BRACE64: #define __SIZEOF_LONG_LONG__ 8
// BRACE64: #define __SIZEOF_LONG__ 8
// BRACE64: #define __SIZEOF_POINTER__ 8
// BRACE64: #define __STDC_HOSTED__ 0
// BRACE64: #define __UINTPTR_TYPE__ long unsigned int
// BRACE64: #define __brace64__ 1
// BRACE64: #define __brace__ 1
