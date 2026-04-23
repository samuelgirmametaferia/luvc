LUV LANGUAGE SEMANTICS — authoritative reference (work-in-progress)

This document is the authoritative per-keyword specification for the language. Each entry contains:
- Short description
- Allowed usage sites
- Collision rules with other modifiers/keywords
- Enforcement strategy (compile-time / lowering / runtime)
- Diagnostics (errors, warnings, suggested fix-its)
- Edge cases and resolution rules

Use the checklist at the end to track implementation & tests. Leave checkboxes unchecked until tests + sema + parser + runtime are updated.

GLOBAL PRINCIPLES

- Resolution targets: Every language interaction must resolve to exactly one of: compile-time error, deterministic lowering (compiler rewrite), or runtime dispatch. No undefined behavior except under explicit 'unsafe'.
- Priority order: Safety > determinism > performance > flexibility. When conflicting policies arise, prefer safety.

GENERAL DIAGNOSTICS GUIDELINES

- Errors must be diagnostic-friendly: message, location, one-line explanation, and at least one "fix-it" suggestion where possible.
- Warnings must be actionable with suggested changes.
- When infeasible to decide statically, prefer deterministic lowering (with explicit runtime checks) rather than silent nondeterminism.

FUNCTION MODIFIERS

static
- Description: function bound to type/module; no instance/'self' context.
- Allowed: top-level, class-level with explicit 'static' declaration. Member lookup: Type::func.
- Collisions: static + virtual => ERROR. static + override => ERROR. static with 'self' parameter => ERROR.
- Enforcement: sema emits compile-time error.
- Diagnostic: "Function cannot be both 'static' and 'virtual'. Remove one or move to instance method." Fix-it: remove 'static' or 'virtual' token.
- Edge cases: static methods may still be marked inline/pure; inlining applies normally.

virtual
- Description: dynamic dispatch through vtable unless devirtualized.
- Allowed: class/trait instance methods only (not free functions).
- Collisions: static + virtual => ERROR.
- Enforcement: sema marks method as virtual and requires vtable emission at codegen unless devirtualized.
- Devirtualization: allowed when either (a) the receiver type at call-site is statically known concrete/final/sealed; or (b) whole-program/class-hierarchy analysis finds no overrides. Compiler should perform per-call-site devirtualization when receiver type is known.
- Diagnostic suggestions: "Consider marking class as 'sealed' or method as 'final' to enable devirtualization." Fix-it: insert 'sealed' at class or 'final' at method.
- Edge cases: inline+virtual triggers call-site analysis heuristic; when heuristics are inconclusive, leave virtual and warn if inline was requested.

override
- Description: declares that this method overrides a base class virtual method.
- Allowed: instance methods in class context only.
- Collisions: override without existing base virtual => ERROR. mismatch in signature => ERROR with suggestion to adjust signature or remove override.
- Enforcement: sema resolves base class symbol, validates existence and signature compatibility.
- Diagnostic: "'override' used but no matching virtual member found in base class. Did you mean 'override' or define base method?" Fix-it: remove 'override' or add base method stub.
- Edge case: accidental override due to name collision with overloaded signatures — compare param count/contravariance rules and offer candidate list.

abstract
- Description: declaration-only method (no body).
- Allowed: inside abstract classes/traits/interfaces.
- Collisions: abstract + body => ERROR. abstract on non-abstract class without 'abstract' marker => class flagged or error.
- Enforcement: sema ensures no body present and that declaring class is abstract.
- Diagnostic: "Abstract methods must not have a body." Fix-it: remove body or remove 'abstract'.
- Edge cases: abstract static methods are allowed as declarations if the language permits; semantics must be explicit.

inline
- Description: request for inlining; also triggers devirtualization attempts for virtual functions.
- Allowed: any function definition.
- Collisions: inline + virtual -> attempt devirtualization. If devirtualization fails, inline is ignored; emit suggestion to make method final/sealed.
- Enforcement: sema emits hint and attempt to mark call-sites for inliner; actual inlining is performed at codegen.
- Diagnostic: on failure: "Inline requested but virtual overrides exist; mark method 'final' or class 'sealed' to enable." Fix-it: add 'final' token.
- Devirtualization rules: do per-call-site resolution. Conditions that allow devirtualization:
  * receiver type is concrete and final/sealed OR known exact type by local inference
  * whole-program analysis finds no overrides for method
  * call-site uses 'super' or direct Type::method access

