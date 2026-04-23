#define _POSIX_C_SOURCE 200809L
#include "sema.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Prototype for comptime evaluator implemented in src/comptime.c */
extern bool comptime_execute_block(Sema* s, ASTNode* block);

void sema_error(Sema* s, int line, int col, const char* msg) {
    fprintf(stderr, "[Semantic Error at %d:%d] %s\n", line, col, msg);
    s->had_error = true;
}

void sema_warning(Sema* s, int line, int col, const char* msg) {
    (void)s;
    fprintf(stderr, "[Semantic Warning at %d:%d] %s\n", line, col, msg);
}

void sema_suggest(Sema* s, int line, int col, const char* msg) {
    (void)s;
    fprintf(stderr, "[Suggestion at %d:%d] %s\n", line, col, msg);
}

static bool ast_contains_kind(ASTNode* node, ASTNodeType kind) {
    if (!node) return false;
    if (node->type == kind) return true;
    switch (node->type) {
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) if (ast_contains_kind(node->as.block.statements[i], kind)) return true;
            return false;
        case AST_EXPR_STMT: return ast_contains_kind(node->as.expr_stmt.expr, kind);
        case AST_ASM: return node->as.asm_stmt.body ? ast_contains_kind(node->as.asm_stmt.body, kind) : false;
        case AST_IF: {
            if (ast_contains_kind(node->as.if_stmt.condition, kind)) return true;
            if (ast_contains_kind(node->as.if_stmt.then_branch, kind)) return true;
            if (node->as.if_stmt.else_branch && ast_contains_kind(node->as.if_stmt.else_branch, kind)) return true;
            return false;
        }
        case AST_WHILE: return ast_contains_kind(node->as.while_stmt.condition, kind) || ast_contains_kind(node->as.while_stmt.body, kind);
        case AST_FOR: return ast_contains_kind(node->as.for_stmt.init, kind) || ast_contains_kind(node->as.for_stmt.condition, kind) || ast_contains_kind(node->as.for_stmt.increment, kind) || ast_contains_kind(node->as.for_stmt.body, kind);
        case AST_RETURN: return node->as.return_stmt.value && ast_contains_kind(node->as.return_stmt.value, kind);
        case AST_ASSIGN: return true; /* Contains assignment node */
        case AST_COMPOUND_ASSIGN: return true;
        case AST_MOVE: return true;
        case AST_BORROW: return true;
        case AST_AWAIT: return true;
        case AST_YIELD: return true;
        case AST_BINARY: return ast_contains_kind(node->as.binary.left, kind) || ast_contains_kind(node->as.binary.right, kind);
        case AST_UNARY: return ast_contains_kind(node->as.unary.right, kind);
        case AST_CALL:
            for (size_t i = 0; i < node->as.call.arg_count; i++) if (ast_contains_kind(node->as.call.args[i], kind)) return true;
            return ast_contains_kind(node->as.call.callee, kind);
        case AST_MEMBER_ACCESS: return ast_contains_kind(node->as.member_access.object, kind);
        case AST_INDEX_ACCESS: return ast_contains_kind(node->as.index_access.object, kind) || ast_contains_kind(node->as.index_access.index, kind);
        case AST_SLICE: return ast_contains_kind(node->as.slice.object, kind) || ast_contains_kind(node->as.slice.start, kind) || ast_contains_kind(node->as.slice.end, kind) || ast_contains_kind(node->as.slice.step, kind);
        case AST_LIST_LITERAL:
            for (size_t i = 0; i < node->as.list_literal.count; i++) if (ast_contains_kind(node->as.list_literal.elements[i], kind)) return true;
            return false;
        case AST_TERNARY: return ast_contains_kind(node->as.ternary.condition, kind) || ast_contains_kind(node->as.ternary.then_expr, kind) || ast_contains_kind(node->as.ternary.else_expr, kind);
        case AST_CAST_EXPR: return ast_contains_kind(node->as.cast_expr.expr, kind) || ast_contains_kind(node->as.cast_expr.target_type, kind);
        case AST_LAMBDA: return ast_contains_kind(node->as.lambda.expr, kind);
        default: return false;
    }
}

const char* analyze_expression_to_name(Sema* s, ASTNode* node);

static bool comptime_expr_ok_inner(Sema* s, ASTNode* node) {
    if (!node) return true;
    SEMA_DBG_PRINT_FLAG(s, SEMA_DBG_COMPTIME, "DEBUG: comptime_expr_ok_inner enter node_type=%d at %d:%d\n", node->type, node->line, node->col);
    switch (node->type) {
        case AST_LITERAL: return true;
        case AST_IDENTIFIER: {
            char name[256]; size_t len = node->as.identifier.name.length; if (len > 255) len = 255;
            strncpy(name, node->as.identifier.name.start, len); name[len] = '\0';
            SEMA_DBG_PRINT_FLAG(s, SEMA_DBG_COMPTIME, "DEBUG: comptime identifier lookup '%s' in scope %p\n", name, (void*)s->current_scope);
            Symbol* sym = scope_lookup(s->current_scope, name);
            SEMA_DBG_PRINT_FLAG(s, SEMA_DBG_COMPTIME, "DEBUG: scope_lookup returned %p for '%s'\n", (void*)sym, name);
            if (sym) SEMA_DBG_PRINT_FLAG(s, SEMA_DBG_COMPTIME, "DEBUG: sym->decl=%p, decl->type=%d\n", (void*)sym->decl, sym->decl ? sym->decl->type : -1);
            if (!sym) { sema_error(s, node->line, node->col, "Undefined identifier in comptime context."); return false; }
            if (sym->decl && sym->decl->type == AST_VAR_DECL) {
                if (sym->decl->as.var_decl.is_comptime) return true;
                if (sym->decl->as.var_decl.init && comptime_expr_ok_inner(s, sym->decl->as.var_decl.init)) return true;
                sema_error(s, node->line, node->col, "Identifier is not a compile-time constant.");
                return false;
            } else if (sym->kind == SYMBOL_FUNC) {
                if (sym->decl && sym->decl->type == AST_FUNC && sym->decl->as.func_decl.is_comptime) return true;
                sema_error(s,node->line,node->col,"Function reference is not compile-time.");
                return false;
            } else {
                sema_error(s,node->line,node->col,"Identifier not allowed in comptime context.");
                return false;
            }
        }
        case AST_BINARY:
            if (!comptime_expr_ok_inner(s,node->as.binary.left) || !comptime_expr_ok_inner(s,node->as.binary.right)) { sema_error(s,node->line,node->col,"Binary expression contains non-comptime operand."); return false; }
            return true;
        case AST_UNARY:
            return comptime_expr_ok_inner(s,node->as.unary.right);
        case AST_TERNARY:
            if (!comptime_expr_ok_inner(s,node->as.ternary.condition)) { sema_error(s,node->line,node->col,"Ternary condition is not compile-time."); return false; }
            if (!comptime_expr_ok_inner(s,node->as.ternary.then_expr) || !comptime_expr_ok_inner(s,node->as.ternary.else_expr)) { sema_error(s,node->line,node->col,"Ternary branches are not compile-time."); return false; }
            return true;
        case AST_LIST_LITERAL:
            for (size_t i=0;i<node->as.list_literal.count;i++) if (!comptime_expr_ok_inner(s,node->as.list_literal.elements[i])) { sema_error(s,node->line,node->col,"List literal element not compile-time"); return false; } return true;
        case AST_MEMBER_ACCESS: {
            char mname[256]; size_t mlen = node->as.member_access.member.length; if (mlen>255) mlen=255; strncpy(mname,node->as.member_access.member.start,mlen); mname[mlen]='\0';
            Intrinsic* ii = intrinsics_lookup(s->intrinsics, mname, NULL);
            if (ii && ii->is_comptime_safe) return true;
            sema_error(s,node->line,node->col,"Member access not allowed in comptime context.");
            return false;
        }
        case AST_CALL: {
            ASTNode* callee = node->as.call.callee;
            const char* cname = NULL;
            char buf[256] = {0};
            if (callee->type == AST_IDENTIFIER) cname = analyze_expression_to_name(s, callee);
            else if (callee->type == AST_MEMBER_ACCESS) { size_t ml = callee->as.member_access.member.length; if (ml>255) ml=255; strncpy(buf, callee->as.member_access.member.start, ml); buf[ml]='\0'; cname = buf; }
            if (!cname) { sema_error(s,node->line,node->col,"Indirect calls not allowed in comptime."); return false; }
            Intrinsic* ii = intrinsics_lookup(s->intrinsics,cname,NULL);
            if (ii) {
                if (!ii->is_comptime_safe) { sema_error(s,node->line,node->col,"Intrinsic not comptime-safe."); return false; }
                for (size_t i=0;i<node->as.call.arg_count;i++) if (!comptime_expr_ok_inner(s,node->as.call.args[i])) { sema_error(s,node->as.call.args[i]->line,node->as.call.args[i]->col,"Argument not compile-time"); return false; }
                return true;
            }
            Symbol* sym = cname ? scope_lookup(s->global_scope, cname) : NULL;
            if (!sym || !sym->decl || sym->decl->type != AST_FUNC || !sym->decl->as.func_decl.is_comptime) { sema_error(s,node->line,node->col,"Call to non-comptime function in comptime context."); return false; }
            for (size_t i=0;i<node->as.call.arg_count;i++) if (!comptime_expr_ok_inner(s,node->as.call.args[i])) { sema_error(s,node->as.call.args[i]->line,node->as.call.args[i]->col,"Argument not compile-time"); return false; }
            return true;
        }
        default:
            sema_error(s,node->line,node->col,"Expression not allowed in comptime context.");
            return false;
    }
}

static bool comptime_expr_ok(Sema* s, ASTNode* node) {
    static int depth = 0;
    if (!node) return true;
    depth++;
    if (depth > 1024) {
        sema_error(s, node->line, node->col, "Comptime recursion depth exceeded (possible cycle).");
        depth--;
        return false;
    }
    bool res = comptime_expr_ok_inner(s, node);
    depth--;
    return res;
}

/* Validate that a statement is allowed inside a comptime block. This is
   conservative: statements are permitted when their component expressions
   are compile-time and the control-flow cannot trivially produce an
   infinite loop. Detailed compile-time execution is not performed here. */
