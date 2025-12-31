/* ========================================
   SUB Language Multi-Language Compiler Driver
   Compile SUB code to: Python, Java, Swift, Kotlin, C, C++, Rust, JS, Assembly, CSS
   File: sub_multilang.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"
#include "targets.h"

/* External codegen functions (from codegen_multilang.c) - NOW WITH SOURCE PARAMETER */
extern char* codegen_python(ASTNode *ast, const char *source);
extern char* codegen_java(ASTNode *ast, const char *source);
extern char* codegen_swift(ASTNode *ast, const char *source);
extern char* codegen_kotlin(ASTNode *ast, const char *source);
extern char* codegen_cpp(ASTNode *ast, const char *source);
extern char* codegen_rust(ASTNode *ast, const char *source);
extern char* codegen_javascript(ASTNode *ast, const char *source);
extern char* codegen_css(ASTNode *ast, const char *source);
extern char* codegen_ruby(ASTNode *ast, const char *source);
extern char* codegen_assembly(ASTNode *ast, const char *source);

/* From codegen.c */
extern char* codegen_generate_c(ASTNode *ast, Platform platform);

/* Utility: Read file */
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
        fclose(file);
        return NULL;
    }
    
    if (fread(content, 1, size, file) != (size_t)size) {
        // Just print warning, don't fail as it might be text mode diff
        // fprintf(stderr, "Warning: Short read\n");
    }
    content[size] = '\0';
    fclose(file);
    return content;
}

/* Utility: Write file */
void write_file(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot write to file %s\n", filename);
        return;
    }
    fprintf(file, "%s", content);
    fclose(file);
}

/* Generate code for target language - NOW PASSES SOURCE */
char* generate_code(ASTNode *ast, TargetLanguage lang, const char *source) {
    if (lang == LANG_C) {
        return codegen_generate_c(ast, PLATFORM_LINUX);
    }
    
    if (!target_is_implemented(lang)) {
        const char *lang_name = language_to_string(lang);
        fprintf(stderr, "%s codegen not yet implemented\n", lang_name);
        return NULL;
    }
    
    CodegenFn codegen = get_codegen_for_target(language_to_string(lang));
    if (!codegen) {
        fprintf(stderr, "Unknown target language\n");
        return NULL;
    }
    
    return codegen(ast, source);
}

/* Print usage */
void print_usage(const char *prog_name) {
    printf("SUB Language Multi-Target Compiler v2.0\n");
    printf("=========================================\n\n");
    printf("Usage: %s <input.sb> [target_language]\n\n", prog_name);
    printf("Supported Target Languages:\n");
    printf("  c, cpp/c++     - C and C++\n");
    printf("  cpp17, cpp20   - C++17, C++20\n");
    printf("  python/py      - Python 3\n");
    printf("  java           - Java\n");
    printf("  swift          - Swift\n");
    printf("  kotlin/kt      - Kotlin\n");
    printf("  rust/rs        - Rust\n");
    printf("  javascript/js  - JavaScript\n");
    printf("  typescript/ts  - TypeScript\n");
    printf("  go/golang      - Go (coming soon)\n");
    printf("  assembly/asm   - x86-64 Assembly\n");
    printf("  css            - CSS Stylesheet\n");
    printf("  ruby/rb        - Ruby\n");
    printf("  llvm           - LLVM IR (coming soon)\n");
    printf("  wasm           - WebAssembly (coming soon)\n");
    printf("\nExamples:\n");
    printf("  %s program.sb python      # Compile to Python\n", prog_name);
    printf("  %s program.sb cpp17       # Compile to C++17\n", prog_name);
    printf("  %s program.sb cpp20       # Compile to C++20\n", prog_name);
    printf("  %s program.sb java        # Compile to Java\n", prog_name);
    printf("  %s program.sb ruby        # Compile to Ruby\n", prog_name);
    printf("  %s program.sb rust        # Compile to Rust\n", prog_name);
    printf("  %s program.sb c           # Compile to C (default)\n\n", prog_name);
}

/* Main function */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *target_lang_str = argc > 2 ? argv[2] : "c";
    TargetLanguage target_lang = parse_language(target_lang_str);
    LanguageInfo *info = language_info_get(target_lang);
    
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║  SUB Language Compiler v2.0            ║\n");
    printf("╚════════════════════════════════════════╝\n\n");
    
    printf("📄 Input:  %s\n", input_file);
    printf("🎯 Target: %s\n", info->name);
    printf("📦 Output: output%s\n\n", info->extension);
    
    // Phase 1: Read source
    printf("[1/5] 📖 Reading source file...\n");
    char *source = read_file(input_file);
    if (!source) return 1;
    printf("      ✓ Read %zu bytes\n", strlen(source));
    
    // Phase 2: Lexical Analysis
    printf("[2/5] 🔤 Lexical analysis...\n");
    int token_count;
    Token *tokens = lexer_tokenize(source, &token_count);
    printf("      ✓ Generated %d tokens\n", token_count);
    
    // Phase 3: Parsing
    printf("[3/5] 🌳 Parsing...\n");
    ASTNode *ast = parser_parse(tokens, token_count);
    printf("      ✓ AST created\n");
    
    // Phase 4: Semantic Analysis
    printf("[4/5] 🔍 Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "      ✗ Semantic analysis failed\n");
        return 1;
    }
    printf("      ✓ Passed\n");
    
    // Phase 5: Code Generation - NOW PASSES SOURCE FOR EMBEDDED CODE
    printf("[5/5] ⚙️  Code generation (%s)...\n", info->name);
    char *output_code = generate_code(ast, target_lang, source);
    
    if (!output_code) {
        fprintf(stderr, "      ✗ Code generation failed\n");
        return 1;
    }
    
    // Write output
    char output_file[256];
    if (target_lang == LANG_JAVA) {
        snprintf(output_file, sizeof(output_file), "SubProgram%s", info->extension);
    } else {
        snprintf(output_file, sizeof(output_file), "output%s", info->extension);
    }
    
    write_file(output_file, output_code);
    printf("      ✓ Generated %zu bytes\n", strlen(output_code));
    
    // Success!
    printf("\n✅ Compilation successful!\n");
    printf("📝 Output: %s\n\n", output_file);
    
    // Print next steps
    printf("Next steps:\n");
    printf("  %s\n\n", info->run_command);
    
    // Cleanup
    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    free(output_code);
    
    return 0;
}
