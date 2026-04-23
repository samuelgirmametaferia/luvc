#ifndef AST_H
#define AST_H

#include "lexer.h"
#include <stddef.h>
#include <stdbool.h>

/* =========================================================
   ARENA ALLOCATOR
   ========================================================= */

typedef struct Arena Arena;

Arena* arena_new(size_t size);
void* arena_alloc(Arena* arena, size_t size);
void arena_free(Arena* arena);

/* =========================================================
   AST NODE TYPES
   ========================================================= */

typedef enum {
    // Declarations
    AST_MODULE,
    AST_IMPORT,
    AST_CLASS,
    AST_TRAIT,
    AST_IMPL,
    AST_FUNC,
    AST_VAR_DECL,
    AST_CONST_DECL,
    AST_INIT,
    AST_DEINIT,
    AST_STRUCT,
    AST_UNION,
    AST_ENUM,
    AST_TYPE_ALIAS,
    AST_MACRO_DEF,
    AST_INTERFACE,

    // Statements
    AST_BLOCK,
    AST_EXPR_STMT,
    AST_ASM,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_FOR_IN,
    AST_RETURN,
    AST_MATCH,
    AST_MATCH_ARM,
    AST_SWITCH,
    AST_SWITCH_CASE,
    AST_BREAK,
    AST_CONTINUE,
    AST_LOOP,
    AST_DEFER,
    AST_UNSAFE_BLOCK,
    AST_GUARD,
    AST_WITH,

    // Async / Concurrency
    AST_SPARK,
    AST_AWAIT,
    AST_YIELD,
    AST_CHAN_DECL,
    AST_CHAN_SEND,
    AST_CHAN_RECV,
    AST_SELECT,
    AST_SELECT_CASE,

    // Error Handling
    AST_TRY_CATCH,
    AST_THROW,

    // Memory
    AST_ALLOC,
    AST_DEALLOC,
    AST_MOVE,
    AST_BORROW,
    AST_DEREF,
    AST_ADDR_OF,

    // Expressions
    AST_BINARY,
    AST_UNARY,
    AST_LITERAL,
    AST_IDENTIFIER,
    AST_CALL,
    AST_MEMBER_ACCESS,
    AST_INDEX_ACCESS,
    AST_SLICE,
    AST_LIST_LITERAL,
    AST_RANGE,
    AST_ASSIGN,
    AST_COMPOUND_ASSIGN,
    AST_TERNARY,
    AST_ATTRIBUTE,
    AST_CUSTOM_ATTR,
    AST_PRIMITIVE_CALL,
    AST_RESERVED_VAR,
    AST_SIZEOF,
    AST_TYPEOF,
    AST_CAST_EXPR,
    AST_NULL_COALESCE,
    AST_SAFE_NAV,
    AST_LAMBDA,
    AST_SELF,
    AST_SUPER,
    AST_IS,
    AST_NEN,
} ASTNodeType;

typedef struct ASTNode ASTNode;

/* ---- Expression Nodes ---- */

typedef struct {
    ASTNode* expr;
} ASTExprStmt;

typedef struct {
    char* content;
    ASTNode* body;
} ASTAsm;

typedef struct {
    Token name;
} ASTModule;

typedef struct {
    ASTNode** paths; // For { a, b }
    size_t path_count;
    ASTNode* from_path; // For from "..."
    Token full_path; // For single path imports
} ASTImport;

typedef struct {
    Token name;
    ASTNode* base_class; // Optional
    ASTNode* body; // AST_BLOCK
    bool is_dyn;
    bool is_abstract;
    bool is_sealed;
    bool is_pub;
    bool is_pri;
    bool is_comptime;
    bool is_extern;
    bool is_export;
} ASTClass;

typedef struct {
    Token name;
    ASTNode* body; // AST_BLOCK
} ASTTrait;

typedef struct {
    Token name;
    ASTNode* trait_name; // Optional: impl Trait for Class
    ASTNode* body; // AST_BLOCK
} ASTImpl;

