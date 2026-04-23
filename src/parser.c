#include "parser.h"
#include "resolve_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   PARSER HELPERS
   ========================================================= */

static void advance(Parser* parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = next_token(&parser->lexer);
        if (parser->current.type != TOKEN_ERROR) break;
        fprintf(stderr, "[line %d] Error: %.*s\n", parser->current.line, (int)parser->current.length, parser->current.start);
        parser->had_error = true;
    }
}

static bool check(Parser* parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser* parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static void consume(Parser* parser, TokenType type, const char* message) {
    if (check(parser, type)) {
        advance(parser);
        return;
    }
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;
    fprintf(stderr, "[line %d] Error at '%.*s': %s\n", 
            parser->current.line, (int)parser->current.length, parser->current.start, message);
}

static bool is_at_end(Parser* parser) {
    return check(parser, TOKEN_EOF);
}

static void consume_identifier(Parser* parser, const char* message) {
    if (check(parser, TOKEN_IDENTIFIER) || parser->current.type == TOKEN_DYN || (parser->current.type >= TOKEN_I8 && parser->current.type <= TOKEN_NEVER) || 
        parser->current.type == TOKEN_MUTEX || parser->current.type == TOKEN_NUMBER || parser->current.type == TOKEN_TNT ||
        parser->current.type == TOKEN_SELF || parser->current.type == TOKEN_SUPER) {
        advance(parser);
        return;
    }
    consume(parser, TOKEN_IDENTIFIER, message);
}

/* =========================================================
   PRECEDENCE & PRATT PARSING
   ========================================================= */

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  
    PREC_TERNARY,     
    PREC_OR,          
    PREC_AND,         
    PREC_EQUALITY,    
    PREC_COMPARISON,  
    PREC_RANGE,       
    PREC_TERM,        
    PREC_FACTOR,      
    PREC_UNARY,       
    PREC_CAST,        
    PREC_CALL,        
    PREC_PRIMARY
} Precedence;

typedef ASTNode* (*ParseFn)(Parser* parser, bool can_assign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

static ASTNode* expression(Parser* parser);
static ASTNode* statement(Parser* parser);
static ASTNode* declaration(Parser* parser);
static ASTNode* parse_block(Parser* parser);
static ParseRule* get_rule(TokenType type);
static ASTNode* parse_precedence(Parser* parser, Precedence precedence);

/* New prefix expression parsers */
static ASTNode* parse_await(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    ASTNode* n=ast_new_node(p->arena,AST_AWAIT,l,c);
    n->as.await_expr.expr=parse_precedence(p,PREC_UNARY); return n;
}
static ASTNode* parse_yield_expr(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    ASTNode* n=ast_new_node(p->arena,AST_YIELD,l,c);
    n->as.yield_expr.value=(!check(p,TOKEN_SEMICOLON)&&!check(p,TOKEN_RBRACE))?expression(p):NULL; return n;
}
static ASTNode* parse_move_expr(Parser* p, bool ca) { (void)ca;
    if (!p->mm_on) {
        fprintf(stderr, "[line %d] Error: Memory feature 'move' requires @luv mm on at the top of the file.\n", p->previous.line);
        p->had_error = true;
    }
    int l=p->previous.line, c=p->previous.column;
    ASTNode* n=ast_new_node(p->arena,AST_MOVE,l,c);
    n->as.move_expr.value=parse_precedence(p,PREC_UNARY); return n;
}
static ASTNode* parse_borrow_expr(Parser* p, bool ca) { (void)ca;
    if (!p->mm_on) {
        fprintf(stderr, "[line %d] Error: Memory feature 'borrow' requires @luv mm on at the top of the file.\n", p->previous.line);
        p->had_error = true;
    }
    int l=p->previous.line, c=p->previous.column;
    ASTNode* n=ast_new_node(p->arena,AST_BORROW,l,c);
    n->as.borrow_expr.is_mut=match(p,TOKEN_MUT);
    n->as.borrow_expr.value=parse_precedence(p,PREC_UNARY); return n;
}
static ASTNode* parse_sizeof_expr(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    consume(p,TOKEN_LPAREN,"Expect '(' after sizeof.");
    ASTNode* n=ast_new_node(p->arena,AST_SIZEOF,l,c);
    n->as.sizeof_expr.expr=expression(p);
    consume(p,TOKEN_RPAREN,"Expect ')'."); return n;
}
static ASTNode* parse_typeof_expr(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    consume(p,TOKEN_LPAREN,"Expect '(' after typeof.");
    ASTNode* n=ast_new_node(p->arena,AST_TYPEOF,l,c);
    n->as.typeof_expr.expr=expression(p);
    consume(p,TOKEN_RPAREN,"Expect ')'."); return n;
}
static ASTNode* parse_alloc_expr(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    consume(p,TOKEN_LPAREN,"Expect '(' after alloc.");
    ASTNode* n=ast_new_node(p->arena,AST_ALLOC,l,c);
    n->as.alloc_expr.type_expr=expression(p);
    n->as.alloc_expr.count=match(p,TOKEN_COMMA)?expression(p):NULL;
    consume(p,TOKEN_RPAREN,"Expect ')'."); return n;
}
static ASTNode* parse_deref(Parser* p, bool ca) { (void)ca;
    Token op=p->previous;
    ASTNode* val=parse_precedence(p,PREC_UNARY);
    ASTNode* n=ast_new_node(p->arena,AST_DEREF,op.line,op.column);
    n->as.deref_expr.value=val; return n;
}
static ASTNode* parse_null_coal(Parser* p, bool ca) { (void)ca;
    Token op=p->previous; ParseRule* r=get_rule(op.type);
    ASTNode* right=parse_precedence(p,(Precedence)(r->precedence+1));
    ASTNode* n=ast_new_node(p->arena,AST_NULL_COALESCE,op.line,op.column);
    n->as.null_coalesce.right=right; return n;
}
static ASTNode* parse_safe_nav_infix(Parser* p, bool ca) { (void)ca;
    consume_identifier(p,"Expect member after '?.'.");
    Token m=p->previous;
    ASTNode* n=ast_new_node(p->arena,AST_SAFE_NAV,m.line,m.column);
    n->as.safe_nav.member=m; return n;
}
static ASTNode* parse_is_infix(Parser* p, bool ca) { (void)ca;
    Token op=p->previous;
    ASTNode* right=parse_precedence(p,PREC_COMPARISON+1);
    ASTNode* n=ast_new_node(p->arena,AST_BINARY,op.line,op.column);
    n->as.binary.op=op; n->as.binary.right=right; return n;
}
static ASTNode* parse_primitive_call(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    /* After @, read the directive (e.g. 'luv', 'build') */
    if(check(p,TOKEN_IDENTIFIER)||p->current.type==TOKEN_IDENTIFIER) {
        advance(p); Token dir=p->previous;
        /* Check if this is a luv reserved var: just @ followed by identifier */
        if(dir.length==3 && strncmp(dir.start,"luv",3)==0) {
            /* Check for @luv primitive call: @luv mm on, @luv parallel, etc */
            if(check(p,TOKEN_IDENTIFIER)||check(p,TOKEN_MUT)) {
                advance(p); Token cmd=p->previous;
                Token mod={0};
                if(check(p,TOKEN_IDENTIFIER)||check(p,TOKEN_TRUE)||check(p,TOKEN_FALSE)) {
                    advance(p); mod=p->previous;
                }
                ASTNode* n=ast_new_node(p->arena,AST_PRIMITIVE_CALL,l,c);
                n->as.primitive_call.namespace_name=dir;
                n->as.primitive_call.name=cmd;
                n->as.primitive_call.qualifier=mod;
                if (cmd.length == 2 && strncmp(cmd.start, "mm", 2) == 0 && mod.length == 2 && strncmp(mod.start, "on", 2) == 0) {
                    p->mm_on = true;
                }
                return n;
            }
        }
        /* Otherwise it's a regular attribute like @build */
        ASTNode* n=ast_new_node(p->arena,AST_ATTRIBUTE,l,c);
        n->as.attribute.name=dir;
        n->as.attribute.expr=NULL;
        if(match(p,TOKEN_LPAREN)) {
            n->as.attribute.expr=expression(p);
            consume(p,TOKEN_RPAREN,"Expect ')'.");
        }
        return resolve_primitive(p, n);
    }
    consume_identifier(p,"Expect directive name after '@'.");
    Token name=p->previous;
    ASTNode* n=ast_new_node(p->arena,AST_ATTRIBUTE,l,c);
    n->as.attribute.name=name; n->as.attribute.expr=NULL;
    if(match(p,TOKEN_LPAREN)) {
        n->as.attribute.expr=expression(p);
        consume(p,TOKEN_RPAREN,"Expect ')'.");
    }
    return resolve_primitive(p, n);
}

/* =========================================================
   EXPRESSION PARSING
   ========================================================= */

static ASTNode* parse_literal(Parser* parser, bool can_assign) { (void)can_assign;
    ASTNode* node = ast_new_node(parser->arena, AST_LITERAL, parser->previous.line, parser->previous.column);
    node->as.literal.token = parser->previous;
    return node;
}

static ASTNode* parse_identifier(Parser* parser, bool can_assign) { (void)can_assign;
    ASTNode* node = ast_new_node(parser->arena, AST_IDENTIFIER, parser->previous.line, parser->previous.column);
    node->as.identifier.name = parser->previous;
    return node;
}

static ASTNode* parse_grouping(Parser* parser, bool can_assign) { (void)can_assign;
    int line = parser->previous.line; int col = parser->previous.column;
    if (match(parser, TOKEN_RPAREN)) {
        return ast_new_node(parser->arena, AST_LIST_LITERAL, line, col); 
    }
    ASTNode* expr = expression(parser);
    if (match(parser, TOKEN_COMMA)) {
        ASTNode** elements = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 64);
        elements[0] = expr; size_t count = 1;
        do {
            elements[count++] = expression(parser);
        } while (match(parser, TOKEN_COMMA));
        consume(parser, TOKEN_RPAREN, "Expect ')'.");
        ASTNode* node = ast_new_node(parser->arena, AST_LIST_LITERAL, line, col);
        node->as.list_literal.elements = elements; node->as.list_literal.count = count;
        return node;
    }
    consume(parser, TOKEN_RPAREN, "Expect ')' after expression.");
    return expr;
}

