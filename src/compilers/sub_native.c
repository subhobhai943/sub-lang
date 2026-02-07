/* ========================================
   SUB Language - Native Compiler Driver
   Compile SUB directly to native machine code
   File: sub_native.c
   ======================================== */

#include "sub_compiler.h"
#include "ir.h"
#include "codegen_native.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

/* Read file */
static char* read_file_native(const char *filename) {
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

/* Write file */
static void write_file_native(const char *filename, const char *content) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot write to file %s\n", filename);
        return;
    }
    fprintf(file, "%s", content);
    fclose(file);
}

/* Print usage */
void print_usage_native(const char *prog_name) {
    printf("\n");
    printf("╭────────────────────────────────────────────────────╮\n");
    printf("│    SUB Native Compiler v1.0 - Real Machine Code  │\n");
    printf("╰────────────────────────────────────────────────────╯\n\n");
    printf("Usage: %s <input.sb> [options]\n\n", prog_name);
    printf("Output Options:\n");
    printf("  -o <file>          Output filename (default: a.out)\n");
    printf("  -S                 Generate assembly only (.s file)\n");
    printf("  -c                 Generate object file only (.o)\n");
    printf("  -emit-ir           Show IR (intermediate representation)\n\n");
    printf("Optimization:\n");
    printf("  -O0                No optimization (fast compile)\n");
    printf("  -O1                Basic optimization\n");
    printf("  -O2                Standard optimization (default)\n");
    printf("  -O3                Aggressive optimization\n\n");
    printf("Platform:\n");
    printf("  -m32               Generate 32-bit code\n");
    printf("  -m64               Generate 64-bit code (default)\n");
    printf("  --target=<arch>    Cross-compile (x86_64, arm64, etc)\n\n");
    printf("Debug:\n");
    printf("  -g                 Include debug information\n");
    printf("  -v, --verbose      Verbose output\n\n");
    printf("Examples:\n");
    printf("  %s program.sb                  # Compile to native binary\n", prog_name);
    printf("  %s program.sb -O3              # Max optimization\n", prog_name);
    printf("  %s program.sb -S               # Generate assembly\n", prog_name);
    printf("  %s program.sb -emit-ir         # Show IR\n", prog_name);
    printf("  %s program.sb -o myapp         # Custom output name\n\n", prog_name);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage_native(argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    const char *output_file = "a.out";
    bool emit_asm = false;
    bool emit_ir = false;
    bool verbose = false;
    int opt_level = 2;
    
    // Parse command line options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0) {
            emit_asm = true;
        } else if (strcmp(argv[i], "-emit-ir") == 0) {
            emit_ir = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
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
    
    printf("\n╭──────────────────────────────────────────────────╮\n");
    printf("│      SUB Native Compiler - Compiling to Machine Code      │\n");
    printf("╰──────────────────────────────────────────────────╯\n\n");
    
    if (verbose) {
        printf("📄 Input:  %s\n", input_file);
        printf("⚙️  Mode:   Native Compilation (-O%d)\n", opt_level);
        printf("📦 Output: %s\n\n", output_file);
    }
    
    // Phase 1: Read source
    if (verbose) printf("[1/6] 📖 Reading source file...\n");
    char *source = read_file_native(input_file);
    if (!source) return 1;
    if (verbose) printf("      ✓ Read %zu bytes\n", strlen(source));
    
    // Phase 2: Lexical Analysis
    if (verbose) printf("[2/6] 🔤 Lexical analysis...\n");
    int token_count;
    Token *tokens = lexer_tokenize(source, &token_count);
    if (verbose) printf("      ✓ Generated %d tokens\n", token_count);
    
    // Phase 3: Parsing
    if (verbose) printf("[3/6] 🌳 Parsing...\n");
    ASTNode *ast = parser_parse(tokens, token_count);
    if (verbose) printf("      ✓ AST created\n");
    
    // Phase 4: Semantic Analysis
    if (verbose) printf("[4/6] 🔍 Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "      ✗ Semantic analysis failed\n");
        return 1;
    }
    if (verbose) printf("      ✓ Passed\n");
    
    // Phase 5: IR Generation
    if (verbose) printf("[5/6] 🧠 Generating IR...\n");
    IRModule *ir = ir_generate_from_ast(ast);
    if (!ir) {
        fprintf(stderr, "      ✗ IR generation failed\n");
        return 1;
    }
    if (verbose) printf("      ✓ IR generated\n");
    
    // Emit IR if requested
    if (emit_ir) {
        printf("\n=== IR Output ===\n");
        ir_print(ir);
        printf("=== End IR ===\n\n");
    }
    
    // Optimize IR
    if (opt_level > 0 && verbose) {
        printf("      💡 Optimizing (level %d)...\n", opt_level);
    }
    if (opt_level > 0) {
        ir_optimize(ir);
    }
    
    // Phase 6: Native Code Generation
    if (verbose) printf("[6/6] ⚡ Generating native code...\n");
    
    NativeCodegenOptions options = {
        .target = codegen_native_get_host_target(),
        .format = codegen_native_get_host_format(),
        .optimize_level = opt_level,
        .debug_info = false,
        .position_independent = false
    };
    
    char *asm_code = codegen_native_generate_asm(ir, options.target);
    if (!asm_code) {
        fprintf(stderr, "      ✗ Code generation failed\n");
        return 1;
    }
    
    if (verbose) printf("      ✓ Generated %zu bytes\n", strlen(asm_code));
    
    // Write assembly file
    char asm_file[256];
    if (emit_asm) {
        snprintf(asm_file, sizeof(asm_file), "%s", output_file);
    } else {
        snprintf(asm_file, sizeof(asm_file), "/tmp/sub_temp_%d.s", getpid());
    }
    
    write_file_native(asm_file, asm_code);
    
    if (emit_asm) {
        printf("\n✅ Assembly generation successful!\n");
        printf("📝 Output: %s\n\n", asm_file);
        printf("Assemble and link with:\n");
#ifdef __APPLE__
        if (options.target == NATIVE_TARGET_ARM64) {
            printf("  as -arch arm64 %s -o temp.o\n", asm_file);
        } else {
            printf("  as -arch x86_64 %s -o temp.o\n", asm_file);
        }
        printf("  ld temp.o -o %s -lSystem\n\n", output_file);
#elif defined(_WIN32)
        printf("  ml64 /c %s\n", asm_file);
        printf("  link /SUBSYSTEM:CONSOLE temp.obj /OUT:%s\n\n", output_file);
#else
        printf("  as %s -o temp.o\n", asm_file);
        if (options.target == NATIVE_TARGET_ARM64) {
            printf("  ld temp.o -o %s -lc -dynamic-linker /lib/ld-linux-aarch64.so.1\n\n", output_file);
        } else {
            printf("  ld temp.o -o %s -lc -dynamic-linker /lib64/ld-linux-x86-64.so.2\n\n", output_file);
        }
#endif
    } else {
        // Assemble and link
        if (verbose) printf("\n🔧 Assembling and linking...\n");
        
        char cmd[512];
#ifdef __APPLE__
        if (options.target == NATIVE_TARGET_ARM64) {
            snprintf(cmd, sizeof(cmd), "as -arch arm64 %s -o /tmp/sub_temp_%d.o && ld /tmp/sub_temp_%d.o -o %s -lSystem 2>/dev/null",
                     asm_file, getpid(), getpid(), output_file);
        } else {
            snprintf(cmd, sizeof(cmd), "as -arch x86_64 %s -o /tmp/sub_temp_%d.o && ld /tmp/sub_temp_%d.o -o %s -lSystem 2>/dev/null",
                     asm_file, getpid(), getpid(), output_file);
        }
#elif defined(_WIN32)
        snprintf(cmd, sizeof(cmd), "ml64 /c /Fo%s.obj %s >nul 2>&1 && link /SUBSYSTEM:CONSOLE %s.obj /OUT:%s >nul 2>&1",
                 output_file, asm_file, output_file, output_file);
#else
        if (options.target == NATIVE_TARGET_ARM64) {
            snprintf(cmd, sizeof(cmd), "as %s -o /tmp/sub_temp_%d.o 2>/dev/null && ld /tmp/sub_temp_%d.o -o %s -lc -dynamic-linker /lib/ld-linux-aarch64.so.1 2>/dev/null",
                     asm_file, getpid(), getpid(), output_file);
        } else {
            snprintf(cmd, sizeof(cmd), "as %s -o /tmp/sub_temp_%d.o 2>/dev/null && ld /tmp/sub_temp_%d.o -o %s -lc -dynamic-linker /lib64/ld-linux-x86-64.so.2 2>/dev/null",
                     asm_file, getpid(), getpid(), output_file);
        }
#endif
        
        int result = system(cmd);
        if (result != 0) {
            fprintf(stderr, "✗ Assembly/linking failed\n");
            fprintf(stderr, "Assembly saved to: %s\n", asm_file);
            return 1;
        }
        
        // Clean up temp files
        if (!emit_asm) {
            remove(asm_file);
        }
        
        printf("\n✅ Native compilation successful!\n");
        printf("🚀 Executable: %s\n\n", output_file);
        printf("Run with:\n");
        printf("  ./%s\n\n", output_file);
    }
    
    // Cleanup
    free(source);
    lexer_free_tokens(tokens, token_count);
    parser_free_ast(ast);
    ir_module_free(ir);
    free(asm_code);
    
    return 0;
}