static bool comptime_stmt_ok(Sema* s, ASTNode* stmt) {
    if (!stmt) return true;
    SEMA_DBG_PRINT_FLAG(s, SEMA_DBG_COMPTIME, "DEBUG: comptime_stmt_ok node_type=%d at %d:%d\n", stmt->type, stmt->line, stmt->col);
    switch (stmt->type) {
        case AST_VAR_DECL:
            if (stmt->as.var_decl.init && !comptime_expr_ok(s, stmt->as.var_decl.init)) {
                sema_error(s, stmt->line, stmt->col, "Variable initializer is not compile-time.");
                return false;
            }
            return true;
        case AST_EXPR_STMT:
            if (!comptime_expr_ok(s, stmt->as.expr_stmt.expr)) {
                sema_error(s, stmt->line, stmt->col, "Expression is not compile-time.");
                return false;
            }
            return true;
        case AST_BLOCK:
            for (size_t i = 0; i < stmt->as.block.count; i++) {
                if (!comptime_stmt_ok(s, stmt->as.block.statements[i])) return false;
            }
            return true;
        case AST_IF:
            if (!comptime_expr_ok(s, stmt->as.if_stmt.condition)) { sema_error(s, stmt->line, stmt->col, "If condition is not compile-time."); return false; }
            if (!comptime_stmt_ok(s, stmt->as.if_stmt.then_branch)) return false;
            if (stmt->as.if_stmt.else_branch && !comptime_stmt_ok(s, stmt->as.if_stmt.else_branch)) return false;
            return true;
        case AST_WHILE:
            if (!comptime_expr_ok(s, stmt->as.while_stmt.condition)) { sema_error(s, stmt->line, stmt->col, "While condition is not compile-time."); return false; }
            if (stmt->as.while_stmt.condition->type == AST_LITERAL && stmt->as.while_stmt.condition->as.literal.token.type == TOKEN_TRUE) {
                if (!ast_contains_kind(stmt->as.while_stmt.body, AST_BREAK) && !ast_contains_kind(stmt->as.while_stmt.body, AST_RETURN)) {
                    sema_error(s, stmt->line, stmt->col, "Potential infinite loop in comptime context (condition is constant 'true' with no break).");
                    return false;
                }
            }
            return comptime_stmt_ok(s, stmt->as.while_stmt.body);
        case AST_FOR:
            if (stmt->as.for_stmt.init) {
                /* init may be a declaration or expression; accept if comptime */
                if (stmt->as.for_stmt.init->type == AST_VAR_DECL) {
                    if (!comptime_stmt_ok(s, stmt->as.for_stmt.init)) return false;
                } else if (stmt->as.for_stmt.init->type == AST_EXPR_STMT) {
                    if (!comptime_expr_ok(s, stmt->as.for_stmt.init->as.expr_stmt.expr)) { sema_error(s, stmt->line, stmt->col, "For-loop init is not compile-time."); return false; }
                }
            }
            if (!stmt->as.for_stmt.condition) {
                sema_error(s, stmt->line, stmt->col, "For loop without condition not allowed in comptime (possible infinite loop).");
                return false;
            }
            if (!comptime_expr_ok(s, stmt->as.for_stmt.condition)) { sema_error(s, stmt->line, stmt->col, "For loop condition is not compile-time."); return false; }
            if (stmt->as.for_stmt.condition->type == AST_LITERAL && stmt->as.for_stmt.condition->as.literal.token.type == TOKEN_TRUE) {
                if (!ast_contains_kind(stmt->as.for_stmt.body, AST_BREAK) && !ast_contains_kind(stmt->as.for_stmt.body, AST_RETURN)) {
                    sema_error(s, stmt->line, stmt->col, "Potential infinite loop in comptime context (for condition is constant 'true' with no break).");
                    return false;
                }
            }
            if (stmt->as.for_stmt.increment && !comptime_expr_ok(s, stmt->as.for_stmt.increment)) { sema_error(s, stmt->line, stmt->col, "For loop increment is not compile-time."); return false; }
            return comptime_stmt_ok(s, stmt->as.for_stmt.body);
        case AST_FOR_IN:
            if (!comptime_expr_ok(s, stmt->as.for_in.iterable)) { sema_error(s, stmt->line, stmt->col, "Iterable in 'for-in' is not compile-time."); return false; }
            return comptime_stmt_ok(s, stmt->as.for_in.body);
        case AST_RETURN:
            if (stmt->as.return_stmt.value && !comptime_expr_ok(s, stmt->as.return_stmt.value)) { sema_error(s, stmt->line, stmt->col, "Return expression is not compile-time."); return false; }
            return true;
        case AST_BREAK:
        case AST_CONTINUE:
            return true;
        case AST_MATCH:
            if (!comptime_expr_ok(s, stmt->as.match_stmt.target)) { sema_error(s, stmt->line, stmt->col, "Match target not compile-time."); return false; }
            for (size_t ai = 0; ai < stmt->as.match_stmt.arm_count; ai++) {
                ASTNode* arm = stmt->as.match_stmt.arms[ai];
                if (arm->as.match_arm.pattern && !comptime_expr_ok(s, arm->as.match_arm.pattern)) { sema_error(s, arm->line, arm->col, "Match pattern not compile-time."); return false; }
                if (!comptime_stmt_ok(s, arm->as.match_arm.body)) return false;
            }
            return true;
        case AST_SWITCH:
            if (!comptime_expr_ok(s, stmt->as.switch_stmt.target)) { sema_error(s, stmt->line, stmt->col, "Switch target not compile-time."); return false; }
            for (size_t ci = 0; ci < stmt->as.switch_stmt.case_count; ci++) {
                ASTNode* sc = stmt->as.switch_stmt.cases[ci];
                if (sc->as.switch_case.value && !comptime_expr_ok(s, sc->as.switch_case.value)) { sema_error(s, sc->line, sc->col, "Switch case value not compile-time."); return false; }
                if (!comptime_stmt_ok(s, sc->as.switch_case.body)) return false;
            }
            if (stmt->as.switch_stmt.default_case && !comptime_stmt_ok(s, stmt->as.switch_stmt.default_case)) return false;
            return true;
        default:
            sema_error(s, stmt->line, stmt->col, "Statement not allowed in comptime block.");
            return false;
    }
}

const char* analyze_expression_to_name(Sema* s, ASTNode* node);
static LuvType* analyze_expression(Sema* s, ASTNode* node);

void sema_init(Sema* sema, Arena* arena) {
    sema->arena = arena;
    sema->global_scope = scope_new(arena, NULL);
    sema->current_scope = sema->global_scope;
    sema->current_function_scope = NULL;
    sema->intrinsics = intrinsics_init(arena);
    /* Register intrinsics into the global scope as callable function symbols so
       pure checks and call-site resolution can treat them like normal functions. */
    if (sema->intrinsics) {
        for (size_t _ii = 0; _ii < sema->intrinsics->count; _ii++) {
            Intrinsic* in = sema->intrinsics->intrinsics[_ii];
            if (!in || !in->name) continue;
            LuvType* ft = type_new(sema->arena, TYPE_FUNCTION);
            ft->as.function.return_type = in->return_type ? in->return_type : type_void(sema->arena);
            ft->as.function.param_count = in->param_count;
            ft->as.function.is_async = false;
            ft->as.function.param_types = in->param_types;
            Symbol* s = scope_define(sema->arena, sema->global_scope, strdup(in->name), ft, SYMBOL_FUNC);
            if (s) s->decl = NULL; /* intrinsic, no AST decl */
        }
    }
    sema->had_error = false;
    sema->current_function = NULL;
    sema->current_class = NULL;
    sema->in_unsafe = 0;
    sema->refcounting_enabled = false;
    sema->in_pure_function = false;
    sema->in_comptime = false;
    sema->loop_depth = 0;

    /* Initialize runtime debug flags (only used if compiled with SEMA_DEBUG) */
#ifdef SEMA_DEBUG
    sema->debug_flags = 0;
    const char* dbg_env = getenv("LUV_SEMA_DEBUG");
    if (dbg_env) {
        if (strcmp(dbg_env, "all") == 0) sema->debug_flags = SEMA_DBG_ALL;
        else if (strcmp(dbg_env, "comptime") == 0) sema->debug_flags = SEMA_DBG_COMPTIME;
        else {
            char* endptr = NULL;
            long v = strtol(dbg_env, &endptr, 0);
            if (endptr != dbg_env) sema->debug_flags = (unsigned int)v;
        }
    }
#else
    sema->debug_flags = 0;
#endif

    // Define primitives in global scope as types
    scope_define(arena, sema->global_scope, "i8", type_new(arena, TYPE_I8), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "i16", type_new(arena, TYPE_I16), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "i32", type_i32(arena), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "i64", type_new(arena, TYPE_I64), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "i128", type_new(arena, TYPE_I128), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "i256", type_new(arena, TYPE_I256), SYMBOL_TYPE);

    scope_define(arena, sema->global_scope, "u8", type_new(arena, TYPE_U8), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "u16", type_new(arena, TYPE_U16), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "u32", type_new(arena, TYPE_U32), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "u64", type_new(arena, TYPE_U64), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "u128", type_new(arena, TYPE_U128), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "u256", type_new(arena, TYPE_U256), SYMBOL_TYPE);

    scope_define(arena, sema->global_scope, "f16", type_new(arena, TYPE_F16), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "f32", type_f32(arena), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "f64", type_new(arena, TYPE_F64), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "f128", type_new(arena, TYPE_F128), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "f256", type_new(arena, TYPE_F256), SYMBOL_TYPE);

    scope_define(arena, sema->global_scope, "bool", type_bool(arena), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "string", type_string(arena), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "char", type_new(arena, TYPE_CHAR), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "void", type_void(arena), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "dyn", type_dyn(arena), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "any", type_new(arena, TYPE_ANY), SYMBOL_TYPE);
    scope_define(arena, sema->global_scope, "never", type_new(arena, TYPE_NEVER), SYMBOL_TYPE);
}

static LuvType* resolve_type_node(Sema* s, ASTNode* node) {
    if (!node) return type_new(s->arena, TYPE_UNKNOWN);
    if (node->type == AST_IDENTIFIER) {
        char name[256];
        size_t len = node->as.identifier.name.length;
        if (len > 255) len = 255;
        strncpy(name, node->as.identifier.name.start, len);
        name[len] = '\0';
        Symbol* sym = scope_lookup(s->current_scope, name);
        if (sym && sym->kind == SYMBOL_TYPE) return sym->type;
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "Unknown type '%.*s'.", (int)node->as.identifier.name.length, node->as.identifier.name.start);
            sema_error(s, node->line, node->col, buf);
        }
    }
    return type_new(s->arena, TYPE_UNKNOWN);
}