static ASTNode* parse_unary(Parser* parser, bool can_assign) { (void)can_assign;
    Token op = parser->previous;
    ASTNode* operand = parse_precedence(parser, PREC_UNARY);
    if (!operand) return NULL;
    ASTNode* node = ast_new_node(parser->arena, AST_UNARY, op.line, op.column);
    node->as.unary.op = op;
    node->as.unary.right = operand;
    return node;
}

static ASTNode* parse_binary(Parser* parser, bool can_assign) { (void)can_assign;
    Token op = parser->previous;
    ParseRule* rule = get_rule(op.type);
    ASTNode* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    if (!right) return NULL;
    ASTNode* node = ast_new_node(parser->arena, AST_BINARY, op.line, op.column);
    node->as.binary.op = op;
    node->as.binary.right = right;
    return node;
}

static ASTNode* parse_range(Parser* parser, bool can_assign) { (void)can_assign;
    Token op = parser->previous;
    ParseRule* rule = get_rule(op.type);
    ASTNode* right = parse_precedence(parser, (Precedence)(rule->precedence + 1));
    if (!right) return NULL;
    ASTNode* node = ast_new_node(parser->arena, AST_RANGE, op.line, op.column);
    node->as.range.end = right;
    node->as.range.inclusive = (op.type == TOKEN_DOT_DOT_DOT);
    return node;
}

static ASTNode* parse_call(Parser* parser, bool can_assign) { (void)can_assign;
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode** args = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128);
    size_t count = 0;
    if (!check(parser, TOKEN_RPAREN)) {
        do {
            if (check(parser, TOKEN_IDENTIFIER)) {
                Lexer saved = parser->lexer; Token curr = parser->current; Token prev = parser->previous;
                advance(parser);
                if (match(parser, TOKEN_COLON)) {
                    ASTNode* value = expression(parser);
                    ASTNode* named = ast_new_node(parser->arena, AST_BINARY, curr.line, curr.column);
                    named->as.binary.op = (Token){TOKEN_COLON, ":", 1, curr.line, curr.column};
                    ASTNode* id = ast_new_node(parser->arena, AST_IDENTIFIER, curr.line, curr.column);
                    id->as.identifier.name = curr; named->as.binary.left = id; named->as.binary.right = value;
                    args[count++] = named;
                } else {
                    parser->lexer = saved; parser->current = curr; parser->previous = prev;
                    args[count++] = expression(parser);
                }
            } else { args[count++] = expression(parser); }
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RPAREN, "Expect ')' after arguments.");
    ASTNode* node = ast_new_node(parser->arena, AST_CALL, line, col);
    node->as.call.args = args; node->as.call.arg_count = count;
    return node;
}

static ASTNode* parse_member(Parser* parser, bool can_assign) { (void)can_assign;
    consume_identifier(parser, "Expect member name.");
    Token member = parser->previous;
    ASTNode* node = ast_new_node(parser->arena, AST_MEMBER_ACCESS, member.line, member.column);
    node->as.member_access.member = member;
    return node;
}

static ASTNode* parse_index(Parser* parser, bool can_assign) { (void)can_assign;
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode* start = NULL; ASTNode* end = NULL; ASTNode* step = NULL;
    bool is_slice = false;

    if (!check(parser, TOKEN_COLON) && !check(parser, TOKEN_RBRACKET)) {
        start = expression(parser);
    }
    
    if (match(parser, TOKEN_COLON)) {
        is_slice = true;
        if (!check(parser, TOKEN_COLON) && !check(parser, TOKEN_RBRACKET)) {
            end = expression(parser);
        }
        if (match(parser, TOKEN_COLON)) {
            if (!check(parser, TOKEN_RBRACKET)) {
                step = expression(parser);
            }
        }
    }

    consume(parser, TOKEN_RBRACKET, "Expect ']'.");

    if (is_slice) {
        ASTNode* node = ast_new_node(parser->arena, AST_SLICE, line, col);
        node->as.slice.start = start; node->as.slice.end = end; node->as.slice.step = step;
        return node;
    } else {
        ASTNode* node = ast_new_node(parser->arena, AST_INDEX_ACCESS, line, col);
        node->as.index_access.index = start;
        return node;
    }
}

static ASTNode* parse_list(Parser* parser, bool can_assign) { (void)can_assign;
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode** elements = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128);
    size_t count = 0;
    if (!check(parser, TOKEN_RBRACKET)) {
        do { elements[count++] = expression(parser); } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RBRACKET, "Expect ']'.");
    ASTNode* node = ast_new_node(parser->arena, AST_LIST_LITERAL, line, col);
    node->as.list_literal.elements = elements; node->as.list_literal.count = count;
    return node;
}

static ASTNode* parse_cast(Parser* parser, bool can_assign) { (void)can_assign;
    Token op = parser->previous;
    ASTNode* type_node = parse_precedence(parser, PREC_CAST);
    ASTNode* node = ast_new_node(parser->arena, AST_BINARY, op.line, op.column);
    node->as.binary.op = op; node->as.binary.right = type_node;
    return node;
}

