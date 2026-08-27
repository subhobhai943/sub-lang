/* ========================================
   SUB Language - Native Compiler Driver
   Compiles SUB to native binary via C backend + gcc
   File: sub_native.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "codegen_cpp.h"
#include "codegen_infer.h"
#include "native.h"
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
#include <sys/wait.h>
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

/* Print usage. main() prints the banner already, so this must not repeat it. */
void print_usage_native(const char *prog_name) {
    printf("Usage: %s <input.sb> [options]\n\n", prog_name);
    printf("Output Options:\n");
    printf("  -o <file>          Output filename (default: derived from input)\n\n");
    printf("Backend:\n");
    printf("  --native           Emit machine code directly (no C compiler).\n");
    printf("                     Fail rather than fall back.\n");
    printf("  --via-c            Generate C and build it with $CC (default: gcc).\n");
    printf("                     Default is --native where supported, --via-c otherwise.\n\n");
    printf("Optimization (--via-c only):\n");
    printf("  -O0 -O1 -O2 -O3    Passed through to the C compiler\n\n");
    printf("Debug:\n");
    printf("  -v, --verbose      Verbose output\n\n");
    printf("Examples:\n");
    printf("  %s hello.sb                  # Compile to ./hello\n", prog_name);
    printf("  %s hello.sb --native         # Refuse to fall back to a C compiler\n", prog_name);
    printf("  %s hello.sb -o myapp         # Custom output name\n\n", prog_name);
}

/* Where the two backends agree: read the file, lex, parse, check.
   Returns the AST, or NULL after reporting why not. Both `tokens` and
   `source` are handed back so the caller can free them in the right order -
   AST nodes borrow strings from the token array. */
static ASTNode* front_end(const char *input_file, bool verbose,
                          Token **tokens_out, int *ntok_out, char **source_out) {
    FILE *f = fopen(input_file, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", input_file); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Failed to seek file %s\n", input_file);
        fclose(f); return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "Error: Failed to determine file size for %s\n", input_file);
        fclose(f); return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Failed to rewind file %s\n", input_file);
        fclose(f); return NULL;
    }
    char *source = malloc((size_t)sz + 1);
    if (!source) {
        fprintf(stderr, "Error: Out of memory reading %s\n", input_file);
        fclose(f); return NULL;
    }
    size_t read_size = fread(source, 1, (size_t)sz, f);
    fclose(f);
    if (read_size != (size_t)sz) {
        fprintf(stderr, "Error: Failed to read complete file %s\n", input_file);
        free(source); return NULL;
    }
    source[read_size] = '\0';

    if (verbose) printf("[1/4] Lexing...\n");
    int ntok;
    Token *tokens = lexer_tokenize(source, &ntok);
    if (!tokens) { free(source); return NULL; }

    if (verbose) printf("[2/4] Parsing...\n");
    ASTNode *ast = parser_parse(tokens, ntok);
    if (!ast) {
        fprintf(stderr, "Parsing failed.\n");
        lexer_free_tokens(tokens, ntok); free(source);
        return NULL;
    }

    if (verbose) printf("[3/4] Semantic analysis...\n");
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "Semantic analysis failed.\n");
        parser_free_ast(ast); lexer_free_tokens(tokens, ntok); free(source);
        return NULL;
    }

    infer_function_signatures(ast);
    *tokens_out = tokens;
    *ntok_out   = ntok;
    *source_out = source;
    return ast;
}

/* Backend selection. `native` means "emit machine code from this process";
   `via_c` means "write C and hand it to a C compiler". */
typedef enum { BACKEND_AUTO, BACKEND_NATIVE, BACKEND_VIA_C } Backend;

