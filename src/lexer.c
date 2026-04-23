#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================
   KEYWORDS
   ========================================================= */

typedef struct {
  const char *text;
  TokenType type;
} Keyword;

static Keyword keywords[] = {
    // Literals
    {"true", TOKEN_TRUE},
    {"false", TOKEN_FALSE},
    {"nen", TOKEN_NEN},

    // Control Flow
    {"if", TOKEN_IF},
    {"ef", TOKEN_EF},
    {"else", TOKEN_ELSE},
    {"while", TOKEN_WHILE},
    {"for", TOKEN_FOR},
    {"in", TOKEN_IN},
    {"switch", TOKEN_SWITCH},
    {"match", TOKEN_MATCH},
    {"break", TOKEN_BREAK},
    {"continue", TOKEN_CONTINUE},
    {"loop", TOKEN_LOOP},

    // Functions & Declarations
    {"fn", TOKEN_FN},
    {"return", TOKEN_RETURN},
    {"mut", TOKEN_MUT},
    {"const", TOKEN_CONST},
    {"static", TOKEN_STATIC},
    {"comptime", TOKEN_COMPTIME},

    // Modules & FFI
    {"use", TOKEN_USE},
    {"from", TOKEN_FROM},
    {"module", TOKEN_MODULE},
    {"extern", TOKEN_EXTERN},
    {"export", TOKEN_EXPORT},

    // Full OOP Suite
    {"class", TOKEN_CLASS},
    {"extends", TOKEN_EXTENDS},
    {"impl", TOKEN_IMPL},
    {"trait", TOKEN_TRAIT},
    {"virtual", TOKEN_VIRTUAL},
    {"override", TOKEN_OVERRIDE},
    {"super", TOKEN_SUPER},
    {"self", TOKEN_SELF},
    {"dyn", TOKEN_DYN},
    {"pub", TOKEN_PUB},
    {"pb", TOKEN_PUB},
    {"pri", TOKEN_PRI},
    {"init", TOKEN_INIT},
    {"deinit", TOKEN_DEINIT},
    {"abstract", TOKEN_ABSTRACT},
    {"sealed", TOKEN_SEALED},
    {"final", TOKEN_FINAL},
    {"interface", TOKEN_INTERFACE},

    // Async / Concurrency
    {"spark", TOKEN_SPARK},
    {"async", TOKEN_ASYNC},
    {"await", TOKEN_AWAIT},
    {"yield", TOKEN_YIELD},
    {"chan", TOKEN_CHAN},
    {"select", TOKEN_SELECT},

    // Memory Management
    {"ptr", TOKEN_PTR},
    {"own", TOKEN_OWN},
    {"ref", TOKEN_REF},
    {"weak", TOKEN_WEAK},
    {"pin", TOKEN_PIN},
    {"vol", TOKEN_VOLATILE},
    {"restrict", TOKEN_RESTRICT},
    {"move", TOKEN_MOVE},
    {"borrow", TOKEN_BORROW},
    {"unsafe", TOKEN_UNSAFE},

    // Type System
    {"struct", TOKEN_STRUCT},
    {"as", TOKEN_AS},
    {"union", TOKEN_UNION},
    {"enum", TOKEN_ENUM},
    {"type", TOKEN_TYPE},
    {"where", TOKEN_WHERE},
    {"is", TOKEN_IS},
    {"not", TOKEN_NOT},
    {"and", TOKEN_AND},
    {"or", TOKEN_OR},
    {"sizeof", TOKEN_SIZEOF},
    {"typeof", TOKEN_TYPEOF},
    {"cast", TOKEN_CAST},

    // Error Handling
    {"try", TOKEN_TRY},
    {"catch", TOKEN_CATCH},
    {"throw", TOKEN_THROW},
    {"defer", TOKEN_DEFER},

    // Advanced
    {"asm", TOKEN_ASM},
    {"parfor", TOKEN_PARFOR},
    {"reduce", TOKEN_REDUCE},
    {"atomizer", TOKEN_ATOMIZER},
    {"mutex", TOKEN_MUTEX},
    {"par", TOKEN_PAR},
    {"inline", TOKEN_INLINE},
    {"embed", TOKEN_EMBED},
    {"macro", TOKEN_MACRO},
    {"pure", TOKEN_PURE},
    {"lazy", TOKEN_LAZY},
    {"frozen", TOKEN_FROZEN},
    {"guard", TOKEN_GUARD},
    {"with", TOKEN_WITH},

    // Arbitrary precision
    {"tnt", TOKEN_TNT},
    {"number", TOKEN_NUMBER},

    // Signed Integer Types
    {"i8", TOKEN_I8},
    {"i16", TOKEN_I16},
    {"i32", TOKEN_I32},
    {"i64", TOKEN_I64},
    {"i128", TOKEN_I128},
    {"i256", TOKEN_I256},

    // Unsigned Integer Types
    {"u8", TOKEN_U8},
    {"u16", TOKEN_U16},
    {"u32", TOKEN_U32},
    {"u64", TOKEN_U64},
    {"u128", TOKEN_U128},
    {"u256", TOKEN_U256},

    // Pointer-sized
    {"usize", TOKEN_USIZE},
    {"isize", TOKEN_ISIZE},

    // Float Types
    {"f16", TOKEN_F16},
    {"f32", TOKEN_F32},
    {"f64", TOKEN_F64},
    {"f128", TOKEN_F128},
    {"f256", TOKEN_F256},// Other Types
    {"bool", TOKEN_BOOL},
    {"byte", TOKEN_BYTE},
    {"bits", TOKEN_BITS},
    {"string", TOKEN_STRING_TYPE},
    {"char", TOKEN_CHAR_TYPE},
    {"void", TOKEN_VOID},
    {"any", TOKEN_ANY},
    {"never", TOKEN_NEVER},

    {NULL, 0}};