static ASTNode* parse_ternary(Parser* parser, bool can_assign) { (void)can_assign;
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode* then_expr = expression(parser);
    consume(parser, TOKEN_COLON, "Expect ':'.");
    ASTNode* else_expr = parse_precedence(parser, PREC_TERNARY);
    ASTNode* node = ast_new_node(parser->arena, AST_TERNARY, line, col);
    node->as.ternary.then_expr = then_expr; node->as.ternary.else_expr = else_expr;
    return node;
}

static ASTNode* parse_arrow(Parser* parser, bool can_assign) { (void)can_assign;
    Token op = parser->previous;
    ASTNode* body = match(parser, TOKEN_LBRACE) ? parse_block(parser) : expression(parser);
    ASTNode* node = ast_new_node(parser->arena, AST_BINARY, op.line, op.column);
    node->as.binary.op = op; node->as.binary.right = body;
    return node;
}

__attribute__((unused)) static ASTNode* parse_attribute(Parser* parser, bool can_assign) { (void)can_assign;
    int line = parser->previous.line; int col = parser->previous.column;
    consume_identifier(parser, "Expect attribute name.");
    Token name = parser->previous; ASTNode* expr = NULL;
    if (match(parser, TOKEN_LPAREN)) {
        expr = expression(parser); consume(parser, TOKEN_RPAREN, "Expect ')'.");
    }
    ASTNode* node = ast_new_node(parser->arena, AST_ATTRIBUTE, line, col);
    node->as.attribute.name = name; node->as.attribute.expr = expr;
    return resolve_primitive(parser, node);
}

static ASTNode* match_statement(Parser* parser);
static ASTNode* switch_statement(Parser* parser);

static ASTNode* parse_match_expr(Parser* p, bool ca) { (void)ca; return match_statement(p); }
static ASTNode* parse_switch_expr(Parser* p, bool ca) { (void)ca; return switch_statement(p); }
static ASTNode* parse_select_expr(Parser* p, bool ca) { (void)ca;
    int l=p->previous.line, c=p->previous.column;
    consume(p,TOKEN_LBRACE,"Expect '{' after select.");
    ASTNode** cases=(ASTNode**)arena_alloc(p->arena,sizeof(ASTNode*)*64); size_t cnt=0;
    while(!check(p,TOKEN_RBRACE)&&!is_at_end(p)) {
        int al=p->current.line, ac=p->current.column;
        ASTNode* pat=expression(p);
        consume(p,TOKEN_ARROW,"Expect '=>'.");
        ASTNode* body=match(p,TOKEN_LBRACE)?parse_block(p):statement(p);
        ASTNode* sc=ast_new_node(p->arena,AST_SELECT_CASE,al,ac);
        sc->as.select_case.pattern=pat; sc->as.select_case.body=body;
        cases[cnt++]=sc; match(p,TOKEN_COMMA);
    }
    consume(p,TOKEN_RBRACE,"Expect '}'.");
    ASTNode* n=ast_new_node(p->arena,AST_SELECT,l,c);
    n->as.select_stmt.cases=cases; n->as.select_stmt.case_count=cnt;
    return n;
}

ParseRule rules[TOKEN_COUNT] = {
    [TOKEN_LPAREN]        = {parse_grouping,  parse_call,   PREC_CALL},
    [TOKEN_RPAREN]        = {NULL,            NULL,         PREC_NONE},
    [TOKEN_LBRACE]        = {NULL,            NULL,         PREC_NONE}, 
    [TOKEN_RBRACE]        = {NULL,            NULL,         PREC_NONE},
    [TOKEN_LBRACKET]      = {parse_list,      parse_index,  PREC_CALL},
    [TOKEN_RBRACKET]      = {NULL,            NULL,         PREC_NONE},
    [TOKEN_COMMA]         = {NULL,            NULL,         PREC_NONE},
    [TOKEN_DOT]           = {NULL,            parse_member, PREC_CALL},
    [TOKEN_DOT_DOT]       = {NULL,            parse_range, PREC_RANGE},
    [TOKEN_DOT_DOT_DOT]   = {NULL,            parse_range, PREC_RANGE},
    [TOKEN_COLON]         = {NULL,            NULL,         PREC_NONE},
    [TOKEN_COLON_COLON]   = {NULL,            parse_member, PREC_CALL},
    [TOKEN_SEMICOLON]     = {NULL,            NULL,         PREC_NONE},
    [TOKEN_MINUS]         = {parse_unary,     parse_binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,            parse_binary, PREC_TERM},
    [TOKEN_STAR]          = {parse_deref,     parse_binary, PREC_FACTOR},
    [TOKEN_SLASH]         = {NULL,            parse_binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL,            parse_binary, PREC_FACTOR},
    [TOKEN_BANG]          = {parse_unary,     NULL,         PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,            parse_binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_EQUAL_EQUAL]   = {NULL,            parse_binary, PREC_EQUALITY},
    [TOKEN_PLUS_EQUAL]    = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_MINUS_EQUAL]   = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_STAR_EQUAL]    = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_SLASH_EQUAL]   = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_PERCENT_EQUAL] = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_GREATER]       = {NULL,            parse_binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,            parse_binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,            parse_binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,            parse_binary, PREC_COMPARISON},
    [TOKEN_NULL_COALESCE] = {NULL,            parse_null_coal, PREC_OR},
    [TOKEN_SAFE_NAV]      = {NULL,            parse_safe_nav_infix, PREC_CALL},
    [TOKEN_IN]            = {NULL,            parse_binary, PREC_COMPARISON},
    [TOKEN_IS]            = {NULL,            parse_is_infix, PREC_COMPARISON},
    [TOKEN_AND_AND]       = {NULL,            parse_binary, PREC_AND},
    [TOKEN_OR_OR]         = {NULL,            parse_binary, PREC_OR},
    [TOKEN_AND]           = {NULL,            parse_binary, PREC_AND},
    [TOKEN_OR]            = {NULL,            parse_binary, PREC_OR},
    [TOKEN_NOT]           = {parse_unary,     NULL,         PREC_UNARY},
    [TOKEN_AMP]           = {parse_unary,     parse_binary, PREC_TERM}, 
    [TOKEN_PIPE]          = {NULL,            parse_binary, PREC_TERM}, 
    [TOKEN_CARET]         = {NULL,            parse_binary, PREC_TERM}, 
    [TOKEN_TILDE]         = {parse_unary,     NULL,         PREC_UNARY},
    [TOKEN_TILDE_ARROW]   = {NULL,            parse_binary, PREC_CALL},
    [TOKEN_LEFT_ARROW]    = {NULL,            parse_binary, PREC_ASSIGNMENT},
    [TOKEN_LEFT_SHIFT]    = {NULL,            parse_binary, PREC_TERM},
    [TOKEN_RIGHT_SHIFT]   = {NULL,            parse_binary, PREC_TERM},
    [TOKEN_QUESTION]      = {NULL,            parse_ternary, PREC_TERNARY},
    [TOKEN_AT]            = {parse_primitive_call, NULL,     PREC_NONE},
    [TOKEN_AS]            = {NULL,            parse_cast,   PREC_CAST},
    [TOKEN_ARROW]         = {NULL,            parse_arrow,  PREC_ASSIGNMENT},
    [TOKEN_ARROW_STAR]    = {NULL,            parse_binary, PREC_CALL},
    [TOKEN_IDENTIFIER]    = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_DYN]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_VARDATA]       = {parse_literal,    NULL,        PREC_NONE},
    [TOKEN_CHAR]          = {parse_literal,    NULL,        PREC_NONE},
    [TOKEN_STRING_INTERP] = {parse_literal,    NULL,        PREC_NONE},
    [TOKEN_TRUE]          = {parse_literal,    NULL,        PREC_NONE},
    [TOKEN_FALSE]         = {parse_literal,    NULL,        PREC_NONE},
    [TOKEN_NEN]           = {parse_literal,    NULL,        PREC_NONE},
    [TOKEN_MATCH]         = {parse_match_expr, NULL,        PREC_NONE},
    [TOKEN_SWITCH]        = {parse_switch_expr, NULL,       PREC_NONE},
    [TOKEN_SELECT]        = {parse_select_expr, NULL,       PREC_NONE},
    [TOKEN_AWAIT]         = {parse_await,      NULL,        PREC_NONE},
    [TOKEN_YIELD]         = {parse_yield_expr, NULL,        PREC_NONE},
    [TOKEN_MOVE]          = {parse_move_expr,  NULL,        PREC_NONE},
    [TOKEN_BORROW]        = {parse_borrow_expr, NULL,       PREC_NONE},
    [TOKEN_SIZEOF]        = {parse_sizeof_expr, NULL,       PREC_NONE},
    [TOKEN_TYPEOF]        = {parse_typeof_expr, NULL,       PREC_NONE},
    [TOKEN_ALLOC]         = {parse_alloc_expr, NULL,        PREC_NONE},
    [TOKEN_MUTEX]         = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_TNT]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_NUMBER]        = {parse_literal, NULL,        PREC_NONE},
    [TOKEN_I8]            = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_I16]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_I32]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_I64]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_I128]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_I256]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_U8]            = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_U16]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_U32]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_U64]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_U128]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_U256]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_USIZE]         = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_ISIZE]         = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_F16]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_F32]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_F64]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_F128]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_F256]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_BOOL]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_BYTE]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_BITS]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_STRING_TYPE]   = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_CHAR_TYPE]     = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_VOID]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_ANY]           = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_NEVER]         = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_SELF]          = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_SUPER]         = {parse_identifier, NULL,        PREC_NONE},
    [TOKEN_EOF]           = {NULL,             NULL,        PREC_NONE},
};

