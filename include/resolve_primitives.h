#ifndef RESOLVE_PRIMITIVES_H
#define RESOLVE_PRIMITIVES_H

#include "ast.h"

typedef struct Parser Parser;

// Resolves a general attribute/primitive like @alloc(type) into a specific AST node or validates it.
// Returns the new or modified ASTNode.
ASTNode* resolve_primitive(Parser* parser, ASTNode* attr_node);

#endif
