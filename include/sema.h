#ifndef LUV_SEMA_H
#define LUV_SEMA_H

#include "ast.h"
#include "symbol.h"
#include "intrinsics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debug flags for Sema (runtime) */
#define SEMA_DBG_COMPTIME (1u << 0)
#define SEMA_DBG_ALL 0xFFFFFFFFu

/*
 * Compile-time control: define SEMA_DEBUG to compile debug printing code into
 * the binary. When SEMA_DEBUG is not defined, all debug printing macros are
 * compiled out to no-ops (zero code).
 */
#ifdef SEMA_DEBUG
#define SEMA_DBG_PRINT_FLAG(s, flag, fmt, ...) \
    do { if ((s) && ((s)->debug_flags & (flag))) fprintf(stderr, fmt, ##__VA_ARGS__); } while(0)
#else
#define SEMA_DBG_PRINT_FLAG(s, flag, fmt, ...) ((void)0)
#endif

typedef struct {
    Arena* arena;
    Scope* global_scope;
    Scope* current_scope;
    Scope* current_function_scope; /* function's own scope for lifetime checks */
    IntrinsicRegistry* intrinsics;
    LuvType* current_function;
    ASTNode* current_class; /* currently analyzing a class/trait */
    bool had_error;
    int in_unsafe; /* nesting level of unsafe blocks */
    bool refcounting_enabled; /* enabled when weak refs are used */
    bool in_pure_function; /* currently analyzing a pure function */
    bool in_comptime; /* currently inside a comptime block */
    int loop_depth; /* nesting level of loops for break/continue checks */
    unsigned int debug_flags; /* runtime debug bitmask, set by sema_init from env var LUV_SEMA_DEBUG */
} Sema;

void sema_init(Sema* sema, Arena* arena);
void sema_analyze(Sema* sema, ASTNode* root);
void sema_error(Sema* s, int line, int col, const char* msg);
void sema_suggest(Sema* s, int line, int col, const char* msg);

#endif
