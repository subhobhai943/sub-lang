/* ========================================
   SUB Language - Native Code Generator Implementation
   Generates x86-64 assembly and machine code from IR
   File: codegen_native.c
   ======================================== */

#include "codegen_native.h"
#include "ir.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "codegen_x64.h"

/* Assembly code buffer */
typedef struct {
    char *code;
    size_t size;
    size_t capacity;
} AsmBuffer;

static AsmBuffer* asm_buffer_create(void) {
    AsmBuffer *buf = malloc(sizeof(AsmBuffer));
    buf->capacity = 16384;
    buf->size = 0;
    buf->code = malloc(buf->capacity);
    buf->code[0] = '\0';
    return buf;
}

static void asm_buffer_append(AsmBuffer *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    while (buf->size + needed + 1 > buf->capacity) {
        buf->capacity *= 2;
        buf->code = realloc(buf->code, buf->capacity);
    }
    
    vsnprintf(buf->code + buf->size, needed + 1, fmt, args);
    buf->size += needed;
    va_end(args);
}

static char* asm_buffer_to_string(AsmBuffer *buf) {
    char *result = strdup(buf->code);
    free(buf->code);
    free(buf);
    return result;
}

/* Get host platform target */
NativeTarget codegen_native_get_host_target(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return NATIVE_TARGET_X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    return NATIVE_TARGET_X86_32;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return NATIVE_TARGET_ARM64;
#elif defined(__arm__) || defined(_M_ARM)
    return NATIVE_TARGET_ARM32;
#elif defined(__riscv) && (__riscv_xlen == 64)
    return NATIVE_TARGET_RISCV64;
#else
    return NATIVE_TARGET_X86_64; // Default
#endif
}

NativeFormat codegen_native_get_host_format(void) {
#if defined(_WIN32)
    return NATIVE_FORMAT_PE;
#elif defined(__APPLE__)
    return NATIVE_FORMAT_MACHO;
#elif defined(__linux__)
    return NATIVE_FORMAT_ELF;
#else
    return NATIVE_FORMAT_RAW;
#endif
}

/* x86-64 Register allocation */
static const char* x86_64_get_reg(int reg_num) {
    static const char *regs[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", 
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };
    return regs[reg_num % 14];
}

/* Generate x86-64 assembly from IR */
static void codegen_x86_64_function(AsmBuffer *buf, IRFunction *func) {
    // Function prologue
    asm_buffer_append(buf, "\n; Function: %s\n", func->name);
    asm_buffer_append(buf, "%s:\n", func->name);
    asm_buffer_append(buf, "    push rbp\n");
    asm_buffer_append(buf, "    mov rbp, rsp\n");
    
    // Allocate stack space for locals
    if (func->local_count > 0) {
        asm_buffer_append(buf, "    sub rsp, %d\n", func->local_count * 8);
    }
    
    // Generate code for each instruction
    IRInstruction *instr = func->instructions;
    while (instr) {
        switch (instr->opcode) {
            case IR_FUNC_START:
            case IR_FUNC_END:
                // Markers, no code
                break;
                
            case IR_MOVE:
                if (instr->src1 && instr->dest) {
                    if (instr->src1->type == IR_TYPE_INT) {
                        asm_buffer_append(buf, "    mov %s, %lld\n", 
                            x86_64_get_reg(instr->dest->data.reg_num),
                            (long long)instr->src1->data.int_val);
                    }
                }
                break;
                
            case IR_ADD:
                if (instr->src1 && instr->src2 && instr->dest) {
                    // Load operands into registers
                    if (instr->src1->type == IR_TYPE_INT) {
                        asm_buffer_append(buf, "    mov rax, %lld\n", 
                            (long long)instr->src1->data.int_val);
                    } else {
                        asm_buffer_append(buf, "    mov rax, %s\n", 
                            x86_64_get_reg(instr->src1->data.reg_num));
                    }
                    
                    if (instr->src2->type == IR_TYPE_INT) {
                        asm_buffer_append(buf, "    add rax, %lld\n", 
                            (long long)instr->src2->data.int_val);
                    } else {
                        asm_buffer_append(buf, "    add rax, %s\n", 
                            x86_64_get_reg(instr->src2->data.reg_num));
                    }
                    
                    asm_buffer_append(buf, "    mov %s, rax\n", 
                        x86_64_get_reg(instr->dest->data.reg_num));
                }
                break;
                
            case IR_SUB:
                // Similar to ADD but with sub instruction
                if (instr->src1 && instr->src2 && instr->dest) {
                    asm_buffer_append(buf, "    mov rax, %s\n", 
                        x86_64_get_reg(instr->src1->data.reg_num));
                    asm_buffer_append(buf, "    sub rax, %s\n", 
                        x86_64_get_reg(instr->src2->data.reg_num));
                    asm_buffer_append(buf, "    mov %s, rax\n", 
                        x86_64_get_reg(instr->dest->data.reg_num));
                }
                break;
                
            case IR_MUL:
                if (instr->src1 && instr->src2 && instr->dest) {
                    asm_buffer_append(buf, "    mov rax, %s\n", 
                        x86_64_get_reg(instr->src1->data.reg_num));
                    asm_buffer_append(buf, "    imul rax, %s\n", 
                        x86_64_get_reg(instr->src2->data.reg_num));
                    asm_buffer_append(buf, "    mov %s, rax\n", 
                        x86_64_get_reg(instr->dest->data.reg_num));
                }
                break;
                
            case IR_CALL:
                // Function call (simplified - assumes C calling convention)
                if (instr->dest && instr->dest->data.string_val) {
                    // For now, handle print specially
                    if (strcmp(instr->dest->data.string_val, "print") == 0 ||
                        strcmp(instr->dest->data.string_val, "printf") == 0) {
                        // Call printf from libc
                        if (instr->src1) {
                            if (instr->src1->type == IR_TYPE_STRING) {
                                asm_buffer_append(buf, "    lea rdi, [rel .str%d]\n", 0);
                            } else {
                                asm_buffer_append(buf, "    mov rdi, %s\n", 
                                    x86_64_get_reg(instr->src1->data.reg_num));
                            }
                        }
                        asm_buffer_append(buf, "    call printf\n");
                    } else {
                        asm_buffer_append(buf, "    call %s\n", instr->dest->data.string_val);
                    }
                }
                break;
                
            case IR_RETURN:
                if (instr->src1) {
                    if (instr->src1->type == IR_TYPE_INT) {
                        asm_buffer_append(buf, "    mov rax, %lld\n", 
                            (long long)instr->src1->data.int_val);
                    } else {
                        asm_buffer_append(buf, "    mov rax, %s\n", 
                            x86_64_get_reg(instr->src1->data.reg_num));
                    }
                }
                break;
                
            case IR_LABEL:
                if (instr->dest && instr->dest->data.label) {
                    asm_buffer_append(buf, "%s:\n", instr->dest->data.label);
                }
                break;
                
            case IR_JUMP:
                if (instr->src1 && instr->src1->data.label) {
                    asm_buffer_append(buf, "    jmp %s\n", instr->src1->data.label);
                }
                break;
                
            case IR_JUMP_IF_NOT:
                if (instr->src1 && instr->src2) {
                    asm_buffer_append(buf, "    cmp %s, 0\n", 
                        x86_64_get_reg(instr->src1->data.reg_num));
                    asm_buffer_append(buf, "    je %s\n", instr->src2->data.label);
                }
                break;
                
            default:
                asm_buffer_append(buf, "    ; TODO: opcode %d\n", instr->opcode);
                break;
        }
        
        instr = instr->next;
    }
    
    // Function epilogue
    asm_buffer_append(buf, "    mov rsp, rbp\n");
    asm_buffer_append(buf, "    pop rbp\n");
    asm_buffer_append(buf, "    ret\n");
}

