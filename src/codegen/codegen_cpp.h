/* ========================================
   SUB Language - C++ Code Generator Header
   Generates C++ code from AST
   File: codegen_cpp.h
   ======================================== */

#ifndef SUB_CODEGEN_CPP_H
#define SUB_CODEGEN_CPP_H

#include "sub_compiler.h"

/* C++ Version Enumeration */
typedef enum {
    CPP_VER_11,
    CPP_VER_14,
    CPP_VER_17,
    CPP_VER_20,
    CPP_VER_23
} CPPVersion;

/* C++ Code Generation Options */
typedef struct {
    CPPVersion version;
    bool use_std_string;
    bool use_auto;
    bool use_concepts;
    bool use_modules;
    bool use_range_based_for;
    bool use_constexpr;
} CPPCodegenOptions;

/* Generate C++ code from AST with version support */
char* codegen_cpp(ASTNode *ast, const char *source, CPPCodegenOptions *options);

/* Get default options for a C++ version */
void codegen_cpp_get_default_options(CPPVersion version, CPPCodegenOptions *options);

/* Convert C++ version enum to string */
const char* codegen_cpp_version_to_string(CPPVersion version);

/* Parse C++ version string to enum */
CPPVersion codegen_cpp_parse_version(const char *version_str);

#endif /* SUB_CODEGEN_CPP_H */