typedef struct {
    Token name;
    ASTNode** params; 
    size_t param_count;
    ASTNode* return_type; 
    ASTNode* body; 
    bool is_pub;
    bool is_static;
    bool is_fn_keyword_used;
    bool is_virtual;
    bool is_override;
    bool is_final;
    bool is_abstract;
    bool is_async;
    bool is_inline;
    bool is_pure;
    bool is_unsafe;
    bool is_unsafe_prefix;
    bool is_comptime;
    bool is_extern;
    bool is_export;
} ASTFunc;

typedef struct {
    Token name;
    ASTNode* type_node; 
    ASTNode* init; 
    bool is_mut;
    bool is_dyn;
    bool is_const;
    bool is_static;
    bool is_comptime;
    bool is_extern;
    bool is_export;
    bool is_ptr;
    bool is_own;
    bool is_ref;
    bool is_weak;
    bool is_pin;
    bool is_volatile;
    bool is_restrict;
    bool is_lazy;
    bool is_frozen;
} ASTVarDecl;

typedef struct {
    Token name;
    ASTNode* type_node;
    ASTNode* init;
} ASTConstDecl;

typedef struct {
    ASTNode** params;
    size_t param_count;
    ASTNode* body;
} ASTInit;

typedef struct {
    ASTNode* body;
} ASTDeinit;

/* ---- Union / Enum / Type Alias ---- */

typedef struct {
    Token name;
    ASTNode** fields;     // array of AST_VAR_DECL
    size_t field_count;
    bool is_pub;
    bool is_pri;
    bool is_sealed;
    bool is_abstract;
    bool is_comptime;
    bool is_extern;
    bool is_export;
} ASTStruct;

typedef struct {
    Token name;
    ASTNode** fields;     // array of AST_VAR_DECL
    size_t field_count;
} ASTUnion;

typedef struct {
    Token name;
    ASTNode** variants;   // array of AST_IDENTIFIER or AST_CALL
    size_t variant_count;
} ASTEnum;

typedef struct {
    Token name;
    ASTNode* target_type;
} ASTTypeAlias;

typedef struct {
    Token name;
    ASTNode* body;        // AST_BLOCK
} ASTMacroDef;

typedef struct {
    Token name;
    ASTNode* body;        // AST_BLOCK
} ASTInterface;

/* ---- Statement Nodes ---- */

typedef struct {
    ASTNode** statements;
    size_t count;
    size_t capacity;
    bool is_comptime;
} ASTBlock;

typedef struct {
    ASTNode* condition;
    ASTNode* then_branch;
    ASTNode* else_branch; 
} ASTIf;

typedef struct {
    ASTNode* condition;
    ASTNode* body;
} ASTWhile;

typedef struct {
    ASTNode* init;
    ASTNode* condition;
    ASTNode* increment;
    ASTNode* body;
} ASTFor;

typedef struct {
    Token value_name;
    Token index_name;
    bool has_index;
    ASTNode* iterable;
    ASTNode* body;
} ASTForIn;

typedef struct {
    ASTNode* value;
} ASTReturn;

typedef struct {
    ASTNode* target;
    ASTNode** arms;
    size_t arm_count;
} ASTMatch;

typedef struct {
    ASTNode* pattern;
    ASTNode* body;
} ASTMatchArm;

typedef struct {
    ASTNode* target;
    ASTNode** cases;
    size_t case_count;
    ASTNode* default_case;
} ASTSwitch;

typedef struct {
    ASTNode* value;
    ASTNode* body;
} ASTSwitchCase;

typedef struct {
    ASTNode* body;
} ASTLoop;

typedef struct {
    ASTNode* body; // can be block or expression
} ASTDefer;

typedef struct {
    ASTNode* body;
} ASTUnsafeBlock;

typedef struct {
    ASTNode* condition;
    ASTNode* else_body;
} ASTGuard;

