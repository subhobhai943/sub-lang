/* ========================================
   SUB Language - x86-64 Native Code Generator
   Generates native machine code for x86-64 architecture
   File: codegen_x64.h
   ======================================== */

#ifndef CODEGEN_X64_H
#define CODEGEN_X64_H

#include "ir.h"
#include <stdio.h>

/* x86-64 Register allocation */
typedef enum {
    X64_REG_RAX = 0,  // Accumulator, return value
    X64_REG_RBX,      // Base register, callee-saved
    X64_REG_RCX,      // Counter, 4th argument
    X64_REG_RDX,      // Data, 3rd argument
    X64_REG_RSI,      // Source index, 2nd argument
    X64_REG_RDI,      // Destination index, 1st argument
    X64_REG_RBP,      // Base pointer
    X64_REG_RSP,      // Stack pointer
    X64_REG_R8,       // 5th argument
    X64_REG_R9,       // 6th argument
    X64_REG_R10,      // Temporary
    X64_REG_R11,      // Temporary
    X64_REG_R12,      // Callee-saved
    X64_REG_R13,      // Callee-saved
    X64_REG_R14,      // Callee-saved
    X64_REG_R15,      // Callee-saved
    X64_REG_COUNT
} X64Register;

/* Code generation context */
typedef struct {
    FILE *output;              // Output file
    int label_counter;         // For generating unique labels
    int string_counter;        // For string literals
    int stack_offset;          // Current stack offset
    bool reg_in_use[X64_REG_COUNT]; // Register allocation tracker
    IRFunction *current_func;   // Current function being generated
} X64Context;

/* Main code generation functions */
X64Context* x64_context_create(FILE *output);
void x64_context_free(X64Context *ctx);

/* Generate complete program */
void x64_generate_program(X64Context *ctx, IRModule *module);

/* Generate function */
void x64_generate_function(X64Context *ctx, IRFunction *func);

/* Generate instruction */
void x64_generate_instruction(X64Context *ctx, IRInstruction *instr);

/* Helper functions */
const char* x64_register_name(X64Register reg, bool is_64bit);
X64Register x64_alloc_register(X64Context *ctx);
void x64_free_register(X64Context *ctx, X64Register reg);
int x64_generate_label(X64Context *ctx);

/* Assembly emission helpers */
void x64_emit(X64Context *ctx, const char *format, ...);
void x64_emit_comment(X64Context *ctx, const char *comment);
void x64_emit_label(X64Context *ctx, const char *label);

#endif /* CODEGEN_X64_H */
