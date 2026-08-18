// RUN: %clang_cc1 -triple brace64-unknown-none-elf -ffreestanding \
// RUN:   -target-abi brace-system-llvm-reference-as1-r0 \
// RUN:   -Wno-incompatible-pointer-types -Wno-compare-distinct-pointer-types \
// RUN:   -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefix=AS1 %s
// RUN: %clang_cc1 -triple brace64-unknown-none-elf -ffreestanding \
// RUN:   -target-abi brace-system-s2-leaf-r0 \
// RUN:   -Wno-incompatible-pointer-types -Wno-compare-distinct-pointer-types \
// RUN:   -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefix=OLD %s
// RUN: not %clang_cc1 -triple brace64-unknown-none-elf -ffreestanding \
// RUN:   -target-abi brace-system-llvm-reference-as1-r0 \
// RUN:   -Wno-incompatible-pointer-types -S -o /dev/null %s 2>&1 | \
// RUN:   FileCheck --check-prefix=NATIVE %s
// RUN: not %clang_cc1 -triple brace64-unknown-none-elf -ffreestanding \
// RUN:   -target-abi brace-system-llvm-reference-as1-r0 \
// RUN:   -Wno-incompatible-pointer-types -emit-obj -o /dev/null %s 2>&1 | \
// RUN:   FileCheck --check-prefix=NATIVE %s

typedef int (*callback_t)(void *);
typedef callback_t (*factory_t)(void *);
typedef int (*variadic_callback_t)(int, ...);
typedef int (*compatible_callback_t)(void *);
typedef unsigned long (*wide_callback_t)(unsigned long);

struct Packet {
  void *object;
  callback_t callback;
  callback_t table[2];
};

union Reference {
  void *object;
  callback_t callback;
};

struct Envelope {
  struct Packet packets[2];
  union Reference reference;
  unsigned long padding[8];
};

// AS1: target datalayout = "e-m:e-p:64:64-p1:64:64-P1-i64:64-i128:128-n32:64-S128"
// AS1-NEXT: target triple = "brace64-unknown-none-elf"
// AS1: %struct.Packet = type { ptr, ptr addrspace(1), [2 x ptr addrspace(1)] }
// AS1: %struct.Envelope = type { [2 x %struct.Packet], %union.Reference, [8 x i64] }
// AS1: %union.Reference = type { ptr }

// OLD: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
// OLD-NOT: addrspace(1)

static int leaf(void *object) { return object != (void *)0; }
extern int external_target(void *);

// AS1: @callback_global = global ptr addrspace(1) @leaf, align 8
// AS1: @callback_array = global [2 x ptr addrspace(1)] [ptr addrspace(1) @leaf, ptr addrspace(1) @external_target], align 8
// AS1: @callback_union = global { ptr addrspace(1) } { ptr addrspace(1) @leaf }, align 8
// AS1: @callback_as_object_global = global ptr addrspacecast (ptr addrspace(1) @leaf to ptr), align 8
// AS1: @object_as_callback_global = global ptr addrspace(1) addrspacecast (ptr @callback_global to ptr addrspace(1)), align 8
callback_t callback_global = leaf;
callback_t callback_array[2] = {leaf, external_target};
union Reference callback_union = {.callback = leaf};
void *callback_as_object_global = (void *)leaf;
callback_t object_as_callback_global = (callback_t)&callback_global;

// AS1-LABEL: define internal i32 @leaf(ptr noundef %object) addrspace(1)
// AS1: declare i32 @external_target(ptr noundef) addrspace(1)

// AS1-LABEL: define dso_local i32 @direct_call(ptr noundef %object) addrspace(1)
// AS1: call addrspace(1) i32 @leaf(ptr noundef %{{.*}})
int direct_call(void *object) { return leaf(object); }

