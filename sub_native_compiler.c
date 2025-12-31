/* ========================================
   SUB Language - Native Compiler Driver
   Compiles SUB to native x86-64 machine code
   File: sub_native_compiler.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "ir.h"
#include "codegen_x64.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <process.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* Read file utility */
static char* read_file_native(const char *filename) {
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
        fclose(file);
        return NULL;
    }
    
    if (fread(content, 1, size, file) != (size_t)size) {
        // fprintf(stderr, "Warning: Short read\n");
    }
    content[size] = '\0';
    fclose(file);
    return content;
}

/* Execute system command */
static int execute_command(const char *cmd) {
    printf("Executing: %s\n", cmd);
    return system(cmd);
}

/* Main native compilation function */
int compile_to_native(const char *input_file, const char *output_file) {
    printf("\n╔═══════════════════════════════════════════╗\n");
    printf("║  SUB Native Compiler (x86-64)            ║\n");
    printf("╚═══════════════════════════════════════════╝\n\n");
    
    printf("📄 Input:  %s\n", input_file);
    printf("🎯 Output: %s\n\n", output_file);
    
    // Phase 1: Read source
    printf("[1/7] 📖 Reading source file...\n");
    char *source = read_file_native(input_file);
    if (!source) return 1;
    printf("      ✓ Read %zu bytes\n", strlen(source));
    
    // Phase 2: Lexical Analysis
    printf("[2/7] 🔤 Lexical analysis...\n");
    int token_count;
    Token *tokens = lexer_tokenize(source, &token_count);
    printf("      ✓ Generated %d tokens\n", token_count);
    
    // Phase 3: Parsing
    printf("[3/7] 🌳 Parsing...\n");
    ASTNode *ast = parser_parse(tokens, token_count);
    printf("      ✓ AST created\n");
    
    // Phase 4: Semantic Analysis
    printf("[4/7] 🔍 Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "      ✗ Semantic analysis failed\n");
        return 1;
    }
    printf("      ✓ Passed\n");
    
    // Phase 4.5: Strict Type Checking
    printf("[4.5/7] 🔬 Type checking...\n");
    if (!semantic_check_types(ast)) {
        fprintf(stderr, "      ✗ Type checking failed\n");
        return 1;
    }
    printf("      ✓ Passed\n");
    
    // Phase 5: IR Generation
    printf("[5/7] 🔄 Generating intermediate representation...\n");
    IRModule *ir_module = ir_generate_from_ast(ast);
    if (!ir_module) {
        fprintf(stderr, "      ✗ IR generation failed\n");
        return 1;
    }
    // Debug: Print IR
    ir_print(ir_module);
    printf("      ✓ IR generated\n");
    
    // Phase 5.5: IR Optimization
    printf("[5.5/7] ⚡ Optimizing IR...\n");
    ir_optimize(ir_module);
    printf("      ✓ IR optimized\n");
    
    // Debug: Print optimized IR
    printf("\n      === Optimized IR ===\n");
    ir_print(ir_module);
    
    // Phase 6: x86-64 Code Generation
    printf("[6/7] ⚙️  Generating x86-64 assembly...\n");
    char asm_file[256];
    snprintf(asm_file, sizeof(asm_file), "%s.s", output_file);
    
    FILE *asm_output = fopen(asm_file, "w");
    if (!asm_output) {
        fprintf(stderr, "      ✗ Cannot create assembly file\n");
        return 1;
    }
    
    X64Context *ctx = x64_context_create(asm_output);
    x64_generate_program(ctx, ir_module);
    x64_context_free(ctx);
    fclose(asm_output);
    printf("      ✓ Assembly written to %s\n", asm_file);
    
    // Phase 7: Assemble and Link
    printf("[7/7] 🔗 Assembling and linking...\n");
    
    char cmd[512];
    
#ifdef _WIN32
    // Windows: Use MSVC assembler (ml64) and linker
    char obj_file[256];
    snprintf(obj_file, sizeof(obj_file), "%s.obj", output_file);
    snprintf(cmd, sizeof(cmd), "ml64 /c /Fo%s %s", obj_file, asm_file);
    if (execute_command(cmd) != 0) {
        fprintf(stderr, "      ✗ Assembly failed\n");
        return 1;
    }
    
    snprintf(cmd, sizeof(cmd), "link /OUT:%s.exe %s", output_file, obj_file);
    if (execute_command(cmd) != 0) {
        fprintf(stderr, "      ✗ Linking failed\n");
        return 1;
    }
    printf("      ✓ Executable: %s.exe\n", output_file);
#else
    // Linux/macOS: Use GCC
    snprintf(cmd, sizeof(cmd), "gcc -o %s %s", output_file, asm_file);
    if (execute_command(cmd) != 0) {
        fprintf(stderr, "      ✗ Compilation failed\n");
        return 1;
    }
    printf("      ✓ Executable: %s\n", output_file);
#endif
    
    // Success!
    printf("\n✅ Native compilation successful!\n");
    printf("📦 Binary: %s\n\n", output_file);
    
    printf("Run your program:\n");
#ifdef _WIN32
    printf("  %s.exe\n\n", output_file);
#else
    printf("  ./%s\n\n", output_file);
#endif
    
    // Cleanup
    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    ir_module_free(ir_module);
    
    return 0;
}

/* Main entry point */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("SUB Native Compiler v1.0.0\n");
        printf("Usage: %s <input.sb> [output]\n\n", argv[0]);
        printf("Examples:\n");
        printf("  %s program.sb              # Output: program\n", argv[0]);
        printf("  %s program.sb myapp        # Output: myapp\n\n", argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *output_file = argc > 2 ? argv[2] : "program";
    
    return compile_to_native(input_file, output_file);
}