/* =========================================================
   SAFETY HELPERS
   ========================================================= */

static inline int at_end(Lexer *l) { return *l->current == '\0'; }
static inline char peek(Lexer *l) { return *l->current; }
static inline char peek_next(Lexer *l) {
  if (at_end(l)) return '\0';
  return l->current[1];
}

static inline char peek_ahead(Lexer *l, int n) {
  for (int i = 0; i < n; i++) {
    if (l->current[i] == '\0') return '\0';
  }
  return l->current[n];
}

static inline char advance(Lexer *l) {
  char c = *l->current++;
  l->column++;
  return c;
}

static int match(Lexer *l, char expected) {
  if (at_end(l) || *l->current != expected) return 0;
  l->current++;
  l->column++;
  return 1;
}

/* =========================================================
   TOKEN CREATION
   ========================================================= */

static Token make_token(Lexer *l, TokenType type, int start_col) {
  Token t;
  t.type = type;
  t.start = l->start;
  t.length = (size_t)(l->current - l->start);
  t.line = l->line;
  t.column = start_col;
  return t;
}

static Token error_token(Lexer *l, const char *msg) {
  Token t;
  t.type = TOKEN_ERROR;
  t.start = msg;
  t.length = (size_t)strlen(msg);
  t.line = l->line;
  t.column = l->column;
  return t;
}

/* =========================================================
   TRIVIA SKIP
   ========================================================= */

static void skip_trivia(Lexer *l) {
  for (;;) {
    char c = peek(l);
    switch (c) {
    case ' ':
    case '\t':
    case '\r':
      advance(l);
      break;
    case '\n':
      advance(l);
      l->line++;
      l->column = 1;
      break;
    case '/':
      if (peek_next(l) == '/') {
        while (!at_end(l) && peek(l) != '\n') advance(l);
      } else if (peek_next(l) == '*') {
        advance(l); advance(l);
        int depth = 1;
        while (depth > 0 && !at_end(l)) {
          if (peek(l) == '/' && peek_next(l) == '*') { advance(l); advance(l); depth++; }
          else if (peek(l) == '*' && peek_next(l) == '/') { advance(l); advance(l); depth--; }
          else { if (peek(l) == '\n') { l->line++; l->column = 1; } advance(l); }
        }
      } else return;
      break;
    default: return;
    }
  }
}

/* =========================================================
   SCANNERS
   ========================================================= */

static Token scan_identifier(Lexer *l, int start_col) {
  while (isalnum((unsigned char)peek(l)) || peek(l) == '_') advance(l);
  int len = (int)(l->current - l->start);
  for (int i = 0; keywords[i].text; i++) {
    if ((int)strlen(keywords[i].text) == len && strncmp(l->start, keywords[i].text, len) == 0) {
      return make_token(l, keywords[i].type, start_col);
    }
  }
  return make_token(l, TOKEN_IDENTIFIER, start_col);
}