typedef struct {
    ASTNode* resource;
    ASTNode* body;
} ASTWith;

/* ---- Async / Concurrency Nodes ---- */

typedef struct {
    ASTNode* expr;       // The expression/call to spark
    bool is_detached;    // fire-and-forget vs. awaitable
} ASTSpark;

typedef struct {
    ASTNode* expr;
} ASTAwait;

typedef struct {
    ASTNode* value;
} ASTYield;

typedef struct {
    Token name;
    ASTNode* elem_type;  // chan<i32>
    ASTNode* buffer_size; // optional
} ASTChanDecl;

typedef struct {
    ASTNode* channel;
    ASTNode* value;
} ASTChanSend;

typedef struct {
    ASTNode* channel;
} ASTChanRecv;

typedef struct {
    ASTNode** cases;
    size_t case_count;
    ASTNode* default_case;
} ASTSelect;

typedef struct {
    ASTNode* pattern;
    ASTNode* body;
} ASTSelectCase;

/* ---- Error Handling Nodes ---- */

typedef struct {
    ASTNode* try_body;
    Token error_name;       // catch (e)
    ASTNode* catch_body;
} ASTTryCatch;

typedef struct {
    ASTNode* value;
} ASTThrow;

/* ---- Memory Nodes ---- */

typedef struct {
    ASTNode* type_expr;
    ASTNode* count;          // alloc(i32, 10) => type=i32, count=10
} ASTAlloc;

typedef struct {
    ASTNode* target;
} ASTDealloc;

typedef struct {
    ASTNode* value;
} ASTMove;

typedef struct {
    ASTNode* value;
    bool is_mut;
} ASTBorrow;

typedef struct {
    ASTNode* value;
} ASTDeref;

typedef struct {
    ASTNode* value;
} ASTAddrOf;

/* ---- Expression Nodes ---- */

typedef struct {
    ASTNode* left;
    Token op;
    ASTNode* right;
} ASTBinary;

typedef struct {
    Token op;
    ASTNode* right;
} ASTUnary;

typedef struct {
    Token token; 
} ASTLiteral;

typedef struct {
    Token name;
} ASTIdentifier;

typedef struct {
    ASTNode* callee;
    ASTNode** args;
    size_t arg_count;
} ASTCall;

typedef struct {
    ASTNode* object;
    Token member;
} ASTMemberAccess;

typedef struct {
    ASTNode* object;
    ASTNode* index;
} ASTIndexAccess;

typedef struct {
    ASTNode* object;
    ASTNode* start;
    ASTNode* end;
    ASTNode* step;
} ASTSlice;

typedef struct {
    ASTNode** elements;
    size_t count;
} ASTListLiteral;

typedef struct {
    ASTNode* start;
    ASTNode* end;
    bool inclusive;
} ASTRange;

typedef struct {
    ASTNode* target;
    ASTNode* value;
} ASTAssign;

typedef struct {
    ASTNode* target;
    Token op; 
    ASTNode* value;
} ASTCompoundAssign;

typedef struct {
    ASTNode* condition;
    ASTNode* then_expr;
    ASTNode* else_expr;
} ASTTernary;

typedef struct {
    Token name;
    ASTNode* expr; // Optional: @attr(expr)
} ASTAttribute;

typedef struct {
    Token name;
    ASTNode** args;
    size_t arg_count;
} ASTCustomAttr;

typedef enum {
    PRIMITIVE_UNKNOWN,
    PRIMITIVE_MM,
    PRIMITIVE_THREAD
} PrimitiveKind;

/* @luv primitive calls: @luv mm on, @luv parallel, etc. */
typedef struct {
    Token namespace_name; // "luv" for namespaced directives
    Token name;           // primitive or directive name
    Token qualifier;      // optional secondary name, such as "on"
    ASTNode** args;
    size_t arg_count;
    PrimitiveKind kind;
    bool requires_mm;
} ASTPrimitiveCall;