// AS1-LABEL: define dso_local i32 @indirect_call(ptr addrspace(1) noundef %callback, ptr noundef %object) addrspace(1)
// AS1: %[[INDIRECT:[0-9]+]] = load ptr addrspace(1), ptr %callback.addr, align 8
// AS1: call addrspace(1) i32 %[[INDIRECT]](ptr noundef %{{.*}})
int indirect_call(callback_t callback, void *object) {
  return callback(object);
}

// AS1-LABEL: define dso_local ptr addrspace(1) @return_callback(ptr noundef %object) addrspace(1)
// AS1: select i1 %{{.*}}, ptr addrspace(1) @leaf, ptr addrspace(1) @external_target
// AS1: ret ptr addrspace(1) %{{.*}}
callback_t return_callback(void *object) {
  return object ? leaf : external_target;
}

// AS1-LABEL: define dso_local ptr addrspace(1) @factory_call(ptr addrspace(1) noundef %factory, ptr noundef %object) addrspace(1)
// AS1: call addrspace(1) ptr addrspace(1) %{{.*}}(ptr noundef %{{.*}})
factory_t factory_identity(factory_t factory) { return factory; }
callback_t factory_call(factory_t factory, void *object) {
  return factory(object);
}

// AS1-LABEL: define dso_local i32 @aggregate_call(ptr noundef %packet) addrspace(1)
// AS1: getelementptr{{.*}} %struct.Packet, ptr %{{.*}}, i32 0, i32 1
// AS1: load ptr addrspace(1), ptr %{{.*}}, align 8
// AS1: getelementptr{{.*}} [2 x ptr addrspace(1)], ptr %{{.*}}, i64 0, i64 1
// AS1: call addrspace(1) i32 %{{.*}}(ptr noundef %{{.*}})
int aggregate_call(struct Packet *packet) {
  return packet->callback(packet->object) + packet->table[1](packet->object);
}

// AS1-LABEL: define dso_local i32 @callback_slot_call(ptr noundef %slot, ptr noundef %object) addrspace(1)
// AS1: load ptr, ptr %slot.addr, align 8
// AS1: load ptr addrspace(1), ptr %{{.*}}, align 8
// AS1: call addrspace(1) i32 %{{.*}}(ptr noundef %{{.*}})
int callback_slot_call(callback_t *slot, void *object) {
  return (*slot)(object);
}

// AS1-LABEL: define dso_local i32 @union_call(ptr noundef %reference, ptr noundef %object) addrspace(1)
// AS1: load ptr addrspace(1), ptr %{{.*}}, align 8
// AS1: call addrspace(1) i32 %{{.*}}(ptr noundef %{{.*}})
int union_call(union Reference *reference, void *object) {
  return reference->callback(object);
}

// AS1-LABEL: define dso_local i32 @byval_call(ptr noundef byval(%struct.Envelope) align 8 %envelope) addrspace(1)
// AS1: load ptr addrspace(1), ptr %{{.*}}, align 8
// AS1: load ptr, ptr %{{.*}}, align 8
// AS1: call addrspace(1) i32 %{{.*}}(ptr noundef %{{.*}})
int byval_call(struct Envelope envelope) {
  return envelope.packets[1].callback(envelope.packets[1].object);
}

// AS1-LABEL: define dso_local i32 @variadic_target(i32 noundef %count, ...) addrspace(1)
int variadic_target(int count, ...) { return count; }

// AS1-LABEL: define dso_local i32 @variadic_call(ptr addrspace(1) noundef %callback) addrspace(1)
// AS1: call addrspace(1) i32 (i32, ...) @variadic_target(i32 noundef 1, ptr addrspace(1) noundef %{{.*}})
int variadic_call(callback_t callback) {
  return variadic_target(1, callback);
}

// AS1-LABEL: define dso_local ptr addrspace(1) @variadic_extract(i32 noundef %count, ...) addrspace(1)
// AS1: call addrspace(1) void @llvm.va_start.p0(ptr %arguments)
// AS1: va_arg ptr %arguments, ptr addrspace(1)
// AS1: call addrspace(1) void @llvm.va_end.p0(ptr %arguments)
callback_t variadic_extract(int count, ...) {
  __builtin_va_list arguments;
  __builtin_va_start(arguments, count);
  callback_t callback = __builtin_va_arg(arguments, callback_t);
  __builtin_va_end(arguments);
  return callback;
}

