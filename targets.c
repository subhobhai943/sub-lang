#define _GNU_SOURCE
#include "targets.h"
#include "windows_compat.h"

#ifndef _WIN32
#include <strings.h>
#endif

static LanguageInfo language_info_table[] = {
    {"c",          ".c",     "gcc",        "gcc output.c -o program && ./program"},
    {"cpp",        ".cpp",   "g++",        "g++ -std=c++17 output.cpp -o program && ./program"},
    {"cpp17",      ".cpp",   "g++",        "g++ -std=c++17 output.cpp -o program && ./program"},
    {"cpp20",      ".cpp",   "g++",        "g++ -std=c++20 output.cpp -o program && ./program"},
    {"python",     ".py",    "python3",    "python3 output.py"},
    {"java",       ".java",  "javac",      "javac SubProgram.java && java SubProgram"},
    {"swift",      ".swift", "swiftc",     "swiftc output.swift -o program && ./program"},
    {"kotlin",     ".kt",    "kotlinc",    "kotlinc output.kt -include-runtime -d output.jar && java -jar output.jar"},
    {"rust",       ".rs",    "rustc",      "rustc output.rs && ./output"},
    {"javascript", ".js",    "node",       "node output.js"},
    {"typescript", ".ts",    "tsc",        "tsc output.ts && node output.js"},
    {"go",         ".go",    "go",         "go run output.go"},
    {"assembly",   ".asm",   "nasm",       "nasm -f elf64 output.asm && ld output.o -o program && ./program"},
    {"css",        ".css",   "(browser)",  "(open in browser)"},
    {"ruby",       ".rb",    "ruby",       "ruby output.rb"},
    {"llvm",       ".ll",    "llc",        "llc output.ll && gcc output.s -o program"},
    {"wasm",       ".wasm",  "(browser)",  "(load in WebAssembly)"},
};

extern char* codegen_generate_c(ASTNode *ast, Platform platform);
extern char* codegen_rust(ASTNode *ast, const char *source);
extern char* codegen_python(ASTNode *ast, const char *source);
extern char* codegen_java(ASTNode *ast, const char *source);
extern char* codegen_swift(ASTNode *ast, const char *source);
extern char* codegen_kotlin(ASTNode *ast, const char *source);
extern char* codegen_javascript(ASTNode *ast, const char *source);
extern char* codegen_css(ASTNode *ast, const char *source);
extern char* codegen_ruby(ASTNode *ast, const char *source);
extern char* codegen_assembly(ASTNode *ast, const char *source);

static char* codegen_typescript(ASTNode *ast, const char *source) {
    return codegen_javascript(ast, source);
}

static char* codegen_unimplemented(ASTNode *ast, const char *source) {
    (void)ast;
    (void)source;
    return NULL;
}

#include "codegen_cpp.h"

static char* codegen_cpp_default(ASTNode *ast, const char *source) {
    CPPCodegenOptions options;
    codegen_cpp_get_default_options(CPP_VER_17, &options);
    return codegen_cpp(ast, source, &options);
}

static char* codegen_cpp_17(ASTNode *ast, const char *source) {
    CPPCodegenOptions options;
    codegen_cpp_get_default_options(CPP_VER_17, &options);
    return codegen_cpp(ast, source, &options);
}

static char* codegen_cpp_20(ASTNode *ast, const char *source) {
    CPPCodegenOptions options;
    codegen_cpp_get_default_options(CPP_VER_20, &options);
    return codegen_cpp(ast, source, &options);
}

