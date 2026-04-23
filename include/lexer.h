#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

/* =========================================================
   TOKEN DEFINITIONS
   ========================================================= */

typedef enum {
  // Literals & Identifiers
  TOKEN_IDENTIFIER,
  TOKEN_VARDATA,       
  TOKEN_CHAR,          
  TOKEN_STRING_INTERP, 
  TOKEN_TRUE,          
  TOKEN_FALSE,         
  TOKEN_NEN,

  // Brackets & Parentheses
  TOKEN_LBRACKET, // [
  TOKEN_RBRACKET, // ]
  TOKEN_LPAREN,   // (
  TOKEN_RPAREN,   // )
  TOKEN_LBRACE,   // {
  TOKEN_RBRACE,   // }

  // Punctuation & Scoping
  TOKEN_COMMA,       
  TOKEN_DOT,         
  TOKEN_DOT_DOT,     
  TOKEN_DOT_DOT_DOT, 
  TOKEN_COLON,       
  TOKEN_COLON_COLON, 
  TOKEN_SEMICOLON,   
  TOKEN_ARROW,       // =>
  TOKEN_THIN_ARROW,  // ->
  TOKEN_TILDE_ARROW, // ~>
  TOKEN_ARROW_STAR,  // ->*
  TOKEN_LEFT_ARROW,  // <-
  TOKEN_QUESTION,    
  TOKEN_AT,          
  TOKEN_BANG,        // !
  TOKEN_HASH,        // #

  // Assignment & Comparison
  TOKEN_EQUAL,         
  TOKEN_EQUAL_EQUAL,   
  TOKEN_PLUS_EQUAL,    
  TOKEN_MINUS_EQUAL,   
  TOKEN_STAR_EQUAL,    
  TOKEN_SLASH_EQUAL,   
  TOKEN_PERCENT_EQUAL, 
  TOKEN_BANG_EQUAL,    
  TOKEN_LESS,          
  TOKEN_LESS_EQUAL,    
  TOKEN_GREATER,       
  TOKEN_GREATER_EQUAL, 
  TOKEN_NULL_COALESCE, // ??
  TOKEN_SAFE_NAV,      // ?.

  // Arithmetic
  TOKEN_PLUS,    
  TOKEN_MINUS,   
  TOKEN_STAR,    
  TOKEN_SLASH,   
  TOKEN_PERCENT, 

  // Bitwise Operators
  TOKEN_AMP,         
  TOKEN_PIPE,        
  TOKEN_CARET,       
  TOKEN_TILDE,       
  TOKEN_LEFT_SHIFT,  
  TOKEN_RIGHT_SHIFT, 

  // Logical Operators
  TOKEN_AND_AND, 
  TOKEN_OR_OR,   

  // Keywords (Control Flow)
  TOKEN_IF,
  TOKEN_EF,
  TOKEN_ELSE,
  TOKEN_WHILE,
  TOKEN_FOR,
  TOKEN_IN,
  TOKEN_MATCH,
  TOKEN_SWITCH,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_LOOP,

  // Keywords (Functions & Declarations)
  TOKEN_FN,       
  TOKEN_RETURN,   
  TOKEN_MUT,      
  TOKEN_CONST,
  TOKEN_STATIC,   
  TOKEN_COMPTIME, 

  // Keywords (Modules & FFI)
  TOKEN_USE,    
  TOKEN_FROM,   
  TOKEN_MODULE, 
  TOKEN_EXTERN, 
  TOKEN_EXPORT, 

  // Keywords (Full OOP Suite)
  TOKEN_CLASS,    
  TOKEN_EXTENDS,  
  TOKEN_IMPL,     
  TOKEN_TRAIT,    
  TOKEN_VIRTUAL,  
  TOKEN_OVERRIDE, 
  TOKEN_SUPER,    
  TOKEN_SELF,     
  TOKEN_DYN,      
  TOKEN_PUB,      
  TOKEN_PRI,      
  TOKEN_INIT,     
  TOKEN_DEINIT,   
  TOKEN_ABSTRACT,
  TOKEN_SEALED,
  TOKEN_FINAL,
  TOKEN_INTERFACE,

  // Keywords (Async / Concurrency)
  TOKEN_SPARK,
  TOKEN_ASYNC,
  TOKEN_AWAIT,
  TOKEN_YIELD,
  TOKEN_CHAN,
  TOKEN_SELECT,

  // Keywords (Memory Management)
  TOKEN_PTR,
  TOKEN_OWN,
  TOKEN_REF,
  TOKEN_WEAK,
  TOKEN_PIN,
  TOKEN_VOLATILE,
  TOKEN_RESTRICT,
  TOKEN_ALLOC,
  TOKEN_REALLOC,
  TOKEN_DEALLOC,
  TOKEN_FREE,
  TOKEN_MOVE,
  TOKEN_BORROW,
  TOKEN_UNSAFE,

  // Keywords (Type System)
  TOKEN_STRUCT,
  TOKEN_AS,       // Casting
  TOKEN_UNION,
  TOKEN_ENUM,
  TOKEN_TYPE,
  TOKEN_WHERE,
  TOKEN_IS,
  TOKEN_NOT,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_SIZEOF,
  TOKEN_TYPEOF,
  TOKEN_CAST,

  // Keywords (Error Handling)
  TOKEN_TRY,
  TOKEN_CATCH,
  TOKEN_THROW,
  TOKEN_DEFER,

  // Keywords (Advanced)
  TOKEN_ASM,
  TOKEN_PARFOR,
  TOKEN_REDUCE,
  TOKEN_ATOMIZER,
  TOKEN_MUTEX,
  TOKEN_PAR,
  TOKEN_INLINE,
  TOKEN_EMBED,
  TOKEN_MACRO,
  TOKEN_PURE,
  TOKEN_LAZY,
  TOKEN_FROZEN,
  TOKEN_GUARD,
  TOKEN_WITH,

  // Arbitrary precision
  TOKEN_TNT,
  TOKEN_NUMBER,
  
  // Signed Integer Types
  TOKEN_I8, TOKEN_I16, TOKEN_I32, TOKEN_I64, TOKEN_I128, TOKEN_I256,
  // Unsigned Integer Types
  TOKEN_U8, TOKEN_U16, TOKEN_U32, TOKEN_U64, TOKEN_U128, TOKEN_U256,
  // Pointer-sized
  TOKEN_USIZE, TOKEN_ISIZE,
  // Float Types
  TOKEN_F16, TOKEN_F32, TOKEN_F64, TOKEN_F128, TOKEN_F256,
  // Other Types
  TOKEN_BOOL, TOKEN_BYTE, TOKEN_BITS,
  TOKEN_STRING_TYPE,
  TOKEN_CHAR_TYPE,
  TOKEN_VOID,
  TOKEN_ANY,
  TOKEN_NEVER,

  // Virtual
  TOKEN_EOF,  
  TOKEN_ERROR,

  TOKEN_COUNT  // Always last — total number of token types
} TokenType;

typedef struct {
  TokenType type;
  const char *start; 
  size_t length;     
  int line;   
  int column; 
} Token;

typedef struct {
  const char *start;   
  const char *current; 
  int line;   
  int column; 
} Lexer;

void lexer_init(Lexer *l, const char *source);
Token next_token(Lexer *l);
const char *token_type_to_string(TokenType t);
void print_token(Token t);

#endif
