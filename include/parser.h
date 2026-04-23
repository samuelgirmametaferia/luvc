#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct Parser Parser;
struct Parser {
    Lexer lexer;
    Token current;
    Token previous;
    Arena* arena;
    bool had_error;
    bool panic_mode;
    bool mm_on;
    bool mm_enabled;
    bool allow_file_directives;
    int block_depth;
};

void parser_init(Parser* parser, const char* source, Arena* arena);
ASTNode* parse(Parser* parser);

#endif
