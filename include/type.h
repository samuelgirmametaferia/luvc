#ifndef LUV_TYPE_H
#define LUV_TYPE_H

#include "lexer.h"
#include "ast.h"
#include <stdbool.h>

typedef enum {
    TYPE_VOID,
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64, TYPE_I128, TYPE_I256,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64, TYPE_U128, TYPE_U256,
    TYPE_F16, TYPE_F32, TYPE_F64, TYPE_F128, TYPE_F256,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_CHAR,
    TYPE_LIST,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_UNION,
    TYPE_ENUM,
    TYPE_CLASS,
    TYPE_INTERFACE,
    TYPE_FUNCTION,
    TYPE_PTR,
    TYPE_OWN,
    TYPE_REF,
    TYPE_ANY,
    TYPE_DYN,
    TYPE_NULL,
    TYPE_NEVER,
    TYPE_UNKNOWN
} LuvTypeKind;

typedef struct LuvType LuvType;

struct LuvType {
    LuvTypeKind kind;
    bool is_mut;
    bool is_const;
    bool is_static;
    bool is_volatile;
    bool is_frozen;
    bool is_weak;
    bool is_pinned;
    bool is_restrict;
    
    // For composite types
    const char* name;
    union {
        struct {
            LuvType* element_type;
            size_t size; // For array
        } list_array;
        struct {
            LuvType** param_types;
            size_t param_count;
            LuvType* return_type;
            bool is_async;
        } function;
        struct {
            LuvType* base_type; // For ptr, own, ref
        } pointer;
    } as;
};

LuvType* type_new(Arena* arena, LuvTypeKind kind);
bool type_equals(LuvType* a, LuvType* b);
bool type_is_compatible(LuvType* target, LuvType* source);
const char* type_to_string(LuvType* type);

// Predefined primitive types
LuvType* type_void(Arena* arena);
LuvType* type_bool(Arena* arena);
LuvType* type_i32(Arena* arena);
LuvType* type_f32(Arena* arena);
LuvType* type_string(Arena* arena);
LuvType* type_dyn(Arena* arena);
LuvType* type_nen(Arena* arena);

#endif
