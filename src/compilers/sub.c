/* ========================================
   SUB Language Compiler - Main Driver
   Supports BOTH platform targets AND language targets
   File: sub.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "logo.h"
#include "windows_compat.h"

#ifndef _WIN32
#include <strings.h>
#endif

/* External codegen functions (from codegen_multilang.c) */
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

/* Helper: strip directory and .sb/.sub extension from input filename */
static void get_output_basename(const char *input_file, char *out, size_t n) {
    const char *base = input_file;
    for (const char *p = input_file; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    strncpy(out, base, n - 1);
    out[n - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot && (strcmp(dot, ".sb") == 0 || strcmp(dot, ".sub") == 0))
        *dot = '\0';
}

// Utility: Read file contents
char* read_file(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Failed to seek file %s\n", filename);
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fprintf(stderr, "Error: Failed to read file size for %s\n", filename);
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Failed to rewind file %s\n", filename);
        fclose(file);
        return NULL;
    }
    
    char *content = malloc(size + 1);
    if (!content) {
        fprintf(stderr, "Error: Out of memory reading %s\n", filename);
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(content, 1, (size_t)size, file);
    if (read_size != (size_t)size) {
        fprintf(stderr, "Error: Failed to read file %s\n", filename);
        free(content);
        fclose(file);
        return NULL;
    }
    content[size] = '\0';
    
    fclose(file);
    return content;
}

// Utility: Write file contents
void write_file(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot write to file %s\n", filename);
        return;
    }
    fprintf(file, "%s", content);
    fclose(file);
}

// Target type classification
typedef enum {
    TARGET_KIND_PLATFORM,
    TARGET_KIND_LANGUAGE
} TargetKind;

// Unified target descriptor
typedef struct {
    const char *name;       // CLI name
    TargetKind kind;
    Platform platform;      // valid when kind == TARGET_KIND_PLATFORM
    const char *extension;  // output file extension (with dot)
    const char *run_hint;   // "next steps" hint for the user
} TargetDescriptor;

