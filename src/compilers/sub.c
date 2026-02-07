/* ========================================
   SUB Language Compiler - Main Driver
   File: sub.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"

// Utility: Read file contents
char* read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
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



// Get proper file extension for platform
const char* get_file_extension(Platform platform) {
    switch (platform) {
        case PLATFORM_ANDROID:
            return ".java";
        case PLATFORM_IOS:
            return ".swift";
        case PLATFORM_WEB:
            return ".html";
        case PLATFORM_WINDOWS:
        case PLATFORM_MACOS:
        case PLATFORM_LINUX:
        default:
            return ".c";
    }
}

// Main function
int main(int argc, char *argv[]) {
    printf("SUB Language Compiler v2.0\n");
    printf("================================\n\n");
    
    if (argc < 2) {
        printf("Usage: sub <input.sb> [platform]\n");
        printf("Platforms: android, ios, web, windows, macos, linux (default: linux)\n");
        printf("\nExample:\n");
        printf("  sub program.sb          # Compile for Linux (generates .c file)\n");
        printf("  sub program.sb android  # Compile for Android (generates .java file)\n");
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *platform_str = argc > 2 ? argv[2] : "linux";
    
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
        fprintf(stderr, "Valid platforms: android, ios, web, windows, macos, linux\n");
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
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "Semantic analysis failed\n");
        return 1;
    }
    printf("      Passed\n");
    
    // Phase 5: Code Generation
    printf("[5/5] Code generation for %s...\n", platform_str);
    char *output_code = codegen_generate(ast, platform);
    
    if (!output_code) {
        fprintf(stderr, "Code generation failed\n");
        return 1;
    }
    
    // Write output file with proper extension
    char output_file[256];
    const char *extension = get_file_extension(platform);
    snprintf(output_file, sizeof(output_file), "output_%s%s", platform_str, extension);
    write_file(output_file, output_code);
    
    printf("\n✓ Compilation successful!\n");
    printf("✓ Output written to: %s\n", output_file);
    
    // Print next steps based on platform
    printf("\nNext steps:\n");
    switch (platform) {
        case PLATFORM_ANDROID:
            printf("  javac %s\n", output_file);
            break;
        case PLATFORM_IOS:
            printf("  swiftc %s -o program\n", output_file);
            break;
        case PLATFORM_WEB:
            printf("  Open %s in a web browser\n", output_file);
            break;
        case PLATFORM_WINDOWS:
        case PLATFORM_MACOS:
        case PLATFORM_LINUX:
            printf("  gcc %s -o program\n", output_file);
            printf("  ./program\n");
            break;
        default:
            printf("  Compile %s with appropriate compiler\n", output_file);
            break;
    }
    
    // Cleanup
    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    free(output_code);
    
    return 0;
}