static void analyze_statement(Sema* s, ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_TRY_CATCH: {
            analyze_statement(s, node->as.try_catch.try_body);
            Scope* prev = s->current_scope;
            s->current_scope = scope_new(s->arena, prev);
            char ename[256]; size_t elen = node->as.try_catch.error_name.length; if (elen > 255) elen = 255;
            strncpy(ename, node->as.try_catch.error_name.start, elen); ename[elen] = '\0';
            scope_define(s->arena, s->current_scope, strdup(ename), type_dyn(s->arena), SYMBOL_VAR);
            analyze_statement(s, node->as.try_catch.catch_body);
            s->current_scope = prev;
            break;
        }
        case AST_THROW: {
            if (node->as.throw_stmt.value) analyze_expression(s, node->as.throw_stmt.value);
            break;
        }
        case AST_SPARK: {
            analyze_expression(s, node->as.spark.expr);
            break;
        }
        case AST_BLOCK: {
            Scope* prev = s->current_scope;
            s->current_scope = scope_new(s->arena, prev);
            bool prev_in_comptime = s->in_comptime;
            if (node->as.block.is_comptime) {
                s->in_comptime = true;
                if (!comptime_execute_block(s, node)) {
                    sema_error(s, node->line, node->col, "Comptime evaluation failed.");
                }
            }
            for (size_t i = 0; i < node->as.block.count; i++) {
                ASTNode* stmt = node->as.block.statements[i];
                if (s->in_comptime) {
                    if (!comptime_stmt_ok(s, stmt)) {
                        /* comptime_stmt_ok reports a specific error; continue analysis to surface more issues */
                    }
                }
                analyze_statement(s, stmt);
            }
            s->in_comptime = prev_in_comptime;
            s->current_scope = prev;
            break;
        }
        case AST_CLASS: {
            char cname[256]; size_t clen = node->as.class_decl.name.length; if (clen > 255) clen = 255;
            strncpy(cname, node->as.class_decl.name.start, clen); cname[clen] = '\0';
            Symbol* csym = scope_lookup(s->current_scope, cname);
            if (!csym) {
                LuvType* ctype = type_new(s->arena, TYPE_CLASS);
                ctype->name = strdup(cname);
                csym = scope_define(s->arena, s->current_scope, strdup(cname), ctype, SYMBOL_TYPE);
                if (csym) csym->decl = node;
            }
            Scope* prev_scope = s->current_scope;
            if (!csym->class_scope) csym->class_scope = scope_new(s->arena, prev_scope);
            s->current_scope = csym->class_scope;
            ASTNode* prev_class = s->current_class;
            s->current_class = node;
            analyze_statement(s, node->as.class_decl.body);

            if (!node->as.class_decl.is_abstract) {
                ASTNode* body = node->as.class_decl.body;
                for (size_t i = 0; i < body->as.block.count; i++) {
                    ASTNode* stmt = body->as.block.statements[i];
                    if (stmt->type == AST_FUNC && stmt->as.func_decl.is_abstract) {
                        sema_error(s, node->line, node->col, "Class contains abstract method but is not marked 'abstract'.");
                        sema_suggest(s, node->line, node->col, "Fix-it: mark the class 'abstract' or remove 'abstract' from the method.");
                        break;
                    }
                }
            }

            s->current_class = prev_class;
            s->current_scope = prev_scope;
            break;
        }
        case AST_FUNC: {
            /* Modifier collision checks */
            if (node->as.func_decl.is_extern || node->as.func_decl.is_export) {
                if (s->current_scope != s->global_scope) {
                    sema_error(s, node->line, node->col, "'extern' or 'export' can only be used on top-level declarations.");
                }
            }
            if (node->as.func_decl.is_static && node->as.func_decl.is_virtual) {
                sema_error(s, node->line, node->col, "Function cannot be both 'static' and 'virtual'.");
                sema_suggest(s, node->line, node->col, "Fix-it: remove 'static' or 'virtual' from the function declaration.");
            }
            if (node->as.func_decl.is_static && node->as.func_decl.is_override) {
                sema_error(s, node->line, node->col, "'static' function cannot 'override'.");
                sema_suggest(s, node->line, node->col, "Fix-it: remove 'static' or 'override' modifier.");
            }
            if (node->as.func_decl.is_override) {
                if (s->current_class == NULL) {
                    sema_error(s, node->line, node->col, "'override' used outside of a class context.");
                    sema_suggest(s, node->line, node->col, "Fix-it: remove 'override' or place the function inside a class that extends a base.");
                } else if (!s->current_class->as.class_decl.base_class) {
                    sema_error(s, node->line, node->col, "'override' used but class has no base to override.");
                    sema_suggest(s, node->line, node->col, "Fix-it: remove 'override' or add a base class with the virtual declaration.");
                } else {
                    /* If base exists, attempt to locate base method and validate signature/virtuality */
                    ASTNode* base_expr = s->current_class->as.class_decl.base_class;
                    if (base_expr && base_expr->type == AST_IDENTIFIER) {
                        char bname[256]; size_t bl = base_expr->as.identifier.name.length; if (bl > 255) bl = 255; strncpy(bname, base_expr->as.identifier.name.start, bl); bname[bl] = '\0';
                        Symbol* base_sym = scope_lookup(s->global_scope, bname);
                        if (base_sym && base_sym->decl && base_sym->decl->type == AST_CLASS) {
                            ASTNode* base_body = base_sym->decl->as.class_decl.body;
                            bool found_base_method = false;
                            for (size_t bi = 0; bi < base_body->as.block.count; bi++) {
                                ASTNode* bstmt = base_body->as.block.statements[bi];
                                if (bstmt->type != AST_FUNC) continue;
                                char bfname[256]; size_t bflen = bstmt->as.func_decl.name.length; if (bflen > 255) bflen = 255; strncpy(bfname, bstmt->as.func_decl.name.start, bflen); bfname[bflen] = '\0';
                                char fname[256]; size_t flen = node->as.func_decl.name.length; if (flen > 255) flen = 255; strncpy(fname, node->as.func_decl.name.start, flen); fname[flen] = '\0';
                                if (strcmp(bfname, fname) == 0) {
                                    found_base_method = true;
                                    /* final methods cannot be overridden */
                                    if (bstmt->as.func_decl.is_final) {
                                        sema_error(s, node->line, node->col, "Cannot override method marked 'final'.");
                                        sema_suggest(s, node->line, node->col, "Fix-it: remove 'override' or remove 'final' from the base method.");
                                    } else {
                                        /* base method must be virtual */
                                        if (!bstmt->as.func_decl.is_virtual) {
                                            sema_error(s, node->line, node->col, "'override' used but base method is not virtual.");
                                            sema_suggest(s, node->line, node->col, "Fix-it: mark base method 'virtual' or remove 'override' from this method.");
                                        }
                                        /* signature compatibility: param count must match */
                                        if (bstmt->as.func_decl.param_count != node->as.func_decl.param_count) {
                                            char buf[512]; snprintf(buf, sizeof(buf), "Override signature mismatch: base has %zu params but override has %zu.", bstmt->as.func_decl.param_count, node->as.func_decl.param_count);
                                            sema_error(s, node->line, node->col, buf);
                                            sema_suggest(s, node->line, node->col, "Fix-it: align parameter count and types with base method signature.");
                                        }
                                    }
                                    /* Further type checks can be added when types are resolved */
                                    break;
                                }
                            }
                            if (!found_base_method) {
                                char buf[512]; snprintf(buf, sizeof(buf), "'override' used but no base method '%s' found in '%s'.", node->as.func_decl.name.start, bname);
                                sema_error(s, node->line, node->col, buf);
                                sema_suggest(s, node->line, node->col, "Fix-it: define the base virtual method or remove 'override' from this method.");
                            }
                        }
                    }
                }
            }
            if (node->as.func_decl.is_inline && node->as.func_decl.is_virtual) {
                /* Attempt devirtualization via owner/param analysis.
                   Heuristic: if the function's first parameter (owner/self) type is known and
                   no other registered functions with the same name accept a different owner type,
                   treat the function as non-virtual (devirtualized). */
                char _fname[256]; size_t _flen = node->as.func_decl.name.length; if (_flen > 255) _flen = 255; strncpy(_fname, node->as.func_decl.name.start, _flen); _fname[_flen] = '\0';
                Symbol* fglob = scope_lookup(s->global_scope, _fname);
                LuvType* owner_type = NULL;
                LuvType* ftype_local = fglob && fglob->kind == SYMBOL_FUNC ? fglob->type : NULL;
                if (ftype_local && ftype_local->as.function.param_count > 0) owner_type = ftype_local->as.function.param_types[0];

                bool override_found = false;
                if (owner_type != NULL) {
                    for (size_t _i = 0; _i < s->global_scope->count; _i++) {
                        Symbol* _sym = s->global_scope->symbols[_i];
                        if (!_sym || _sym->kind != SYMBOL_FUNC) continue;
                        if (strcmp(_sym->name, _fname) != 0) continue;
                        LuvType* other_ft = _sym->type;
                        if (!other_ft || other_ft->as.function.param_count == 0) { override_found = true; break; }
                        LuvType* other_owner = other_ft->as.function.param_types[0];
                        if (!type_equals(owner_type, other_owner)) { override_found = true; break; }
                    }

                    if (!override_found) {
                        sema_warning(s, node->line, node->col, "'inline' on virtual function: devirtualized (no overrides found).");
                        node->as.func_decl.is_virtual = false; /* allow inlining to proceed */
                        sema_suggest(s, node->line, node->col, "Fix-it: remove 'virtual' or mark function 'final' to document devirtualization (e.g., fn name(...) final { ... }).");
                    } else {
                        sema_warning(s, node->line, node->col, "'inline' ignored on virtual functions (overrides exist). ");
                        sema_suggest(s, node->line, node->col, "Suggestion: mark class 'sealed' or this function 'final' to enable devirtualization where safe.");
                        node->as.func_decl.is_inline = false;
                    }
                } else {
                    /* Fallback to older override-detection if owner unknown */
                    bool simple_override_found = false;
                    if (s->global_scope) {
                        for (size_t _i = 0; _i < s->global_scope->count; _i++) {
                            Symbol* _sym = s->global_scope->symbols[_i];
                            if (_sym && _sym->kind == SYMBOL_FUNC && _sym->decl) {
                                if (_sym->decl->as.func_decl.is_override && strcmp(_sym->name, _fname) == 0) {
                                    simple_override_found = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!simple_override_found) {
                        sema_warning(s, node->line, node->col, "'inline' on virtual function: devirtualized (no overrides found).");
                        node->as.func_decl.is_virtual = false;
                        sema_suggest(s, node->line, node->col, "Fix-it: remove 'virtual' or mark function 'final' to document devirtualization.");
                    } else {
                        sema_warning(s, node->line, node->col, "'inline' ignored on virtual functions (overrides exist). Consider marking the function 'final' or 'sealed' to enable devirtualization.");
                        node->as.func_decl.is_inline = false;
                    }
                }
            }

            if (node->as.func_decl.is_static) {
                for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                    ASTNode* p = node->as.func_decl.params[i];
                    if (p->as.var_decl.name.length == 4 && strncmp(p->as.var_decl.name.start, "self", 4) == 0) {
                        sema_error(s, node->line, node->col, "Static function cannot have a 'self' parameter.");
                        sema_suggest(s, node->line, node->col, "Fix-it: remove 'static' or remove the 'self' parameter.");
                    }
                }
            }
            if (node->as.func_decl.is_abstract && node->as.func_decl.body) {
                sema_error(s, node->line, node->col, "Abstract function must not have implementation.");
            }

            /* comptime + async is invalid */
            if (node->as.func_decl.is_comptime && node->as.func_decl.is_async) {
                sema_error(s, node->line, node->col, "Function modifiers conflict: 'comptime' cannot be combined with 'async'.");
            }

            /* pure vs unsafe handling */
            if (node->as.func_decl.is_pure) {
                if (node->as.func_decl.is_unsafe) {
                    if (node->as.func_decl.is_unsafe_prefix) {
                        sema_warning(s, node->line, node->col, "Function declared 'unsafe pure': 'pure' ignored; function treated as unsafe.");
                        node->as.func_decl.is_pure = false;
                    } else {
                        sema_error(s, node->line, node->col, "Conflicting modifiers: 'pure' cannot be combined with 'unsafe'. To opt out write 'unsafe pure'.");
                    }
                } else {
                    if (ast_contains_kind(node->as.func_decl.body, AST_UNSAFE_BLOCK)) {
                        sema_error(s, node->line, node->col, "Pure function contains 'unsafe' block; declare as 'unsafe pure' to opt out.");
                    }
                    if (ast_contains_kind(node->as.func_decl.body, AST_ASSIGN) || ast_contains_kind(node->as.func_decl.body, AST_COMPOUND_ASSIGN) || ast_contains_kind(node->as.func_decl.body, AST_MOVE) || ast_contains_kind(node->as.func_decl.body, AST_BORROW) || ast_contains_kind(node->as.func_decl.body, AST_DEREF) || ast_contains_kind(node->as.func_decl.body, AST_ALLOC) || ast_contains_kind(node->as.func_decl.body, AST_DEALLOC)) {
                        sema_error(s, node->line, node->col, "Pure function must not perform mutation or moves.");
                    }
                    if (node->as.func_decl.is_async && ast_contains_kind(node->as.func_decl.body, AST_AWAIT)) {
                        sema_error(s, node->line, node->col, "Pure async functions cannot contain 'await'.");
                    }
                }
            }

            // Create a new scope for function body and register parameters
            char fname[256]; size_t flen = node->as.func_decl.name.length; if (flen > 255) flen = 255; strncpy(fname, node->as.func_decl.name.start, flen); fname[flen] = '\0';
            Symbol* fsym = scope_lookup(s->current_scope, fname);
            LuvType* ftype = fsym && fsym->kind == SYMBOL_FUNC ? fsym->type : NULL;

            Scope* prev = s->current_scope;
            s->current_scope = scope_new(s->arena, prev);
            /* record the function scope for lifetime checks */
            Scope* prev_func_scope = s->current_function_scope;
            s->current_function_scope = s->current_scope;

            // Define parameters in function scope
            if (ftype) {
                for (size_t i = 0; i < ftype->as.function.param_count; i++) {
                    ASTNode* param = node->as.func_decl.params[i];
                    char pname[256]; size_t plen = param->as.var_decl.name.length; if (plen > 255) plen = 255; strncpy(pname, param->as.var_decl.name.start, plen); pname[plen] = '\0';
                    LuvType* ptype = ftype->as.function.param_types[i];
                    scope_define(s->arena, s->current_scope, strdup(pname), ptype, SYMBOL_VAR);
                }
            }

            // Analyze body with current function tracking
            int prev_in_unsafe = s->in_unsafe;
            if (node->as.func_decl.is_unsafe) s->in_unsafe++;
            s->current_function = ftype;
            s->in_pure_function = node->as.func_decl.is_pure;
            analyze_statement(s, node->as.func_decl.body);

            /* Enforce simple "never" semantics: if the function's declared return
               type is 'never', it must not contain returns and should exhibit an
               obvious divergence (throw or an explicit loop). This is a
               conservative check — full control-flow analysis would be required
               for soundness. */
            if (ftype && ftype->as.function.return_type && ftype->as.function.return_type->kind == TYPE_NEVER) {
                if (ast_contains_kind(node->as.func_decl.body, AST_RETURN)) {
                    sema_error(s, node->line, node->col, "Function declared 'never' must not return a value.");
                }
                if (!ast_contains_kind(node->as.func_decl.body, AST_THROW) && !ast_contains_kind(node->as.func_decl.body, AST_LOOP)) {
                    sema_warning(s, node->line, node->col, "Function declared 'never' does not contain an obvious divergence (throw or infinite loop).");
                }
            }

            s->in_pure_function = false;
            s->current_function = NULL;
            s->in_unsafe = prev_in_unsafe;

            /* restore previous function scope */
            s->current_function_scope = prev_func_scope;
            s->current_scope = prev;
            break;
        }
        case AST_VAR_DECL: {
            /* Modifier collision checks */
            if (node->as.var_decl.is_extern || node->as.var_decl.is_export) {
                if (s->current_scope != s->global_scope) {
                    sema_error(s, node->line, node->col, "'extern' or 'export' can only be used on top-level declarations.");
                }
            }
            if (node->as.var_decl.is_const && node->as.var_decl.is_mut) {
                sema_error(s, node->line, node->col, "Variable modifiers conflict: 'const' cannot be combined with 'mut'.");
            }
            if (node->as.var_decl.is_const && node->as.var_decl.is_volatile) {
                sema_warning(s, node->line, node->col, "Variable declared 'const' and 'volatile': treating as volatile (non-const).");
            }
            if (node->as.var_decl.is_static && node->as.var_decl.is_comptime) {
                sema_error(s, node->line, node->col, "Variable modifiers conflict: 'static' and 'comptime' cannot be combined.");
            }
            if (node->as.var_decl.is_frozen && node->as.var_decl.is_mut) {
                sema_error(s, node->line, node->col, "'frozen' cannot be combined with 'mut'.");
            }

            LuvType* type = NULL;
            if (node->as.var_decl.type_node) {
                type = resolve_type_node(s, node->as.var_decl.type_node);
            } else if (node->as.var_decl.is_dyn) {
                type = type_dyn(s->arena);
            }
            
            if (node->as.var_decl.init) {
                LuvType* init_type = analyze_expression(s, node->as.var_decl.init);
                if (type == NULL || type->kind == TYPE_UNKNOWN) {
                    type = init_type;
                } else if (!type_is_compatible(type, init_type)) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Type mismatch in variable initialization: expected %s but got %s", type_to_string(type), type_to_string(init_type));
                    sema_error(s, node->line, node->col, buf);
                }
            }
            
            if (type == NULL) type = type_new(s->arena, TYPE_UNKNOWN);
            
            // Handle qualifiers
            if (node->as.var_decl.is_own) {
                LuvType* own_type = type_new(s->arena, TYPE_OWN);
                own_type->as.pointer.base_type = type;
                type = own_type;
            } else if (node->as.var_decl.is_ptr) {
                LuvType* ptr_type = type_new(s->arena, TYPE_PTR);
                ptr_type->as.pointer.base_type = type;
                type = ptr_type;
            } else if (node->as.var_decl.is_ref) {
                LuvType* ref_type = type_new(s->arena, TYPE_REF);
                ref_type->as.pointer.base_type = type;
                type = ref_type;
            }
            
            /* Variables are mutable by default unless declared const. */
            type->is_mut = node->as.var_decl.is_mut || !node->as.var_decl.is_const;
            type->is_const = node->as.var_decl.is_const;

            if (node->as.var_decl.is_const && node->as.var_decl.is_volatile) {
                type->is_const = false;
                type->is_volatile = true;
            }

            // lazy + const: evaluated once then frozen (mark const)
            if (node->as.var_decl.is_lazy) {
                if (!(node->as.var_decl.is_static || s->current_scope == s->global_scope)) {
                    sema_error(s, node->line, node->col, "'lazy' can only be used on 'static' or top-level variables.");
                }
                if (node->as.var_decl.is_const) {
                    type->is_const = true;
                }
            }

            if (node->as.var_decl.is_frozen) {
                type->is_const = true;
                type->is_frozen = true;
            }

            // weak references: require pointer/own, enable refcounting
            if (node->as.var_decl.is_weak) {
                if (!(type->kind == TYPE_OWN || type->kind == TYPE_PTR)) {
                    sema_error(s, node->line, node->col, "'weak' applies only to owned or pointer types.");
                } else {
                    type->is_weak = true;
                    if (!s->refcounting_enabled) {
                        s->refcounting_enabled = true;
                        sema_warning(s, node->line, node->col, "Enabling implicit reference counting for weak references.");
                    }
                }
            }

            type->is_pinned = node->as.var_decl.is_pin;
            type->is_restrict = node->as.var_decl.is_restrict;

            if (node->as.var_decl.is_restrict && !(type->kind == TYPE_PTR || type->kind == TYPE_REF)) {
                sema_error(s, node->line, node->col, "'restrict' qualifier only applies to pointers or references.");
            }

            char name[256];
            size_t len = node->as.var_decl.name.length;
            if (len > 255) len = 255;
            strncpy(name, node->as.var_decl.name.start, len);
            name[len] = '\0';

            Symbol* existing = scope_lookup(s->current_scope, name);
            if (existing && existing->kind == SYMBOL_VAR) {
                // luv philosophy: Standard variables are reassignable.
                // ONLY 'const' blocks reassignment.
                if (existing->type->is_const) {
                    char buf[1024];
                    snprintf(buf, sizeof(buf), "Cannot reassign to a 'const' variable '%s'.", existing->name ? existing->name : name);
                    sema_error(s, node->line, node->col, buf);
                }

                // If inside a pure function, any reassignment is forbidden
                if (s->in_pure_function && node->as.var_decl.init) {
                    if (existing && existing->defined_in == s->global_scope) {
                        char buf2[512];
                        snprintf(buf2, sizeof(buf2), "Pure function must not mutate global variable '%s'. Consider making it 'const' or moving mutation out of the pure function.", existing->name ? existing->name : name);
                        sema_error(s, node->line, node->col, buf2);
                    } else {
                        sema_error(s, node->line, node->col, "Pure function must not perform mutation or moves. Consider marking the function 'unsafe pure' to opt out or move side-effects out.");
                    }
                }

                if (node->as.var_decl.init) {
                    // Update the symbol's declaration initializer so that comptime resolution
                    // and other passes can see the latest initializer for this variable.
                    if (existing->decl && existing->decl->type == AST_VAR_DECL) {
                        existing->decl->as.var_decl.init = node->as.var_decl.init;
                    }

                    LuvType* init_type = analyze_expression(s, node->as.var_decl.init);
                    if (existing->type->kind == TYPE_UNKNOWN) {
                        existing->type = init_type;
                    } else if (!type_is_compatible(existing->type, init_type)) {
                        char buf[1024];
                        snprintf(buf, sizeof(buf), "Type mismatch in reassignment: expected %s but got %s", type_to_string(existing->type), type_to_string(init_type));
                        sema_error(s, node->line, node->col, buf);
                    }

                    // Move semantics for 'own'
                    if (node->as.var_decl.init->type == AST_IDENTIFIER) {
                        char src_name[256]; size_t src_len = node->as.var_decl.init->as.identifier.name.length;
                        if (src_len > 255) src_len = 255;
                        strncpy(src_name, node->as.var_decl.init->as.identifier.name.start, src_len);
                        src_name[src_len] = '\0';
                        Symbol* src_sym = scope_lookup(s->current_scope, src_name);
                        if (src_sym && src_sym->type->kind == TYPE_OWN) src_sym->is_moved = true;
                    }
                }
            } else {
                scope_define(s->arena, s->current_scope, strdup(name), type, SYMBOL_VAR);
                // set weak flag on symbol if needed
                Symbol* new_sym = scope_lookup(s->current_scope, name);
                if (new_sym) {
                    new_sym->decl = node;
                    if (node->as.var_decl.is_weak) new_sym->is_weak = true;
                }
            }
            break;
        }        case AST_EXPR_STMT:
            if (node->as.expr_stmt.expr == NULL) {
                /* Parser represents opaque asm blocks as an empty expression-stmt.
                   Enforce semantic constraints here: not allowed in comptime or pure
                   contexts and must appear in an unsafe region. */
                if (s->in_comptime) {
                    sema_error(s, node->line, node->col, "'asm' block is not allowed in comptime context.");
                }
                if (s->in_pure_function) {
                    sema_error(s, node->line, node->col, "'asm' block not allowed in pure functions.");
                }
                if (s->in_unsafe == 0) {
                    sema_error(s, node->line, node->col, "'asm' block must be inside an 'unsafe' function or 'unsafe' block.");
                }
                /* treat asm as having void effect */
            } else {
                analyze_expression(s, node->as.expr_stmt.expr);
            }
            break;
        case AST_UNSAFE_BLOCK:
            /* Enter unsafe region: allow pointer arithmetic and other unsafe ops */
            s->in_unsafe++;
            analyze_statement(s, node->as.unsafe_block.body);
            s->in_unsafe--;
            break;
        case AST_IF:
            if (analyze_expression(s, node->as.if_stmt.condition)->kind != TYPE_BOOL) {
                sema_error(s, node->line, node->col, "If condition must be boolean.");
            }
            analyze_statement(s, node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch) analyze_statement(s, node->as.if_stmt.else_branch);
            break;
        case AST_WHILE:
            if (analyze_expression(s, node->as.while_stmt.condition)->kind != TYPE_BOOL) {
                sema_error(s, node->line, node->col, "While condition must be boolean.");
            }
            s->loop_depth++;
            analyze_statement(s, node->as.while_stmt.body);
            s->loop_depth--;
            break;
        case AST_FOR:
            if (node->as.for_stmt.init) analyze_statement(s, node->as.for_stmt.init);
            if (node->as.for_stmt.condition && analyze_expression(s, node->as.for_stmt.condition)->kind != TYPE_BOOL) {
                sema_error(s, node->line, node->col, "For condition must be boolean.");
            }
            if (node->as.for_stmt.increment) analyze_expression(s, node->as.for_stmt.increment);
            s->loop_depth++;
            analyze_statement(s, node->as.for_stmt.body);
            s->loop_depth--;
            break;
        case AST_FOR_IN:
            analyze_expression(s, node->as.for_in.iterable);
            s->loop_depth++;
            analyze_statement(s, node->as.for_in.body);
            s->loop_depth--;
            break;
        case AST_LOOP:
            s->loop_depth++;
            analyze_statement(s, node->as.loop_stmt.body);
            s->loop_depth--;
            break;
        case AST_BREAK:
            if (s->loop_depth == 0) {
                sema_error(s, node->line, node->col, "'break' used outside of a loop.");
            }
            break;
        case AST_CONTINUE:
            if (s->loop_depth == 0) {
                sema_error(s, node->line, node->col, "'continue' used outside of a loop.");
            }
            break;
        case AST_RETURN:
            if (!s->current_function) { sema_error(s, node->line, node->col, "Return outside function."); break; }
            if (node->as.return_stmt.value) {
                ASTNode* rexpr = node->as.return_stmt.value;
                /* Lifetime check: returning a borrow to a local variable is invalid */
                if (rexpr->type == AST_BORROW || rexpr->type == AST_ADDR_OF) {
                    ASTNode* inner = (rexpr->type == AST_BORROW) ? rexpr->as.borrow_expr.value : rexpr->as.addr_of.value;
                    if (inner && inner->type == AST_IDENTIFIER) {
                        char oname[256]; size_t ol = inner->as.identifier.name.length; if (ol > 255) ol = 255; strncpy(oname, inner->as.identifier.name.start, ol); oname[ol] = '\0';
                        Symbol* owner = scope_lookup(s->current_scope, oname);
                        if (owner) {
                            if (s->current_function_scope && owner->defined_in && owner->defined_in != s->current_function_scope) {
                                /* owner defined in an inner scope (descendant) -> dangling */
                                /* detect descendant relationship */
                                Scope* p = owner->defined_in; bool is_descendant = false;
                                while (p) { if (p == s->current_function_scope) { is_descendant = true; break; } p = p->parent; }
                                if (is_descendant && owner->defined_in != s->current_function_scope) {
                                    char buf[512]; snprintf(buf, sizeof(buf), "Returning borrow of local variable '%s' would create a dangling reference.", oname);
                                    sema_error(s, node->line, node->col, buf);
                                }
                            }
                            if (owner->defined_in == s->current_function_scope) {
                                /* parameter: only safe if parameter is a reference/mut/pointer */
                                if (!(owner->type && (owner->type->is_mut || owner->type->kind == TYPE_REF || owner->type->kind == TYPE_PTR))) {
                                    char buf[512]; snprintf(buf, sizeof(buf), "Returning borrow of parameter '%s' is unsafe (parameter is not a reference).", oname);
                                    sema_error(s, node->line, node->col, buf);
                                }
                            }
                        }
                    } else {
                        sema_error(s, node->line, node->col, "Returning borrow of a temporary value is invalid.");
                    }
                }

                LuvType* ret_type = analyze_expression(s, node->as.return_stmt.value);
                LuvType* fret = s->current_function->as.function.return_type;
                if (fret->kind == TYPE_UNKNOWN) {
                    s->current_function->as.function.return_type = ret_type;
                } else {
                    if (!type_is_compatible(fret, ret_type)) {
                    char buf[1024];
                    snprintf(buf, sizeof(buf), "Return type mismatch: expected %s but got %s", type_to_string(fret), type_to_string(ret_type));
                    sema_error(s, node->line, node->col, buf);
                }
                }
            } else {
                LuvType* fret = s->current_function->as.function.return_type;
                if (fret->kind == TYPE_UNKNOWN) {
                    s->current_function->as.function.return_type = type_void(s->arena);
                } else if (fret->kind != TYPE_VOID) {
                    sema_error(s, node->line, node->col, "Return type mismatch: expected value.");
                }
            }
            break;
        case AST_DEFER:
            if (!s->current_function) {
                sema_error(s, node->line, node->col, "'defer' used outside of a function context.");
                sema_suggest(s, node->line, node->col, "Fix-it: move 'defer' inside a function body.");
            }
            analyze_statement(s, node->as.defer_stmt.body);
            break;
        case AST_GUARD: {
            if (!s->current_function) {
                sema_error(s, node->line, node->col, "'guard' used outside of a function context.");
            }
            if (analyze_expression(s, node->as.guard_stmt.condition)->kind != TYPE_BOOL) {
                sema_error(s, node->line, node->col, "'guard' condition must be boolean.");
            }
            /* guard 'else' must diverge (return, break, continue, or throw) */
            if (!ast_contains_kind(node->as.guard_stmt.else_body, AST_RETURN) &&
                !ast_contains_kind(node->as.guard_stmt.else_body, AST_BREAK) &&
                !ast_contains_kind(node->as.guard_stmt.else_body, AST_CONTINUE) &&
                !ast_contains_kind(node->as.guard_stmt.else_body, AST_THROW)) {
                sema_error(s, node->line, node->col, "'guard' else block must exit the current scope (return, break, continue, or throw).");
                sema_suggest(s, node->line, node->col, "Fix-it: add a 'return', 'break', or 'throw' to the guard else block.");
            }
            analyze_statement(s, node->as.guard_stmt.else_body);
            break;
        }
        case AST_WITH: {
            if (node->as.with_stmt.resource) analyze_expression(s, node->as.with_stmt.resource);
            analyze_statement(s, node->as.with_stmt.body);
            break;
        }
        case AST_CHAN_DECL: {
            char name[256]; size_t len = node->as.chan_decl.name.length; if (len > 255) len = 255;
            strncpy(name, node->as.chan_decl.name.start, len); name[len] = '\0';
            LuvType* t = resolve_type_node(s, node->as.chan_decl.elem_type);
            scope_define(s->arena, s->current_scope, strdup(name), t, SYMBOL_VAR);
            break;
        }
        case AST_CHAN_SEND: {
            analyze_expression(s, node->as.chan_send.channel);
            analyze_expression(s, node->as.chan_send.value);
            break;
        }
        case AST_CHAN_RECV: {
            analyze_expression(s, node->as.chan_recv.channel);
            break;
        }
        case AST_SELECT: {
            for (size_t i = 0; i < node->as.select_stmt.case_count; i++) {
                analyze_statement(s, node->as.select_stmt.cases[i]);
            }
            break;
        }
        case AST_MATCH: {
            LuvType* targ_type = analyze_expression(s, node->as.match_stmt.target);
            if (node->as.match_stmt.arm_count == 0) {
                sema_error(s, node->line, node->col, "'match' must have at least one arm.");
            }

            /* Handle boolean exhaustiveness: require true and false or wildcard '_' */
            if (targ_type && targ_type->kind == TYPE_BOOL) {
                bool seen_true = false, seen_false = false, has_wild = false;
                for (size_t i = 0; i < node->as.match_stmt.arm_count; i++) {
                    ASTNode* arm = node->as.match_stmt.arms[i];
                    ASTNode* pat = arm->as.match_arm.pattern;
                    if (!pat) { has_wild = true; analyze_statement(s, arm); continue; }
                    if (pat->type == AST_LITERAL) {
                        Token tt = pat->as.literal.token;
                        if (tt.type == TOKEN_TRUE) seen_true = true;
                        if (tt.type == TOKEN_FALSE) seen_false = true;
                    } else if (pat->type == AST_IDENTIFIER) {
                        Token idtok = pat->as.identifier.name;
                        if (idtok.type == TOKEN_TRUE) seen_true = true;
                        if (idtok.type == TOKEN_FALSE) seen_false = true;
                        /* '_' is treated as wildcard */
                        if (idtok.length == 1 && idtok.start[0] == '_') { has_wild = true; }
                    }
                    analyze_statement(s, arm);
                }
                if (!has_wild && !(seen_true && seen_false)) {
                    sema_error(s, node->line, node->col, "Non-exhaustive 'match' over bool: missing true/false or wildcard '_'.");
                }
                break;
            }

            /* Handle enum exhaustiveness: lookup enum declaration and compare variants */
            if (targ_type && targ_type->kind == TYPE_ENUM && targ_type->name) {
                Symbol* et = scope_lookup(s->global_scope, targ_type->name);
                if (et && et->decl && et->decl->type == AST_ENUM) {
                    size_t vcount = et->decl->as.enum_decl.variant_count;
                    bool* covered = (bool*)arena_alloc(s->arena, sizeof(bool) * vcount);
                    for (size_t vi = 0; vi < vcount; vi++) covered[vi] = false;
                    bool has_wild = false;

                    for (size_t i = 0; i < node->as.match_stmt.arm_count; i++) {
                        ASTNode* arm = node->as.match_stmt.arms[i];
                        ASTNode* pat = arm->as.match_arm.pattern;
                        if (!pat) { has_wild = true; analyze_statement(s, arm); continue; }

                        if (pat->type == AST_IDENTIFIER) {
                            Token id = pat->as.identifier.name;
                            if (id.length == 1 && id.start[0] == '_') { has_wild = true; analyze_statement(s, arm); continue; }
                            char pname[256]; size_t plen = id.length; if (plen > 255) plen = 255; strncpy(pname, id.start, plen); pname[plen] = '\0';

                            for (size_t vi = 0; vi < vcount; vi++) {
                                ASTNode* v = et->decl->as.enum_decl.variants[vi];
                                if (v->type == AST_IDENTIFIER) {
                                    Token vt = v->as.identifier.name; char vname[256]; size_t vlen = vt.length; if (vlen > 255) vlen = 255; strncpy(vname, vt.start, vlen); vname[vlen] = '\0';
                                    if (strcmp(pname, vname) == 0) { covered[vi] = true; break; }
                                } else if (v->type == AST_CALL) {
                                    if (v->as.call.callee && v->as.call.callee->type == AST_IDENTIFIER) {
                                        Token vt = v->as.call.callee->as.identifier.name; char vname[256]; size_t vlen = vt.length; if (vlen > 255) vlen = 255; strncpy(vname, vt.start, vlen); vname[vlen] = '\0';
                                        if (strcmp(pname, vname) == 0) { covered[vi] = true; break; }
                                    }
                                }
                            }
                        } else if (pat->type == AST_CALL && pat->as.call.callee && pat->as.call.callee->type == AST_IDENTIFIER) {
                            Token id = pat->as.call.callee->as.identifier.name; char pname[256]; size_t plen = id.length; if (plen > 255) plen = 255; strncpy(pname, id.start, plen); pname[plen] = '\0';
                            for (size_t vi = 0; vi < vcount; vi++) {
                                ASTNode* v = et->decl->as.enum_decl.variants[vi];
                                if (v->type == AST_IDENTIFIER) {
                                    Token vt = v->as.identifier.name; char vname[256]; size_t vlen = vt.length; if (vlen > 255) vlen = 255; strncpy(vname, vt.start, vlen); vname[vlen] = '\0';
                                    if (strcmp(pname, vname) == 0) { covered[vi] = true; break; }
                                } else if (v->type == AST_CALL) {
                                    if (v->as.call.callee && v->as.call.callee->type == AST_IDENTIFIER) {
                                        Token vt = v->as.call.callee->as.identifier.name; char vname[256]; size_t vlen = vt.length; if (vlen > 255) vlen = 255; strncpy(vname, vt.start, vlen); vname[vlen] = '\0';
                                        if (strcmp(pname, vname) == 0) { covered[vi] = true; break; }
                                    }
                                }
                            }
                        }
                        analyze_statement(s, arm);
                    }

                    if (!has_wild) {
                        bool all = true;
                        for (size_t vi = 0; vi < vcount; vi++) { if (!covered[vi]) { all = false; break; } }
                        if (!all) {
                            sema_error(s, node->line, node->col, "Non-exhaustive 'match' over enum type.");
                        }
                    }
                    break;
                }
            }

            /* Fallback: require a wildcard '_' arm if type unknown/unsupported */
            {
                bool found_wild = false;
                for (size_t i = 0; i < node->as.match_stmt.arm_count; i++) {
                    ASTNode* arm = node->as.match_stmt.arms[i];
                    ASTNode* pat = arm->as.match_arm.pattern;
                    if (pat && pat->type == AST_IDENTIFIER) {
                        Token id = pat->as.identifier.name;
                        if (id.length == 1 && id.start[0] == '_') { found_wild = true; }
                    }
                    analyze_statement(s, arm);
                }
                if (!found_wild) {
                    sema_error(s, node->line, node->col, "Non-exhaustive 'match': add a wildcard arm '_' to cover remaining cases.");
                }
            }

            break;
        }
        case AST_SWITCH: {
            LuvType* targ_type = analyze_expression(s, node->as.switch_stmt.target);
            if (node->as.switch_stmt.case_count == 0) {
                sema_error(s, node->line, node->col, "'switch' must have at least one case.");
            }

            /* Detect an explicit default: either switch_stmt.default_case or case value 'else' */
            bool has_default = node->as.switch_stmt.default_case != NULL;
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTNode* sc = node->as.switch_stmt.cases[i];
                ASTNode* val = sc->as.switch_case.value;
                if (val && val->type == AST_IDENTIFIER && val->as.identifier.name.type == TOKEN_ELSE) { has_default = true; }
            }

            /* Boolean switch: require true/false or default */
            if (targ_type && targ_type->kind == TYPE_BOOL) {
                bool seen_true = false, seen_false = false;
                for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                    ASTNode* sc = node->as.switch_stmt.cases[i];
                    ASTNode* val = sc->as.switch_case.value;
                    if (!val) { /* shouldn't happen */ }
                    else if (val->type == AST_LITERAL) {
                        Token tt = val->as.literal.token;
                        if (tt.type == TOKEN_TRUE) seen_true = true;
                        if (tt.type == TOKEN_FALSE) seen_false = true;
                    } else if (val->type == AST_IDENTIFIER) {
                        Token idt = val->as.identifier.name;
                        if (idt.type == TOKEN_TRUE) seen_true = true;
                        if (idt.type == TOKEN_FALSE) seen_false = true;
                    }
                    analyze_statement(s, sc->as.switch_case.body);
                }
                if (!has_default && !(seen_true && seen_false)) {
                    sema_error(s, node->line, node->col, "Non-exhaustive 'switch' over bool: missing true/false or default 'else'.");
                }
                break;
            }

            /* Enum switch: check all variants covered or default present */
            if (targ_type && targ_type->kind == TYPE_ENUM && targ_type->name) {
                Symbol* et = scope_lookup(s->global_scope, targ_type->name);
                if (et && et->decl && et->decl->type == AST_ENUM) {
                    size_t vcount = et->decl->as.enum_decl.variant_count;
                    bool* covered = (bool*)arena_alloc(s->arena, sizeof(bool) * vcount);
                    for (size_t vi = 0; vi < vcount; vi++) covered[vi] = false;

                    for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                        ASTNode* sc = node->as.switch_stmt.cases[i];
                        ASTNode* val = sc->as.switch_case.value;
                        if (!val) { analyze_statement(s, sc->as.switch_case.body); continue; }
                        if (val->type == AST_IDENTIFIER && val->as.identifier.name.type == TOKEN_ELSE) { /* default */ analyze_statement(s, sc->as.switch_case.body); continue; }

                        char vname_buf[256];
                        if (val->type == AST_IDENTIFIER) {
                            Token id = val->as.identifier.name; size_t l = id.length; if (l > 255) l = 255; strncpy(vname_buf, id.start, l); vname_buf[l] = '\0';
                        } else if (val->type == AST_CALL && val->as.call.callee && val->as.call.callee->type == AST_IDENTIFIER) {
                            Token id = val->as.call.callee->as.identifier.name; size_t l = id.length; if (l > 255) l = 255; strncpy(vname_buf, id.start, l); vname_buf[l] = '\0';
                        } else {
                            vname_buf[0] = '\0';
                        }

                        if (vname_buf[0] != '\0') {
                            for (size_t vi = 0; vi < vcount; vi++) {
                                ASTNode* v = et->decl->as.enum_decl.variants[vi];
                                if (v->type == AST_IDENTIFIER) { Token vt = v->as.identifier.name; char vn[256]; size_t vl = vt.length; if (vl > 255) vl = 255; strncpy(vn, vt.start, vl); vn[vl] = '\0'; if (strcmp(vn, vname_buf) == 0) { covered[vi] = true; break; } }
                                else if (v->type == AST_CALL) { if (v->as.call.callee && v->as.call.callee->type == AST_IDENTIFIER) { Token vt = v->as.call.callee->as.identifier.name; char vn[256]; size_t vl = vt.length; if (vl > 255) vl = 255; strncpy(vn, vt.start, vl); vn[vl] = '\0'; if (strcmp(vn, vname_buf) == 0) { covered[vi] = true; break; } } }
                            }
                        }
                        analyze_statement(s, sc->as.switch_case.body);
                    }

                    if (!has_default) {
                        bool all = true;
                        for (size_t vi = 0; vi < vcount; vi++) { if (!covered[vi]) { all = false; break; } }
                        if (!all) {
                            sema_error(s, node->line, node->col, "Non-exhaustive 'switch' over enum type: not all variants are covered and no default 'else' provided.");
                        }
                    }
                    break;
                }
            }

            /* Fallback: just analyze cases and ensure default exists if no obvious coverage */
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTNode* sc = node->as.switch_stmt.cases[i];
                analyze_statement(s, sc->as.switch_case.body);
            }
            if (!has_default) {
                sema_error(s, node->line, node->col, "Non-exhaustive 'switch': consider adding an 'else' case to cover remaining values.");
            }

            break;
        }
        default: break;
    }
}

