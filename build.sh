gcc -Iinclude src/main.c src/lexer.c src/parser.c src/ast.c src/resolve_primitives.c src/sema.c src/symbol.c src/type.c src/intrinsics.c src/comptime.c -o build/luv
