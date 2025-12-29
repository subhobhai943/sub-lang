/* ========================================
   SUB Language - IR Implementation
   File: ir.c
   ======================================== */

#include "ir.h"
#include "sub_compiler.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_reg = 0;
static int next_label = 0;

/* Create IR Module */
IRModule* ir_module_create(void) {
    IRModule *module = calloc(1, sizeof(IRModule));
    module->functions = NULL;
    module->string_literals = NULL;
    module->string_count = 0;
    module->entry_point = strdup("main");
    return module;
}

/* Free IR Module */
void ir_module_free(IRModule *module) {
    if (!module) return;
    
    // Free functions
    IRFunction *func = module->functions;
    while (func) {
        IRFunction *next = func->next;
        free(func->name);
        
        // Free instructions
        IRInstruction *instr = func->instructions;
        while (instr) {
            IRInstruction *next_instr = instr->next;
            if (instr->dest) free(instr->dest);
            if (instr->src1) free(instr->src1);
            if (instr->src2) free(instr->src2);
            if (instr->comment) free(instr->comment);
            free(instr);
            instr = next_instr;
        }
        
        free(func);
        func = next;
    }
    
    // Free string literals
    for (int i = 0; i < module->string_count; i++) {
        free(module->string_literals[i]);
    }
    free(module->string_literals);
    free(module->entry_point);
    free(module);
}

/* Create IR Function */
IRFunction* ir_function_create(const char *name, IRType return_type) {
    IRFunction *func = calloc(1, sizeof(IRFunction));
    func->name = strdup(name);
    func->return_type = return_type;
    func->params = NULL;
    func->param_count = 0;
    func->instructions = NULL;
    func->local_count = 0;
    func->reg_count = 0;
    func->next = NULL;
    return func;
}

/* Add parameter to function */
void ir_function_add_param(IRFunction *func, IRValue *param) {
    func->params = realloc(func->params, sizeof(IRValue*) * (func->param_count + 1));
    func->params[func->param_count++] = param;
}

/* Add instruction to function */
void ir_function_add_instruction(IRFunction *func, IRInstruction *instr) {
    if (!func->instructions) {
        func->instructions = instr;
    } else {
        IRInstruction *last = func->instructions;
        while (last->next) last = last->next;
        last->next = instr;
    }
}

/* Create IR Instruction */
IRInstruction* ir_instruction_create(IROpcode opcode) {
    IRInstruction *instr = calloc(1, sizeof(IRInstruction));
    instr->opcode = opcode;
    instr->dest = NULL;
    instr->src1 = NULL;
    instr->src2 = NULL;
    instr->comment = NULL;
    instr->next = NULL;
    return instr;
}

/* Create IR Values */
IRValue* ir_value_create_int(int64_t value) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_INT;
    val->data.int_val = value;
    return val;
}

IRValue* ir_value_create_float(double value) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_FLOAT;
    val->data.float_val = value;
    return val;
}

IRValue* ir_value_create_string(const char *value) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_STRING;
    val->data.string_val = strdup(value);
    return val;
}

IRValue* ir_value_create_reg(int reg_num, IRType type) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = type;
    val->data.reg_num = reg_num;
    return val;
}

IRValue* ir_value_create_label(const char *label) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_LABEL;
    val->data.label = strdup(label);
    return val;
}

/* Helper: Get next virtual register */
static int ir_get_next_reg(void) {
    return next_reg++;
}

/* Helper: Get next label */
static char* ir_get_next_label(const char *prefix) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%s%d", prefix, next_label++);
    return strdup(buffer);
}

/* Convert AST node to IR */
static IRValue* ir_gen_expr(IRFunction *func, ASTNode *node);

static void ir_gen_statement(IRFunction *func, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_VAR_DECL: {
            // #var x = 5
            IRValue *dest = ir_value_create_reg(ir_get_next_reg(), IR_TYPE_INT);
            dest->name = node->value ? strdup(node->value) : NULL;
            
            if (node->right) {
                IRValue *src = ir_gen_expr(func, node->right);
                IRInstruction *instr = ir_instruction_create(IR_MOVE);
                instr->dest = dest;
                instr->src1 = src;
                ir_function_add_instruction(func, instr);
            }
            func->local_count++;
            break;
        }
        
        case AST_RETURN_STMT: {
            IRInstruction *instr = ir_instruction_create(IR_RETURN);
            if (node->left) {
                instr->src1 = ir_gen_expr(func, node->left);
            }
            ir_function_add_instruction(func, instr);
            break;
        }
        
        case AST_IF_STMT: {
            // Generate: condition, jump_if_not else_label, then_block, jump end_label, else_label:, else_block, end_label:
            IRValue *cond = ir_gen_expr(func, node->condition);
            char *else_label = ir_get_next_label("else");
            char *end_label = ir_get_next_label("endif");
            
            IRInstruction *jump_if_not = ir_instruction_create(IR_JUMP_IF_NOT);
            jump_if_not->src1 = cond;
            jump_if_not->src2 = ir_value_create_label(else_label);
            ir_function_add_instruction(func, jump_if_not);
            
            // Then block
            if (node->body) ir_gen_statement(func, node->body);
            
            IRInstruction *jump_end = ir_instruction_create(IR_JUMP);
            jump_end->src1 = ir_value_create_label(end_label);
            ir_function_add_instruction(func, jump_end);
            
            // Else label
            IRInstruction *else_lbl = ir_instruction_create(IR_LABEL);
            else_lbl->dest = ir_value_create_label(else_label);
            ir_function_add_instruction(func, else_lbl);
            
            // Else block
            if (node->right) ir_gen_statement(func, node->right);
            
            // End label
            IRInstruction *end_lbl = ir_instruction_create(IR_LABEL);
            end_lbl->dest = ir_value_create_label(end_label);
            ir_function_add_instruction(func, end_lbl);
            
            free(else_label);
            free(end_label);
            break;
        }
        
        case AST_CALL_EXPR: {
            // Function call (e.g., print)
            IRInstruction *instr = ir_instruction_create(IR_CALL);
            instr->dest = ir_value_create_string(node->value ? node->value : "unknown");
            if (node->left) {
                instr->src1 = ir_gen_expr(func, node->left);
            }
            ir_function_add_instruction(func, instr);
            break;
        }
        
        case AST_BLOCK: {
            ASTNode *stmt = node->body;
            while (stmt) {
                ir_gen_statement(func, stmt);
                stmt = stmt->next;
            }
            break;
        }
        
        default:
            break;
    }
    
    // Handle next statement
    if (node->next) {
        ir_gen_statement(func, node->next);
    }
}