/* Append the platform's executable suffix unless it is already there. */
static void with_exe_suffix(const char *name, char *out, size_t n) {
    const char *ext = sub_host_exe_suffix();
    size_t name_len = strlen(name), ext_len = strlen(ext);
    int has_ext = (ext_len > 0 && name_len >= ext_len &&
                   strcmp(name + name_len - ext_len, ext) == 0);
    snprintf(out, n, "%s%s", name, has_ext ? "" : ext);
}

/* Run the C compiler as a process rather than as a shell command line.
   Handing a string to system() meant quoting the paths, and any character
   the shell treats specially had to be rejected outright - which refused
   every absolute Windows path, because "D:/..." contains a colon. Spawning
   the compiler directly removes the quoting and the need to filter at all:
   there is no shell to inject into. */
static int run_cc(const char *cc, const char *opt,
                  const char *out, const char *src) {
    const char *argv[] = { cc, opt, "-o", out, src, "-lm", NULL };
#ifdef _WIN32
    /* MinGW's _spawnvp takes const char *const *; POSIX execvp below takes
       char *const *. The array is never written either way. */
    intptr_t rc = _spawnvp(_P_WAIT, cc, (const char *const *)argv);
    return rc < 0 ? 127 : (int)rc;
#else
    pid_t pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {
        execvp(cc, (char *const *)argv);
        _exit(127);              /* only reached when cc is not on PATH */
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 127;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
#endif
}

static int build_via_c(ASTNode *ast, const char *out_with_ext,
                       bool verbose, int opt_level) {
    if (verbose) printf("[4/4] Generating C and invoking the host compiler...\n");
    char *c_code = codegen_generate(ast, sub_host_platform());
    if (!c_code) { fprintf(stderr, "Code generation failed.\n"); return 1; }

    char tmp_c[512];
#ifdef _WIN32
    snprintf(tmp_c, sizeof(tmp_c), "sub_tmp_%d.c", (int)_getpid());
#else
    snprintf(tmp_c, sizeof(tmp_c), "/tmp/sub_tmp_%d.c", (int)getpid());
#endif
    FILE *cf = fopen(tmp_c, "w");
    if (!cf) { free(c_code); fprintf(stderr, "Cannot write temp file.\n"); return 1; }
    fputs(c_code, cf); fclose(cf); free(c_code);

    const char *opt = opt_level >= 2 ? "-O2" : opt_level == 1 ? "-O1" : "-O0";
    const char *cc  = sub_host_cc();
    int ret = run_cc(cc, opt, out_with_ext, tmp_c);
    remove(tmp_c);
    if (ret != 0) {
        fprintf(stderr, "Compilation failed. Make sure %s is installed "
                        "(set CC to choose a different compiler).\n", cc);
        return 1;
    }
    return 0;
}

int compile_to_native(const char *input_file, const char *output_name,
                      bool verbose, int opt_level, Backend backend) {
    if (!output_name || !*output_name) {
        fprintf(stderr, "Error: empty output name.\n");
        return 1;
    }

    Token *tokens = NULL; int ntok = 0; char *source = NULL;
    ASTNode *ast = front_end(input_file, verbose, &tokens, &ntok, &source);
    if (!ast) return 1;

    char out_with_ext[512];
    with_exe_suffix(output_name, out_with_ext, sizeof(out_with_ext));

    int rc = 1;
    int used_native = 0;

    if (SUB_NATIVE_BACKEND && backend != BACKEND_VIA_C) {
        if (verbose) printf("[4/4] Emitting machine code...\n");
        char why[256] = "";
        if (native_compile(ast, out_with_ext, why, sizeof(why)) == 0) {
            rc = 0;
            used_native = 1;
        } else if (backend == BACKEND_NATIVE) {
            /* The message already names the backend, so do not prefix it. */
            fprintf(stderr, "%s\n", why);
            fprintf(stderr, "Drop --native to build this program through the "
                            "C backend instead.\n");
            parser_free_ast(ast); lexer_free_tokens(tokens, ntok); free(source);
            return 1;
        } else {
            /* Falling back silently would make it impossible to tell whether
               a build needed a C compiler, so say so. */
            fprintf(stderr, "Note: %s\n", why);
            fprintf(stderr, "Note: falling back to the C backend (%s).\n",
                    sub_host_cc());
        }
    } else if (backend == BACKEND_NATIVE) {
        fprintf(stderr, "The native backend is not available for this host "
                        "(it targets x86-64 Linux).\n");
        parser_free_ast(ast); lexer_free_tokens(tokens, ntok); free(source);
        return 1;
    }

    if (rc != 0) rc = build_via_c(ast, out_with_ext, verbose, opt_level);

    parser_free_ast(ast);
    lexer_free_tokens(tokens, ntok);
    free(source);

    if (rc == 0)
        printf("\u2705 Compiled: %s%s\n", out_with_ext,
               used_native ? "  (native backend, no C compiler used)" : "");
    return rc;
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
    char *content = malloc((size_t)size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(content, 1, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        fprintf(stderr, "Error: Failed to read complete file %s\n", filename);
        free(content);
        return NULL;
    }
    content[read_size] = '\0';
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
    infer_function_signatures(ast);
    if (strcasecmp(name, "c") == 0) return codegen_generate(ast, sub_host_platform());
    if (strcasecmp(name, "cpp") == 0 || strcasecmp(name, "c++") == 0) return codegen_cpp_generate(ast, source);
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
    sub_console_init_utf8();
    printf(SUB_LOGO);

    if (argc < 2) {
        print_usage_native(argv[0]);
        return 1;
    }

    /* Asking for help is not an error, so it exits 0 - `subc --help` used to
       report failure, which makes it unusable as a CI liveness check. */
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage_native(argv[0]);
        return 0;
    }
    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        printf("subc %s\n", SUB_VERSION);
        return 0;
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
        if (!tokens) { free(source); return 1; }
        ASTNode *ast = parser_parse(tokens, token_count);
        if (!ast) {
            fprintf(stderr, "Parsing failed.\n");
            free(source);
            lexer_free_tokens(tokens, token_count);
            return 1;
        }
        
        if (!semantic_analyze(ast)) {
            fprintf(stderr, "Semantic analysis failed\n");
            free(source);
            lexer_free_tokens(tokens, token_count);
            parser_free_ast(ast);
            return 1;
        }
        
        char *output_code = NULL;
        if (target->kind == TARGET_KIND_PLATFORM) {
            infer_function_signatures(ast);
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
            char bin_name[512];
            const char *bin_ext = (target->platform == PLATFORM_WINDOWS) ? ".exe" : "";
            snprintf(bin_name, sizeof(bin_name), "%s%s", base_name, bin_ext);
            printf("\nCompiling intermediate C code to native machine code...\n");
            int ret = run_cc(sub_host_cc(), "-O2", bin_name, output_file);
            if (ret == 0) {
                printf("\u2705 Machine code compiled successfully: ./%s\n", bin_name);
            } else {
                fprintf(stderr, "Warning: %s compilation failed. Make sure it "
                                "is installed.\n", sub_host_cc());
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
    Backend backend = BACKEND_AUTO;
    
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
        } else if (strcmp(argv[i], "--native") == 0) {
            backend = BACKEND_NATIVE;
        } else if (strcmp(argv[i], "--via-c") == 0) {
            backend = BACKEND_VIA_C;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    char output_name[512];
    derive_output_name(input_file, user_out, output_name, sizeof(output_name));

    if (verbose) {
        printf("[Input]   %s\n", input_file);
        printf("[Mode]    %s\n",
               backend == BACKEND_VIA_C ? "C backend + host compiler" :
               SUB_NATIVE_BACKEND       ? "direct machine code" :
                                          "C backend + host compiler");
        printf("[Output]  %s\n\n", output_name);
    }
    
    return compile_to_native(input_file, output_name, verbose, opt_level, backend);
}
