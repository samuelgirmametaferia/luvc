# luv Semantic Rules

## 1. Variable Mutability
- By default, all variables are **immutable** (`const` is the default behavior even if omitted, but `const` keyword explicitly enforces it).
- Only variables declared with `mut` can be reassigned.
- Semantic Analysis Rule: `AST_ASSIGN` or re-definition must check if the target symbol's type has `is_mut == true`.

## 2. List vs. Array Operations
- **Arrays**: Fixed-size memory blocks.
    - Methods `push`, `pop`, `append` are **prohibited**.
    - Must be indexed with `[]`.
- **Lists**: Dynamic-size heap-allocated structures.
    - Support `push(T)`, `pop() -> T`, `append([T])`.
- Implementation: Member access on `List` types checks the Intrinsic Registry for valid operations.

## 3. Ownership & Move Semantics (RSS Basis)
- Variables marked `own` follow **Unique Ownership** rules.
- When an `own` variable is assigned to another, the original variable is marked as **moved**.
- Accessing a "moved" variable results in a compile-time semantic error.
- Rule: `Symbol` struct tracks `is_moved` state during AST traversal.

## 4. Function & Type Inference
- Variable types are inferred from their initializer if the explicit type is omitted.
- Function return types can be inferred from `return` statements if all paths are consistent.
- Intrinsics (built-in methods) are automatically resolved based on the caller's type.

## 5. Control Flow Strictness
- `if` and `while` conditions **must** be of `bool` type. No implicit numeric truthiness.
- `guard` statements must diverge (return, throw, or break) in their `else` block.
- `match` statements must be exhaustive for `enum` types.

## 6. Built-in Intrinsics System
- The language provides an extensible C-based registry for system functions.
- Default intrinsics include:
    - `ToString(any) -> string`
    - `push(list<T>, T) -> void`
    - `pop(list<T>) -> T`
    - `len(iterable) -> usize`