/* Generate assembly code */
char* codegen_native_generate_asm(IRModule *module, NativeTarget target) {
    if (!module) return NULL;
    
    AsmBuffer *buf = asm_buffer_create();
    
    // Assembly header
    switch (target) {
        case NATIVE_TARGET_X86_64:
            asm_buffer_append(buf, "; SUB Language - Native x86-64 Assembly\n");
            asm_buffer_append(buf, "; Generated by SUB Compiler\n\n");
            
            // Platform-specific directives
#ifdef __APPLE__
            asm_buffer_append(buf, ".section __TEXT,__text,regular,pure_instructions\n");
            asm_buffer_append(buf, ".globl _main\n");
            asm_buffer_append(buf, ".p2align 4, 0x90\n");
#elif defined(_WIN32)
            asm_buffer_append(buf, ".section .text\n");
            asm_buffer_append(buf, ".globl main\n");
#else // Linux
            asm_buffer_append(buf, ".section .text\n");
            asm_buffer_append(buf, ".globl main\n");
            asm_buffer_append(buf, ".type main, @function\n");
#endif
            
            // External declarations
            asm_buffer_append(buf, ".extern printf\n\n");
            
            // Generate functions
            IRFunction *func = module->functions;
            while (func) {
                codegen_x86_64_function(buf, func);
                func = func->next;
            }
            
            // Data section for strings
            asm_buffer_append(buf, "\n.section .rodata\n");
            for (int i = 0; i < module->string_count; i++) {
                asm_buffer_append(buf, ".str%d:\n", i);
                asm_buffer_append(buf, "    .asciz \"%s\"\n", module->string_literals[i]);
            }
            
            break;
            
        default:
            asm_buffer_append(buf, "; Unsupported target architecture\n");
            break;
    }
    
    return asm_buffer_to_string(buf);
}

/* Generate native code */
char* codegen_native_generate(IRModule *module, NativeCodegenOptions *options) {
    if (!module) return NULL;
    
    // For now, return assembly code
    // TODO: Implement actual binary generation
    return codegen_native_generate_asm(module, options->target);
}

/* Write executable binary */
bool codegen_native_write_executable(const char *filename, const char *machine_code, 
                                     size_t code_size, NativeFormat format) {
    // This is a simplified version
    // In production, you would:
    // 1. Generate proper ELF/PE/Mach-O headers
    // 2. Create sections (.text, .data, .rodata)
    // 3. Set up relocation tables
    // 4. Link with runtime libraries
    
    FILE *f = fopen(filename, "wb");
    if (!f) return false;
    
    fwrite(machine_code, 1, code_size, f);
    fclose(f);
    
    // Make executable (Unix)
#ifndef _WIN32
    chmod(filename, 0755);
#endif
    
    return true;
}
