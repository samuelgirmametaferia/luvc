#ifndef LUV_INTRINSICS_H
#define LUV_INTRINSICS_H

#include "type.h"
#include <stdbool.h>

typedef struct {
    const char* name;
    LuvType* owner_type; // For member functions (e.g. List.push)
    LuvType** param_types;
    size_t param_count;
    LuvType* return_type;
    bool is_io; /* intrinsic performs I/O or other impure ops */
    bool is_comptime_safe; /* allowed in compile-time evaluation */
} Intrinsic;

typedef struct {
    Intrinsic** intrinsics;
    size_t count;
    size_t capacity;
} IntrinsicRegistry;

IntrinsicRegistry* intrinsics_init(Arena* arena);
void intrinsics_register(Arena* arena, IntrinsicRegistry* reg, const char* name, LuvType* owner_type, LuvType** params, size_t param_count, LuvType* ret, bool is_io, bool is_comptime_safe);
Intrinsic* intrinsics_lookup(IntrinsicRegistry* reg, const char* name, LuvType* owner_type);

#endif
