#include "symbol.h"
#include <string.h>
#include <stdio.h>

Scope* scope_new(Arena* arena, Scope* parent) {
    Scope* s = (Scope*)arena_alloc(arena, sizeof(Scope));
    s->arena = arena; // Need to store arena for realloc
    s->capacity = 128;
    s->count = 0;
    s->symbols = (Symbol**)arena_alloc(arena, sizeof(Symbol*) * s->capacity);
    s->parent = parent;
    return s;
}

Symbol* scope_define(Arena* arena, Scope* s, const char* name, LuvType* type, SymbolKind kind) {
    if (s->count >= s->capacity) {
        size_t new_cap = s->capacity * 2;
        Symbol** new_syms = (Symbol**)arena_alloc(arena, sizeof(Symbol*) * new_cap);
        memcpy(new_syms, s->symbols, sizeof(Symbol*) * s->count);
        s->symbols = new_syms;
        s->capacity = new_cap;
    }
    Symbol* sym = (Symbol*)arena_alloc(arena, sizeof(Symbol));
    sym->name = name;
    sym->type = type;
    sym->kind = kind;
    sym->is_defined = true;
    sym->is_moved = false;
    sym->is_weak = false;
    sym->is_refcounted = false;
    sym->borrowed_from = NULL;
    sym->defined_in = s;
    sym->decl = NULL;
    s->symbols[s->count++] = sym;
    return sym;
}

Symbol* scope_lookup(Scope* s, const char* name) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->symbols[i]->name, name) == 0) {
            return s->symbols[i];
        }
    }
    if (s->parent) {
        return scope_lookup(s->parent, name);
    }
    return NULL;
}