pure
- Description: function guarantees no observable side-effects.
- Allowed: functions that do not: mutate global state, perform I/O, allocate (unless SSO/alloc-free), await, or deref mutable global/stateful pointers; they may call other pure functions.
- Collisions: pure + unsafe => ERROR unless explicitly expressed as 'unsafe pure' (opt-out) — this is permitted but treated as: purity claim suppressed and function treated as unsafe.
- Enforcement: sema performs purity analysis using the intrinsics registry and call-graph analysis. Calls to unknown externals mark function impure unless annotated pure.
- Diagnostics: "Function marked 'pure' but calls I/O intrinsic 'print'". Fix-it: remove 'pure' or make callee pure.
- Edge cases: hidden I/O through FFI; require explicit annotation in extern decls.

unsafe
- Description: allows unsafe operations inside the block/function: pointer arithmetic, unchecked casts, raw mem access.
- Allowed: unsafe blocks and unsafe functions.
- Collisions: unsafe does not negate purity errors unless 'unsafe pure' is explicitly used (and documented). unsafe does not permit unchecked lifetime violations — sema still warns unless lifetime proof exists.
- Enforcement: sema tracks nesting level and suppresses certain safe-check errors while still enforcing obvious invariants.
- Diagnostic: warn for suspicious usage even in unsafe context: "Unsafe block contains deref of potentially invalid pointer." Fix-it: add comments or assertions.

comptime
- Description: code executed at compile time; must only depend on compile-time-known values.
- Allowed: comptime blocks/functions, used for metaprogramming and constant folding.
- Collisions: comptime + async => ERROR. Using runtime-only APIs inside comptime => ERROR.
- Enforcement: compiler attempts evaluation; any dependency on runtime symbol -> compile-time error.
- Diagnostic: "Comptime evaluation failed: used runtime value 'x'". Fix-it: remove runtime dependency or move to runtime code.

async
- Description: marks function as asynchronous; 'await' allowed inside.
- Allowed: functions, lambdas. Await only valid inside async contexts.
- Collisions: async + comptime => ERROR. Lock held across await => warning/error depending on lock type.
- Enforcement: sema verifies await only within async and checks lock/await patterns.
- Diagnostic: "Cannot await outside async function." Fix-it: add 'async' to fn or remove 'await'.

ef
- Description: shorthand token for 'else if'.
- Allowed: parser-level alias replacing 'else if'.
- Enforcement: parser accepts 'ef' as equivalent to 'else if'.
- Edge cases: ensure lexer yields single TOKEN_EF so other tokens such as identifier 'ef' in local scope are handled consistently.

VARIABLE / STORAGE MODIFIERS

const
- Description: variable is immutable.
- Allowed: top-level and local variables.
- Collisions: const + mut => ERROR. const + volatile => volatile dominates -> compiler treats it as non-const (warning). const in comptime contexts must be computable.
- Enforcement: sema enforces immutability and errors on writes.
- Diagnostic: "Cannot assign to 'const' variable 'x'." Fix-it: remove 'const' or declare a new mutable variable.
- Edge cases: const references to heap-owned types—internal mutability patterns require explicit markers (e.g. Cell/TInteriorMut).

mut (variable default mutability policy)
- Description: marks parameter as pass-by-reference mut so callee mutates caller's binding. Note: variables are mutable by default, but 'mut' in parameter position denotes pass-by-ref semantics.
- Allowed: parameter declarations that explicitly want caller binding mutated.
- Collisions: Passing expression (non-identifier, e.g., a+b) to a mut parameter => ERROR. Passing const variable to mut parameter => ERROR.
- Enforcement: sema enforces call-site checks; parameters flagged mut must correspond to lvalue identifier.
- Diagnostic: "Argument must be an identifier to bind to 'mut' parameter." Fix-it: store expr into local var and pass the local.
- Edge cases: mut with temporary borrows—disallow unless the language provides lvalue-to-temp promotion with lifetime extension.

