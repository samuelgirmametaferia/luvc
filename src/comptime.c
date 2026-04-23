#include "sema.h"
#include "ast.h"
#include "symbol.h"
#include "type.h"
#include "intrinsics.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/* Minimal comptime evaluator/interpreter.
   - Evaluates expressions into literal AST nodes when possible.
   - Executes statements in comptime blocks (var decl, assignments, if, for, for-in, while, return).
   - For comptime function calls, invokes functions declared with 'comptime'.

   This is intentionally conservative: it fails when encountering non-comptime calls or unknown constructs.
*/

typedef enum { CE_OK = 0, CE_RETURN = 1, CE_BREAK = 2, CE_CONTINUE = 3, CE_ERROR = 4 } CEStatus;

static ASTNode* make_number_literal(Sema* s, long long v, int line, int col) {
    char* buf = (char*)arena_alloc(s->arena, 32);
    int n = snprintf(buf, 32, "%lld", (long long)v);
    Token tok = { TOKEN_NUMBER, buf, (size_t)n, line, col };
    ASTNode* nnode = ast_new_node(s->arena, AST_LITERAL, line, col);
    nnode->as.literal.token = tok;
    return nnode;
}

static ASTNode* make_bool_literal(Sema* s, bool v, int line, int col) {
    const char* tstr = v ? "true" : "false";
    char* buf = (char*)arena_alloc(s->arena, strlen(tstr)+1);
    strcpy(buf, tstr);
    Token tok = { v ? TOKEN_TRUE : TOKEN_FALSE, buf, strlen(buf), line, col };
    ASTNode* nnode = ast_new_node(s->arena, AST_LITERAL, line, col);
    nnode->as.literal.token = tok;
    return nnode;
}

static ASTNode* make_string_literal(Sema* s, const char* str, int line, int col) {
    size_t len = strlen(str);
    char* buf = (char*)arena_alloc(s->arena, len+1);
    memcpy(buf, str, len+1);
    Token tok = { TOKEN_VARDATA, buf, len, line, col };
    ASTNode* nnode = ast_new_node(s->arena, AST_LITERAL, line, col);
    nnode->as.literal.token = tok;
    return nnode;
}

static long long parse_number_token(Token t) {
    char tmp[64]; size_t l = t.length < sizeof(tmp)-1 ? t.length : sizeof(tmp)-1;
    memcpy(tmp, t.start, l); tmp[l] = '\0';
    return strtoll(tmp, NULL, 0);
}

static bool is_integer_literal_node(ASTNode* n) {
    return n && n->type == AST_LITERAL && n->as.literal.token.type == TOKEN_NUMBER;
}

static bool is_bool_literal_node(ASTNode* n) {
    return n && n->type == AST_LITERAL && (n->as.literal.token.type == TOKEN_TRUE || n->as.literal.token.type == TOKEN_FALSE);
}

static const char* ident_name(ASTNode* id, char* out, size_t out_sz) {
    if (!id || id->type != AST_IDENTIFIER) return NULL;
    size_t len = id->as.identifier.name.length;
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, id->as.identifier.name.start, len);
    out[len] = '\0';
    return out;
}

/* Forward decls */
static char* arena_strdup(Sema* s, const char* src) {
    if (!src) return NULL;
    size_t l = strlen(src);
    char* out = (char*)arena_alloc(s->arena, l + 1);
    memcpy(out, src, l + 1);
    return out;
}
static CEStatus comptime_exec_block_inner(Sema* s, ASTNode* block, ASTNode** out_ret);
static ASTNode* comptime_eval_expr(Sema* s, ASTNode* expr, bool* ok);