/* Reserved variables: luv.name, luv.version, etc. */
typedef struct {
    Token member;       // "name", "version", "file", etc.
} ASTReservedVar;

typedef struct {
    ASTNode* expr;
} ASTSizeof;

typedef struct {
    ASTNode* expr;
} ASTTypeof;

typedef struct {
    ASTNode* expr;
    ASTNode* target_type;
} ASTCastExpr;

typedef struct {
    ASTNode* left;
    ASTNode* right;
} ASTNullCoalesce;

typedef struct {
    ASTNode* object;
    Token member;
} ASTSafeNav;

typedef struct {
    ASTNode* expr;
} ASTLambda;

typedef struct {
    int line;
} ASTSelf;

typedef struct {
    int line;
} ASTSuper;

typedef struct {
    ASTNode* left;
    ASTNode* right_type;
} ASTIs;

typedef struct {
    int line;
} ASTNen;

/* =========================================================
   AST NODE UNION
   ========================================================= */

struct ASTNode {
    ASTNodeType type;
    union {
        ASTModule module;
        ASTImport import;
        ASTClass class_decl;
        ASTTrait trait_decl;
        ASTImpl impl_decl;
        ASTFunc func_decl;
        ASTVarDecl var_decl;
        ASTConstDecl const_decl;
        ASTInit init_decl;
        ASTDeinit deinit_decl;
        ASTStruct struct_decl;
        ASTUnion union_decl;
        ASTEnum enum_decl;
        ASTTypeAlias type_alias;
        ASTMacroDef macro_def;
        ASTInterface interface_decl;
        ASTBlock block;
        ASTExprStmt expr_stmt;
        ASTAsm asm_stmt;
        ASTIf if_stmt;
        ASTWhile while_stmt;
        ASTFor for_stmt;
        ASTForIn for_in;
        ASTReturn return_stmt;
        ASTMatch match_stmt;
        ASTMatchArm match_arm;
        ASTSwitch switch_stmt;
        ASTSwitchCase switch_case;
        ASTLoop loop_stmt;
        ASTDefer defer_stmt;
        ASTUnsafeBlock unsafe_block;
        ASTGuard guard_stmt;
        ASTWith with_stmt;
        ASTSpark spark;
        ASTAwait await_expr;
        ASTYield yield_expr;
        ASTChanDecl chan_decl;
        ASTChanSend chan_send;
        ASTChanRecv chan_recv;
        ASTSelect select_stmt;
        ASTSelectCase select_case;
        ASTTryCatch try_catch;
        ASTThrow throw_stmt;
        ASTAlloc alloc_expr;
        ASTDealloc dealloc_expr;
        ASTMove move_expr;
        ASTBorrow borrow_expr;
        ASTDeref deref_expr;
        ASTAddrOf addr_of;
        ASTBinary binary;
        ASTUnary unary;
        ASTLiteral literal;
        ASTIdentifier identifier;
        ASTCall call;
        ASTMemberAccess member_access;
        ASTIndexAccess index_access;
        ASTSlice slice;
        ASTListLiteral list_literal;
        ASTRange range;
        ASTAssign assign;
        ASTCompoundAssign compound_assign;
        ASTTernary ternary;
        ASTAttribute attribute;
        ASTCustomAttr custom_attr;
        ASTPrimitiveCall primitive_call;
        ASTReservedVar reserved_var;
        ASTSizeof sizeof_expr;
        ASTTypeof typeof_expr;
        ASTCastExpr cast_expr;
        ASTNullCoalesce null_coalesce;
        ASTSafeNav safe_nav;
        ASTLambda lambda;
        ASTSelf self_expr;
        ASTSuper super_expr;
        ASTIs is_expr;
        ASTNen nen_expr;
    } as;
    int line;
    int col;
};

ASTNode* ast_new_node(Arena* arena, ASTNodeType type, int line, int col);
void ast_print(ASTNode* node, int indent);

#endif