static TargetRegistry target_registry[] = {
    {LANG_C,          "c",          NULL,                  true},
    {LANG_CPP,        "cpp",        codegen_cpp_default,   true},
    {LANG_CPP17,      "cpp17",      codegen_cpp_17,        true},
    {LANG_CPP20,      "cpp20",      codegen_cpp_20,        true},
    {LANG_PYTHON,     "python",     codegen_python,        true},
    {LANG_JAVA,       "java",       codegen_java,          true},
    {LANG_SWIFT,      "swift",      codegen_swift,         true},
    {LANG_KOTLIN,     "kotlin",     codegen_kotlin,        true},
    {LANG_RUST,       "rust",       codegen_rust,          true},
    {LANG_JAVASCRIPT, "javascript", codegen_javascript,    true},
    {LANG_TYPESCRIPT, "typescript", codegen_typescript,    true},
    {LANG_GO,         "go",         codegen_unimplemented,  false},
    {LANG_ASSEMBLY,   "assembly",   codegen_assembly,      true},
    {LANG_CSS,        "css",        codegen_css,           true},
    {LANG_RUBY,       "ruby",       codegen_ruby,          true},
    {LANG_LLVM_IR,    "llvm",       codegen_unimplemented,  false},
    {LANG_WASM,       "wasm",       codegen_unimplemented,  false},
    {LANG_COUNT,      NULL,         NULL,                  false}
};

LanguageInfo* language_info_get(TargetLanguage lang) {
    if (lang >= 0 && lang < LANG_COUNT) {
        return &language_info_table[lang];
    }
    return NULL;
}

CodegenFn get_codegen_for_target(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < LANG_COUNT; i++) {
        if (target_registry[i].name && strcasecmp(name, target_registry[i].name) == 0) {
            return target_registry[i].codegen;
        }
    }
    return NULL;
}

TargetLanguage parse_language(const char *lang_str) {
    if (!lang_str) return LANG_C;
    
    if (strcasecmp(lang_str, "c") == 0) return LANG_C;
    if (strcasecmp(lang_str, "cpp") == 0 || strcasecmp(lang_str, "c++") == 0) return LANG_CPP;
    if (strcasecmp(lang_str, "cpp17") == 0 || strcasecmp(lang_str, "c++17") == 0) return LANG_CPP17;
    if (strcasecmp(lang_str, "cpp20") == 0 || strcasecmp(lang_str, "c++20") == 0) return LANG_CPP20;
    if (strcasecmp(lang_str, "python") == 0 || strcasecmp(lang_str, "py") == 0) return LANG_PYTHON;
    if (strcasecmp(lang_str, "java") == 0) return LANG_JAVA;
    if (strcasecmp(lang_str, "swift") == 0) return LANG_SWIFT;
    if (strcasecmp(lang_str, "kotlin") == 0 || strcasecmp(lang_str, "kt") == 0) return LANG_KOTLIN;
    if (strcasecmp(lang_str, "rust") == 0 || strcasecmp(lang_str, "rs") == 0) return LANG_RUST;
    if (strcasecmp(lang_str, "javascript") == 0 || strcasecmp(lang_str, "js") == 0) return LANG_JAVASCRIPT;
    if (strcasecmp(lang_str, "typescript") == 0 || strcasecmp(lang_str, "ts") == 0) return LANG_TYPESCRIPT;
    if (strcasecmp(lang_str, "go") == 0 || strcasecmp(lang_str, "golang") == 0) return LANG_GO;
    if (strcasecmp(lang_str, "assembly") == 0 || strcasecmp(lang_str, "asm") == 0) return LANG_ASSEMBLY;
    if (strcasecmp(lang_str, "css") == 0) return LANG_CSS;
    if (strcasecmp(lang_str, "ruby") == 0 || strcasecmp(lang_str, "rb") == 0) return LANG_RUBY;
    if (strcasecmp(lang_str, "llvm") == 0) return LANG_LLVM_IR;
    if (strcasecmp(lang_str, "wasm") == 0) return LANG_WASM;
    
    return LANG_C;
}

const char* language_to_string(TargetLanguage lang) {
    if (lang >= 0 && lang < LANG_COUNT) {
        return language_info_table[lang].name;
    }
    return "unknown";
}

bool target_is_implemented(TargetLanguage lang) {
    if (lang >= 0 && lang < LANG_COUNT) {
        return target_registry[lang].implemented;
    }
    return false;
}