static Token scan_number(Lexer *l, int start_col) {
  // Hex: 0x...
  if (*l->start == '0' && (peek(l) == 'x' || peek(l) == 'X')) {
    advance(l);
    while (isxdigit((unsigned char)peek(l)) || peek(l) == '_') advance(l);
    return make_token(l, TOKEN_VARDATA, start_col);
  }
  // Binary: 0b...
  if (*l->start == '0' && (peek(l) == 'b' || peek(l) == 'B')) {
    advance(l);
    while (peek(l) == '0' || peek(l) == '1' || peek(l) == '_') advance(l);
    return make_token(l, TOKEN_VARDATA, start_col);
  }
  // Octal: 0o...
  if (*l->start == '0' && (peek(l) == 'o' || peek(l) == 'O')) {
    advance(l);
    while ((peek(l) >= '0' && peek(l) <= '7') || peek(l) == '_') advance(l);
    return make_token(l, TOKEN_VARDATA, start_col);
  }
  // Decimal (with optional underscores)
  while (isdigit((unsigned char)peek(l)) || peek(l) == '_') advance(l);
  if (peek(l) == '.' && isdigit((unsigned char)peek_next(l))) {
    advance(l);
    while (isdigit((unsigned char)peek(l)) || peek(l) == '_') advance(l);
  }
  // Scientific notation
  if (peek(l) == 'e' || peek(l) == 'E') {
    advance(l);
    if (peek(l) == '+' || peek(l) == '-') advance(l);
    while (isdigit((unsigned char)peek(l))) advance(l);
  }
  return make_token(l, TOKEN_NUMBER, start_col);
}

static Token scan_char(Lexer *l, int start_col) {
  while (!at_end(l) && peek(l) != '\'') {
    if (peek(l) == '\\') advance(l);
    advance(l);
  }
  if (at_end(l)) return error_token(l, "Unterminated char.");
  advance(l);
  return make_token(l, TOKEN_CHAR, start_col);
}

static Token scan_string_all(Lexer *l, char quote, int is_raw, int is_interp, int start_col) {
  int triple = 0;
  if (quote == '"' && peek(l) == '"' && peek_next(l) == '"') {
    triple = 1; advance(l); advance(l); advance(l);
  }
  while (!at_end(l)) {
    if (triple) {
      if (peek(l) == '"' && peek_next(l) == '"' && l->current[2] == '"') {
        advance(l); advance(l); advance(l); goto ok;
      }
    } else if (peek(l) == quote) { advance(l); goto ok; }
    if (peek(l) == '\\' && !is_raw) { advance(l); if (!at_end(l)) advance(l); }
    else { if (peek(l) == '\n') { l->line++; l->column = 1; } advance(l); }
  }
  return error_token(l, "Unterminated string.");
ok:
  return make_token(l, is_interp ? TOKEN_STRING_INTERP : TOKEN_VARDATA, start_col);
}

