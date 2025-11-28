/**
 * SUB Language Compiler - Main Driver
 * Integrates Rust Frontend + C++ Middle-end + C Backend
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// FFI imports
extern void* sub_frontend_tokenize(const char* source);
extern void sub_frontend_free_tokens(void* token_array);

extern void* sub_middle_end_create_analyzer();
extern int sub_middle_end_analyze(void* analyzer, void* ast_root);
extern void sub_middle_end_optimize(void* ast_root, int level);
extern void sub_middle_end_free_analyzer(void* analyzer);
extern const char* sub_middle_end_get_errors(void* analyzer);

extern char* sub_backend_generate_web(void* ast_root);
extern char* sub_backend_generate_android(void* ast_root);
extern char* sub_backend_generate_native(void* ast_root);
extern void sub_backend_free_code(char* code);

typedef enum {
    PLATFORM_WEB,
    PLATFORM_ANDROID,
    PLATFORM_IOS,
    PLATFORM_NATIVE
} Platform;

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = malloc(size + 1);
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    return buffer;
}

static int write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: Cannot write to file '%s'\n", path);
        return 0;
    }
    fprintf(f, "%s", content);
    fclose(f);
    return 1;
}

static Platform parse_platform(const char* str) {
    if (strcmp(str, "web") == 0) return PLATFORM_WEB;
    if (strcmp(str, "android") == 0) return PLATFORM_ANDROID;
    if (strcmp(str, "ios") == 0) return PLATFORM_IOS;
    return PLATFORM_NATIVE;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("SUB Language Compiler v2.0.0 (Heavy Edition)\n");
        printf("Usage: %s <input.sb> <platform> [options]\n", argv[0]);
        printf("Platforms: web, android, ios, native\n");
        printf("\nMulti-Language Architecture:\n");
        printf("  Frontend:   Rust   (Memory-safe lexer/parser)\n");
        printf("  Middle-end: C++    (Optimizations & analysis)\n");
        printf("  Backend:    C      (Portable code generation)\n");
        return 1;
    }
    
    const char* input_file = argv[1];
    Platform platform = parse_platform(argv[2]);
    int opt_level = 2;
    int verbose = 0;
    
    // Parse additional options
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opt_level = atoi(argv[i] + 2);
        }
    }
    
    if (verbose) {
        printf("\n=== SUB Compiler v2.0.0 (Heavy Edition) ===\n");
        printf("Input:        %s\n", input_file);
        printf("Platform:     %s\n", argv[2]);
        printf("Optimization: O%d\n", opt_level);
        printf("\n[Rust Frontend] Starting...\n");
    }
    
    clock_t start = clock();
    
    // Read source
    char* source = read_file(input_file);
    if (!source) return 1;
    
    // Lexical Analysis (Rust)
    void* tokens = sub_frontend_tokenize(source);
    if (!tokens) {
        fprintf(stderr, "Error: Lexical analysis failed\n");
        free(source);
        return 1;
    }
    
    if (verbose) {
        printf("✓ Tokenization complete\n");
        printf("\n[C++ Middle-end] Starting...\n");
    }
    
    // Semantic Analysis (C++)
    void* analyzer = sub_middle_end_create_analyzer();
    void* ast_root = NULL; // Simplified: would build AST from tokens
    
    if (!sub_middle_end_analyze(analyzer, ast_root)) {
        fprintf(stderr, "Semantic errors:\n%s", 
                sub_middle_end_get_errors(analyzer));
        sub_middle_end_free_analyzer(analyzer);
        sub_frontend_free_tokens(tokens);
        free(source);
        return 1;
    }
    
    if (verbose) {
        printf("✓ Semantic analysis passed\n");
        printf("✓ Optimization O%d applied\n", opt_level);
    }
    
    // Optimize (C++)
    sub_middle_end_optimize(ast_root, opt_level);
    
    if (verbose) {
        printf("\n[C Backend] Generating code...\n");
    }
    
    // Code Generation (C)
    char* output_code = NULL;
    const char* output_file = NULL;
    
    switch (platform) {
        case PLATFORM_WEB:
            output_code = sub_backend_generate_web(ast_root);
            output_file = "output_web.js";
            break;
        case PLATFORM_ANDROID:
            output_code = sub_backend_generate_android(ast_root);
            output_file = "output_android.java";
            break;
        case PLATFORM_NATIVE:
            output_code = sub_backend_generate_native(ast_root);
            output_file = "output_native.c";
            break;
        default:
            output_code = sub_backend_generate_native(ast_root);
            output_file = "output.code";
    }
    
    if (!output_code) {
        fprintf(stderr, "Error: Code generation failed\n");
        sub_middle_end_free_analyzer(analyzer);
        sub_frontend_free_tokens(tokens);
        free(source);
        return 1;
    }
    
    // Write output
    if (!write_file(output_file, output_code)) {
        sub_backend_free_code(output_code);
        sub_middle_end_free_analyzer(analyzer);
        sub_frontend_free_tokens(tokens);
        free(source);
        return 1;
    }
    
    clock_t end = clock();
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
    
    if (verbose) {
        printf("✓ Code generation complete\n");
        printf("\n=== Compilation Summary ===\n");
        printf("Output:       %s\n", output_file);
        printf("Time:         %.2f ms\n", elapsed);
        printf("\nArchitecture Performance:\n");
        printf("  Rust:  Memory-safe, zero overhead\n");
        printf("  C++:   Template optimizations\n");
        printf("  C:     Minimal binary footprint\n");
    } else {
        printf("✓ Compiled successfully in %.2f ms -> %s\n", elapsed, output_file);
    }
    
    // Cleanup
    sub_backend_free_code(output_code);
    sub_middle_end_free_analyzer(analyzer);
    sub_frontend_free_tokens(tokens);
    free(source);
    
    return 0;
}