const char* analyze_expression_to_name(Sema* s, ASTNode* node) {
    (void)s;
    if (node->type == AST_IDENTIFIER) {
        static char name[256];
        size_t len = node->as.identifier.name.length;
        if (len > 255) len = 255;
        strncpy(name, node->as.identifier.name.start, len);
        name[len] = '\0';
        return name;
    }
    return NULL;
}

LuvType* analyze_expression(Sema* s, ASTNode* node) {
    if (!node) return type_void(s->arena);
    switch (node->type) {
        case AST_LITERAL: {
            Token t = node->as.literal.token;
            if (t.type == TOKEN_TRUE || t.type == TOKEN_FALSE) return type_bool(s->arena);
            if (t.type == TOKEN_NUMBER) return type_i32(s->arena);
            if (t.type == TOKEN_STRING_INTERP || t.type == TOKEN_VARDATA) return type_string(s->arena);
            if (t.type == TOKEN_NEN) return type_nen(s->arena);
            return type_void(s->arena);
        }
        case AST_NEN:
            return type_nen(s->arena);
        case AST_IDENTIFIER: {
            char name[256]; size_t len = node->as.identifier.name.length;
            if (len > 255) len = 255;
            strncpy(name, node->as.identifier.name.start, len);
            name[len] = '\0';
            Symbol* sym = scope_lookup(s->current_scope, name);
            if (!sym) { sema_error(s, node->line, node->col, "Undefined identifier."); return type_new(s->arena, TYPE_UNKNOWN); }
            if (sym->is_moved) { sema_error(s, node->line, node->col, "Use of moved variable."); }
            if (sym->borrowed_from && sym->borrowed_from->is_moved) {
                sema_error(s, node->line, node->col, "Use of moved variable (borrow invalidated).");
            }
            return sym->type;
        }
        case AST_MOVE: {
            ASTNode* val = node->as.move_expr.value;
            if (val->type == AST_IDENTIFIER) {
                char src[256]; size_t sl = val->as.identifier.name.length; if (sl > 255) sl = 255;
                strncpy(src, val->as.identifier.name.start, sl); src[sl] = '\0';
                Symbol* src_sym = scope_lookup(s->current_scope, src);
                if (!src_sym) { sema_error(s, node->line, node->col, "Move source undefined."); return type_new(s->arena, TYPE_UNKNOWN); }
                if (src_sym->type->kind != TYPE_OWN && src_sym->type->kind != TYPE_PTR) {
                    sema_error(s, node->line, node->col, "Move expects an owned or pointer value.");
                }
                if (src_sym->type->is_pinned) {
                    sema_error(s, node->line, node->col, "Cannot move a pinned value.");
                }
                src_sym->is_moved = true;
                return src_sym->type;
            }
            sema_error(s, node->line, node->col, "Move expects identifier.");
            return type_new(s->arena, TYPE_UNKNOWN);
        }
        case AST_BORROW: {
            ASTNode* inner = node->as.borrow_expr.value;
            LuvType* valtype = analyze_expression(s, inner);
            if (inner->type == AST_IDENTIFIER) {
                char nm[256]; size_t nl = inner->as.identifier.name.length; if (nl > 255) nl = 255;
                strncpy(nm, inner->as.identifier.name.start, nl); nm[nl] = '\0';
                Symbol* owner = scope_lookup(s->current_scope, nm);
                if (!owner) { sema_error(s, node->line, node->col, "Borrow of undefined variable."); }
                if (owner && owner->is_moved) sema_error(s, node->line, node->col, "Cannot borrow moved variable.");
            }
            LuvType* base = valtype;
            if (valtype->kind == TYPE_OWN || valtype->kind == TYPE_PTR) base = valtype->as.pointer.base_type;
            LuvType* ref_t = type_new(s->arena, TYPE_REF);
            ref_t->as.pointer.base_type = base;
            ref_t->is_mut = node->as.borrow_expr.is_mut;
            return ref_t;
        }
        case AST_BINARY: {
            LuvType* left = analyze_expression(s, node->as.binary.left);
            LuvType* right = analyze_expression(s, node->as.binary.right);
            if (node->as.binary.op.type == TOKEN_EQUAL) {
                // luv philosophy: Standard variables are reassignable.
                // ONLY 'const' blocks reassignment. 'mut' is for mutation-by-reference.
                if (left->is_const) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "Cannot reassign to a 'const' variable (left-hand expression).");
                    sema_error(s, node->line, node->col, buf);
                }
                if (!type_is_compatible(left, right)) {
                    char buf[1024];
                    snprintf(buf, sizeof(buf), "Type mismatch in assignment: left is %s but right is %s", type_to_string(left), type_to_string(right));
                    sema_error(s, node->line, node->col, buf);
                }

                // Inference: if LHS is an identifier with unknown type, adopt RHS type.
                if (node->as.binary.left->type == AST_IDENTIFIER) {
                    const char* lname = analyze_expression_to_name(s, node->as.binary.left);
                    Symbol* lsym = lname ? scope_lookup(s->current_scope, lname) : NULL;
                    if (lsym && lsym->type->kind == TYPE_UNKNOWN) {
                        lsym->type = right;
                    }
                }
                return left;
            }

            // pointer arithmetic check
            if ((node->as.binary.op.type == TOKEN_PLUS || node->as.binary.op.type == TOKEN_MINUS) &&
                (left->kind == TYPE_PTR || right->kind == TYPE_PTR)) {
                if (!s->in_unsafe) {
                    sema_error(s, node->line, node->col, "Pointer arithmetic requires 'unsafe' block.");
                    return type_new(s->arena, TYPE_UNKNOWN);
                }
                if (left->kind == TYPE_PTR) return left;
                if (right->kind == TYPE_PTR) return right;
            }

            // If either side is dynamic, result is dynamic
            // But comparisons and logical ops return bool
            TokenType op = node->as.binary.op.type;
            bool is_comp = (op == TOKEN_EQUAL_EQUAL || op == TOKEN_BANG_EQUAL ||
                            op == TOKEN_GREATER || op == TOKEN_GREATER_EQUAL ||
                            op == TOKEN_LESS || op == TOKEN_LESS_EQUAL);
            bool is_logical = (op == TOKEN_AND_AND || op == TOKEN_OR_OR ||
                               op == TOKEN_AND || op == TOKEN_OR);
            
            if (left->kind == TYPE_DYN || right->kind == TYPE_DYN) {
                return (is_comp || is_logical) ? type_bool(s->arena) : type_dyn(s->arena);
            }
            // Basic arithmetic compatibility
            if (left->kind == right->kind) return left;
            return type_new(s->arena, TYPE_UNKNOWN);
        }
        case AST_MEMBER_ACCESS: {
            LuvType* obj = analyze_expression(s, node->as.member_access.object);
            if (obj && obj->kind == TYPE_DYN) {
                // dynamic member access -> dynamic
                return type_dyn(s->arena);
            }
            char mname[256]; size_t mlen = node->as.member_access.member.length;
            if (mlen > 255) mlen = 255;
            strncpy(mname, node->as.member_access.member.start, mlen);
            mname[mlen] = '\0';
            
            // Check intrinsics for the type
            Intrinsic* in = intrinsics_lookup(s->intrinsics, mname, obj);
            if (in) {
                // Return a function type representing the intrinsic
                LuvType* ft = type_new(s->arena, TYPE_FUNCTION);
                ft->as.function.return_type = in->return_type;
                ft->as.function.param_types = in->param_types;
                ft->as.function.param_count = in->param_count;
                ft->as.function.is_async = false;
                return ft;
            }
            sema_error(s, node->line, node->col, "Unknown member.");
            return type_new(s->arena, TYPE_UNKNOWN);
        }
        case AST_CALL: {
            /* Purity and I/O checks for calls inside pure functions and comptime enforcement */
            const char* callee_name = NULL;
            LuvType* callee_owner_type = NULL;
            ASTNode* callee_node = node->as.call.callee;
            char mnamebuf[256] = {0};
            if (callee_node->type == AST_IDENTIFIER) {
                callee_name = analyze_expression_to_name(s, callee_node);
            } else if (callee_node->type == AST_MEMBER_ACCESS) {
                size_t mlen = callee_node->as.member_access.member.length;
                if (mlen > 255) mlen = 255;
                strncpy(mnamebuf, callee_node->as.member_access.member.start, mlen);
                mnamebuf[mlen] = '\0';
                callee_name = mnamebuf;
                callee_owner_type = analyze_expression(s, callee_node->as.member_access.object);
            }

            /* Comptime enforcement */
            if (s->in_comptime) {
                if (!callee_name) {
                    sema_error(s, node->line, node->col, "Comptime context cannot perform indirect or dynamic calls.");
                    return type_new(s->arena, TYPE_UNKNOWN);
                }
                Intrinsic* ci = intrinsics_lookup(s->intrinsics, callee_name, callee_owner_type);
                if (ci) {
                    if (!ci->is_comptime_safe) {
                        char bufio[512];
                        snprintf(bufio, sizeof(bufio), "Intrinsic '%s' is not allowed in comptime context.", callee_name);
                        sema_error(s, node->line, node->col, bufio);
                        sema_suggest(s, node->line, node->col, "Fix-it: remove call from comptime block or provide a comptime-safe intrinsic.");
                        return type_new(s->arena, TYPE_UNKNOWN);
                    }
                    for (size_t i = 0; i < node->as.call.arg_count; i++) {
                        if (!comptime_expr_ok(s, node->as.call.args[i])) {
                            sema_error(s, node->as.call.args[i]->line, node->as.call.args[i]->col, "Argument not computable at compile-time.");
                            return type_new(s->arena, TYPE_UNKNOWN);
                        }
                    }
                    /* allowed intrinsic call; continue normal resolution */
                } else {
                    Symbol* csym = callee_name ? scope_lookup(s->global_scope, callee_name) : NULL;
                    if (!csym || !csym->decl || csym->decl->type != AST_FUNC || !csym->decl->as.func_decl.is_comptime) {
                        char buf[512];
                        snprintf(buf, sizeof(buf), "Call to non-comptime function '%s' is not allowed in comptime context.", callee_name ? callee_name : "<call>");
                        sema_error(s, node->line, node->col, buf);
                        sema_suggest(s, node->line, node->col, "Fix-it: mark callee 'comptime' or move call to runtime context.");
                        return type_new(s->arena, TYPE_UNKNOWN);
                    }
                    for (size_t i = 0; i < node->as.call.arg_count; i++) {
                        if (!comptime_expr_ok(s, node->as.call.args[i])) {
                            sema_error(s, node->as.call.args[i]->line, node->as.call.args[i]->col, "Argument not computable at compile-time.");
                            return type_new(s->arena, TYPE_UNKNOWN);
                        }
                    }
                    /* function is comptime, allowed — continue to standard resolution */
                }
            }

            if (s->in_pure_function) {
                if (callee_name) {
                    Intrinsic* ii = intrinsics_lookup(s->intrinsics, callee_name, callee_owner_type);
                    if (ii && ii->is_io) {
                        char bufio[512];
                        snprintf(bufio, sizeof(bufio), "Pure function cannot perform I/O by calling intrinsic '%s'. Move I/O to an impure function or opt out using 'unsafe pure'.", callee_name);
                        sema_error(s, node->line, node->col, bufio);
                        sema_suggest(s, node->line, node->col, "Fix-it: move I/O to non-pure function or use 'unsafe pure' to opt out.");
                    } else {
                        Symbol* csym = callee_name ? scope_lookup(s->global_scope, callee_name) : NULL;
                        if (csym) {
                            if (csym->decl) {
                                if (!csym->decl->as.func_decl.is_pure) {
                                    char bufp[512];
                                    if (callee_name) {
                                        char shortname[128];
                                        size_t cnl = strlen(callee_name);
                                        if (cnl >= sizeof(shortname)) cnl = sizeof(shortname)-1;
                                        memcpy(shortname, callee_name, cnl); shortname[cnl] = '\0';
                                        snprintf(bufp, sizeof(bufp), "Pure function cannot call impure function '%s'. Consider marking '%s' as 'pure' if safe, or make caller 'unsafe'.", shortname, shortname);
                                    } else {
                                        snprintf(bufp, sizeof(bufp), "Pure function cannot call impure function (unknown). Consider marking it 'pure' if safe, or make caller 'unsafe'.");
                                    }
                                    sema_error(s, node->line, node->col, bufp);
                                    sema_suggest(s, node->line, node->col, "Suggestion: declare the callee 'pure' if it has no side-effects, or mark caller 'unsafe pure'.");
                                }
                            } else {
                                char bufn[512];
                                snprintf(bufn, sizeof(bufn), "Pure function calling external or unknown function '%s' considered impure.", callee_name);
                                sema_error(s, node->line, node->col, bufn);
                                sema_suggest(s, node->line, node->col, "Suggestion: add a pure wrapper or mark caller 'unsafe'.");
                            }
                        } else {
                            char bufn2[512];
                            snprintf(bufn2, sizeof(bufn2), "Pure function calling unknown function '%s' considered impure.", callee_name);
                            sema_error(s, node->line, node->col, bufn2);
                            sema_suggest(s, node->line, node->col, "Suggestion: declare the function or mark caller 'unsafe'.");
                        }
                    }
                } else {
                    sema_error(s, node->line, node->col, "Pure function cannot perform indirect or dynamic calls (member calls, function pointers, or dyn)." );
                    sema_suggest(s, node->line, node->col, "Consider making the call explicit or marking function 'unsafe pure'.");
                }
            }

            /* CALL-SITE: attempt to resolve as class method and devirtualize when possible */
            if (callee_node->type == AST_MEMBER_ACCESS && callee_name && callee_owner_type) {
                Symbol* classsym = NULL;
                if (callee_owner_type->name) classsym = scope_lookup(s->global_scope, callee_owner_type->name);
                Symbol* chosen = NULL;
                size_t candidates = 0;
                if (classsym && classsym->class_scope) {
                    Scope* cls_scope = classsym->class_scope;
                    /* find candidate methods with matching name */
                    for (size_t i = 0; i < cls_scope->count; i++) {
                        Symbol* msym = cls_scope->symbols[i];
                        if (!msym || msym->kind != SYMBOL_FUNC) continue;
                        if (!msym->name) continue;
                        if (strcmp(msym->name, callee_name) != 0) continue;
                        chosen = msym;
                        candidates++;
                    }
                    /* if multiple overloads, prefer exact param count match */
                    if (candidates > 1) {
                        Symbol* exact = NULL;
                        for (size_t i = 0; i < cls_scope->count; i++) {
                            Symbol* msym = cls_scope->symbols[i];
                            if (!msym || msym->kind != SYMBOL_FUNC) continue;
                            if (!msym->name) continue;
                            if (strcmp(msym->name, callee_name) != 0) continue;
                            LuvType* mft = msym->type;
                            if (mft && mft->kind == TYPE_FUNCTION && mft->as.function.param_count == node->as.call.arg_count) { exact = msym; break; }
                        }
                        if (exact) { chosen = exact; candidates = 1; }
                    }
                }

                if (candidates == 1 && chosen) {
                    LuvType* ftype = chosen->type;
                    bool is_virtual_method = false;
                    if (chosen->decl && chosen->decl->type == AST_FUNC) is_virtual_method = chosen->decl->as.func_decl.is_virtual;

                    bool devirtualize_here = false;
                    if (!is_virtual_method) devirtualize_here = true;
                    else {
                        /* final methods are devirtualizable */
                        if (chosen->decl && chosen->decl->type == AST_FUNC && chosen->decl->as.func_decl.is_final) {
                            devirtualize_here = true;
                        } else if (classsym && classsym->decl && classsym->decl->type == AST_CLASS && classsym->decl->as.class_decl.is_sealed) {
                            devirtualize_here = true;
                        } else {
                            bool override_found = false;
                            /* scan for subclasses that override this method */
                            for (size_t ii = 0; ii < s->global_scope->count; ii++) {
                                Symbol* tsym = s->global_scope->symbols[ii];
                                if (!tsym || tsym->kind != SYMBOL_TYPE) continue;
                                if (!tsym->decl || tsym->decl->type != AST_CLASS) continue;
                                ASTNode* base = tsym->decl->as.class_decl.base_class;
                                while (base) {
                                    if (base->type == AST_IDENTIFIER) {
                                        char bname[256]; size_t bl = base->as.identifier.name.length; if (bl > 255) bl = 255; strncpy(bname, base->as.identifier.name.start, bl); bname[bl] = '\0';
                                        if (classsym && classsym->name && strcmp(bname, classsym->name) == 0) {
                                            if (tsym->class_scope) {
                                                for (size_t k = 0; k < tsym->class_scope->count; k++) {
                                                    Symbol* os = tsym->class_scope->symbols[k];
                                                    if (os && os->kind == SYMBOL_FUNC && os->name && strcmp(os->name, callee_name) == 0) { override_found = true; break; }
                                                }
                                            }
                                            break;
                                        } else {
                                            Symbol* base_sym = scope_lookup(s->global_scope, bname);
                                            if (base_sym && base_sym->decl && base_sym->decl->type == AST_CLASS) base = base_sym->decl->as.class_decl.base_class;
                                            else break;
                                        }
                                    } else break;
                                }
                                if (override_found) break;
                            }
                            if (!override_found) devirtualize_here = true;
                        }
                    }

                    if (devirtualize_here) {
                        sema_suggest(s, node->line, node->col, "Call-site devirtualized: resolved to class method. Consider marking class 'sealed' or method 'final' to guarantee devirtualization.");
                    } else if (is_virtual_method) {
                        sema_suggest(s, node->line, node->col, "Virtual call at call-site requires runtime dispatch. Consider 'sealed'/'final' to optimize.");
                    }

                    /* Now perform argument checking: parameters correspond 1:1 to call args */
                    if (!ftype || ftype->kind != TYPE_FUNCTION) { sema_error(s, node->line, node->col, "Resolved symbol is not callable."); return type_new(s->arena, TYPE_UNKNOWN); }
                    if (node->as.call.arg_count != ftype->as.function.param_count) {
                        sema_error(s, node->line, node->col, "Incorrect number of arguments for method call.");
                    } else {
                        for (size_t i = 0; i < node->as.call.arg_count; i++) {
                            LuvType* arg_type = analyze_expression(s, node->as.call.args[i]);
                            LuvType* param_type = ftype->as.function.param_types[i];
                            if (param_type->kind == TYPE_UNKNOWN) {
                                bool expect_mut = param_type->is_mut;
                                ftype->as.function.param_types[i] = arg_type;
                                ftype->as.function.param_types[i]->is_mut = ftype->as.function.param_types[i]->is_mut || expect_mut;
                                param_type = ftype->as.function.param_types[i];
                            }
                            if (!type_is_compatible(param_type, arg_type)) {
                                char buf[1024]; snprintf(buf, sizeof(buf), "Argument type mismatch for parameter %zu: expected %s but got %s", i, type_to_string(param_type), type_to_string(arg_type)); sema_error(s,node->line,node->col,buf);
                            }
                            if (param_type->is_mut) {
                                const char* arg_name = analyze_expression_to_name(s, node->as.call.args[i]);
                                Symbol* arg_sym = arg_name ? scope_lookup(s->current_scope, arg_name) : NULL;
                                if (!arg_sym) {
                                    sema_error(s, node->line, node->col, "Cannot pass an expression to a 'mut' parameter; must be a variable.");
                                    sema_suggest(s, node->line, node->col, "Fix-it: store the expression in a local variable and pass that variable (e.g., let tmp = <expr>; fn(tmp)).");
                                } else if (arg_sym->type->is_const) {
                                    char buf[1024]; snprintf(buf, sizeof(buf), "Cannot pass a 'const' variable '%s' to a 'mut' parameter.", arg_name ? arg_name : "<arg>"); sema_error(s,node->line,node->col,buf);
                                    sema_suggest(s,node->line,node->col,"Fix-it: remove 'const' or pass a mutable copy instead.");
                                }
                            }
                        }
                    }
                    return ftype->as.function.return_type;
                }
            }

            LuvType* callee = analyze_expression(s, node->as.call.callee);
            if (callee->kind == TYPE_DYN) {
                // dynamic call; runtime will resolve. Analyze args for side-effects.
                for (size_t i = 0; i < node->as.call.arg_count; i++) analyze_expression(s, node->as.call.args[i]);
                return type_dyn(s->arena);
            }
            if (callee->kind != TYPE_FUNCTION) { sema_error(s, node->line, node->col, "Expression is not callable."); return type_new(s->arena, TYPE_UNKNOWN); }
            
            // Verify arguments
            if (node->as.call.arg_count != callee->as.function.param_count) {
                sema_error(s, node->line, node->col, "Incorrect number of arguments.");
            } else {
                for (size_t i = 0; i < node->as.call.arg_count; i++) {
    LuvType* arg_type = analyze_expression(s, node->as.call.args[i]);
    LuvType* param_type = callee->as.function.param_types[i];

    // Inference: if callee param is unknown, adopt argument type (preserve mut flag)
    if (param_type->kind == TYPE_UNKNOWN) {
        bool expect_mut = param_type->is_mut;
        callee->as.function.param_types[i] = arg_type;
        callee->as.function.param_types[i]->is_mut = callee->as.function.param_types[i]->is_mut || expect_mut;
        param_type = callee->as.function.param_types[i];
    }

    if (!type_is_compatible(param_type, arg_type)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "Argument type mismatch for parameter %zu: expected %s but got %s", i, type_to_string(param_type), type_to_string(arg_type));
        sema_error(s, node->line, node->col, buf);
    }

    if (param_type->is_mut) {
        const char* arg_name = analyze_expression_to_name(s, node->as.call.args[i]);
        Symbol* arg_sym = arg_name ? scope_lookup(s->current_scope, arg_name) : NULL;
        if (!arg_sym) {
            sema_error(s, node->line, node->col, "Cannot pass an expression to a 'mut' parameter; must be a variable.");
            sema_suggest(s, node->line, node->col, "Fix-it: store the expression in a local variable and pass that variable (e.g., let tmp = <expr>; fn(tmp)).");
        } else if (arg_sym->type->is_const) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "Cannot pass a 'const' variable '%s' to a 'mut' parameter.", arg_name ? arg_name : "<arg>");
            sema_error(s, node->line, node->col, buf);
            sema_suggest(s, node->line, node->col, "Fix-it: remove 'const' or pass a mutable copy instead.");
        } else if (!type_is_compatible(param_type, arg_sym->type)) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "Type mismatch for 'mut' parameter %zu: expected %s but got %s (argument variable %s)", i, type_to_string(param_type), type_to_string(arg_sym->type), arg_sym->name ? arg_sym->name : "<arg>");
            sema_error(s, node->line, node->col, buf);
        }
    } else {
        if (!type_is_compatible(param_type, arg_type)) {
            char buf[1024];
            snprintf(buf, sizeof(buf), "Argument type mismatch for parameter %zu: expected %s but got %s", i, type_to_string(param_type), type_to_string(arg_type));
            sema_error(s, node->line, node->col, buf);
        }
    }
}
return callee->as.function.return_type;
        }
        case AST_AWAIT: {
            if (!s->current_function || !s->current_function->as.function.is_async) {
                sema_error(s, node->line, node->col, "Cannot await outside async function.");
                sema_suggest(s, node->line, node->col, "Fix-it: mark the containing function as 'async'.");
            }
            return analyze_expression(s, node->as.await_expr.expr);
        }
        case AST_YIELD: {
            if (!s->current_function) {
                sema_error(s, node->line, node->col, "Cannot yield outside function.");
            }
            return analyze_expression(s, node->as.yield_expr.value);
        }
        case AST_TYPEOF: {
            analyze_expression(s, node->as.typeof_expr.expr);
            return type_string(s->arena); // Returns a type name string
        }
        case AST_SIZEOF: {
            analyze_expression(s, node->as.sizeof_expr.expr);
            return type_i32(s->arena); // Returns an integer size
        }
        case AST_NULL_COALESCE: {
            LuvType* left = analyze_expression(s, node->as.null_coalesce.left);
            LuvType* right = analyze_expression(s, node->as.null_coalesce.right);
            if (!type_is_compatible(left, right)) {
                sema_error(s, node->line, node->col, "Type mismatch in null-coalescing expression.");
            }
            return left;
        }
        case AST_SAFE_NAV: {
            LuvType* obj = analyze_expression(s, node->as.safe_nav.object);
            if (obj->kind == TYPE_DYN) return type_dyn(s->arena);
            // In a real implementation, we'd check if the member exists on the type.
            // For now, we'll just return unknown if not dynamic.
            return type_new(s->arena, TYPE_UNKNOWN);
        }
        case AST_CAST_EXPR: {
            analyze_expression(s, node->as.cast_expr.expr);
            return resolve_type_node(s, node->as.cast_expr.target_type);
        }
        case AST_ALLOC: {
            LuvType* t = resolve_type_node(s, node->as.alloc_expr.type_expr);
            if (node->as.alloc_expr.count) analyze_expression(s, node->as.alloc_expr.count);
            LuvType* ptr = type_new(s->arena, TYPE_PTR);
            ptr->as.pointer.base_type = t;
            return ptr;
        }
        case AST_DEALLOC: {
            analyze_expression(s, node->as.dealloc_expr.target);
            return type_void(s->arena);
        }
        case AST_ADDR_OF: {
            LuvType* inner = analyze_expression(s, node->as.addr_of.value);
            LuvType* ptr = type_new(s->arena, TYPE_PTR);
            ptr->as.pointer.base_type = inner;
            return ptr;
        }
        case AST_DEREF: {
            LuvType* inner = analyze_expression(s, node->as.deref_expr.value);
            if (inner->kind == TYPE_PTR || inner->kind == TYPE_REF || inner->kind == TYPE_OWN) {
                return inner->as.pointer.base_type;
            }
            sema_error(s, node->line, node->col, "Cannot dereference non-pointer type.");
            return type_new(s->arena, TYPE_UNKNOWN);
        }
        case AST_UNARY: {
            LuvType* r = analyze_expression(s, node->as.unary.right);
            if (node->as.unary.op.type == TOKEN_BANG || node->as.unary.op.type == TOKEN_NOT) {
                if (r->kind != TYPE_BOOL && r->kind != TYPE_DYN) {
                    sema_error(s, node->line, node->col, "Unary '!' or 'not' expects a boolean expression.");
                }
                return type_bool(s->arena);
            }
            return r;
        }
        case AST_SELF: {
            if (!s->current_class) {
                sema_error(s, node->line, node->col, "'self' used outside of a class context.");
                return type_new(s->arena, TYPE_UNKNOWN);
            }
            char cname[256]; size_t clen = s->current_class->as.class_decl.name.length; if (clen > 255) clen = 255;
            strncpy(cname, s->current_class->as.class_decl.name.start, clen); cname[clen] = '\0';
            Symbol* csym = scope_lookup(s->global_scope, cname);
            return csym ? csym->type : type_new(s->arena, TYPE_UNKNOWN);
        }
        case AST_SUPER: {
            if (!s->current_class) {
                sema_error(s, node->line, node->col, "'super' used outside of a class context.");
                return type_new(s->arena, TYPE_UNKNOWN);
            }
            if (!s->current_class->as.class_decl.base_class) {
                sema_error(s, node->line, node->col, "'super' used in a class with no base class.");
                return type_new(s->arena, TYPE_UNKNOWN);
            }
            return resolve_type_node(s, s->current_class->as.class_decl.base_class);
        }
        case AST_IS: {
            analyze_expression(s, node->as.is_expr.left);
            resolve_type_node(s, node->as.is_expr.right_type);
            return type_bool(s->arena);
        }
        default: return type_void(s->arena);
    }
}
}

