#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "sema.h"

/* =========================
   CLI UTILITIES
   ========================= */

#define COLOR_RESET   "\x1b[0m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_YELLOW  "\x1b[33m"

static void print_usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s <file>            Parse a file\n", prog);
    printf("  %s -e \"code\"        Parse inline code\n", prog);
}

/* =========================
   FILE LOADING
   ========================= */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("Failed to open file");
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    fread(buffer, 1, size, f);
    buffer[size] = '\0';

    fclose(f);
    return buffer;
}

/* =========================
   PARSER RUNNER
   ========================= */

static void run_parser(const char *source) {
    Arena* arena = arena_new(1024 * 1024); // 1MB arena
    Parser parser;
    parser_init(&parser, source, arena);

    ASTNode* root = parse(&parser);

    if (parser.had_error) {
        printf("%sParsing failed with errors.%s\n", COLOR_RED, COLOR_RESET);
    } else {
        printf("%sAST Structure:%s\n", COLOR_YELLOW, COLOR_RESET);
        ast_print(root, 0);

        printf("\n%sRunning Semantic Analysis...%s\n", COLOR_CYAN, COLOR_RESET);
        Sema sema;
        sema_init(&sema, arena);
        sema_analyze(&sema, root);

        if (!sema.had_error) {
            printf("%sSemantic Analysis Successful!%s\n", COLOR_GREEN, COLOR_RESET);
        } else {
            printf("%sSemantic Analysis Failed.%s\n", COLOR_RED, COLOR_RESET);
        }
    }

    arena_free(arena);
}

/* =========================
   MAIN
   ========================= */

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "-e") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Expected code after -e\n");
            return 1;
        }

        run_parser(argv[2]);
        return 0;
    }

    // assume file input
    char *source = read_file(argv[1]);
    run_parser(source);
    free(source);

    return 0;
}
