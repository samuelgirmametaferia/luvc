# luv Grammar Specification

## Declarations
- **Variable**: `(mut|const|dyn|static|comptime)? IDENTIFIER (: Type)? (= Expression)?;`
- **Function**: `(pub|pri|static|inline|pure)? fn IDENTIFIER ( "(" Params ")" ) ( "->" Type )? Block`
- **Struct**: `struct IDENTIFIER "{" ( (mut)? IDENTIFIER ":" Type (","|";")? )* "}"`
- **Union**: `union IDENTIFIER "{" ( IDENTIFIER ":" Type (","|";")? )* "}"`
- **Enum**: `enum IDENTIFIER "{" ( IDENTIFIER ( "(" Params ")" )? (","|";")? )* "}"`
- **Class**: `(abstract|sealed)? class IDENTIFIER (extends IDENTIFIER)? "{" Body "}"`
- **Trait/Interface**: `trait|interface IDENTIFIER "{" Body "}"`

## Expressions
- **Literal**: `number | string | bool | char | nen`
- **Binary**: `Expr op Expr` (Supported: `+ - * / % == != < > <= >= && || and or is as ??`)
- **Unary**: `op Expr` (Supported: `- ! not * & move borrow`)
- **Call**: `Expr "(" Args ")"`
- **Member**: `Expr . IDENTIFIER`
- **Index/Slice**: `Expr "[" Expr (: Expr (: Expr)?)? "]"`
- **Safe Nav**: `Expr ?. IDENTIFIER`
- **Cast**: `Expr as Type` or `cast<Type>(Expr)`

## Statements
- **If**: `if (Expr) Block (ef (Expr) Block)* (else Block)?`
- **While**: `while (Expr) Block`
- **For**: `for (Init; Cond; Inc) Block` or `for (IDENTIFIER (, IDENTIFIER)? in Expr) Block`
- **Match**: `match Expr "{" (Pattern "=>" (Block|Expr) (",")? )* "}"`
- **Switch**: `switch Expr "{" ( (case)? Expr ":" Statement )* (else ":" Statement)? "}"`
- **Concurrency**: `spark Expr`, `await Expr`, `yield Expr`
- **Error Handling**: `try Block catch(IDENTIFIER) Block`, `throw Expr`
- **Memory**: `alloc(Type, Count?)`, `dealloc(Expr)`, `free(Expr)`
- **Other**: `return Expr?`, `break`, `continue`, `defer Statement`, `guard Expr else Block`, `unsafe Block`

## Primitives
- **Integers**: `i8, i16, i32, i64, i128, i256`, `u8, u16, u32, u64, u128, u256`, `usize, isize`
- **Floats**: `f16, f32, f64, f128, f256`
- **Other**: `bool, byte, bits, string, char, void, any, never, tnt, number`

## Memory Qualifiers
- `ptr T`: Raw pointer (unsafe).
- `own T`: Uniquely owned resource (RSS managed).
- `ref T`: Shared reference (RSS managed).
- `weak T`: Non-owning reference.
- `pin T`: Immovable reference.