static IRValue* ir_gen_expr(IRFunction *func, ASTNode *node) {
    if (!node) return NULL;
    
    switch (node->type) {
        case AST_LITERAL: {
            // Check if it's a number or string
            if (node->value && (node->value[0] >= '0' && node->value[0] <= '9')) {
                return ir_value_create_int(atoi(node->value));
            } else {
                return ir_value_create_string(node->value);
            }
        }
        
        case AST_IDENTIFIER: {
            // Load variable - for now return a register reference
            IRValue *val = ir_value_create_reg(ir_get_next_reg(), IR_TYPE_INT);
            val->name = node->value ? strdup(node->value) : NULL;
            return val;
        }
        
        case AST_BINARY_EXPR: {
            IRValue *left = ir_gen_expr(func, node->left);
            IRValue *right = ir_gen_expr(func, node->right);
            IRValue *result = ir_value_create_reg(ir_get_next_reg(), IR_TYPE_INT);
            
            IRInstruction *instr = ir_instruction_create(IR_ADD);
            if (node->value) {
                if (strcmp(node->value, "+") == 0) instr->opcode = IR_ADD;
                else if (strcmp(node->value, "-") == 0) instr->opcode = IR_SUB;
                else if (strcmp(node->value, "*") == 0) instr->opcode = IR_MUL;
                else if (strcmp(node->value, "/") == 0) instr->opcode = IR_DIV;
                else if (strcmp(node->value, "==") == 0) instr->opcode = IR_EQ;
                else if (strcmp(node->value, "<") == 0) instr->opcode = IR_LT;
            }
            
            instr->dest = result;
            instr->src1 = left;
            instr->src2 = right;
            ir_function_add_instruction(func, instr);
            
            return result;
        }
        
        default:
            return ir_value_create_int(0);
    }
}

/* Generate IR from AST */
IRModule* ir_generate_from_ast(void *ast_root) {
    ASTNode *ast = (ASTNode*)ast_root;
    if (!ast) return NULL;
    
    IRModule *module = ir_module_create();
    
    // Create main function
    IRFunction *main_func = ir_function_create("main", IR_TYPE_INT);
    
    // Add function start marker
    IRInstruction *func_start = ir_instruction_create(IR_FUNC_START);
    ir_function_add_instruction(main_func, func_start);
    
    // Generate IR for program statements
    if (ast->type == AST_PROGRAM && ast->left) {
        ir_gen_statement(main_func, ast->left);
    }
    
    // Add implicit return 0 if not present
    IRInstruction *ret = ir_instruction_create(IR_RETURN);
    ret->src1 = ir_value_create_int(0);
    ir_function_add_instruction(main_func, ret);
    
    // Add function end marker
    IRInstruction *func_end = ir_instruction_create(IR_FUNC_END);
    ir_function_add_instruction(main_func, func_end);
    
    // Add function to module
    module->functions = main_func;
    
    return module;
}

/* Optimize IR (placeholder for now) */
void ir_optimize(IRModule *module) {
    // TODO: Implement optimizations:
    // - Constant folding
    // - Dead code elimination
    // - Register allocation
    // - Strength reduction
    (void)module;
}

/* Print IR for debugging */
void ir_print(IRModule *module) {
    if (!module) return;
    
    printf("; SUB IR Module\n\n");
    
    IRFunction *func = module->functions;
    while (func) {
        printf("function %s() -> %d {\n", func->name, func->return_type);
        printf("  ; %d registers, %d locals\n", func->reg_count, func->local_count);
        
        IRInstruction *instr = func->instructions;
        while (instr) {
            printf("  ");
            
            switch (instr->opcode) {
                case IR_FUNC_START: printf("<function start>"); break;
                case IR_FUNC_END: printf("<function end>"); break;
                case IR_MOVE: printf("move r%d, ", instr->dest->data.reg_num); break;
                case IR_ADD: printf("r%d = add ", instr->dest->data.reg_num); break;
                case IR_SUB: printf("r%d = sub ", instr->dest->data.reg_num); break;
                case IR_RETURN: printf("return "); break;
                case IR_CALL: printf("call %s", instr->dest->data.string_val); break;
                case IR_LABEL: printf("%s:", instr->dest->data.label); break;
                case IR_JUMP: printf("jump %s", instr->src1->data.label); break;
                default: printf("<opcode %d>", instr->opcode); break;
            }
            
            printf("\n");
            instr = instr->next;
        }
        
        printf("}\n\n");
        func = func->next;
    }
}