volatile
- Description: memory may change externally; optimization restrictions apply.
- Allowed: hardware registers, memory-mapped IO.
- Collisions: volatile + const => volatile dominates; compiler should generate minimal loads/stores.
- Enforcement: codegen and optimizer honor volatile semantics.
- Diagnostic: "Variable declared 'volatile' — optimizer will not reorder accesses." (informational)

lazy
- Description: initialize on first access; combined with const -> evaluate once then freeze.
- Allowed: global/static expensive init.
- Collisions: concurrency hazards: must define thread-safety model (e.g., double-checked locking or synchronization).
- Enforcement: lower to guard + init-once helper with proper atomic or mutex semantics.
- Diagnostic: warn if lazy init calls non-threadsafe code without guard.

frozen
- Description: variable becomes immutable after initialization.
- Allowed: local or global variables.
- Collisions: frozen + mut => ERROR.
- Enforcement: sema tracks the freeze point; writes after freeze => error.

static (var-level)
- Description: module/class-level storage duration.
- Allowed: top-level or class members.
- Collisions: static + comptime => ERROR (ambiguous semantics); static + local -> allowed if intended as file-scope.
- Enforcement: parser + sema restrict combinations.

extern / export
- Description: external linkage and export semantics for FFI or ABI boundary.
- Allowed: declarations for linking or ABI.
- Collisions: extern pure — callee purity must be explicitly annotated on extern decl.
- Enforcement: sema records ABI expectations and emits warnings for mismatched calling convention.

own
- Description: owning pointer/unique ownership. Move semantics apply.
- Allowed: heap-managed unique ownership, e.g. own T.
- Collisions: moving own invalidates source; borrow rules apply.
- Enforcement: sema marks moved values and errors on use-after-move.
- Diagnostic: "Use-after-move of 'x'. Consider cloning or borrowing instead." Fix-it: use 'let tmp = x.clone();' or change signature to accept ref.
- Edge cases: implicit copy for small trivially-copyable types — specify copy/clone trait.

ptr
- Description: raw pointer type. Pointer arithmetic & unsafe ops require 'unsafe'.
- Allowed: pointer types and low-level operations in unsafe blocks.
- Enforcement: sema enforces use of 'unsafe' for pointer arithmetic/deref when beyond safe abstractions.

ref
- Description: borrow/reference type. Temporaries have limited lifetimes.
- Allowed: parameter borrows, local borrows with checked lifetimes.
- Collisions: borrow-return (returning a ref to a local) => compile-time ERROR unless proven safe by lifetime analysis.
- Enforcement: sema conservative checks and later full lifetime analyzer will prove more cases.
- Diagnostic: "Returning reference to local 'x' is invalid — it will outlive the referent." Fix-it: return owned value or extend lifetime via move.

weak
- Description: non-owning reference; requires GC or implicit reference counting.
- Allowed: weak references to own/rc-managed types.
- Collisions: using weak on types with no RC/GC -> sema enables RC mode and marks types as reference-counted; warn user about potential costs.
- Enforcement: sema toggles reference-counted support when weak usage is detected and emits informational message.
- Edge cases: weak to stack-allocated objects disallowed.

pin
- Description: pin value in memory to prevent moves (for FFI/stable address).
- Allowed: FFI or places that require stable address.
- Collisions: pinned types cannot be moved; moving pinned value => ERROR.
- Enforcement: sema tracks pin marker and disallows moves/transfer.

restrict
- Description: aliasing guarantee for pointers; enables optimizer assumptions.
- Allowed: pointer declarations to help optimizer.
- Collisions: misuse may break program correctness; used only when programmer guarantees non-aliasing.
- Enforcement: optimizer-level assumption; sema emits a strong warning if restrictions appear unsound.

TYPES & KEYWORDS