static ParseRule* get_rule(TokenType type) { return &rules[type]; }

static ASTNode* parse_precedence(Parser* parser, Precedence precedence) {
    advance(parser); ParseFn prefixRule = get_rule(parser->previous.type)->prefix;
    if (prefixRule == NULL) return NULL;
    bool can_assign = precedence <= PREC_ASSIGNMENT;
    ASTNode* node = prefixRule(parser, can_assign);
    if (!node) return NULL;
    while (precedence <= get_rule(parser->current.type)->precedence) {
        advance(parser); ParseFn infixRule = get_rule(parser->previous.type)->infix;
        if (!infixRule) break;
        ASTNode* newNode = infixRule(parser, can_assign);
        if (!newNode) break;
        switch (newNode->type) {
            case AST_BINARY: newNode->as.binary.left = node; break;
            case AST_CALL: newNode->as.call.callee = node; break;
            case AST_MEMBER_ACCESS: newNode->as.member_access.object = node; break;
            case AST_INDEX_ACCESS: newNode->as.index_access.object = node; break;
            case AST_RANGE: newNode->as.range.start = node; break;
            case AST_ASSIGN: newNode->as.assign.target = node; break;
            case AST_COMPOUND_ASSIGN: newNode->as.compound_assign.target = node; break;
            case AST_TERNARY: newNode->as.ternary.condition = node; break;
            default: break;
        }
        node = newNode;
    }
    return node;
}

static ASTNode* expression(Parser* parser) { return parse_precedence(parser, PREC_ASSIGNMENT); }

/* =========================================================
   STATEMENTS & DECLARATIONS
   ========================================================= */

static ASTNode* if_statement(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    bool has_paren = match(parser, TOKEN_LPAREN); ASTNode* condition = expression(parser);
    if (has_paren) consume(parser, TOKEN_RPAREN, "Expect ')'.");
    ASTNode* then_branch = match(parser, TOKEN_LBRACE) ? parse_block(parser) : statement(parser);
    ASTNode* else_branch = NULL;
    if (match(parser, TOKEN_EF)) else_branch = if_statement(parser);
    else if (match(parser, TOKEN_ELSE)) else_branch = match(parser, TOKEN_LBRACE) ? parse_block(parser) : statement(parser);
    ASTNode* node = ast_new_node(parser->arena, AST_IF, line, col);
    node->as.if_stmt.condition = condition; node->as.if_stmt.then_branch = then_branch; node->as.if_stmt.else_branch = else_branch;
    return node;
}

static ASTNode* while_statement(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    bool has_paren = match(parser, TOKEN_LPAREN); ASTNode* condition = expression(parser);
    if (has_paren) consume(parser, TOKEN_RPAREN, "Expect ')'.");
    ASTNode* body = match(parser, TOKEN_LBRACE) ? parse_block(parser) : statement(parser);
    ASTNode* node = ast_new_node(parser->arena, AST_WHILE, line, col);
    node->as.while_stmt.condition = condition; node->as.while_stmt.body = body;
    return node;
}

static ASTNode* for_statement(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    bool has_paren = match(parser, TOKEN_LPAREN);
    if (check(parser, TOKEN_IDENTIFIER)) {
        Lexer saved = parser->lexer; Token curr = parser->current; Token prev = parser->previous;
        advance(parser);
        if (match(parser, TOKEN_IN)) {
            parser->lexer = saved; parser->current = curr; parser->previous = prev;
            consume_identifier(parser, "Expect loop variable."); Token val_name = parser->previous;
            Token idx_name = {0}; bool has_idx = false;
            if (match(parser, TOKEN_COMMA)) {
                consume_identifier(parser, "Expect index variable."); idx_name = parser->previous; has_idx = true;
            }
            consume(parser, TOKEN_IN, "Expect 'in'."); ASTNode* iterable = expression(parser);
            if (has_paren) consume(parser, TOKEN_RPAREN, "Expect ')'.");
            ASTNode* body = match(parser, TOKEN_LBRACE) ? parse_block(parser) : statement(parser);
            ASTNode* node = ast_new_node(parser->arena, AST_FOR_IN, line, col);
            node->as.for_in.value_name = val_name; node->as.for_in.index_name = idx_name;
            node->as.for_in.has_index = has_idx; node->as.for_in.iterable = iterable; node->as.for_in.body = body;
            return node;
        }
        parser->lexer = saved; parser->current = curr; parser->previous = prev;
    }
    ASTNode* init = !match(parser, TOKEN_SEMICOLON) ? declaration(parser) : NULL;
    ASTNode* condition = !match(parser, TOKEN_SEMICOLON) ? expression(parser) : NULL;
    if (condition) consume(parser, TOKEN_SEMICOLON, "Expect ';'.");
    ASTNode* increment = (!check(parser, TOKEN_RPAREN) && !check(parser, TOKEN_LBRACE)) ? expression(parser) : NULL;
    if (has_paren) consume(parser, TOKEN_RPAREN, "Expect ')'.");
    ASTNode* body = match(parser, TOKEN_LBRACE) ? parse_block(parser) : statement(parser);
    ASTNode* node = ast_new_node(parser->arena, AST_FOR, line, col);
    node->as.for_stmt.init = init; node->as.for_stmt.condition = condition; node->as.for_stmt.increment = increment; node->as.for_stmt.body = body;
    return node;
}

static ASTNode* match_statement(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode* target = expression(parser); if (!target) return NULL;
    consume(parser, TOKEN_LBRACE, "Expect '{'.");
    ASTNode** arms = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128); size_t count = 0;
    while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        int aline = parser->current.line; int acol = parser->current.column;
        ASTNode* pattern = parse_precedence(parser, PREC_OR); 
        if (!pattern) { advance(parser); continue; }
        consume(parser, TOKEN_ARROW, "Expect '=>'.");
        ASTNode* body = match(parser, TOKEN_LBRACE) ? parse_block(parser) : expression(parser);
        if (!body) { advance(parser); continue; }
        ASTNode* arm = ast_new_node(parser->arena, AST_MATCH_ARM, aline, acol);
        arm->as.match_arm.pattern = pattern; arm->as.match_arm.body = body;
        arms[count++] = arm; match(parser, TOKEN_COMMA);
    }
    consume(parser, TOKEN_RBRACE, "Expect '}'.");
    ASTNode* node = ast_new_node(parser->arena, AST_MATCH, line, col);
    node->as.match_stmt.target = target; node->as.match_stmt.arms = arms; node->as.match_stmt.arm_count = count;
    return node;
}

