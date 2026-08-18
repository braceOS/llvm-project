// RUN: %clang_cc1 -triple avr-unknown-none -Wno-incompatible-pointer-types \
// RUN:   -Wno-compare-distinct-pointer-types -emit-llvm -o - %s | \
// RUN:   FileCheck %s

// A nonzero program address space gives ordinary C function pointers a
// different LLVM address space from object pointers.  Sema deliberately
// represents the accepted explicit and implicit extension conversions below
// as CK_BitCast.  CodeGen must select addrspacecast from the converted LLVM
// types rather than assert or emit an invalid bitcast.

typedef int (*callback_t)(void *);

static int target(void *object) { return object != (void *)0; }
extern void consume_object(void *);
extern void consume_callback(callback_t);

// CHECK: @function_as_object = global ptr addrspacecast (ptr addrspace(1) @target to ptr)
// CHECK: @object_as_function = global ptr addrspace(1) addrspacecast (ptr @function_as_object to ptr addrspace(1))
void *function_as_object = (void *)target;
callback_t object_as_function = (callback_t)&function_as_object;

// CHECK-LABEL: define{{.*}} ptr @explicit_to_object(ptr addrspace(1) noundef %callback) addrspace(1)
// CHECK: addrspacecast ptr addrspace(1) %{{.*}} to ptr
void *explicit_to_object(callback_t callback) { return (void *)callback; }

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @explicit_to_function(ptr noundef %object) addrspace(1)
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(1)
callback_t explicit_to_function(void *object) { return (callback_t)object; }

// CHECK-LABEL: define{{.*}} ptr @implicit_to_object(ptr addrspace(1) noundef %callback) addrspace(1)
// CHECK: addrspacecast ptr addrspace(1) %{{.*}} to ptr
void *implicit_to_object(callback_t callback) { return callback; }

// CHECK-LABEL: define{{.*}} ptr addrspace(1) @implicit_to_function(ptr noundef %object) addrspace(1)
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(1)
callback_t implicit_to_function(void *object) { return object; }

// CHECK-LABEL: define{{.*}} void @implicit_arguments(ptr addrspace(1) noundef %callback, ptr noundef %object) addrspace(1)
// CHECK: addrspacecast ptr addrspace(1) %{{.*}} to ptr
// CHECK: call addrspace(1) void @consume_object(ptr noundef %{{.*}})
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(1)
// CHECK: call addrspace(1) void @consume_callback(ptr addrspace(1) noundef %{{.*}})
void implicit_arguments(callback_t callback, void *object) {
  consume_object(callback);
  consume_callback(object);
}

// CHECK-LABEL: define{{.*}} i16 @compare_references(ptr addrspace(1) noundef %callback, ptr noundef %object) addrspace(1)
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(1)
// CHECK: icmp eq ptr addrspace(1) %{{.*}}, %{{.*}}
int compare_references(callback_t callback, void *object) {
  return callback == object;
}
