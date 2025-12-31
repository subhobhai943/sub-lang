/* ========================================
   SUB Language - Rust Code Generator
   Generates Rust code from AST
   File: codegen_rust.h
   ======================================== */

#ifndef CODEGEN_RUST_H
#define CODEGEN_RUST_H

#include "sub_compiler.h"
#include <stdio.h>

/* Rust code generation context */
typedef struct {
    FILE *output;
    int indent_level;
    int label_counter;
} RustContext;

/* Main code generation functions */
RustContext* rust_context_create(FILE *output);
void rust_context_free(RustContext *ctx);

/* Generate complete program */
char* codegen_rust(ASTNode *ast, const char *source);

/* Generate individual nodes */
void rust_generate_node(RustContext *ctx, ASTNode *node);

/* Helper functions */
void rust_indent(RustContext *ctx);
const char* rust_get_type(DataType type);
void rust_emit(RustContext *ctx, const char *format, ...);

#endif /* CODEGEN_RUST_H */