static Token symbol(Lexer *l, char c, int start_col) {
  switch (c) {
  case '{': return make_token(l, TOKEN_LBRACE, start_col);
  case '}': return make_token(l, TOKEN_RBRACE, start_col);
  case '[': return make_token(l, TOKEN_LBRACKET, start_col);
  case ']': return make_token(l, TOKEN_RBRACKET, start_col);
  case '(': return make_token(l, TOKEN_LPAREN, start_col);
  case ')': return make_token(l, TOKEN_RPAREN, start_col);
  case ',': return make_token(l, TOKEN_COMMA, start_col);
  case ';': return make_token(l, TOKEN_SEMICOLON, start_col);
  case '#': return make_token(l, TOKEN_HASH, start_col);
  case '~':
    if (match(l, '>')) return make_token(l, TOKEN_TILDE_ARROW, start_col);
    return make_token(l, TOKEN_TILDE, start_col);
  case '^': return make_token(l, TOKEN_CARET, start_col);
  case '%':
    if (match(l, '=')) return make_token(l, TOKEN_PERCENT_EQUAL, start_col);
    return make_token(l, TOKEN_PERCENT, start_col);
  case ':':
    if (match(l, ':')) return make_token(l, TOKEN_COLON_COLON, start_col);
    return make_token(l, TOKEN_COLON, start_col);
  case '-':
    if (peek(l) == '>' && peek_next(l) == '*') { advance(l); advance(l); return make_token(l, TOKEN_ARROW_STAR, start_col); }
    if (match(l, '>')) return make_token(l, TOKEN_THIN_ARROW, start_col);
    if (match(l, '=')) return make_token(l, TOKEN_MINUS_EQUAL, start_col);
    return make_token(l, TOKEN_MINUS, start_col);
  case '+':
    if (match(l, '=')) return make_token(l, TOKEN_PLUS_EQUAL, start_col);
    return make_token(l, TOKEN_PLUS, start_col);
  case '*':
    if (match(l, '=')) return make_token(l, TOKEN_STAR_EQUAL, start_col);
    return make_token(l, TOKEN_STAR, start_col);
  case '/':
    if (match(l, '=')) return make_token(l, TOKEN_SLASH_EQUAL, start_col);
    return make_token(l, TOKEN_SLASH, start_col);
  case '=':
    if (match(l, '>')) return make_token(l, TOKEN_ARROW, start_col);
    return make_token(l, match(l, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL, start_col);
  case '!':
    return make_token(l, match(l, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG, start_col);
  case '?':
    if (match(l, '?')) return make_token(l, TOKEN_NULL_COALESCE, start_col);
    if (match(l, '.')) return make_token(l, TOKEN_SAFE_NAV, start_col);
    return make_token(l, TOKEN_QUESTION, start_col);
  case '<':
    if (match(l, '-')) return make_token(l, TOKEN_LEFT_ARROW, start_col);
    if (match(l, '<')) return make_token(l, TOKEN_LEFT_SHIFT, start_col);
    return make_token(l, match(l, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS, start_col);
  case '>':
    if (match(l, '>')) return make_token(l, TOKEN_RIGHT_SHIFT, start_col);
    return make_token(l, match(l, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER, start_col);
  case '&':
    if (match(l, '&')) return make_token(l, TOKEN_AND_AND, start_col);
    if (peek(l) == '"') { int col = l->column; advance(l); return scan_string_all(l, '"', 0, 1, col); }
    return make_token(l, TOKEN_AMP, start_col);
  case '|':
    if (match(l, '|')) return make_token(l, TOKEN_OR_OR, start_col);
    return make_token(l, TOKEN_PIPE, start_col);
  case '@': return make_token(l, TOKEN_AT, start_col);
  case '.':
    if (peek(l) == '.') {
      advance(l);
      if (peek(l) == '.') { advance(l); return make_token(l, TOKEN_DOT_DOT_DOT, start_col); }
      return make_token(l, TOKEN_DOT_DOT, start_col);
    }
    return make_token(l, TOKEN_DOT, start_col);
  default: {
    static char buf[32]; snprintf(buf, sizeof(buf), "Unexpected char: '%c'", c);
    return error_token(l, buf);
  }
  }
}

Token next_token(Lexer *l) {
  skip_trivia(l);
  l->start = l->current;
  int start_col = l->column;
  if (at_end(l)) return (Token){TOKEN_EOF, l->current, 0, l->line, l->column};
  char c = advance(l);
  if (c == '\'') return scan_char(l, start_col);
  if (c == '`') return scan_string_all(l, '`', 0, 0, start_col);
  if (c == 'r' && peek(l) == '"') { advance(l); return scan_string_all(l, '"', 1, 0, start_col); }
  if (c == '"') return scan_string_all(l, '"', 0, 0, start_col);
  if (isalpha((unsigned char)c) || c == '_') return scan_identifier(l, start_col);
  if (isdigit((unsigned char)c)) return scan_number(l, start_col);
  return symbol(l, c, start_col);
}

void lexer_init(Lexer *l, const char *src) {
  l->start = src; l->current = src; l->line = 1; l->column = 1;
}

const char *token_type_to_string(TokenType t) {
  switch (t) {
  case TOKEN_IDENTIFIER: return "IDENTIFIER";
  case TOKEN_VARDATA: return "VARDATA";
  case TOKEN_CHAR: return "CHAR";
  case TOKEN_STRING_INTERP: return "STRING_INTERP";
  case TOKEN_TRUE: return "TRUE";
  case TOKEN_FALSE: return "FALSE";
  case TOKEN_NEN: return "NEN";

  case TOKEN_LBRACKET: return "LBRACKET";
  case TOKEN_RBRACKET: return "RBRACKET";
  case TOKEN_LPAREN: return "LPAREN";
  case TOKEN_RPAREN: return "RPAREN";
  case TOKEN_LBRACE: return "LBRACE";
  case TOKEN_RBRACE: return "RBRACE";

  case TOKEN_COMMA: return "COMMA";
  case TOKEN_DOT: return "DOT";
  case TOKEN_DOT_DOT: return "DOT_DOT";
  case TOKEN_DOT_DOT_DOT: return "DOT_DOT_DOT";
  case TOKEN_COLON: return "COLON";
  case TOKEN_COLON_COLON: return "COLON_COLON";
  case TOKEN_SEMICOLON: return "SEMICOLON";
  case TOKEN_ARROW: return "ARROW";
  case TOKEN_THIN_ARROW: return "THIN_ARROW";
  case TOKEN_TILDE_ARROW: return "TILDE_ARROW";
  case TOKEN_ARROW_STAR: return "ARROW_STAR";
  case TOKEN_LEFT_ARROW: return "LEFT_ARROW";
  case TOKEN_QUESTION: return "QUESTION";
  case TOKEN_AT: return "AT";
  case TOKEN_BANG: return "BANG";
  case TOKEN_HASH: return "HASH";
  case TOKEN_NULL_COALESCE: return "NULL_COALESCE";
  case TOKEN_SAFE_NAV: return "SAFE_NAV";

  case TOKEN_EQUAL: return "EQUAL";
  case TOKEN_EQUAL_EQUAL: return "EQUAL_EQUAL";
  case TOKEN_PLUS_EQUAL: return "PLUS_EQUAL";
  case TOKEN_MINUS_EQUAL: return "MINUS_EQUAL";
  case TOKEN_STAR_EQUAL: return "STAR_EQUAL";
  case TOKEN_SLASH_EQUAL: return "SLASH_EQUAL";
  case TOKEN_PERCENT_EQUAL: return "PERCENT_EQUAL";
  case TOKEN_BANG_EQUAL: return "BANG_EQUAL";
  case TOKEN_LESS: return "LESS";
  case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
  case TOKEN_GREATER: return "GREATER";
  case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";

  case TOKEN_PLUS: return "PLUS";
  case TOKEN_MINUS: return "MINUS";
  case TOKEN_STAR: return "STAR";
  case TOKEN_SLASH: return "SLASH";
  case TOKEN_PERCENT: return "PERCENT";

  case TOKEN_AMP: return "AMP";
  case TOKEN_PIPE: return "PIPE";
  case TOKEN_CARET: return "CARET";
  case TOKEN_TILDE: return "TILDE";
  case TOKEN_LEFT_SHIFT: return "LEFT_SHIFT";
  case TOKEN_RIGHT_SHIFT: return "RIGHT_SHIFT";

  case TOKEN_AND_AND: return "AND_AND";
  case TOKEN_OR_OR: return "OR_OR";

  case TOKEN_IF: return "IF";
  case TOKEN_EF: return "EF";
  case TOKEN_ELSE: return "ELSE";
  case TOKEN_WHILE: return "WHILE";
  case TOKEN_FOR: return "FOR";
  case TOKEN_IN: return "IN";
  case TOKEN_MATCH: return "MATCH";
  case TOKEN_SWITCH: return "SWITCH";
  case TOKEN_BREAK: return "BREAK";
  case TOKEN_CONTINUE: return "CONTINUE";
  case TOKEN_LOOP: return "LOOP";

  case TOKEN_FN: return "FN";
  case TOKEN_RETURN: return "RETURN";
  case TOKEN_MUT: return "MUT";
  case TOKEN_CONST: return "CONST";
  case TOKEN_STATIC: return "STATIC";
  case TOKEN_COMPTIME: return "COMPTIME";

  case TOKEN_USE: return "USE";
  case TOKEN_FROM: return "FROM";
  case TOKEN_MODULE: return "MODULE";
  case TOKEN_EXTERN: return "EXTERN";
  case TOKEN_EXPORT: return "EXPORT";

  case TOKEN_CLASS: return "CLASS";
  case TOKEN_EXTENDS: return "EXTENDS";
  case TOKEN_IMPL: return "IMPL";
  case TOKEN_TRAIT: return "TRAIT";
  case TOKEN_VIRTUAL: return "VIRTUAL";
  case TOKEN_OVERRIDE: return "OVERRIDE";
  case TOKEN_SUPER: return "SUPER";
  case TOKEN_SELF: return "SELF";
  case TOKEN_DYN: return "DYN";
  case TOKEN_PUB: return "PUB";
  case TOKEN_PRI: return "PRI";
  case TOKEN_INIT: return "INIT";
  case TOKEN_DEINIT: return "DEINIT";
  case TOKEN_ABSTRACT: return "ABSTRACT";
  case TOKEN_SEALED: return "SEALED";
  case TOKEN_INTERFACE: return "INTERFACE";

  case TOKEN_SPARK: return "SPARK";
  case TOKEN_ASYNC: return "ASYNC";
  case TOKEN_AWAIT: return "AWAIT";
  case TOKEN_YIELD: return "YIELD";
  case TOKEN_CHAN: return "CHAN";
  case TOKEN_SELECT: return "SELECT";

  case TOKEN_PTR: return "PTR";
  case TOKEN_OWN: return "OWN";
  case TOKEN_REF: return "REF";
  case TOKEN_WEAK: return "WEAK";
  case TOKEN_PIN: return "PIN";
  case TOKEN_VOLATILE: return "VOLATILE";
  case TOKEN_RESTRICT: return "RESTRICT";
  case TOKEN_MOVE: return "MOVE";
  case TOKEN_BORROW: return "BORROW";
  case TOKEN_UNSAFE: return "UNSAFE";

  case TOKEN_STRUCT: return "STRUCT";
  case TOKEN_AS: return "AS";
  case TOKEN_UNION: return "UNION";
  case TOKEN_ENUM: return "ENUM";
  case TOKEN_TYPE: return "TYPE";
  case TOKEN_WHERE: return "WHERE";
  case TOKEN_IS: return "IS";
  case TOKEN_NOT: return "NOT";
  case TOKEN_AND: return "AND";
  case TOKEN_OR: return "OR";
  case TOKEN_SIZEOF: return "SIZEOF";
  case TOKEN_TYPEOF: return "TYPEOF";
  case TOKEN_CAST: return "CAST";

  case TOKEN_TRY: return "TRY";
  case TOKEN_CATCH: return "CATCH";
  case TOKEN_THROW: return "THROW";
  case TOKEN_DEFER: return "DEFER";

  case TOKEN_ASM: return "ASM";
  case TOKEN_PARFOR: return "PARFOR";
  case TOKEN_REDUCE: return "REDUCE";
  case TOKEN_ATOMIZER: return "ATOMIZER";
  case TOKEN_MUTEX: return "MUTEX";
  case TOKEN_PAR: return "PAR";
  case TOKEN_INLINE: return "INLINE";
  case TOKEN_EMBED: return "EMBED";
  case TOKEN_MACRO: return "MACRO";
  case TOKEN_PURE: return "PURE";
  case TOKEN_LAZY: return "LAZY";
  case TOKEN_FROZEN: return "FROZEN";
  case TOKEN_GUARD: return "GUARD";
  case TOKEN_WITH: return "WITH";

  case TOKEN_TNT: return "TNT";
  case TOKEN_NUMBER: return "NUMBER";

  case TOKEN_I8: return "I8";
  case TOKEN_I16: return "I16";
  case TOKEN_I32: return "I32";
  case TOKEN_I64: return "I64";
  case TOKEN_I128: return "I128";
  case TOKEN_I256: return "I256";
  case TOKEN_U8: return "U8";
  case TOKEN_U16: return "U16";
  case TOKEN_U32: return "U32";
  case TOKEN_U64: return "U64";
  case TOKEN_U128: return "U128";
  case TOKEN_U256: return "U256";
  case TOKEN_USIZE: return "USIZE";
  case TOKEN_ISIZE: return "ISIZE";
  case TOKEN_F16: return "F16";
  case TOKEN_F32: return "F32";
  case TOKEN_F64: return "F64";
  case TOKEN_F128: return "F128";
  case TOKEN_F256: return "F256";
  case TOKEN_BOOL: return "BOOL";
  case TOKEN_BYTE: return "BYTE";
  case TOKEN_BITS: return "BITS";
  case TOKEN_STRING_TYPE: return "STRING_TYPE";
  case TOKEN_CHAR_TYPE: return "CHAR_TYPE";
  case TOKEN_VOID: return "VOID";
  case TOKEN_ANY: return "ANY";
  case TOKEN_NEVER: return "NEVER";

  case TOKEN_EOF: return "EOF";
  case TOKEN_ERROR: return "ERROR";
  case TOKEN_COUNT: return "COUNT";
  default: return "UNKNOWN";
  }
}

void print_token(Token t) {
  printf("%-15s | '%.*s' | (%d:%d)\n", token_type_to_string(t.type), (int)t.length, t.start, t.line, t.column);
}
