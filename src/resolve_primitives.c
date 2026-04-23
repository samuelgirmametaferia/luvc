#include "resolve_primitives.h"
#include "parser.h"
#include <string.h>
#include <stdio.h>

ASTNode* resolve_primitive(Parser* parser, ASTNode* attr_node) {
    Arena* arena = parser->arena;
    if (attr_node->type != AST_ATTRIBUTE) {
        return attr_node;
    }

    Token name = attr_node->as.attribute.name;
    ASTNode* expr = attr_node->as.attribute.expr;

    if (name.length == 5 && strncmp(name.start, "alloc", 5) == 0) {
        if (!parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Memory primitive @alloc requires @luv mm on at the top of the file.\n", attr_node->line);
            parser->had_error = true;
        }
        ASTNode* n = ast_new_node(arena, AST_ALLOC, attr_node->line, attr_node->col);
        // We expect expr to be a call-like argument list or just a type
        // For simplicity, we just store expr in type_expr for now.
        // A more complex parser might have passed a list of args.
        if (expr && expr->type == AST_LIST_LITERAL) {
            n->as.alloc_expr.type_expr = expr->as.list_literal.elements[0];
            if (expr->as.list_literal.count > 1) {
                n->as.alloc_expr.count = expr->as.list_literal.elements[1];
            }
        } else {
            n->as.alloc_expr.type_expr = expr;
            n->as.alloc_expr.count = NULL;
        }
        return n;
    }

    if (name.length == 4 && strncmp(name.start, "free", 4) == 0) {
        if (!parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Memory primitive @free requires @luv mm on at the top of the file.\n", attr_node->line);
            parser->had_error = true;
        }
        ASTNode* n = ast_new_node(arena, AST_DEALLOC, attr_node->line, attr_node->col);
        n->as.dealloc_expr.target = expr;
        return n;
    }

    if (name.length == 7 && strncmp(name.start, "realloc", 7) == 0) {
        if (!parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Memory primitive @realloc requires @luv mm on at the top of the file.\n", attr_node->line);
            parser->had_error = true;
        }
        // Implement realloc AST node or just return as is if no AST node
    }

    if (name.length == 7 && strncmp(name.start, "dealloc", 7) == 0) {
        if (!parser->mm_on) {
            fprintf(stderr, "[line %d] Error: Memory primitive @dealloc requires @luv mm on at the top of the file.\n", attr_node->line);
            parser->had_error = true;
        }
        ASTNode* n = ast_new_node(arena, AST_DEALLOC, attr_node->line, attr_node->col);
        n->as.dealloc_expr.target = expr;
        return n;
    }

    return attr_node;
}