// AS1-LABEL: define dso_local i32 @indirect_variadic_call(ptr addrspace(1) noundef %callback, ptr addrspace(1) noundef %extra) addrspace(1)
// AS1: call addrspace(1) i32 (i32, ...) %{{.*}}(i32 noundef 1, ptr addrspace(1) noundef %{{.*}})
int indirect_variadic_call(variadic_callback_t callback, callback_t extra) {
  return callback(1, extra);
}

// AS1-LABEL: define dso_local i32 @compatible_function_cast_call(ptr addrspace(1) noundef %callback, ptr noundef %object) addrspace(1)
// AS1: call addrspace(1) i32 %{{.*}}(ptr noundef %{{.*}})
int compatible_function_cast_call(callback_t callback, void *object) {
  return ((compatible_callback_t)callback)(object);
}

// This call is source-level undefined behavior.  It is a lowering-only guard:
// opaque LLVM pointers must retain the destination callsite signature and AS1.
// AS1-LABEL: define dso_local i64 @incompatible_function_cast_call(ptr addrspace(1) noundef %callback, i64 noundef %value) addrspace(1)
// AS1: call addrspace(1) i64 %{{.*}}(i64 noundef %{{.*}})
unsigned long incompatible_function_cast_call(callback_t callback,
                                               unsigned long value) {
  return ((wide_callback_t)callback)(value);
}

// AS1-LABEL: define dso_local ptr @explicit_to_object(ptr addrspace(1) noundef %callback) addrspace(1)
// AS1: addrspacecast ptr addrspace(1) %{{.*}} to ptr
void *explicit_to_object(callback_t callback) { return (void *)callback; }

// AS1-LABEL: define dso_local ptr addrspace(1) @explicit_to_function(ptr noundef %object) addrspace(1)
// AS1: addrspacecast ptr %{{.*}} to ptr addrspace(1)
callback_t explicit_to_function(void *object) { return (callback_t)object; }

// AS1-LABEL: define dso_local ptr @implicit_to_object(ptr addrspace(1) noundef %callback) addrspace(1)
// AS1: addrspacecast ptr addrspace(1) %{{.*}} to ptr
void *implicit_to_object(callback_t callback) { return callback; }

// AS1-LABEL: define dso_local ptr addrspace(1) @implicit_to_function(ptr noundef %object) addrspace(1)
// AS1: addrspacecast ptr %{{.*}} to ptr addrspace(1)
callback_t implicit_to_function(void *object) { return object; }

// AS1-LABEL: define dso_local i32 @compare_references(ptr addrspace(1) noundef %callback, ptr noundef %object) addrspace(1)
// AS1: addrspacecast ptr %{{.*}} to ptr addrspace(1)
// AS1: icmp eq ptr addrspace(1) %{{.*}}, %{{.*}}
int compare_references(callback_t callback, void *object) {
  return callback == object;
}

// AS1-LABEL: define dso_local i64 @callback_to_integer(ptr addrspace(1) noundef %callback) addrspace(1)
// AS1: ptrtoint ptr addrspace(1) %{{.*}} to i64
unsigned long callback_to_integer(callback_t callback) {
  return (unsigned long)callback;
}

// AS1-LABEL: define dso_local ptr addrspace(1) @integer_to_callback(i64 noundef %token) addrspace(1)
// AS1: inttoptr i64 %{{.*}} to ptr addrspace(1)
callback_t integer_to_callback(unsigned long token) {
  return (callback_t)token;
}

// AS1: !llvm.module.flags = !{
// AS1-DAG: !{{[0-9]+}} = !{i32 1, !"target-abi", !"brace-system-llvm-reference-as1-r0"}

// NATIVE: error: unable to interface with target machine