dyn
- Runtime model: dyn values are boxed (small-value optimization allowed), carry a runtime type tag, and have a vtable for method/operator dispatch.
- Allowed: any type position (var, param, return). Semantics: permissive — most operations permitted but checked at runtime.
- Operations semantics:
  * Arithmetic dyn + dyn -> runtime operator lookup (vtable), runtime type coercion if possible, else runtime error.
  * Comparison dyn == dyn -> if same runtime type, compare; else coerce if possible; else false.
  * Method call -> runtime lookup: direct method on boxed type -> trait impl -> error.
  * Assignment -> dyn variable stores boxed value (reboxing replaces box).
  * Nested dyn flattening: dyn x = dyn y -> x receives underlying boxed value.
- Performance fixes: SVO (embed small values in box), inline cache per-call-site for repeated lookups.
- Enforcement: sema treats dyn as permissive; detailed runtime support required in stdlib/rt.
- Diagnostic: "Operation on 'dyn' incurs runtime dispatch. Consider concrete type for performance." Fix-it suggestion: add explicit type or cast.

any
- Description: erased type for raw storage; no methods or dispatch.
- Allowed: storage boxes for heterogeneous data; requires cast to concrete type to use methods.

never
- Description: indicates function does not return. Compiler enforces unreachable end.
- Enforcement: sema checks that all paths diverge.

void
- Description: no value; expressions of void are discarded.
- Enforcement: sema allows void in expression context with optional warnings.

union
- Description: raw memory union. Either track active field (tagged) or require unsafe for field reads.
- Enforcement: sema requires explicit tag checks or unsafe for raw access.
- Diagnostic: "Accessing union field without active tag — mark unsafe or track active variant." Fix-it: wrap with 'unsafe'.

enum
- Description: ephemeral enumerations; support integer, string or tagged variants.
- Enforcement: sema checks discriminant types and variant constructors.

CLASS / TRAIT / INTERFACE

sealed
- Description: class cannot be extended outside its module.
- Enforcement: sema records module of declaration and rejects extends in other modules.
- Diagnostic: "Cannot extend sealed class 'X' outside module 'm'." Fix-it: remove 'sealed' from base or move subclass.

override checks
- Semantics: when 'override' used, sema resolves base member and validates signature. If mismatch, present candidates and suggest 'overload' vs 'override'.

abstract class checks
- Semantics: abstract methods must not have bodies; classes with abstract members implicitly are abstract.
- Enforcement: sema enforces and suggests marking class abstract.

OOP edge cases
- static methods cannot be virtual; method-owner mismatches produce diagnostics stating expected owner types.

MEMORY MODEL & LIFETIMES

Move
- After move, source becomes poisoned (invalid). Sema prevents use-after-move.
- Edge cases: copyable primitives behave as copy; user-defined copy trait allowed.

Borrow & borrow-return
- Conservative rule: returning a borrow to a local or ephemeral is forbidden.
- Proven-safe rule: lifetime analyser may later prove some borrows safe to return (e.g., returning borrow tied to argument outlives callee).
- Diagnostics: "Returning borrow of local 'x' would outlive 'x'." Fix-it: return owned value or change parameter/return types.

Weak + RC
- If weak is used and language has no GC, sema enables RC mode and marks types as reference-counted; warn user about potential costs.

Pointer arithmetic
- Only allowed in 'unsafe'. Sema errors otherwise.

Control-flow features

defer
- Always runs on return/throw/panic in LIFO order. Sema ensures proper lowering and lifetime ordering.

await
- Only in async; sema warns when a lock might be held across await and suggests moving lock scope.

ef
- Same semantics as 'else if'; parser accepts TOKEN_EF.

OPERATORS & PRECEDENCE

- Precedence table (high->low): ., ?., [], function-call -> unary (* ! -) -> *, /, % -> +, - -> comparisons -> logical -> ?? -> arrows and assignment.
- Null coalescing (??) triggers only for null, not false/0.
- Safe nav (?.) short-circuits entire chain when a null encountered.

DIAGNOSTICS, FIX-ITS & SUGGESTIONS

Guidelines for messages:
- Primary: concise error message
- Secondary: one-line explanation
- Suggestion: 1-2 fix-it suggestions where applicable
Examples:
- "Function cannot be both 'static' and 'virtual'. Remove 'static' or 'virtual'."
- "Possible devirtualization: add 'sealed' to class 'X' to enable inlining." Fix-it: insert token 'sealed' before class.