static ASTNode* switch_statement(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode* target = expression(parser); if (!target) return NULL;
    consume(parser, TOKEN_LBRACE, "Expect '{'.");
    ASTNode** cases = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128); size_t count = 0;
    while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        int aline = parser->current.line; int acol = parser->current.column;
        ASTNode* val = NULL;
        if (match(parser, TOKEN_ELSE)) {
            val = ast_new_node(parser->arena, AST_IDENTIFIER, aline, acol);
            val->as.identifier.name = parser->previous;
        } else { val = expression(parser); }
        if (!val) { advance(parser); continue; }
        consume(parser, TOKEN_COLON, "Expect ':'.");
        ASTNode* body = statement(parser); if (!body) { advance(parser); continue; }
        ASTNode* sc = ast_new_node(parser->arena, AST_SWITCH_CASE, val->line, val->col);
        sc->as.switch_case.value = val; sc->as.switch_case.body = body;
        cases[count++] = sc;
    }
    consume(parser, TOKEN_RBRACE, "Expect '}'.");
    ASTNode* node = ast_new_node(parser->arena, AST_SWITCH, line, col);
    node->as.switch_stmt.target = target; node->as.switch_stmt.cases = cases; node->as.switch_stmt.case_count = count;
    return node;
}

static ASTNode* return_statement(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode* value = (!check(parser, TOKEN_SEMICOLON) && !check(parser, TOKEN_RBRACE) && !is_at_end(parser)) ? expression(parser) : NULL;
    match(parser, TOKEN_SEMICOLON);
    ASTNode* node = ast_new_node(parser->arena, AST_RETURN, line, col);
    node->as.return_stmt.value = value;
    return node;
}

static ASTNode* statement(Parser* parser) {
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_WHILE)) return while_statement(parser);
    if (match(parser, TOKEN_FOR)) return for_statement(parser);
    if (match(parser, TOKEN_RETURN)) return return_statement(parser);
    if (match(parser, TOKEN_MATCH)) return match_statement(parser);
    if (match(parser, TOKEN_SWITCH)) return switch_statement(parser);
    if (match(parser, TOKEN_LBRACE)) return parse_block(parser);
    if (match(parser, TOKEN_PAR)) { 
        consume(parser, TOKEN_LBRACE, "Expect '{' after 'par'.");
        return parse_block(parser);
    }
    if (match(parser, TOKEN_ASM)) {
        if (!parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Low-level feature 'asm' requires @luv mm on at the top of the file.\n", parser->previous.line);
            parser->had_error = true;
        }
        int line = parser->previous.line; int col = parser->previous.column;
        /* Capture raw content between braces as a string for future passes. */
        consume(parser, TOKEN_LBRACE, "Expect '{' after 'asm'.");
        const char* content_start = parser->previous.start + parser->previous.length;
        while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) advance(parser);
        const char* content_end = parser->current.start;
        size_t content_len = content_end > content_start ? (size_t)(content_end - content_start) : 0;
        char* content_copy = NULL;
        if (content_len > 0) {
            content_copy = arena_alloc(parser->arena, content_len + 1);
            memcpy(content_copy, content_start, content_len);
            content_copy[content_len] = '\0';
        }
        consume(parser, TOKEN_RBRACE, "Expect '}' after asm block.");
        ASTNode* n = ast_new_node(parser->arena, AST_ASM, line, col);
        n->as.asm_stmt.content = content_copy;
        n->as.asm_stmt.body = NULL;
        return n;
    }
    if (match(parser, TOKEN_PARFOR) || match(parser, TOKEN_REDUCE) || match(parser, TOKEN_ATOMIZER) || match(parser, TOKEN_MUTEX)) {
        return statement(parser); 
    }
    /* spark: async routine */
    if (match(parser, TOKEN_SPARK)) {
        int l=parser->previous.line, c=parser->previous.column;
        ASTNode* n=ast_new_node(parser->arena, AST_SPARK, l, c);
        if (match(parser, TOKEN_LBRACE)) {
            n->as.spark.expr=parse_block(parser);
            n->as.spark.is_detached=true;
        } else {
            n->as.spark.expr=expression(parser);
            n->as.spark.is_detached=false;
        }
        match(parser, TOKEN_SEMICOLON); return n;
    }
    /* defer */
    if (match(parser, TOKEN_DEFER)) {
        int l=parser->previous.line, c=parser->previous.column;
        ASTNode* n=ast_new_node(parser->arena, AST_DEFER, l, c);
        n->as.defer_stmt.body=match(parser,TOKEN_LBRACE)?parse_block(parser):statement(parser);
        return n;
    }
    /* try/catch */
    if (match(parser, TOKEN_TRY)) {
        int l=parser->previous.line, c=parser->previous.column;
        consume(parser, TOKEN_LBRACE, "Expect '{' after 'try'.");
        ASTNode* tb=parse_block(parser); Token ename={0}; ASTNode* cb=NULL;
        if (match(parser, TOKEN_CATCH)) {
            if (match(parser, TOKEN_LPAREN)) { consume_identifier(parser,"Expect error name."); ename=parser->previous; consume(parser,TOKEN_RPAREN,"Expect ')'."); }
            consume(parser, TOKEN_LBRACE, "Expect '{' after 'catch'.");
            cb=parse_block(parser);
        }
        ASTNode* n=ast_new_node(parser->arena, AST_TRY_CATCH, l, c);
        n->as.try_catch.try_body=tb; n->as.try_catch.error_name=ename; n->as.try_catch.catch_body=cb;
        return n;
    }
    /* throw */
    if (match(parser, TOKEN_THROW)) {
        int l=parser->previous.line, c=parser->previous.column;
        ASTNode* n=ast_new_node(parser->arena, AST_THROW, l, c);
        n->as.throw_stmt.value=expression(parser); match(parser,TOKEN_SEMICOLON); return n;
    }
    /* loop */
    if (match(parser, TOKEN_LOOP)) {
        int l=parser->previous.line, c=parser->previous.column;
        consume(parser, TOKEN_LBRACE, "Expect '{' after 'loop'.");
        ASTNode* n=ast_new_node(parser->arena, AST_LOOP, l, c);
        n->as.loop_stmt.body=parse_block(parser); return n;
    }
    /* break */
    if (match(parser, TOKEN_BREAK)) {
        int l=parser->previous.line, c=parser->previous.column;
        match(parser,TOKEN_SEMICOLON); return ast_new_node(parser->arena, AST_BREAK, l, c);
    }
    /* continue */
    if (match(parser, TOKEN_CONTINUE)) {
        int l=parser->previous.line, c=parser->previous.column;
        match(parser,TOKEN_SEMICOLON); return ast_new_node(parser->arena, AST_CONTINUE, l, c);
    }
    /* unsafe block */
    if (match(parser, TOKEN_UNSAFE)) {
        if (!parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Low-level feature 'unsafe' requires @luv mm on at the top of the file.\n", parser->previous.line);
            parser->had_error = true;
        }
        int l=parser->previous.line, c=parser->previous.column;
        consume(parser, TOKEN_LBRACE, "Expect '{' after 'unsafe'.");
        ASTNode* n=ast_new_node(parser->arena, AST_UNSAFE_BLOCK, l, c);
        n->as.unsafe_block.body=parse_block(parser); return n;
    }
    /* guard */
    if (match(parser, TOKEN_GUARD)) {
        int l=parser->previous.line, c=parser->previous.column;
        ASTNode* cond=expression(parser);
        consume(parser, TOKEN_ELSE, "Expect 'else' after guard condition.");
        consume(parser, TOKEN_LBRACE, "Expect '{'.");
        ASTNode* n=ast_new_node(parser->arena, AST_GUARD, l, c);
        n->as.guard_stmt.condition=cond; n->as.guard_stmt.else_body=parse_block(parser); return n;
    }
    /* with */
    if (match(parser, TOKEN_WITH)) {
        int l=parser->previous.line, c=parser->previous.column;
        ASTNode* res=expression(parser);
        consume(parser, TOKEN_LBRACE, "Expect '{'.");
        ASTNode* n=ast_new_node(parser->arena, AST_WITH, l, c);
        n->as.with_stmt.resource=res; n->as.with_stmt.body=parse_block(parser); return n;
    }
    /* dealloc / free */
    if (match(parser, TOKEN_DEALLOC) || match(parser, TOKEN_FREE)) {
        int l=parser->previous.line, c=parser->previous.column;
        consume(parser, TOKEN_LPAREN, "Expect '('.");
        ASTNode* n=ast_new_node(parser->arena, AST_DEALLOC, l, c);
        n->as.dealloc_expr.target=expression(parser);
        consume(parser, TOKEN_RPAREN, "Expect ')'."); match(parser,TOKEN_SEMICOLON); return n;
    }
    ASTNode* expr = expression(parser); if (!expr) return NULL;
    match(parser, TOKEN_SEMICOLON);
    ASTNode* stmt = ast_new_node(parser->arena, AST_EXPR_STMT, expr->line, expr->col);
    stmt->as.expr_stmt.expr = expr; return stmt;
}

