/* ========================================
   SUB Language Compiler - Main Driver
   File: sub.c
   ======================================== */

#include "sub_compiler.h"

// Utility: Read file contents
char* read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(content, 1, size, file);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Error: Failed to read file %s\n", filename);
        fclose(file);
        free(content);
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

// Utility: Print compilation error
void compile_error(const char *message, int line) {
    fprintf(stderr, "Compilation error at line %d: %s\n", line, message);
}

// Main function
int main(int argc, char *argv[]) {
    printf("SUB Language Compiler v1.0\n");
    printf("================================\n\n");
    
    if (argc < 3) {
        printf("Usage: sub <input.sb> <platform>\n");
        printf("Platforms: android, ios, web, windows, macos, linux\n");
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *platform_str = argv[2];
    
    // Determine target platform
    Platform platform;
    if (strcmp(platform_str, "android") == 0) platform = PLATFORM_ANDROID;
    else if (strcmp(platform_str, "ios") == 0) platform = PLATFORM_IOS;
    else if (strcmp(platform_str, "web") == 0) platform = PLATFORM_WEB;
    else if (strcmp(platform_str, "windows") == 0) platform = PLATFORM_WINDOWS;
    else if (strcmp(platform_str, "macos") == 0) platform = PLATFORM_MACOS;
    else if (strcmp(platform_str, "linux") == 0) platform = PLATFORM_LINUX;
    else {
        fprintf(stderr, "Error: Unknown platform %s\n", platform_str);
        return 1;
    }
    
    printf("Compiling %s for %s...\n\n", input_file, platform_str);
    
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
    SymbolTable *symbols = symbol_table_create(64);
    if (!semantic_analyze(ast, symbols)) {
        fprintf(stderr, "Semantic analysis failed\n");
        symbol_table_free(symbols);
        parser_free_ast(ast);
        lexer_free_tokens(tokens, token_count);
        free(source);
        return 1;
    }
    printf("      Passed\n");
    
    // Phase 5: Code Generation
    printf("[5/5] Code generation for %s...\n", platform_str);
    
    // Prepare compilation options
    CompilationOptions options = {0};
    options.optimize = true;
    options.optimization_level = 2;
    options.debug_symbols = false;
    options.minify = false;
    options.warnings_as_errors = false;
    options.verbose = false;
    options.use_cpp = false;
    options.enable_simd = false;
    options.parallel_compile = false;
    
    char *output_code = codegen_generate(ast, platform, &options);
    
    // Write output file
    char output_file[256];
    snprintf(output_file, sizeof(output_file), "output_%s.code", platform_str);
    write_file(output_file, output_code);
    
    printf("\nCompilation successful!\n");
    printf("Output written to: %s\n", output_file);
    
    // Cleanup
    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    symbol_table_free(symbols);
    free(output_code);
    
    return 0;
}
