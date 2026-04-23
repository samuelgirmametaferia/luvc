#include "intrinsics.h"
#include <string.h>
#include <stdbool.h>

#include "intrinsics.h"
#include <string.h>
#include <stdbool.h>

IntrinsicRegistry* intrinsics_init(Arena* arena) {
    IntrinsicRegistry* reg = (IntrinsicRegistry*)arena_alloc(arena, sizeof(IntrinsicRegistry));
    reg->capacity = 128;
    reg->count = 0;
    reg->intrinsics = (Intrinsic**)arena_alloc(arena, sizeof(Intrinsic*) * reg->capacity);
    
    // Register basic intrinsics
    intrinsics_register(arena, reg, "ToString", NULL, (LuvType*[]){type_new(arena, TYPE_ANY)}, 1, type_string(arena), false, true);
    
    /* Pure mathematical/string intrinsics (pure, allowed in comptime) */
    intrinsics_register(arena, reg, "abs", NULL, (LuvType*[]){type_i32(arena)}, 1, type_i32(arena), false, true);
    intrinsics_register(arena, reg, "sqrt", NULL, (LuvType*[]){type_f32(arena)}, 1, type_f32(arena), false, true);
    
    // I/O intrinsics (considered impure)
    intrinsics_register(arena, reg, "print", NULL, (LuvType*[]){type_string(arena)}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "println", NULL, (LuvType*[]){type_string(arena)}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "printf", NULL, (LuvType*[]){type_string(arena)}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "read", NULL, (LuvType*[]){type_string(arena)}, 1, type_string(arena), true, false);
    intrinsics_register(arena, reg, "write", NULL, (LuvType*[]){type_new(arena, TYPE_LIST)}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "open", NULL, (LuvType*[]){type_string(arena)}, 1, type_new(arena, TYPE_ANY), true, false);
    intrinsics_register(arena, reg, "close", NULL, (LuvType*[]){type_new(arena, TYPE_ANY)}, 1, type_void(arena), true, false);

    // Memory allocation intrinsics
    intrinsics_register(arena, reg, "malloc", NULL, (LuvType*[]){type_i32(arena)}, 1, type_new(arena, TYPE_ANY), true, false);
    intrinsics_register(arena, reg, "free", NULL, (LuvType*[]){type_new(arena, TYPE_ANY)}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "alloc", NULL, (LuvType*[]){type_i32(arena)}, 1, type_new(arena, TYPE_ANY), true, false);
    intrinsics_register(arena, reg, "dealloc", NULL, (LuvType*[]){type_new(arena, TYPE_ANY)}, 1, type_void(arena), true, false);

    // Container mutation considered impure for purity checks
    LuvType* list_any = type_new(arena, TYPE_LIST);
    list_any->as.list_array.element_type = type_new(arena, TYPE_ANY);
    intrinsics_register(arena, reg, "push", list_any, (LuvType*[]){type_new(arena, TYPE_ANY)}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "pop", list_any, (LuvType*[]){}, 0, type_new(arena, TYPE_ANY), true, false);
    intrinsics_register(arena, reg, "append", list_any, (LuvType*[]){list_any}, 1, type_void(arena), true, false);
    intrinsics_register(arena, reg, "len", list_any, (LuvType*[]){}, 0, type_i32(arena), false, true);

    // Non-deterministic / environment effects
    intrinsics_register(arena, reg, "rand", NULL, (LuvType*[]){}, 0, type_i32(arena), true, false);

    // Networking / sockets
    intrinsics_register(arena, reg, "socket", NULL, (LuvType*[]){}, 0, type_new(arena, TYPE_ANY), true, false);
    intrinsics_register(arena, reg, "connect", NULL, (LuvType*[]){type_new(arena, TYPE_ANY), type_string(arena)}, 2, type_bool(arena), true, false);
    intrinsics_register(arena, reg, "accept", NULL, (LuvType*[]){}, 0, type_new(arena, TYPE_ANY), true, false);
    intrinsics_register(arena, reg, "send", NULL, (LuvType*[]){type_new(arena, TYPE_ANY), type_new(arena, TYPE_LIST)}, 2, type_i32(arena), true, false);
    intrinsics_register(arena, reg, "recv", NULL, (LuvType*[]){type_new(arena, TYPE_ANY), type_new(arena, TYPE_LIST)}, 2, type_i32(arena), true, false);

    return reg;
}

void intrinsics_register(Arena* arena, IntrinsicRegistry* reg, const char* name, LuvType* owner_type, LuvType** params, size_t param_count, LuvType* ret, bool is_io, bool is_comptime_safe) {
    if (reg->count >= reg->capacity) return; // Should realloc
    
    Intrinsic* in = (Intrinsic*)arena_alloc(arena, sizeof(Intrinsic));
    in->name = name;
    in->owner_type = owner_type;
    in->param_types = (LuvType**)arena_alloc(arena, sizeof(LuvType*) * (param_count > 0 ? param_count : 1));
    for (size_t i = 0; i < param_count; i++) in->param_types[i] = params[i];
    in->param_count = param_count;
    in->return_type = ret;
    in->is_io = is_io;
    in->is_comptime_safe = is_comptime_safe;
    
    reg->intrinsics[reg->count++] = in;
}

Intrinsic* intrinsics_lookup(IntrinsicRegistry* reg, const char* name, LuvType* owner_type) {
    for (size_t i = 0; i < reg->count; i++) {
        Intrinsic* in = reg->intrinsics[i];
        if (strcmp(in->name, name) == 0) {
            if (owner_type == NULL && in->owner_type == NULL) return in;
            if (owner_type && in->owner_type && owner_type->kind == in->owner_type->kind) return in;
        }
    }
    return NULL;
}
