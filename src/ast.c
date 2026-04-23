#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Arena {
    char* memory;
    size_t size;
    size_t offset;
};

Arena* arena_new(size_t size) {
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    arena->memory = (char*)malloc(size);
    arena->size = size;
    arena->offset = 0;
    return arena;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (arena->offset + size > arena->size) {
        fprintf(stderr, "Arena overflow\n");
        exit(1);
    }
    void* ptr = arena->memory + arena->offset;
    arena->offset += size;
    // Align to 8 bytes
    arena->offset = (arena->offset + 7) & ~7;
    return ptr;
}

void arena_free(Arena* arena) {
    free(arena->memory);
    free(arena);
}

ASTNode* ast_new_node(Arena* arena, ASTNodeType type, int line, int col) {
    ASTNode* node = (ASTNode*)arena_alloc(arena, sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    node->line = line;
    node->col = col;
    return node;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

void ast_print(ASTNode* node, int indent) {
    if (!node) return;
    print_indent(indent);
    printf("[%d:%d] ", node->line, node->col);

    switch (node->type) {
        case AST_MODULE:
            printf("Module: %.*s\n", (int)node->as.module.name.length, node->as.module.name.start);
            break;
        case AST_IMPORT:
            printf("Import\n");
            if (node->as.import.path_count > 0) {
                for (size_t i = 0; i < node->as.import.path_count; i++) {
                    ast_print(node->as.import.paths[i], indent + 1);
                }
            } else {
                print_indent(indent + 1);
                printf("Path: %.*s\n", (int)node->as.import.full_path.length, node->as.import.full_path.start);
            }
            if (node->as.import.from_path) {
                print_indent(indent + 1);
                printf("From:\n");
                ast_print(node->as.import.from_path, indent + 2);
            }
            break;
        case AST_CLASS:
            printf("Class: %.*s (dyn: %d)\n", (int)node->as.class_decl.name.length, node->as.class_decl.name.start, node->as.class_decl.is_dyn);
            if (node->as.class_decl.base_class) {
                print_indent(indent + 1);
                printf("Extends:\n");
                ast_print(node->as.class_decl.base_class, indent + 2);
            }
            ast_print(node->as.class_decl.body, indent + 1);
            break;
        case AST_TRAIT:
            printf("Trait: %.*s\n", (int)node->as.trait_decl.name.length, node->as.trait_decl.name.start);
            ast_print(node->as.trait_decl.body, indent + 1);
            break;
        case AST_IMPL:
            printf("Impl: %.*s\n", (int)node->as.impl_decl.name.length, node->as.impl_decl.name.start);
            if (node->as.impl_decl.trait_name) {
                print_indent(indent + 1);
                printf("For Trait:\n");
                ast_print(node->as.impl_decl.trait_name, indent + 2);
            }
            ast_print(node->as.impl_decl.body, indent + 1);
            break;
        case AST_FUNC:
            printf("Func: %.*s (pub: %d, static: %d, virt: %d, over: %d)\n", 
                (int)node->as.func_decl.name.length, node->as.func_decl.name.start, 
                node->as.func_decl.is_pub, node->as.func_decl.is_static,
                node->as.func_decl.is_virtual, node->as.func_decl.is_override);
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                ast_print(node->as.func_decl.params[i], indent + 1);
            }
            if (node->as.func_decl.return_type) {
                print_indent(indent + 1);
                printf("Returns:\n");
                ast_print(node->as.func_decl.return_type, indent + 2);
            }
            ast_print(node->as.func_decl.body, indent + 1);
            break;
        case AST_INIT:
            printf("Init\n");
            for (size_t i = 0; i < node->as.init_decl.param_count; i++) {
                ast_print(node->as.init_decl.params[i], indent + 1);
            }
            ast_print(node->as.init_decl.body, indent + 1);
            break;
        case AST_DEINIT:
            printf("Deinit\n");
            ast_print(node->as.deinit_decl.body, indent + 1);
            break;
        case AST_STRUCT:
            printf("Struct: %.*s\n", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
            for (size_t i = 0; i < node->as.struct_decl.field_count; i++) {
                ast_print(node->as.struct_decl.fields[i], indent + 1);
            }
            break;
        case AST_UNION:
            printf("Union: %.*s\n", (int)node->as.union_decl.name.length, node->as.union_decl.name.start);
            for (size_t i = 0; i < node->as.union_decl.field_count; i++) {
                ast_print(node->as.union_decl.fields[i], indent + 1);
            }
            break;
        case AST_ENUM:
            printf("Enum: %.*s\n", (int)node->as.enum_decl.name.length, node->as.enum_decl.name.start);
            for (size_t i = 0; i < node->as.enum_decl.variant_count; i++) {
                ast_print(node->as.enum_decl.variants[i], indent + 1);
            }
            break;
        case AST_VAR_DECL:
            printf("VarDecl: %.*s (mut: %d, static: %d, comptime: %d, extern: %d, export: %d)\n", 
                (int)node->as.var_decl.name.length, node->as.var_decl.name.start, 
                node->as.var_decl.is_mut, node->as.var_decl.is_static,
                node->as.var_decl.is_comptime, node->as.var_decl.is_extern,
                node->as.var_decl.is_export);
            if (node->as.var_decl.type_node) ast_print(node->as.var_decl.type_node, indent + 1);
            if (node->as.var_decl.init) ast_print(node->as.var_decl.init, indent + 1);
            break;
        case AST_BLOCK:
            printf("Block (comptime: %d)\n", node->as.block.is_comptime);
            for (size_t i = 0; i < node->as.block.count; i++) {
                ast_print(node->as.block.statements[i], indent + 1);
            }
            break;
        case AST_EXPR_STMT:
            printf("ExprStmt\n");
            ast_print(node->as.expr_stmt.expr, indent + 1);
            break;
        case AST_IF:
            printf("If\n");
            ast_print(node->as.if_stmt.condition, indent + 1);
            ast_print(node->as.if_stmt.then_branch, indent + 1);
            if (node->as.if_stmt.else_branch) {
                print_indent(indent);
                printf("Else\n");
                ast_print(node->as.if_stmt.else_branch, indent + 1);
            }
            break;
        case AST_WHILE:
            printf("While\n");
            ast_print(node->as.while_stmt.condition, indent + 1);
            ast_print(node->as.while_stmt.body, indent + 1);
            break;
        case AST_FOR:
            printf("For\n");
            if (node->as.for_stmt.init) ast_print(node->as.for_stmt.init, indent + 1);
            if (node->as.for_stmt.condition) ast_print(node->as.for_stmt.condition, indent + 1);
            if (node->as.for_stmt.increment) ast_print(node->as.for_stmt.increment, indent + 1);
            ast_print(node->as.for_stmt.body, indent + 1);
            break;
        case AST_FOR_IN:
            printf("ForIn: %.*s", (int)node->as.for_in.value_name.length, node->as.for_in.value_name.start);
            if (node->as.for_in.has_index) {
                printf(", %.*s", (int)node->as.for_in.index_name.length, node->as.for_in.index_name.start);
            }
            printf("\n");
            ast_print(node->as.for_in.iterable, indent + 1);
            ast_print(node->as.for_in.body, indent + 1);
            break;
        case AST_RETURN:
            printf("Return\n");
            if (node->as.return_stmt.value) ast_print(node->as.return_stmt.value, indent + 1);
            break;
        case AST_MATCH:
            printf("Match\n");
            ast_print(node->as.match_stmt.target, indent + 1);
            for (size_t i = 0; i < node->as.match_stmt.arm_count; i++) {
                ast_print(node->as.match_stmt.arms[i], indent + 1);
            }
            break;
        case AST_MATCH_ARM:
            printf("MatchArm\n");
            ast_print(node->as.match_arm.pattern, indent + 1);
            ast_print(node->as.match_arm.body, indent + 1);
            break;
        case AST_SWITCH:
            printf("Switch\n");
            ast_print(node->as.switch_stmt.target, indent + 1);
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ast_print(node->as.switch_stmt.cases[i], indent + 1);
            }
            if (node->as.switch_stmt.default_case) {
                print_indent(indent + 1);
                printf("Default:\n");
                ast_print(node->as.switch_stmt.default_case, indent + 2);
            }
            break;
        case AST_SWITCH_CASE:
            printf("Case\n");
            ast_print(node->as.switch_case.value, indent + 1);
            ast_print(node->as.switch_case.body, indent + 1);
            break;
        case AST_BINARY:
            printf("Binary: %.*s\n", (int)node->as.binary.op.length, node->as.binary.op.start);
            ast_print(node->as.binary.left, indent + 1);
            ast_print(node->as.binary.right, indent + 1);
            break;
        case AST_UNARY:
            printf("Unary: %.*s\n", (int)node->as.unary.op.length, node->as.unary.op.start);
            ast_print(node->as.unary.right, indent + 1);
            break;
        case AST_LITERAL:
            printf("Literal: %.*s\n", (int)node->as.literal.token.length, node->as.literal.token.start);
            break;
        case AST_IDENTIFIER:
            printf("Identifier: %.*s\n", (int)node->as.identifier.name.length, node->as.identifier.name.start);
            break;
        case AST_CALL:
            printf("Call\n");
            ast_print(node->as.call.callee, indent + 1);
            for (size_t i = 0; i < node->as.call.arg_count; i++) {
                ast_print(node->as.call.args[i], indent + 1);
            }
            break;
        case AST_MEMBER_ACCESS:
            printf("MemberAccess: %.*s\n", (int)node->as.member_access.member.length, node->as.member_access.member.start);
            ast_print(node->as.member_access.object, indent + 1);
            break;
        case AST_INDEX_ACCESS:
            printf("IndexAccess\n");
            ast_print(node->as.index_access.object, indent + 1);
            ast_print(node->as.index_access.index, indent + 1);
            break;
        case AST_SLICE:
            printf("Slice\n");
            ast_print(node->as.slice.object, indent + 1);
            if (node->as.slice.start) ast_print(node->as.slice.start, indent + 2);
            if (node->as.slice.end) ast_print(node->as.slice.end, indent + 2);
            if (node->as.slice.step) ast_print(node->as.slice.step, indent + 2);
            break;
        case AST_LIST_LITERAL:
            printf("ListLiteral\n");
            for (size_t i = 0; i < node->as.list_literal.count; i++) {
                ast_print(node->as.list_literal.elements[i], indent + 1);
            }
            break;
        case AST_RANGE:
            printf("Range (inclusive: %d)\n", node->as.range.inclusive);
            ast_print(node->as.range.start, indent + 1);
            ast_print(node->as.range.end, indent + 1);
            break;
        case AST_ASSIGN:
            printf("Assign\n");
            ast_print(node->as.assign.target, indent + 1);
            ast_print(node->as.assign.value, indent + 1);
            break;
        case AST_COMPOUND_ASSIGN:
            printf("CompoundAssign: %.*s\n", (int)node->as.compound_assign.op.length, node->as.compound_assign.op.start);
            ast_print(node->as.compound_assign.target, indent + 1);
            ast_print(node->as.compound_assign.value, indent + 1);
            break;
        case AST_TERNARY:
            printf("Ternary\n");
            ast_print(node->as.ternary.condition, indent + 1);
            ast_print(node->as.ternary.then_expr, indent + 1);
            ast_print(node->as.ternary.else_expr, indent + 1);
            break;
        case AST_ATTRIBUTE:
            printf("Attribute: @%.*s\n", (int)node->as.attribute.name.length, node->as.attribute.name.start);
            if (node->as.attribute.expr) ast_print(node->as.attribute.expr, indent + 1);
            break;
        default:
            printf("Node type %d\n", node->type);
            break;
    }
}
