#ifndef TARGETS_H
#define TARGETS_H

#include "sub_compiler.h"

typedef enum {
    LANG_C,
    LANG_CPP,
    LANG_CPP17,
    LANG_CPP20,
    LANG_PYTHON,
    LANG_JAVA,
    LANG_SWIFT,
    LANG_KOTLIN,
    LANG_RUST,
    LANG_JAVASCRIPT,
    LANG_TYPESCRIPT,
    LANG_GO,
    LANG_ASSEMBLY,
    LANG_CSS,
    LANG_RUBY,
    LANG_LLVM_IR,
    LANG_WASM,
    LANG_COUNT
} TargetLanguage;

typedef struct {
    const char *name;
    const char *extension;
    const char *compiler;
    const char *run_command;
} LanguageInfo;

typedef char* (*CodegenFn)(ASTNode*, const char*);

typedef struct {
    TargetLanguage lang;
    const char *name;
    CodegenFn codegen;
    bool implemented;
} TargetRegistry;

LanguageInfo* language_info_get(TargetLanguage lang);
CodegenFn get_codegen_for_target(const char *name);
TargetLanguage parse_language(const char *lang_str);
const char* language_to_string(TargetLanguage lang);
bool target_is_implemented(TargetLanguage lang);

#endif