void register_function(Sema* s, ASTNode* node) {
    char name[256]; size_t len = node->as.func_decl.name.length;
    if (len > 255) len = 255; 
    strncpy(name, node->as.func_decl.name.start, len); 
    name[len] = '\0';

    LuvType* ft = type_new(s->arena, TYPE_FUNCTION);
    ft->as.function.return_type = resolve_type_node(s, node->as.func_decl.return_type);
    ft->as.function.param_count = node->as.func_decl.param_count;
    ft->as.function.is_async = node->as.func_decl.is_async;
    ft->as.function.param_types = (LuvType**)arena_alloc(s->arena, sizeof(LuvType*) * ft->as.function.param_count);

    for (size_t i = 0; i < ft->as.function.param_count; i++) {
        ASTNode* param = node->as.func_decl.params[i];
        LuvType* pt = resolve_type_node(s, param->as.var_decl.type_node);
        if (!param->as.var_decl.type_node && param->as.var_decl.is_dyn) {
            pt = type_dyn(s->arena);
        }
        pt->is_mut = param->as.var_decl.is_mut;
        ft->as.function.param_types[i] = pt;
    }

    Symbol* sym = scope_define(s->arena, s->current_scope, strdup(name), ft, SYMBOL_FUNC);
    if (sym) sym->decl = node;
}