IMPLEMENTATION CHECKLIST (per-keyword)

(Use these checkboxes to mark when parser+AST+sema+tests+runtime are implemented)

Function modifiers:
- [ ] static
- [ ] virtual
- [ ] override
- [ ] abstract
- [ ] inline (with devirtualization heuristics)
- [ ] pure (intrinsics + call-graph analysis)
- [ ] unsafe
- [ ] unsafe pure
- [ ] comptime
- [ ] async
- [ ] ef

Variable/storage qualifiers:
- [ ] const
- [ ] mut (param lvalue semantics)
- [ ] volatile
- [ ] lazy
- [ ] frozen
- [ ] static (var-level)
- [ ] extern
- [ ] export
- [ ] own
- [ ] ptr
- [ ] ref
- [ ] weak (RC enablement)
- [ ] pin
- [ ] restrict

Types & special keywords:
- [ ] dyn (runtime boxing + vtable + SVO + inline-caches)
- [ ] any
- [ ] never
- [ ] void
- [ ] list/array/struct/union/enum/class/interface/trait/impl

Memory & ops:
- [ ] move (poison state + copy/clone rules)
- [ ] borrow
- [ ] alloc/dealloc (mm primitive)
- [ ] addr-of / deref
- [ ] pointer arithmetic (unsafe-only)

Control flow:
- [ ] if / ef / else
- [ ] match / switch
- [ ] for / while / loop / break / continue
- [ ] return
- [ ] defer
- [ ] yield / generator auto-upgrade
- [ ] await
- [ ] guard
- [ ] with

Concurrency:
- [ ] async/await
- [ ] spark
- [ ] parfor (auto-detect reductions)
- [ ] chan<T>
- [ ] select

Error handling:
- [ ] try/catch/throw

Comptime/macros/attributes:
- [ ] comptime evaluation
- [ ] macro hygiene
- [ ] unsafe macro
- [ ] embed
- [ ] @ attributes (e.g., @luv mm on)

Operators & syntax:
- [ ] ?.
- [ ] ??
- [ ] =>, ->, ~>, ->*, <-
- [ ] typeof
- [ ] sizeof

OOP & safety:
- [ ] sealed
- [ ] super
- [ ] init / deinit

NUMERICS & COERCIONS

- [ ] implicit coercions (int -> float -> TNT) with disambiguation diagnostics
- [ ] overflow behavior (wrap by default; optional checked mode)

EXAMPLES (small snippets)

- static/virtual conflict:
  static virtual fn foo() { }
  => sema error: remove one modifier or make method instance-level

- mut param usage:
  fn inc(mut x) { x = x + 1 }
  let a = 4
  inc(a) // ERROR: must pass identifier lvalue
  let b = a
  inc(b) // OK (b is identifier)

- dyn usage:
  dyn x = 5
  x + 1 // runtime dispatch -> numeric add
  x.foo() // runtime lookup

IMPLEMENTATION ROADMAP

For each unchecked item:
1. Parser: accept token and verify grammar placement
2. AST: store modifier flags and locations
3. Sema: collisions and invariants; produce diagnostics and fix-its
4. Lowering: deterministically lower constructs when possible (e.g., defer lowering to try-finally like blocks)
5. Runtime/Codegen: add runtime support for dyn boxing/vtables, reference counting, and SVO
6. Tests: unit/regression for both positive and negative cases

PRIORITY SUGGESTIONS

1. Memory model (borrow-return, move, weak RC)
2. Dyn runtime (boxing, vtable, SVO, inline caches)
3. Full comptime validation and macro hygiene
4. Class/trait enforcement + devirtualization
5. Type-inference overhaul (constraint solver -> remove call-order dependence)

CHANGELOG & NOTES

- This file should be the single source of truth for semantics decisions. Add incremental entries with date/short summary when rules change.
- For every diagnostic added to the compiler, add a matching example in the tests/diagnostics directory and reference the exact message string (so editors/IDE can match expected fix-its).

END