static ASTNode* parse_block(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    ASTNode* block = ast_new_node(parser->arena, AST_BLOCK, line, col);
    block->as.block.capacity = 512; block->as.block.statements = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 512); block->as.block.count = 0;
    while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        ASTNode* decl = declaration(parser);
        if (decl) {
            if (block->as.block.count < 512) {
                block->as.block.statements[block->as.block.count++] = decl;
            }
        } else if (parser->panic_mode) {
             if (!parser->had_error) advance(parser); 
        }
    }
    consume(parser, TOKEN_RBRACE, "Expect '}'."); return block;
}

static bool is_function_declaration(Parser* parser) {
    if (!check(parser, TOKEN_IDENTIFIER) && !check(parser, TOKEN_INIT) && !check(parser, TOKEN_DEINIT)) return false;
    Lexer saved = parser->lexer; Token curr = parser->current; Token prev = parser->previous; advance(parser);
    if (!match(parser, TOKEN_LPAREN)) { parser->lexer = saved; parser->current = curr; parser->previous = prev; return false; }
    while (!check(parser, TOKEN_RPAREN) && !is_at_end(parser)) advance(parser);
    if (is_at_end(parser)) { parser->lexer = saved; parser->current = curr; parser->previous = prev; return false; }
    advance(parser); bool is_decl = check(parser, TOKEN_LBRACE) || check(parser, TOKEN_THIN_ARROW);
    parser->lexer = saved; parser->current = curr; parser->previous = prev; return is_decl;
}

static ASTNode* parse_func(Parser* parser, bool is_pub, bool is_static, bool used_fn, bool is_virt, bool is_over, bool is_final, bool is_async, bool is_inline, bool is_pure, bool is_abstract, bool is_comptime, bool is_unsafe, bool is_unsafe_prefix, bool is_extern, bool is_export) {
    int line = parser->previous.line; int col = parser->previous.column;
    if (match(parser, TOKEN_INIT)) {
        consume(parser, TOKEN_LPAREN, "Expect '('."); ASTNode** params = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 64); size_t count = 0;
        if (!check(parser, TOKEN_RPAREN)) {
            do { bool p_dyn = match(parser, TOKEN_DYN); bool p_mut = match(parser, TOKEN_MUT); advance(parser);
                Token pname = parser->previous; ASTNode* ptype = match(parser, TOKEN_COLON) ? expression(parser) : NULL;
                ASTNode* param = ast_new_node(parser->arena, AST_VAR_DECL, pname.line, pname.column);
                param->as.var_decl.name = pname; param->as.var_decl.type_node = ptype; param->as.var_decl.is_dyn = p_dyn; param->as.var_decl.is_mut = p_mut;
                params[count++] = param;
            } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_RPAREN, "Expect ')'."); 
        consume(parser, TOKEN_LBRACE, "Expect '{'.");
        ASTNode* body = parse_block(parser);
        ASTNode* node = ast_new_node(parser->arena, AST_INIT, line, col); node->as.init_decl.params = params; node->as.init_decl.param_count = count; node->as.init_decl.body = body; return node;
    }
    if (match(parser, TOKEN_DEINIT)) { consume(parser, TOKEN_LBRACE, "Expect '{'."); ASTNode* body = parse_block(parser); ASTNode* node = ast_new_node(parser->arena, AST_DEINIT, line, col); node->as.deinit_decl.body = body; return node; }
    consume_identifier(parser, "Expect function name."); Token name = parser->previous;
    consume(parser, TOKEN_LPAREN, "Expect '('."); ASTNode** params = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 64); size_t count = 0;
    if (!check(parser, TOKEN_RPAREN)) {
        do { bool p_dyn = match(parser, TOKEN_DYN); bool p_mut = match(parser, TOKEN_MUT); advance(parser);
            Token pname = parser->previous; ASTNode* ptype = match(parser, TOKEN_COLON) ? expression(parser) : NULL;
            ASTNode* param = ast_new_node(parser->arena, AST_VAR_DECL, pname.line, pname.column);
            param->as.var_decl.name = pname; param->as.var_decl.type_node = ptype; param->as.var_decl.is_dyn = p_dyn; param->as.var_decl.is_mut = p_mut;
            params[count++] = param;
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RPAREN, "Expect ')'."); ASTNode* ret = match(parser, TOKEN_THIN_ARROW) ? expression(parser) : NULL;
    ASTNode* body = match(parser, TOKEN_LBRACE) ? parse_block(parser) : NULL;
    ASTNode* node = ast_new_node(parser->arena, AST_FUNC, line, col);
    node->as.func_decl.name = name; node->as.func_decl.params = params; node->as.func_decl.param_count = count;
    node->as.func_decl.return_type = ret; node->as.func_decl.body = body;
    node->as.func_decl.is_pub = is_pub; node->as.func_decl.is_static = is_static; node->as.func_decl.is_fn_keyword_used = used_fn;
    node->as.func_decl.is_virtual = is_virt; node->as.func_decl.is_override = is_over; node->as.func_decl.is_final = is_final; node->as.func_decl.is_abstract = is_abstract;
    node->as.func_decl.is_async = is_async; node->as.func_decl.is_inline = is_inline; node->as.func_decl.is_pure = is_pure; node->as.func_decl.is_unsafe = is_unsafe; node->as.func_decl.is_unsafe_prefix = is_unsafe_prefix; node->as.func_decl.is_comptime = is_comptime;
    node->as.func_decl.is_extern = is_extern; node->as.func_decl.is_export = is_export;
    return node;
}