static void pre_register_functions(Sema* s, ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) pre_register_functions(s, node->as.block.statements[i]);
            break;
        case AST_CLASS: {
            char cname[256]; size_t clen = node->as.class_decl.name.length; if (clen > 255) clen = 255;
            strncpy(cname, node->as.class_decl.name.start, clen); cname[clen] = '\0';
            Symbol* csym = scope_lookup(s->current_scope, cname);
            if (!csym) {
                LuvType* ctype = type_new(s->arena, TYPE_CLASS);
                ctype->name = strdup(cname);
                csym = scope_define(s->arena, s->current_scope, strdup(cname), ctype, SYMBOL_TYPE);
                if (csym) csym->decl = node;
            }
            if (!csym->class_scope) csym->class_scope = scope_new(s->arena, s->current_scope);
            Scope* prev_scope = s->current_scope;
            s->current_scope = csym->class_scope;
            ASTNode* prev_class = s->current_class;
            s->current_class = node;
            pre_register_functions(s, node->as.class_decl.body);
            s->current_class = prev_class;
            s->current_scope = prev_scope;
            break;
        }
        case AST_FUNC:
            register_function(s, node);
            break;
        default:
            /* Recurse into likely child containers */
            switch (node->type) {
                case AST_IF:
                    pre_register_functions(s, node->as.if_stmt.then_branch);
                    if (node->as.if_stmt.else_branch) pre_register_functions(s, node->as.if_stmt.else_branch);
                    break;
                case AST_FOR:
                    if (node->as.for_stmt.init) pre_register_functions(s, node->as.for_stmt.init);
                    if (node->as.for_stmt.body) pre_register_functions(s, node->as.for_stmt.body);
                    break;
                default:
                    break;
            }
            break;
    }
}

void sema_analyze(Sema* sema, ASTNode* root) {
    if (root->type == AST_BLOCK) {
        /* Pre-register all functions (top-level and nested) so call-site analysis can find candidates */
        pre_register_functions(sema, root);
        analyze_statement(sema, root);
    }
}
