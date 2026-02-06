/* ========================================
   SUB Language - Native Code Generator
   Generates machine code from IR
   File: codegen_native.h
   ======================================== */

#ifndef SUB_CODEGEN_NATIVE_H
#define SUB_CODEGEN_NATIVE_H

#include "ir.h"
#include <stddef.h>

typedef enum {
    NATIVE_TARGET_X86_64,
    NATIVE_TARGET_X86_32,
    NATIVE_TARGET_ARM64,
    NATIVE_TARGET_ARM32,
    NATIVE_TARGET_RISCV64
} NativeTarget;

typedef enum {
    NATIVE_FORMAT_ELF,      // Linux
    NATIVE_FORMAT_PE,       // Windows
    NATIVE_FORMAT_MACHO,    // macOS
    NATIVE_FORMAT_RAW       // Raw machine code
} NativeFormat;

typedef struct {
    NativeTarget target;
    NativeFormat format;
    int optimize_level;     // 0-3
    bool debug_info;
    bool position_independent;
} NativeCodegenOptions;

/* Generate native code from IR */
char* codegen_native_generate(IRModule *module, NativeCodegenOptions *options);

/* Generate assembly code (intermediate step) */
char* codegen_native_generate_asm(IRModule *module, NativeTarget target);

/* Write binary executable */
bool codegen_native_write_executable(const char *filename, const char *machine_code, 
                                     size_t code_size, NativeFormat format);

/* Helper: Get native target for current platform */
NativeTarget codegen_native_get_host_target(void);
NativeFormat codegen_native_get_host_format(void);

#endif /* SUB_CODEGEN_NATIVE_H */