// Look up a target by name (case-insensitive). Returns NULL if not found.
static const TargetDescriptor* lookup_target(const char *name) {
    static const TargetDescriptor targets[] = {
        /* ---- Platform targets ---- */
        {"interpret",  TARGET_KIND_PLATFORM, PLATFORM_LINUX,   "",       "Interpret directly"},
        {"run",        TARGET_KIND_PLATFORM, PLATFORM_LINUX,   "",       "Interpret directly"},
        {"android",    TARGET_KIND_PLATFORM, PLATFORM_ANDROID, ".java",  "javac SubProgram.java"},
        {"ios",        TARGET_KIND_PLATFORM, PLATFORM_IOS,     ".swift", "swiftc output.swift -o program && ./program"},
        {"web",        TARGET_KIND_PLATFORM, PLATFORM_WEB,     ".html",  "Open output.html in a web browser"},
        {"windows",    TARGET_KIND_PLATFORM, PLATFORM_WINDOWS, ".c",     "gcc output.c -o program && program.exe"},
        {"macos",      TARGET_KIND_PLATFORM, PLATFORM_MACOS,   ".c",     "gcc output.c -o program && ./program"},
        {"linux",      TARGET_KIND_PLATFORM, PLATFORM_LINUX,   ".c",     "gcc output.c -o program && ./program"},

        /* ---- Language targets ---- */
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

// Generate code for a language target. Returns allocated string or NULL.
static char* generate_language_code(const char *name, ASTNode *ast, const char *source) {
    /* C target → use the existing platform codegen (produces C code) */
    if (strcasecmp(name, "c") == 0) {
        return codegen_generate(ast, PLATFORM_LINUX);
    }

    /* C++ target → use the dedicated C++ codegen */
    if (strcasecmp(name, "cpp") == 0 || strcasecmp(name, "c++") == 0) {
        return codegen_generate_cpp(ast, PLATFORM_LINUX);
    }

    /* TypeScript → same codegen as JavaScript */
    if (strcasecmp(name, "typescript") == 0 || strcasecmp(name, "ts") == 0) {
        return codegen_javascript(ast, source);
    }

    /* Dispatch to the matching codegen function */
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

    fprintf(stderr, "Error: No codegen available for language '%s'\n", name);
    return NULL;
}

// Print help / usage
static void print_help(const char *prog) {
    printf(SUB_LOGO);
    printf("Usage: %s <input.sb> [target] [--help]\n\n", prog);
    printf("Targets can be a PLATFORM or a LANGUAGE:\n\n");
    printf("  Platform targets (generates platform-specific code):\n");
    printf("    android          Java for Android\n");
    printf("    ios              Swift for iOS\n");
    printf("    web              HTML/JS for Web\n");
    printf("    windows          C for Windows\n");
    printf("    macos            C for macOS\n");
    printf("    linux            C for Linux (default)\n\n");
    printf("  Language targets (transpile to another language):\n");
    printf("    python  / py     Python 3\n");
    printf("    javascript / js  JavaScript (Node.js)\n");
    printf("    typescript / ts  TypeScript\n");
    printf("    java             Java\n");
    printf("    c                C (same as linux platform)\n");
    printf("    cpp / c++        C++\n");
    printf("    rust / rs        Rust\n");
    printf("    go / golang      Go\n");
    printf("    swift            Swift\n");
    printf("    kotlin / kt      Kotlin\n");
    printf("    ruby / rb        Ruby\n");
    printf("    assembly / asm   x86-64 Assembly\n");
    printf("    css              CSS Stylesheet\n\n");
    printf("Examples:\n");
    printf("  %s hello.sb              # Compile for Linux (hello.c)\n", prog);
    printf("  %s hello.sb android      # Compile for Android (SubProgram.java)\n", prog);
    printf("  %s hello.sb python       # Transpile to Python (hello.py)\n", prog);
    printf("  %s hello.sb rust         # Transpile to Rust (hello.rs)\n", prog);
    printf("  %s hello.sb js           # Transpile to JavaScript (hello.js)\n", prog);
    printf("  %s hello.sb --help       # Show this help message\n\n", prog);
}

// Main function
int main(int argc, char *argv[]) {
    printf(SUB_LOGO);

    // Check for --help anywhere in argv
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }
    
    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *target_str = argc > 2 ? argv[2] : "linux";

    // Direct interpreter run
    if (strcasecmp(target_str, "interpret") == 0 || strcasecmp(target_str, "run") == 0) {
        printf("Interpreting %s...\n\n", input_file);
        extern int interpret_file(const char *path);
        return interpret_file(input_file);
    }
    
    // Look up the target
    const TargetDescriptor *target = lookup_target(target_str);
    if (!target) {
        fprintf(stderr, "Error: Unknown target '%s'\n", target_str);
        fprintf(stderr, "Run with --help for a list of valid targets.\n");
        return 1;
    }
    
    printf("Compiling %s for %s...\n\n", input_file, target_str);
    
    // Phase 1: Read source file
    printf("[1/5] Reading source file...\n");
    char *source = read_file(input_file);
    if (!source) return 1;
    
    // Phase 2: Lexical Analysis
    printf("[2/5] Lexical analysis...\n");
    int token_count;
    Token *tokens = lexer_tokenize(source, &token_count);
    printf("      Generated %d tokens\n", token_count);
    
    // Phase 3: Parsing
    printf("[3/5] Parsing...\n");
    ASTNode *ast = parser_parse(tokens, token_count);
    printf("      AST created\n");
    
    // Phase 4: Semantic Analysis
    printf("[4/5] Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "Semantic analysis failed\n");
        free(source);
        lexer_free_tokens(tokens, token_count);
        parser_free_ast(ast);
        return 1;
    }
    printf("      Passed\n");
    
    // Phase 5: Code Generation
    printf("[5/5] Code generation for %s...\n", target_str);
    
    char *output_code = NULL;
    
    if (target->kind == TARGET_KIND_PLATFORM) {
        /* Platform target → existing codegen_generate() */
        output_code = codegen_generate(ast, target->platform);
    } else {
        /* Language target → multilang codegen */
        output_code = generate_language_code(target_str, ast, source);
    }
    
    if (!output_code) {
        fprintf(stderr, "Code generation failed\n");
        free(source);
        lexer_free_tokens(tokens, token_count);
        parser_free_ast(ast);
        return 1;
    }
    
    // Build output filename from input basename or override
    char output_file[256];
    char base_name[256];
    get_output_basename(input_file, base_name, sizeof(base_name));
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
        // Print next steps for other targets
        printf("\nNext steps:\n");
        printf("  %s\n", target->run_hint);
    }
    
    // Cleanup
    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    free(output_code);
    
    return 0;
}