static ASTNode* parse_struct(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    consume_identifier(parser, "Expect struct name.");
    Token name = parser->previous;
    consume(parser, TOKEN_LBRACE, "Expect '{' before struct body.");
    ASTNode** fields = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128);
    size_t count = 0;
    while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        bool is_mut = match(parser, TOKEN_MUT);
        consume_identifier(parser, "Expect field name.");
        Token fname = parser->previous;
        consume(parser, TOKEN_COLON, "Expect ':' after field name.");
        ASTNode* type = expression(parser);
        match(parser, TOKEN_COMMA);
        match(parser, TOKEN_SEMICOLON);
        ASTNode* field = ast_new_node(parser->arena, AST_VAR_DECL, fname.line, fname.column);
        field->as.var_decl.name = fname;
        field->as.var_decl.type_node = type;
        field->as.var_decl.is_mut = is_mut;
        fields[count++] = field;
    }
    consume(parser, TOKEN_RBRACE, "Expect '}' after struct body.");
    ASTNode* node = ast_new_node(parser->arena, AST_STRUCT, line, col);
    node->as.struct_decl.name = name;
    node->as.struct_decl.fields = fields;
    node->as.struct_decl.field_count = count;
    return node;
}

static ASTNode* parse_union(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    consume_identifier(parser, "Expect union name.");
    Token name = parser->previous;
    consume(parser, TOKEN_LBRACE, "Expect '{' before union body.");
    ASTNode** fields = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128);
    size_t count = 0;
    while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        consume_identifier(parser, "Expect field name.");
        Token fname = parser->previous;
        consume(parser, TOKEN_COLON, "Expect ':' after field name.");
        ASTNode* type = expression(parser);
        match(parser, TOKEN_COMMA);
        match(parser, TOKEN_SEMICOLON);
        ASTNode* field = ast_new_node(parser->arena, AST_VAR_DECL, fname.line, fname.column);
        field->as.var_decl.name = fname;
        field->as.var_decl.type_node = type;
        fields[count++] = field;
    }
    consume(parser, TOKEN_RBRACE, "Expect '}' after union body.");
    ASTNode* node = ast_new_node(parser->arena, AST_UNION, line, col);
    node->as.union_decl.name = name;
    node->as.union_decl.fields = fields;
    node->as.union_decl.field_count = count;
    return node;
}

static ASTNode* parse_enum(Parser* parser) {
    int line = parser->previous.line; int col = parser->previous.column;
    consume_identifier(parser, "Expect enum name.");
    Token name = parser->previous;
    consume(parser, TOKEN_LBRACE, "Expect '{' before enum body.");
    ASTNode** variants = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 128);
    size_t count = 0;
    while (!check(parser, TOKEN_RBRACE) && !is_at_end(parser)) {
        consume_identifier(parser, "Expect variant name.");
        Token vname = parser->previous;
        if (match(parser, TOKEN_LPAREN)) {
            // Variant with data: Variant(i32, string)
            ASTNode** args = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 64);
            size_t acount = 0;
            if (!check(parser, TOKEN_RPAREN)) {
                do { args[acount++] = expression(parser); } while (match(parser, TOKEN_COMMA));
            }
            consume(parser, TOKEN_RPAREN, "Expect ')' after variant data.");
            ASTNode* call = ast_new_node(parser->arena, AST_CALL, vname.line, vname.column);
            ASTNode* id = ast_new_node(parser->arena, AST_IDENTIFIER, vname.line, vname.column);
            id->as.identifier.name = vname;
            call->as.call.callee = id;
            call->as.call.args = args;
            call->as.call.arg_count = acount;
            variants[count++] = call;
        } else {
            ASTNode* id = ast_new_node(parser->arena, AST_IDENTIFIER, vname.line, vname.column);
            id->as.identifier.name = vname;
            variants[count++] = id;
        }
        match(parser, TOKEN_COMMA);
        match(parser, TOKEN_SEMICOLON);
    }
    consume(parser, TOKEN_RBRACE, "Expect '}' after enum body.");
    ASTNode* node = ast_new_node(parser->arena, AST_ENUM, line, col);
    node->as.enum_decl.name = name;
    node->as.enum_decl.variants = variants;
    node->as.enum_decl.variant_count = count;
    return node;
}