/* Execute an expression and return a literal ASTNode* on success; NULL and ok=false on failure. */
static ASTNode* comptime_eval_expr(Sema* s, ASTNode* expr, bool* ok) {
    if (!expr) { *ok = true; return NULL; }
    switch (expr->type) {
        case AST_LITERAL:
            *ok = true; return expr;
        case AST_IDENTIFIER: {
            char name[256]; size_t len = expr->as.identifier.name.length; if (len > 255) len = 255; strncpy(name, expr->as.identifier.name.start, len); name[len] = '\0';
            Symbol* sym = scope_lookup(s->current_scope, name);
            if (!sym) sym = scope_lookup(s->global_scope, name);
            if (!sym) { sema_error(s, expr->line, expr->col, "Undefined identifier in comptime evaluation."); *ok = false; return NULL; }
            if (sym->decl && sym->decl->type == AST_VAR_DECL && sym->decl->as.var_decl.init) {
                return comptime_eval_expr(s, sym->decl->as.var_decl.init, ok);
            }
            sema_error(s, expr->line, expr->col, "Identifier is not a compile-time constant."); *ok = false; return NULL;
        }
        case AST_UNARY: {
            bool rok;
            ASTNode* rv = comptime_eval_expr(s, expr->as.unary.right, &rok);
            if (!rok || !rv) { *ok = false; return NULL; }
            if (!is_integer_literal_node(rv) && expr->as.unary.op.type == TOKEN_MINUS) { sema_error(s, expr->line, expr->col, "Unary '-' on non-integer in comptime."); *ok = false; return NULL; }
            if (expr->as.unary.op.type == TOKEN_MINUS) {
                long long v = parse_number_token(rv->as.literal.token);
                return make_number_literal(s, -v, expr->line, expr->col);
            }
            if (expr->as.unary.op.type == TOKEN_BANG || expr->as.unary.op.type == TOKEN_NOT) {
                if (!is_bool_literal_node(rv)) { sema_error(s, expr->line, expr->col, "Unary '!' on non-bool in comptime."); *ok = false; return NULL; }
                bool bv = (rv->as.literal.token.type == TOKEN_TRUE);
                return make_bool_literal(s, !bv, expr->line, expr->col);
            }
            sema_error(s, expr->line, expr->col, "Unsupported unary operator in comptime."); *ok = false; return NULL;
        }
        case AST_BINARY: {
            /* Assignment handled at statement-level. Here we only evaluate pure binary ops. */
            if (expr->as.binary.op.type == TOKEN_EQUAL) { sema_error(s, expr->line, expr->col, "Assignment not allowed in expression comptime evaluation."); *ok = false; return NULL; }
            bool lok=false, rok=false;
            ASTNode* l = comptime_eval_expr(s, expr->as.binary.left, &lok);
            ASTNode* r = comptime_eval_expr(s, expr->as.binary.right, &rok);
            if (!lok || !rok || !l || !r) { *ok = false; return NULL; }
            TokenType op = expr->as.binary.op.type;
            /* Arithmetic on integers */
            if (is_integer_literal_node(l) && is_integer_literal_node(r)) {
                long long lv = parse_number_token(l->as.literal.token);
                long long rv = parse_number_token(r->as.literal.token);
                switch (op) {
                    case TOKEN_PLUS: return make_number_literal(s, lv + rv, expr->line, expr->col);
                    case TOKEN_MINUS: return make_number_literal(s, lv - rv, expr->line, expr->col);
                    case TOKEN_STAR: return make_number_literal(s, lv * rv, expr->line, expr->col);
                    case TOKEN_SLASH: if (rv == 0) { sema_error(s, expr->line, expr->col, "Division by zero in comptime."); *ok = false; return NULL; } return make_number_literal(s, lv / rv, expr->line, expr->col);
                    case TOKEN_PERCENT: if (rv == 0) { sema_error(s, expr->line, expr->col, "Modulo by zero in comptime."); *ok = false; return NULL; } return make_number_literal(s, lv % rv, expr->line, expr->col);
                    case TOKEN_LESS: return make_bool_literal(s, lv < rv, expr->line, expr->col);
                    case TOKEN_LESS_EQUAL: return make_bool_literal(s, lv <= rv, expr->line, expr->col);
                    case TOKEN_GREATER: return make_bool_literal(s, lv > rv, expr->line, expr->col);
                    case TOKEN_GREATER_EQUAL: return make_bool_literal(s, lv >= rv, expr->line, expr->col);
                    case TOKEN_EQUAL_EQUAL: return make_bool_literal(s, lv == rv, expr->line, expr->col);
                    case TOKEN_BANG_EQUAL: return make_bool_literal(s, lv != rv, expr->line, expr->col);
                    default: break;
                }
            }
            /* Boolean ops */
            if (is_bool_literal_node(l) && is_bool_literal_node(r)) {
                bool lv = (l->as.literal.token.type == TOKEN_TRUE);
                bool rv = (r->as.literal.token.type == TOKEN_TRUE);
                switch (op) {
                    case TOKEN_AND_AND: return make_bool_literal(s, lv && rv, expr->line, expr->col);
                    case TOKEN_OR_OR: return make_bool_literal(s, lv || rv, expr->line, expr->col);
                    case TOKEN_EQUAL_EQUAL: return make_bool_literal(s, lv == rv, expr->line, expr->col);
                    case TOKEN_BANG_EQUAL: return make_bool_literal(s, lv != rv, expr->line, expr->col);
                    default: break;
                }
            }
            sema_error(s, expr->line, expr->col, "Unsupported binary operands in comptime evaluation."); *ok = false; return NULL;
        }
        case AST_TERNARY: {
            bool cok=false;
            ASTNode* cond = comptime_eval_expr(s, expr->as.ternary.condition, &cok);
            if (!cok || !cond || !is_bool_literal_node(cond)) { sema_error(s, expr->line, expr->col, "Ternary condition not compile-time boolean."); *ok = false; return NULL; }
            bool cv = (cond->as.literal.token.type == TOKEN_TRUE);
            return comptime_eval_expr(s, cv ? expr->as.ternary.then_expr : expr->as.ternary.else_expr, ok);
        }
        case AST_LIST_LITERAL: {
            size_t n = expr->as.list_literal.count;
            ASTNode** elems = (ASTNode**)arena_alloc(s->arena, sizeof(ASTNode*) * n);
            for (size_t i = 0; i < n; i++) {
                bool eok=false; ASTNode* e = comptime_eval_expr(s, expr->as.list_literal.elements[i], &eok);
                if (!eok || !e) { *ok = false; return NULL; }
                elems[i] = e;
            }
            ASTNode* nnode = ast_new_node(s->arena, AST_LIST_LITERAL, expr->line, expr->col);
            nnode->as.list_literal.elements = elems; nnode->as.list_literal.count = n;
            *ok = true; return nnode;
        }
        case AST_CALL: {
            /* Identify callee name */
            const char* cname = NULL; char buf[256] = {0};
            if (expr->as.call.callee->type == AST_IDENTIFIER) {
                size_t l = expr->as.call.callee->as.identifier.name.length; if (l > 255) l = 255; strncpy(buf, expr->as.call.callee->as.identifier.name.start, l); buf[l] = '\0'; cname = buf;
            } else if (expr->as.call.callee->type == AST_MEMBER_ACCESS) {
                size_t l = expr->as.call.callee->as.member_access.member.length; if (l > 255) l = 255; strncpy(buf, expr->as.call.callee->as.member_access.member.start, l); buf[l] = '\0'; cname = buf;
            }
            if (!cname) { sema_error(s, expr->line, expr->col, "Indirect calls not supported in comptime evaluation."); *ok = false; return NULL; }

            Intrinsic* ii = intrinsics_lookup(s->intrinsics, cname, NULL);
            if (ii) {
                if (!ii->is_comptime_safe) { sema_error(s, expr->line, expr->col, "Intrinsic not allowed in comptime evaluation."); *ok = false; return NULL; }
                /* No generic intrinsic evaluator implemented; fail conservatively */
                sema_error(s, expr->line, expr->col, "Comptime intrinsic evaluation not implemented for this intrinsic."); *ok = false; return NULL;
            }

            /* Function call: look up symbol */
            Symbol* fsym = scope_lookup(s->global_scope, cname);
            if (!fsym || !fsym->decl || fsym->decl->type != AST_FUNC || !fsym->decl->as.func_decl.is_comptime) {
                sema_error(s, expr->line, expr->col, "Call to non-comptime function in comptime evaluation."); *ok = false; return NULL;
            }
            /* Evaluate arguments */
            size_t argc = expr->as.call.arg_count;
            ASTNode** eval_args = (ASTNode**)arena_alloc(s->arena, sizeof(ASTNode*) * argc);
            for (size_t i = 0; i < argc; i++) {
                bool aok=false; ASTNode* aev = comptime_eval_expr(s, expr->as.call.args[i], &aok);
                if (!aok || !aev) { *ok = false; return NULL; }
                eval_args[i] = aev;
            }
            /* Invoke function by executing its body in a nested scope */
            ASTNode* fdecl = fsym->decl;
            /* create a new scope chained to current */
            Scope* prev_scope = s->current_scope;
            s->current_scope = scope_new(s->arena, prev_scope);
            /* bind parameters */
            for (size_t i = 0; i < fdecl->as.func_decl.param_count; i++) {
                ASTNode* param = fdecl->as.func_decl.params[i];
                char pname[256]; size_t plen = param->as.var_decl.name.length; if (plen > 255) plen = 255; strncpy(pname, param->as.var_decl.name.start, plen); pname[plen] = '\0';
                /* define symbol with unknown type and set decl init to evaluated arg */
                Symbol* psym = scope_define(s->arena, s->current_scope, arena_strdup(s, pname), type_new(s->arena, TYPE_UNKNOWN), SYMBOL_VAR);
                if (psym) {
                    /* create a var_decl node to represent the parameter binding */
                    ASTNode* pv = ast_new_node(s->arena, AST_VAR_DECL, param->line, param->col);
                    pv->as.var_decl.name = param->as.var_decl.name;
                    pv->as.var_decl.type_node = NULL;
                    pv->as.var_decl.init = eval_args[i];
                    psym->decl = pv;
                }
            }
            /* Execute function body */
            ASTNode* retval = NULL;
            CEStatus st = CE_OK;
            if (fdecl->as.func_decl.body) {
                st = comptime_exec_block_inner(s, fdecl->as.func_decl.body, &retval);
            }
            /* restore scope */
            s->current_scope = prev_scope;
            if (st == CE_RETURN && retval) { *ok = true; return retval; }
            if (st == CE_OK) { *ok = true; return NULL; }
            *ok = false; return NULL;
        }
        default:
            sema_error(s, expr->line, expr->col, "Expression not supported in comptime evaluation."); *ok = false; return NULL;
    }
}

