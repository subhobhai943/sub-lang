/* ========================================
   SUB Language - Native Compiler Driver
   Compiles SUB directly to native machine code via C backend + GCC
   File: sub_native_compiler.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <process.h>
#define popen  _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* codegen_generate_c is in codegen.c */
extern char* codegen_generate_c(ASTNode *ast, Platform platform);

/* Read file */
static char* read_file_native(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "Error: Cannot open file %s\n", filename); return NULL; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

/* Strip path + .sb/.sub extension to get basename */
static void get_basename(const char *input, char *out, size_t n) {
    const char *base = input;
    for (const char *p = input; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    strncpy(out, base, n - 1);
    out[n - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && (strcmp(dot, ".sb") == 0 || strcmp(dot, ".sub") == 0))
        *dot = '\0';
}

/* Main native compilation: SUB -> C -> gcc -> binary */
int compile_to_native(const char *input_file, const char *output_file) {
    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║  SUB Native Compiler  (machine code)     ║\n");
    printf("╚═══════════════════════════════════════════╝\n\n");
    printf("📄 Input:  %s\n", input_file);
    printf("🎯 Output: %s\n\n", output_file);

    /* --- Phase 1: Read --- */
    printf("[1/5] 📖 Reading source...\n");
    char *source = read_file_native(input_file);
    if (!source) return 1;
    printf("      ✓ %zu bytes\n", strlen(source));

    /* --- Phase 2: Lex --- */
    printf("[2/5] 🔤 Lexical analysis...\n");
    int token_count = 0;
    Token *tokens = lexer_tokenize(source, &token_count);
    if (!tokens) {
        fprintf(stderr, "      ✗ Lexer failed\n");
        free(source); return 1;
    }
    printf("      ✓ %d tokens\n", token_count);

    /* --- Phase 3: Parse --- */
    printf("[3/5] 🌳 Parsing...\n");
    ASTNode *ast = parser_parse(tokens, token_count);
    if (!ast) {
        fprintf(stderr, "      ✗ Parse failed\n");
        free(source); lexer_free_tokens(tokens, token_count); return 1;
    }
    printf("      ✓ AST built\n");

    /* --- Phase 4: Semantic --- */
    printf("[4/5] 🔍 Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "      ✗ Semantic analysis failed\n");
        free(source); lexer_free_tokens(tokens, token_count); parser_free_ast(ast);
        return 1;
    }
    printf("      ✓ OK\n");

    /* --- Phase 5: C codegen + GCC --- */
    printf("[5/5] ⚙️  Generating machine code via C backend + GCC...\n");

    char *c_code = codegen_generate_c(ast, PLATFORM_LINUX);
    if (!c_code) {
        fprintf(stderr, "      ✗ C code generation failed\n");
        free(source); lexer_free_tokens(tokens, token_count); parser_free_ast(ast);
        return 1;
    }

    /* Write C to temp file */
    char c_tmp[512];
#ifdef _WIN32
    snprintf(c_tmp, sizeof(c_tmp), "%s_build_tmp.c", output_file);
#else
    snprintf(c_tmp, sizeof(c_tmp), "/tmp/_sub_%ld_build.c", (long)getpid());
#endif

    FILE *cf = fopen(c_tmp, "w");
    if (!cf) {
        fprintf(stderr, "      ✗ Cannot write temp C file: %s\n", c_tmp);
        free(c_code); free(source); lexer_free_tokens(tokens, token_count); parser_free_ast(ast);
        return 1;
    }
    fprintf(cf, "%s", c_code);
    fclose(cf);
    free(c_code);

    /* Invoke GCC */
    char cmd[1024];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "gcc -O2 -o \"%s.exe\" \"%s\" 2>&1", output_file, c_tmp);
#else
    snprintf(cmd, sizeof(cmd), "gcc -O2 -o \"%s\" \"%s\" 2>&1", output_file, c_tmp);
#endif
    printf("      → %s\n", cmd);
    int ret = system(cmd);
    remove(c_tmp);  /* clean up temp */

    if (ret != 0) {
        fprintf(stderr, "      ✗ GCC failed (exit %d) — check C code above\n", ret);
        free(source); lexer_free_tokens(tokens, token_count); parser_free_ast(ast);
        return 1;
    }

#ifdef _WIN32
    printf("      ✓ Binary: %s.exe\n", output_file);
    printf("\n✅ Success!\n📦 Run: %s.exe\n\n", output_file);
#else
    printf("      ✓ Binary: %s\n", output_file);
    printf("\n✅ Success!\n📦 Run: ./%s\n\n", output_file);
#endif

    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    return 0;
}

/* Entry point */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("SUB Native Compiler v2.0\n");
        printf("Usage: %s <input.sb> [output]\n\n", argv[0]);
        printf("Examples:\n");
        printf("  %s program.sb           # output: ./program\n", argv[0]);
        printf("  %s program.sb myapp     # output: ./myapp\n\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    char base[256];
    get_basename(input_file, base, sizeof(base));
    const char *output_file = argc > 2 ? argv[2] : base;

    return compile_to_native(input_file, output_file);
}
