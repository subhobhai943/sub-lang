/* ========================================
   SUB Language - Native Compiler Driver
   Compiles SUB to native binary via C backend + gcc
   File: sub_native.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "logo.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

/* Helper: derive output name from input filename, or use user_out if provided */
static void derive_output_name(const char *input, const char *user_out,
                               char *out, size_t n) {
    if (user_out && *user_out) {
        strncpy(out, user_out, n - 1);
        out[n - 1] = '\0';
        return;
    }
    const char *base = input;
    for (const char *p = input; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    strncpy(out, base, n - 1);
    out[n - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

/* Print usage */
void print_usage_native(const char *prog_name) {
    printf(SUB_LOGO);
    printf("Usage: %s <input.sb> [options]\n\n", prog_name);
    printf("Output Options:\n");
    printf("  -o <file>          Output filename (default: derived from input)\n\n");
    printf("Optimization:\n");
    printf("  -O0                No optimization (fast compile)\n");
    printf("  -O1                Basic optimization\n");
    printf("  -O2                Standard optimization (default)\n");
    printf("  -O3                Aggressive optimization\n\n");
    printf("Debug:\n");
    printf("  -v, --verbose      Verbose output\n\n");
    printf("Examples:\n");
    printf("  %s hello.sb                  # Compile to ./hello\n", prog_name);
    printf("  %s hello.sb -O3              # Max optimization\n", prog_name);
    printf("  %s hello.sb -o myapp         # Custom output name\n\n", prog_name);
}

int compile_to_native(const char *input_file, const char *output_name,
                      bool verbose, int opt_level) {
    /* ---- Phase 1: Read source ---- */
    FILE *f = fopen(input_file, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", input_file); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *source = malloc(sz + 1);
    if (!source) { fclose(f); return 1; }
    fread(source, 1, sz, f); source[sz] = '\0'; fclose(f);

    /* ---- Phase 2: Lex ---- */
    if (verbose) printf("[1/4] Lexing...\n");
    int ntok;
    Token *tokens = lexer_tokenize(source, &ntok);

    /* ---- Phase 3: Parse ---- */
    if (verbose) printf("[2/4] Parsing...\n");
    ASTNode *ast = parser_parse(tokens, ntok);

    /* ---- Phase 4: Semantic ---- */
    if (verbose) printf("[3/4] Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "Semantic analysis failed.\n");
        parser_free_ast(ast); lexer_free_tokens(tokens, ntok); free(source);
        return 1;
    }

    /* ---- Phase 5: Generate C, write temp file ---- */
    if (verbose) printf("[4/4] Generating binary via gcc...\n");
    char *c_code = codegen_generate(ast, PLATFORM_LINUX);
    parser_free_ast(ast); lexer_free_tokens(tokens, ntok); free(source);
    if (!c_code) { fprintf(stderr, "Code generation failed.\n"); return 1; }

    char tmp_c[512];
#ifdef _WIN32
    snprintf(tmp_c, sizeof(tmp_c), "sub_tmp_%d.c", (int)GetCurrentProcessId());
#else
    snprintf(tmp_c, sizeof(tmp_c), "/tmp/sub_tmp_%d.c", (int)getpid());
#endif
    FILE *cf = fopen(tmp_c, "w");
    if (!cf) { free(c_code); fprintf(stderr, "Cannot write temp file.\n"); return 1; }
    fputs(c_code, cf); fclose(cf); free(c_code);

    /* ---- Phase 6: Compile with gcc ---- */
    const char *opt = opt_level >= 2 ? "-O2" : opt_level == 1 ? "-O1" : "-O0";
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gcc %s -o \"%s\" \"%s\"", opt, output_name, tmp_c);
    int ret = system(cmd);
    remove(tmp_c);

    if (ret != 0) {
        fprintf(stderr, "Compilation failed. Make sure gcc is installed.\n");
        return 1;
    }
    printf("\u2705 Compiled: %s\n", output_name);
    return 0;
}

/* Target type classification */
typedef enum {
    TARGET_KIND_PLATFORM,
    TARGET_KIND_LANGUAGE
} TargetKind;

typedef struct {
    const char *name;
    TargetKind kind;
    Platform platform;
    const char *extension;
    const char *run_hint;
} TargetDescriptor;

static const TargetDescriptor* lookup_target_native(const char *name) {
    static const TargetDescriptor targets[] = {
        {"interpret",  TARGET_KIND_PLATFORM, PLATFORM_LINUX,   "",       "Interpret directly"},
        {"run",        TARGET_KIND_PLATFORM, PLATFORM_LINUX,   "",       "Interpret directly"},
        {"android",    TARGET_KIND_PLATFORM, PLATFORM_ANDROID, ".java",  "javac SubProgram.java"},
        {"ios",        TARGET_KIND_PLATFORM, PLATFORM_IOS,     ".swift", "swiftc output.swift -o program && ./program"},
        {"web",        TARGET_KIND_PLATFORM, PLATFORM_WEB,     ".html",  "Open output.html in a web browser"},
        {"windows",    TARGET_KIND_PLATFORM, PLATFORM_WINDOWS, ".c",     "gcc output.c -o program && program.exe"},
        {"macos",      TARGET_KIND_PLATFORM, PLATFORM_MACOS,   ".c",     "gcc output.c -o program && ./program"},
        {"linux",      TARGET_KIND_PLATFORM, PLATFORM_LINUX,   ".c",     "gcc output.c -o program && ./program"},
        {"python",     TARGET_KIND_LANGUAGE, 0, ".py",    "python3 output.py"},
        {"py",         TARGET_KIND_LANGUAGE, 0, ".py",    "python3 output.py"},
        {"javascript", TARGET_KIND_LANGUAGE, 0, ".js",    "node output.js"},
        {"js",         TARGET_KIND_LANGUAGE, 0, ".js",    "node output.js"},
        {"typescript", TARGET_KIND_LANGUAGE, 0, ".ts",    "tsc output.ts && node output.js"},
        {"ts",         TARGET_KIND_LANGUAGE, 0, ".ts",    "tsc output.ts && node output.js"},
        {"java",       TARGET_KIND_LANGUAGE, 0, ".java",  "javac SubProgram.java && java SubProgram"},
        {"c",          TARGET_KIND_LANGUAGE, PLATFORM_LINUX, ".c", "gcc output.c -o program && ./program"},
        {"cpp",        TARGET_KIND_LANGUAGE, PLATFORM_LINUX, ".cpp", "g++ output.cpp -o program && ./program"},
        {"c++",        TARGET_KIND_LANGUAGE, PLATFORM_LINUX, ".cpp", "g++ output.cpp -o program && ./program"},
        {"rust",       TARGET_KIND_LANGUAGE, 0, ".rs",    "rustc output.rs && ./output"},
        {"rs",         TARGET_KIND_LANGUAGE, 0, ".rs",    "rustc output.rs && ./output"},
        {"go",         TARGET_KIND_LANGUAGE, 0, ".go",    "go run output.go"},
        {"golang",     TARGET_KIND_LANGUAGE, 0, ".go",    "go run output.go"},
        {"swift",      TARGET_KIND_LANGUAGE, 0, ".swift", "swiftc output.swift -o program && ./program"},
        {"kotlin",     TARGET_KIND_LANGUAGE, 0, ".kt",    "kotlinc output.kt -include-runtime -d output.jar && java -jar output.jar"},
        {"kt",         TARGET_KIND_LANGUAGE, 0, ".kt",    "kotlinc output.kt -include-runtime -d output.jar && java -jar output.jar"},
        {"ruby",       TARGET_KIND_LANGUAGE, 0, ".rb",    "ruby output.rb"},
        {"rb",         TARGET_KIND_LANGUAGE, 0, ".rb",    "ruby output.rb"},
        {"assembly",   TARGET_KIND_LANGUAGE, 0, ".asm",   "nasm -f elf64 output.asm && ld output.o -o program && ./program"},
        {"asm",        TARGET_KIND_LANGUAGE, 0, ".asm",   "nasm -f elf64 output.asm && ld output.o -o program && ./program"},
        {"css",        TARGET_KIND_LANGUAGE, 0, ".css",   "(open in browser)"},
    };
    static const int target_count = sizeof(targets) / sizeof(targets[0]);
    for (int i = 0; i < target_count; i++) {
        if (strcasecmp(name, targets[i].name) == 0) {
            return &targets[i];
        }
    }
    return NULL;
}

extern char* codegen_python(ASTNode *ast, const char *source);
extern char* codegen_javascript(ASTNode *ast, const char *source);
extern char* codegen_java(ASTNode *ast, const char *source);
extern char* codegen_swift(ASTNode *ast, const char *source);
extern char* codegen_kotlin(ASTNode *ast, const char *source);
extern char* codegen_rust(ASTNode *ast, const char *source);
extern char* codegen_go(ASTNode *ast, const char *source);
extern char* codegen_ruby(ASTNode *ast, const char *source);
extern char* codegen_assembly(ASTNode *ast, const char *source);
extern char* codegen_css(ASTNode *ast, const char *source);
char* read_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *content = malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    fclose(file);
    return content;
}

void write_file(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot write to file %s\n", filename);
        return;
    }
    fputs(content, file);
    fclose(file);
}

static char* generate_language_code_native(const char *name, ASTNode *ast, const char *source) {
    if (strcasecmp(name, "c") == 0) return codegen_generate(ast, PLATFORM_LINUX);
    if (strcasecmp(name, "cpp") == 0 || strcasecmp(name, "c++") == 0) return codegen_generate_cpp(ast, PLATFORM_LINUX);
    if (strcasecmp(name, "typescript") == 0 || strcasecmp(name, "ts") == 0) return codegen_javascript(ast, source);
    if (strcasecmp(name, "python") == 0     || strcasecmp(name, "py") == 0)     return codegen_python(ast, source);
    if (strcasecmp(name, "javascript") == 0 || strcasecmp(name, "js") == 0)     return codegen_javascript(ast, source);
    if (strcasecmp(name, "java") == 0)                                          return codegen_java(ast, source);
    if (strcasecmp(name, "swift") == 0)                                         return codegen_swift(ast, source);
    if (strcasecmp(name, "kotlin") == 0     || strcasecmp(name, "kt") == 0)     return codegen_kotlin(ast, source);
    if (strcasecmp(name, "rust") == 0       || strcasecmp(name, "rs") == 0)     return codegen_rust(ast, source);
    if (strcasecmp(name, "go") == 0         || strcasecmp(name, "golang") == 0) return codegen_go(ast, source);
    if (strcasecmp(name, "ruby") == 0       || strcasecmp(name, "rb") == 0)     return codegen_ruby(ast, source);
    if (strcasecmp(name, "assembly") == 0   || strcasecmp(name, "asm") == 0)    return codegen_assembly(ast, source);
    if (strcasecmp(name, "css") == 0)                                           return codegen_css(ast, source);
    return NULL;
}

static void get_output_basename_native(const char *input_file, char *out, size_t n) {
    const char *base = input_file;
    for (const char *p = input_file; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    strncpy(out, base, n - 1);
    out[n - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && (strcmp(dot, ".sb") == 0 || strcmp(dot, ".sub") == 0))
        *dot = '\0';
}

int main(int argc, char *argv[]) {
    printf(SUB_LOGO);

    if (argc < 2) {
        print_usage_native(argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];

    if (argc > 2 && argv[2][0] != '-') {
        // Run transpiler/interpreter driver instead
        const char *target_str = argv[2];
        
        // Direct interpreter run
        if (strcasecmp(target_str, "interpret") == 0 || strcasecmp(target_str, "run") == 0) {
            printf("Interpreting %s...\n\n", input_file);
            extern int interpret_file(const char *path);
            return interpret_file(input_file);
        }
        
        const TargetDescriptor *target = lookup_target_native(target_str);
        if (!target) {
            fprintf(stderr, "Error: Unknown target '%s'\n", target_str);
            return 1;
        }
        
        printf("Compiling %s for %s...\n\n", input_file, target_str);
        char *source = read_file(input_file);
        if (!source) return 1;
        
        int token_count;
        Token *tokens = lexer_tokenize(source, &token_count);
        ASTNode *ast = parser_parse(tokens, token_count);
        
        if (!semantic_analyze(ast)) {
            fprintf(stderr, "Semantic analysis failed\n");
            free(source);
            lexer_free_tokens(tokens, token_count);
            parser_free_ast(ast);
            return 1;
        }
        
        char *output_code = NULL;
        if (target->kind == TARGET_KIND_PLATFORM) {
            output_code = codegen_generate(ast, target->platform);
        } else {
            output_code = generate_language_code_native(target_str, ast, source);
        }
        
        if (!output_code) {
            fprintf(stderr, "Code generation failed\n");
            free(source);
            lexer_free_tokens(tokens, token_count);
            parser_free_ast(ast);
            return 1;
        }
        
        char output_file[256];
        char base_name[256];
        get_output_basename_native(input_file, base_name, sizeof(base_name));
        if (argc > 3) {
            snprintf(output_file, sizeof(output_file), "%s", argv[3]);
        } else if (target->kind == TARGET_KIND_LANGUAGE &&
            strcasecmp(target_str, "java") == 0) {
            snprintf(output_file, sizeof(output_file), "SubProgram%s", target->extension);
        } else {
            snprintf(output_file, sizeof(output_file), "%s%s", base_name, target->extension);
        }
        write_file(output_file, output_code);
        
        printf("\n\u2713 Compilation successful!\n");
        printf("\u2713 Output written to: %s\n", output_file);
        
        // If it is a platform target that compiles to C under the hood, compile to machine code directly
        if (target->kind == TARGET_KIND_PLATFORM && 
            (target->platform == PLATFORM_LINUX || target->platform == PLATFORM_WINDOWS || target->platform == PLATFORM_MACOS)) {
            char compile_cmd[1024];
            const char *bin_ext = (target->platform == PLATFORM_WINDOWS) ? ".exe" : "";
            snprintf(compile_cmd, sizeof(compile_cmd), "gcc -O2 -o %s%s %s", base_name, bin_ext, output_file);
            printf("\nCompiling intermediate C code to native machine code...\n");
            int ret = system(compile_cmd);
            if (ret == 0) {
                printf("\u2705 Machine code compiled successfully: ./%s%s\n", base_name, bin_ext);
            } else {
                fprintf(stderr, "Warning: gcc compilation failed. Make sure gcc is installed.\n");
            }
        } else {
            printf("\nNext steps:\n");
            printf("  %s\n", target->run_hint);
        }
        
        free(source);
        lexer_free_tokens(tokens, token_count);
        parser_free_ast(ast);
        free(output_code);
        return 0;
    }

    const char *user_out = NULL;
    bool verbose = false;
    int opt_level = 2;
    
    // Parse command line options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            user_out = argv[++i];
        } else if (strcmp(argv[i], "-O0") == 0) {
            opt_level = 0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            opt_level = 1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            opt_level = 2;
        } else if (strcmp(argv[i], "-O3") == 0) {
            opt_level = 3;
        }
    }

    char output_name[512];
    derive_output_name(input_file, user_out, output_name, sizeof(output_name));

    if (verbose) {
        printf("[Input]   %s\n", input_file);
        printf("[Mode]    Native via gcc (-O%d)\n", opt_level);
        printf("[Output]  %s\n\n", output_name);
    }
    
    return compile_to_native(input_file, output_name, verbose, opt_level);
}