/* Execute a single statement in comptime context. Returns CEStatus. out_ret is set if a return occurs. */
static CEStatus comptime_exec_stmt(Sema* s, ASTNode* stmt, ASTNode** out_ret) {
    if (!stmt) return CE_OK;
    switch (stmt->type) {
        case AST_VAR_DECL: {
            char name[256]; size_t len = stmt->as.var_decl.name.length; if (len > 255) len = 255; strncpy(name, stmt->as.var_decl.name.start, len); name[len] = '\0';
            Symbol* sym = scope_lookup(s->current_scope, name);
            if (!sym) {
                /* define with unknown type */
                sym = scope_define(s->arena, s->current_scope, arena_strdup(s, name), type_new(s->arena, TYPE_UNKNOWN), SYMBOL_VAR);
                if (sym) sym->decl = stmt;
            }
            if (stmt->as.var_decl.init) {
                bool ok=false; ASTNode* val = comptime_eval_expr(s, stmt->as.var_decl.init, &ok);
                if (!ok) return CE_ERROR;
                /* fold initializer into symbol's declaration */
                if (sym && sym->decl && sym->decl->type == AST_VAR_DECL) {
                    sym->decl->as.var_decl.init = val;
                }
            }
            return CE_OK;
        }
        case AST_EXPR_STMT: {
            ASTNode* e = stmt->as.expr_stmt.expr;
            if (!e) return CE_OK;
            /* Handle assignment as statement */
            if (e->type == AST_BINARY && e->as.binary.op.type == TOKEN_EQUAL) {
                ASTNode* lhs = e->as.binary.left;
                ASTNode* rhs = e->as.binary.right;
                if (lhs->type != AST_IDENTIFIER) { sema_error(s, e->line, e->col, "Left-hand side of assignment must be identifier in comptime."); return CE_ERROR; }
                char name[256]; size_t len = lhs->as.identifier.name.length; if (len > 255) len = 255; strncpy(name, lhs->as.identifier.name.start, len); name[len] = '\0';
                Symbol* sym = scope_lookup(s->current_scope, name);
                if (!sym) { sym = scope_define(s->arena, s->current_scope, arena_strdup(s, name), type_new(s->arena, TYPE_UNKNOWN), SYMBOL_VAR); if (sym) sym->decl = lhs; }
                bool ok=false; ASTNode* val = comptime_eval_expr(s, rhs, &ok);
                if (!ok) return CE_ERROR;
                if (sym && sym->decl && sym->decl->type == AST_VAR_DECL) {
                    sym->decl->as.var_decl.init = val;
                }
                return CE_OK;
            }
            /* Otherwise, evaluate expression for side-effects (calls) */
            bool ok=false; ASTNode* v = comptime_eval_expr(s, e, &ok);
            if (!ok) return CE_ERROR;
            (void)v;
            return CE_OK;
        }
        case AST_IF: {
            bool ok=false; ASTNode* cond = comptime_eval_expr(s, stmt->as.if_stmt.condition, &ok);
            if (!ok || !cond || !is_bool_literal_node(cond)) { sema_error(s, stmt->line, stmt->col, "If condition must be compile-time boolean for evaluation."); return CE_ERROR; }
            bool cv = (cond->as.literal.token.type == TOKEN_TRUE);
            if (cv) return comptime_exec_block_inner(s, stmt->as.if_stmt.then_branch, out_ret);
            else if (stmt->as.if_stmt.else_branch) return comptime_exec_block_inner(s, stmt->as.if_stmt.else_branch, out_ret);
            return CE_OK;
        }
        case AST_RETURN: {
            if (stmt->as.return_stmt.value) {
                bool ok=false; ASTNode* rv = comptime_eval_expr(s, stmt->as.return_stmt.value, &ok);
                if (!ok) return CE_ERROR;
                if (out_ret) *out_ret = rv;
            } else {
                if (out_ret) *out_ret = NULL;
            }
            return CE_RETURN;
        }
        case AST_WHILE: {
            /* Evaluate condition each iteration; guard iterations */
            int max_iter = 10000;
            for (int iter = 0; iter < max_iter; iter++) {
                bool ok=false; ASTNode* cond = comptime_eval_expr(s, stmt->as.while_stmt.condition, &ok);
                if (!ok || !cond || !is_bool_literal_node(cond)) { sema_error(s, stmt->line, stmt->col, "While condition must be compile-time boolean for evaluation."); return CE_ERROR; }
                if (cond->as.literal.token.type == TOKEN_FALSE) break;
                CEStatus st = comptime_exec_block_inner(s, stmt->as.while_stmt.body, out_ret);
                if (st == CE_BREAK) break;
                if (st == CE_CONTINUE) continue;
                if (st == CE_RETURN || st == CE_ERROR) return st;
            }
            return CE_OK;
        }
        case AST_FOR: {
            /* Treat as: exec init once, then while(condition) { body; exec increment } */
            if (stmt->as.for_stmt.init) {
                /* init can be declaration or expr-stmt */
                if (stmt->as.for_stmt.init->type == AST_VAR_DECL) {
                    CEStatus sst = comptime_exec_stmt(s, stmt->as.for_stmt.init, out_ret); if (sst != CE_OK) return sst;
                } else if (stmt->as.for_stmt.init->type == AST_EXPR_STMT) {
                    bool ok=false; ASTNode* v = comptime_eval_expr(s, stmt->as.for_stmt.init->as.expr_stmt.expr, &ok); if (!ok) return CE_ERROR;
                }
            }
            int max_iter = 10000;
            for (int iter = 0; iter < max_iter; iter++) {
                if (!stmt->as.for_stmt.condition) { sema_error(s, stmt->line, stmt->col, "For loop without condition not allowed in comptime."); return CE_ERROR; }
                bool ok=false; ASTNode* cond = comptime_eval_expr(s, stmt->as.for_stmt.condition, &ok);
                if (!ok || !cond || !is_bool_literal_node(cond)) { sema_error(s, stmt->line, stmt->col, "For condition must be compile-time boolean for evaluation."); return CE_ERROR; }
                if (cond->as.literal.token.type == TOKEN_FALSE) break;
                CEStatus st = comptime_exec_block_inner(s, stmt->as.for_stmt.body, out_ret);
                if (st == CE_BREAK) break;
                if (st == CE_RETURN || st == CE_ERROR) return st;
                if (stmt->as.for_stmt.increment) {
                    bool iok=false; ASTNode* inc = comptime_eval_expr(s, stmt->as.for_stmt.increment, &iok);
                    if (!iok) return CE_ERROR;
                    /* increment is expected to be an assignment expression handled previously by expr-stmt; here we ignore returned value */
                }
            }
            return CE_OK;
        }
        case AST_FOR_IN: {
            /* iterable must be a list literal */
            bool ok=false; ASTNode* it = comptime_eval_expr(s, stmt->as.for_in.iterable, &ok);
            if (!ok || !it || it->type != AST_LIST_LITERAL) { sema_error(s, stmt->line, stmt->col, "Iterable in for-in must be compile-time list."); return CE_ERROR; }
            for (size_t i = 0; i < it->as.list_literal.count; i++) {
                /* bind loop variable */
                char vname[256]; size_t vn = stmt->as.for_in.value_name.length; if (vn > 255) vn = 255; strncpy(vname, stmt->as.for_in.value_name.start, vn); vname[vn] = '\0';
                Symbol* vs = scope_lookup(s->current_scope, vname);
                if (!vs) vs = scope_define(s->arena, s->current_scope, arena_strdup(s, vname), type_new(s->arena, TYPE_UNKNOWN), SYMBOL_VAR);
                if (vs) {
                    ASTNode* pv = ast_new_node(s->arena, AST_VAR_DECL, stmt->line, stmt->col);
                    pv->as.var_decl.name = stmt->as.for_in.value_name;
                    pv->as.var_decl.init = it->as.list_literal.elements[i];
                    vs->decl = pv;
                }
                CEStatus st = comptime_exec_block_inner(s, stmt->as.for_in.body, out_ret);
                if (st == CE_BREAK) break;
                if (st == CE_RETURN || st == CE_ERROR) return st;
            }
            return CE_OK;
        }
        case AST_BREAK: return CE_BREAK;
        case AST_CONTINUE: return CE_CONTINUE;
        case AST_BLOCK: return comptime_exec_block_inner(s, stmt, out_ret);
        default:
            sema_error(s, stmt->line, stmt->col, "Statement not supported in comptime execution."); return CE_ERROR;
    }
}

/* Execute all statements in a block. out_ret set when return occurs. */
static CEStatus comptime_exec_block_inner(Sema* s, ASTNode* block, ASTNode** out_ret) {
    if (!block) return CE_OK;
    /* If block is AST_BLOCK node, iterate statements */
    if (block->type != AST_BLOCK) return CE_OK;
    for (size_t i = 0; i < block->as.block.count; i++) {
        ASTNode* stmt = block->as.block.statements[i];
        CEStatus st = comptime_exec_stmt(s, stmt, out_ret);
        if (st != CE_OK) return st;
    }
    return CE_OK;
}

/* Public entry: execute a comptime block. Returns true on success. */
bool comptime_execute_block(Sema* s, ASTNode* block) {
    ASTNode* dummy = NULL;
    CEStatus st = comptime_exec_block_inner(s, block, &dummy);
    return st != CE_ERROR;
}
