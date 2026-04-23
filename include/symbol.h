#ifndef LUV_SYMBOL_H
#define LUV_SYMBOL_H

#include "ast.h"
#include "type.h"

typedef enum {
    SYMBOL_VAR,
    SYMBOL_FUNC,
    SYMBOL_TYPE
} SymbolKind;

typedef struct Symbol Symbol;
struct Symbol {
    const char* name;
    LuvType* type;
    SymbolKind kind;
    bool is_defined;
    bool is_moved;
    bool is_weak;
    bool is_refcounted;
    struct Symbol* borrowed_from;
    struct Scope* defined_in; /* scope where symbol was defined */
    struct Scope* class_scope; /* for type symbols (classes), the scope that contains member symbols */
    ASTNode* decl; /* pointer to AST node (for functions/types) */
};

typedef struct Scope Scope;

struct Scope {
    Arena* arena;
    Symbol** symbols;
    size_t count;
    size_t capacity;
    Scope* parent;
};


Scope* scope_new(Arena* arena, Scope* parent);
Symbol* scope_define(Arena* arena, Scope* s, const char* name, LuvType* type, SymbolKind kind);
Symbol* scope_lookup(Scope* s, const char* name);

#endif
