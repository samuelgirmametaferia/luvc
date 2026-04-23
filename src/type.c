#include "type.h"
#include <string.h>
#include <stdio.h>

LuvType* type_new(Arena* arena, LuvTypeKind kind) {
    LuvType* t = (LuvType*)arena_alloc(arena, sizeof(LuvType));
    memset(t, 0, sizeof(LuvType));
    t->kind = kind;
    return t;
}

bool type_equals(LuvType* a, LuvType* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    
    switch (a->kind) {
        case TYPE_LIST:
        case TYPE_ARRAY:
            if (!type_equals(a->as.list_array.element_type, b->as.list_array.element_type)) return false;
            if (a->kind == TYPE_ARRAY && a->as.list_array.size != b->as.list_array.size) return false;
            return true;
        case TYPE_FUNCTION:
            if (a->as.function.param_count != b->as.function.param_count) return false;
            if (!type_equals(a->as.function.return_type, b->as.function.return_type)) return false;
            for (size_t i = 0; i < a->as.function.param_count; i++) {
                if (!type_equals(a->as.function.param_types[i], b->as.function.param_types[i])) return false;
            }
            return true;
        case TYPE_PTR:
        case TYPE_OWN:
        case TYPE_REF:
            return type_equals(a->as.pointer.base_type, b->as.pointer.base_type);
        case TYPE_STRUCT:
        case TYPE_UNION:
        case TYPE_ENUM:
        case TYPE_CLASS:
        case TYPE_INTERFACE:
            return strcmp(a->name, b->name) == 0;
        default:
            return true;
    }
}

bool type_is_compatible(LuvType* target, LuvType* source) {
    if (!target || !source) return false;
    if (target->kind == TYPE_UNKNOWN || source->kind == TYPE_UNKNOWN) return true;
    if (type_equals(target, source)) return true;
    if (target->kind == TYPE_ANY) return true;
    if (target->kind == TYPE_DYN || source->kind == TYPE_DYN) return true;
    if (source->kind == TYPE_NEVER) return true;
    
    // Null compatibility
    if (source->kind == TYPE_NULL) {
        if (target->kind == TYPE_PTR || target->kind == TYPE_REF || target->kind == TYPE_OWN || target->kind == TYPE_DYN || target->kind == TYPE_CLASS) return true;
    }

    bool target_is_int = (target->kind >= TYPE_I8 && target->kind <= TYPE_U256);
    bool source_is_int = (source->kind >= TYPE_I8 && source->kind <= TYPE_U256);
    bool target_is_float = (target->kind >= TYPE_F16 && target->kind <= TYPE_F256);
    bool source_is_float = (source->kind >= TYPE_F16 && source->kind <= TYPE_F256);

    // Allow integer<->integer, float<->float and cross int<->float compatibility for now
    if ((target_is_int && source_is_int) || (target_is_float && source_is_float) || (target_is_float && source_is_int) || (target_is_int && source_is_float)) return true;

    return false;
}

const char* type_to_string(LuvType* t) {
    if (!t) return "unknown";
    switch (t->kind) {
        case TYPE_VOID: return "void";
        case TYPE_I32: return "i32";
        case TYPE_F32: return "f32";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_CHAR: return "char";
        case TYPE_LIST: return "list";
        case TYPE_ARRAY: return "array";
        case TYPE_STRUCT: return "struct";
        case TYPE_UNION: return "union";
        case TYPE_ENUM: return "enum";
        case TYPE_CLASS: return "class";
        case TYPE_INTERFACE: return "interface";
        case TYPE_FUNCTION: return "function";
        case TYPE_PTR: return "ptr";
        case TYPE_OWN: return "own";
        case TYPE_REF: return "ref";
        case TYPE_ANY: return "any";
        case TYPE_DYN: return "dyn";
        case TYPE_NULL: return "nen";
        case TYPE_NEVER: return "never";
        default: return "unknown";
    }
}

LuvType* type_void(Arena* arena) { return type_new(arena, TYPE_VOID); }
LuvType* type_bool(Arena* arena) { return type_new(arena, TYPE_BOOL); }
LuvType* type_i32(Arena* arena) { return type_new(arena, TYPE_I32); }
LuvType* type_f32(Arena* arena) { return type_new(arena, TYPE_F32); }
LuvType* type_string(Arena* arena) { return type_new(arena, TYPE_STRING); }
LuvType* type_dyn(Arena* arena) { return type_new(arena, TYPE_DYN); }
LuvType* type_nen(Arena* arena) { return type_new(arena, TYPE_NULL); }