static ASTNode* declaration(Parser* parser) {
    int line = parser->current.line; int col = parser->current.column;
    if (match(parser, TOKEN_MODULE)) {
        consume_identifier(parser, "Expect module name."); ASTNode* node = ast_new_node(parser->arena, AST_MODULE, parser->previous.line, parser->previous.column);
        node->as.module.name = parser->previous; return node;
    }
    if (match(parser, TOKEN_USE)) {
        ASTNode* node = ast_new_node(parser->arena, AST_IMPORT, line, col);
        if (match(parser, TOKEN_LBRACE)) {
            ASTNode** paths = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 64); size_t count = 0;
            do { paths[count++] = expression(parser); } while (match(parser, TOKEN_COMMA));
            consume(parser, TOKEN_RBRACE, "Expect '}'."); node->as.import.paths = paths; node->as.import.path_count = count;
        } else {
            Token curr = parser->current;
            while (!check(parser, TOKEN_FROM) && !check(parser, TOKEN_SEMICOLON) && parser->current.line == line && !is_at_end(parser)) advance(parser);
            node->as.import.full_path = (Token){TOKEN_IDENTIFIER, curr.start, (size_t)(parser->previous.start + parser->previous.length - curr.start), line, col};
        }
        if (match(parser, TOKEN_FROM)) node->as.import.from_path = expression(parser);
        match(parser, TOKEN_SEMICOLON); return node;
    }
    if (match(parser, TOKEN_BANG)) {
        consume(parser, TOKEN_LBRACKET, "Expect '[' after '!'.");
        while (!check(parser, TOKEN_RBRACKET) && !is_at_end(parser)) advance(parser);
        consume(parser, TOKEN_RBRACKET, "Expect ']'."); return NULL;
    }
    bool is_pub=false, is_pri=false, is_static=false, is_comp=false, is_ext=false, is_exp=false;
    bool is_sealed=false;
    bool is_virt=false, is_over=false, is_final=false, is_dyn=false, is_mut=false, is_const=false, is_weak=false;
    bool is_async=false, is_inline=false, is_pure=false, is_abstract=false, is_frozen=false, is_lazy=false, is_unsafe=false;
    bool unsafe_before_pure = false;

    /* consume modifiers in any order */
    for (;;) {
        if (match(parser, TOKEN_PUB)) { is_pub = true; continue; }
        if (match(parser, TOKEN_PRI)) { is_pri = true; continue; }
        if (match(parser, TOKEN_STATIC)) { is_static = true; continue; }
        if (match(parser, TOKEN_COMPTIME)) { is_comp = true; continue; }
        if (match(parser, TOKEN_EXTERN)) { is_ext = true; continue; }
        if (match(parser, TOKEN_EXPORT)) { is_exp = true; continue; }
        if (match(parser, TOKEN_SEALED)) { is_sealed = true; continue; }
        if (match(parser, TOKEN_VIRTUAL)) { is_virt = true; continue; }
        if (match(parser, TOKEN_OVERRIDE)) { is_over = true; continue; }
        if (match(parser, TOKEN_FINAL)) { is_final = true; continue; }
        if (match(parser, TOKEN_DYN)) { is_dyn = true; continue; }
        if (match(parser, TOKEN_MUT)) { is_mut = true; continue; }
        if (match(parser, TOKEN_CONST)) { is_const = true; continue; }
        if (match(parser, TOKEN_WEAK)) { is_weak = true; continue; }
        if (match(parser, TOKEN_ASYNC)) { is_async = true; continue; }
        if (match(parser, TOKEN_INLINE)) { is_inline = true; continue; }
        if (match(parser, TOKEN_PURE)) { is_pure = true; continue; }
        if (match(parser, TOKEN_ABSTRACT)) { is_abstract = true; continue; }
        if (match(parser, TOKEN_FROZEN)) { is_frozen = true; continue; }
        if (match(parser, TOKEN_LAZY)) { is_lazy = true; continue; }
        if (match(parser, TOKEN_UNSAFE)) { if (!is_pure) unsafe_before_pure = true; is_unsafe = true; continue; }
        break;
    }

    if (is_comp && match(parser, TOKEN_LBRACE)) { ASTNode* b = parse_block(parser); if (b && b->type == AST_BLOCK) b->as.block.is_comptime = true; return b; }
    if (is_unsafe && match(parser, TOKEN_LBRACE)) { ASTNode* n = ast_new_node(parser->arena, AST_UNSAFE_BLOCK, line, col); n->as.unsafe_block.body = parse_block(parser); return n; }

    bool is_mm = false;
    bool is_ptr = false;
    bool is_own = false;
    bool is_ref = false;
    
    while (true) {
        if (match(parser, TOKEN_PTR)) { is_ptr = true; is_mm = true; }
        else if (match(parser, TOKEN_OWN)) { is_own = true; is_mm = true; }
        else if (match(parser, TOKEN_REF)) { is_ref = true; is_mm = true; }
        else break;
    }

    if (match(parser, TOKEN_CLASS)) {
        consume_identifier(parser, "Expect name."); Token name = parser->previous;
        ASTNode* base = match(parser, TOKEN_EXTENDS) ? expression(parser) : NULL;
        consume(parser, TOKEN_LBRACE, "Expect '{'.");
        ASTNode* body = parse_block(parser);
        ASTNode* node = ast_new_node(parser->arena, AST_CLASS, line, col);
        node->as.class_decl.name = name;
        node->as.class_decl.base_class = base;
        node->as.class_decl.body = body;
        node->as.class_decl.is_dyn = is_dyn;
        node->as.class_decl.is_abstract = is_abstract;
        node->as.class_decl.is_sealed = is_sealed;
        node->as.class_decl.is_pub = is_pub;
        node->as.class_decl.is_pri = is_pri;
        node->as.class_decl.is_comptime = is_comp;
        node->as.class_decl.is_extern = is_ext;
        node->as.class_decl.is_export = is_exp;
        return node;
    }
    if (match(parser, TOKEN_STRUCT)) {
        ASTNode* node = parse_struct(parser);
        /* Validate modifiers allowed on struct */
        if (is_static || is_virt || is_over || is_dyn || is_mut || is_const || is_weak || is_async || is_inline || is_pure || is_abstract || is_frozen || is_lazy || is_unsafe || is_ptr || is_own || is_ref) {
            fprintf(stderr, "[line %d] Error: modifier not allowed on 'struct' declaration.\n", line);
            parser->had_error = true;
        }
        node->as.struct_decl.is_pub = is_pub;
        node->as.struct_decl.is_pri = is_pri;
        node->as.struct_decl.is_sealed = is_sealed;
        node->as.struct_decl.is_abstract = is_abstract;
        node->as.struct_decl.is_comptime = is_comp;
        node->as.struct_decl.is_extern = is_ext;
        node->as.struct_decl.is_export = is_exp;
        return node;
    }
    if (match(parser, TOKEN_UNION)) return parse_union(parser);
    if (match(parser, TOKEN_ENUM)) return parse_enum(parser);
    if (match(parser, TOKEN_TRAIT)) {
        consume_identifier(parser, "Expect name."); Token name = parser->previous;
        consume(parser, TOKEN_LBRACE, "Expect '{'.");
        ASTNode* body = parse_block(parser); ASTNode* node = ast_new_node(parser->arena, AST_TRAIT, line, col);
        node->as.trait_decl.name = name; node->as.trait_decl.body = body; return node;
    }
    if (match(parser, TOKEN_IMPL)) {
        ASTNode* target = expression(parser); ASTNode* trait = NULL;
        if (match(parser, TOKEN_FOR)) { trait = target; target = expression(parser); }
        consume(parser, TOKEN_LBRACE, "Expect '{'."); ASTNode* body = parse_block(parser);
        ASTNode* node = ast_new_node(parser->arena, AST_IMPL, line, col);
        node->as.impl_decl.name = target->type == AST_IDENTIFIER ? target->as.identifier.name : (Token){0};
        node->as.impl_decl.trait_name = trait; node->as.impl_decl.body = body; return node;
    }
    if (match(parser, TOKEN_FN)) return parse_func(parser, is_pub || is_pri, is_static, true, is_virt, is_over, is_final, is_async, is_inline, is_pure, is_abstract, is_comp, is_unsafe, unsafe_before_pure, is_ext, is_exp);
    if (is_function_declaration(parser)) return parse_func(parser, is_pub || is_pri, is_static, false, is_virt, is_over, is_final, is_async, is_inline, is_pure, is_abstract, is_comp, is_unsafe, unsafe_before_pure, is_ext, is_exp);
    
    if (is_mut || is_const || is_dyn || is_static || is_comp || is_mm || check(parser, TOKEN_IDENTIFIER)) {
        if (is_mm && !parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Memory management features require @luv mm on at the top of the file.\n", line);
            parser->had_error = true;
        }

        // Lookahead to see if it's really a declaration
        if (!is_mut && !is_const && !is_dyn && !is_static && !is_comp && !is_mm) {
            Lexer saved = parser->lexer;
            Token curr = parser->current;
            Token prev = parser->previous;
            
            advance(parser); // past IDENTIFIER
            bool is_decl = check(parser, TOKEN_COLON) || check(parser, TOKEN_EQUAL) || check(parser, TOKEN_SEMICOLON);
            
            parser->lexer = saved;
            parser->current = curr;
            parser->previous = prev;
            
            if (!is_decl) return statement(parser);
        }
        
        consume_identifier(parser, "Expect name."); 
        Token name = parser->previous;
        ASTNode* type = match(parser, TOKEN_COLON) ? expression(parser) : NULL;
        ASTNode* init = match(parser, TOKEN_EQUAL) ? expression(parser) : NULL; match(parser, TOKEN_SEMICOLON);
        ASTNode* node = ast_new_node(parser->arena, AST_VAR_DECL, line, col);
        node->as.var_decl.name = name; node->as.var_decl.type_node = type; node->as.var_decl.init = init;
        node->as.var_decl.is_dyn = is_dyn; node->as.var_decl.is_mut = is_mut; node->as.var_decl.is_weak = is_weak; node->as.var_decl.is_const = is_const; node->as.var_decl.is_static = is_static;
        node->as.var_decl.is_comptime = is_comp; node->as.var_decl.is_extern = is_ext; node->as.var_decl.is_export = is_exp; 
        node->as.var_decl.is_ptr = is_ptr; node->as.var_decl.is_own = is_own; node->as.var_decl.is_ref = is_ref;
        node->as.var_decl.is_frozen = is_frozen; node->as.var_decl.is_lazy = is_lazy;
        return node;
    }
    return statement(parser);
}

void parser_init(Parser* parser, const char* source, Arena* arena) {
    lexer_init(&parser->lexer, source); parser->arena = arena; parser->had_error = false; parser->panic_mode = false; parser->mm_on = false; advance(parser);
}

ASTNode* parse(Parser* parser) {
    ASTNode* block = ast_new_node(parser->arena, AST_BLOCK, 1, 1);
    block->as.block.capacity = 1024; block->as.block.statements = (ASTNode**)arena_alloc(parser->arena, sizeof(ASTNode*) * 1024); block->as.block.count = 0;
    while (!is_at_end(parser)) {
        ASTNode* node = declaration(parser);
        if (node) { if (block->as.block.count < 1024) block->as.block.statements[block->as.block.count++] = node; }
        if (parser->panic_mode) {
            while (!is_at_end(parser)) {
                if (parser->previous.type == TOKEN_SEMICOLON) break;
                switch (parser->current.type) {
                    case TOKEN_CLASS: case TOKEN_FN: case TOKEN_MUT: case TOKEN_IF:
                    case TOKEN_FOR: case TOKEN_WHILE: case TOKEN_RETURN: case TOKEN_MODULE: goto sync_done;
                    default: advance(parser);
                }
            }
            sync_done: parser->panic_mode = false;
        }
    }
    return block;
}
