/* ========================================
   SUB Language - IR Implementation
   Converts AST to intermediate representation
   File: ir.c
   ======================================== */

#define _GNU_SOURCE
#include "ir.h"
#include "sub_compiler.h"
#include "windows_compat.h"
#include "codegen_x64.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* ========================================
   Symbol Table Implementation
   ======================================== */

IRSymbolTable* ir_symbol_table_create(IRSymbolTable *parent) {
    IRSymbolTable *table = calloc(1, sizeof(IRSymbolTable));
    table->parent = parent;
    table->head = NULL;
    // Stack grows down. First local is at -8, next at -16, etc.
    table->current_offset = parent ? parent->current_offset : 0;
    return table;
}

void ir_symbol_table_free(IRSymbolTable *table) {
    if (!table) return;
    IRSymbol *current = table->head;
    while (current) {
        IRSymbol *next = current->next;
        free(current->name);
        free(current);
        current = next;
    }
    free(table);
}

IRSymbol* ir_symbol_table_add(IRSymbolTable *table, const char *name, IRType type) {
    if (!table || !name) return NULL;
    IRSymbol *sym = calloc(1, sizeof(IRSymbol));
    sym->name = strdup(name);
    sym->type = type;
    // Each local variable is 8 bytes (for simplicity)
    table->current_offset -= 8;
    sym->stack_offset = table->current_offset;
    
    // Add to head of list
    sym->next = table->head;
    table->head = sym;
    
    return sym;
}

IRSymbol* ir_symbol_table_lookup(IRSymbolTable *table, const char *name) {
    if (!table || !name) return NULL;
    IRSymbolTable *current_scope = table;
    while (current_scope) {
        IRSymbol *sym = current_scope->head;
        while (sym) {
            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
            sym = sym->next;
        }
        current_scope = current_scope->parent;
    }
    return NULL;
}


/* ========================================
   IR Creation and Management
   ======================================== */

IRModule* ir_module_create(void) {
    IRModule *module = calloc(1, sizeof(IRModule));
    module->entry_point = strdup("main");
    return module;
}

void ir_module_free(IRModule *module) {
    if (!module) return;
    
    IRFunction *func = module->functions;
    while (func) {
        IRFunction *next = func->next;
        IRInstruction *instr = func->instructions;
        while (instr) {
            IRInstruction *next_instr = instr->next;
            if (instr->dest) free(instr->dest);
            if (instr->src1) free(instr->src1);
            if (instr->src2) free(instr->src2);
            free(instr);
            instr = next_instr;
        }
        ir_symbol_table_free(func->sym_table);
        free(func->name);
        free(func);
        func = next;
    }
    
    for (int i = 0; i < module->string_count; i++) {
        free(module->string_literals[i]);
    }
    free(module->string_literals);
    free(module->entry_point);
    free(module);
}

IRFunction* ir_function_create(const char *name, IRType return_type, IRSymbolTable *parent_scope) {
    IRFunction *func = calloc(1, sizeof(IRFunction));
    func->name = strdup(name);
    func->return_type = return_type;
    func->sym_table = ir_symbol_table_create(parent_scope);
    return func;
}

void ir_function_add_instruction(IRFunction *func, IRInstruction *instr) {
    if (!func->instructions) {
        func->instructions = instr;
    } else {
        IRInstruction *last = func->instructions;
        while (last->next) last = last->next;
        last->next = instr;
    }
}

IRInstruction* ir_instruction_create(IROpcode opcode) {
    IRInstruction *instr = calloc(1, sizeof(IRInstruction));
    instr->opcode = opcode;
    return instr;
}

IRValue* ir_value_create_int(int64_t value) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_INT;
    val->kind = IR_VAL_CONST;
    val->data.int_val = value;
    return val;
}

IRValue* ir_value_create_string_literal(IRModule *module, const char *value) {
    // Add string to module's literal pool
    module->string_literals = realloc(module->string_literals, sizeof(char*) * (module->string_count + 1));
    char *str_label = malloc(32);
    sprintf(str_label, ".LC%d", module->string_count);
    module->string_literals[module->string_count] = strdup(value);
    module->string_count++;
    
    // Return a value representing the label
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_STRING;
    val->kind = IR_VAL_LABEL;
    val->data.label = str_label;
    return val;
}

IRValue* ir_value_create_reg(int reg_num, IRType type) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = type;
    val->kind = IR_VAL_REG;
    val->data.reg_num = reg_num;
    return val;
}

IRValue* ir_value_create_label(const char *label) {
    IRValue *val = calloc(1, sizeof(IRValue));
    val->type = IR_TYPE_LABEL;
    val->kind = IR_VAL_LABEL;
    val->data.label = strdup(label);
    return val;
}

/* ========================================
   AST to IR Conversion
   ======================================== */

// Forward declaration
static void ir_generate_from_ast_node(IRModule *module, IRFunction *func, ASTNode *node);

IRModule* ir_generate_from_ast(void *ast_root) {
    if (!ast_root) return NULL;
    
    ASTNode *root = (ASTNode*)ast_root;
    IRModule *module = ir_module_create();
    IRSymbolTable *global_scope = ir_symbol_table_create(NULL);
    
    // Create main function
    IRFunction *main_func = ir_function_create("main", IR_TYPE_INT, global_scope);
    module->functions = main_func;
    
    // First pass: find all function declarations and create IRFunctions
    if (root->type == AST_PROGRAM) {
        for (ASTNode *stmt = root->left; stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL && stmt->value) {
                if (strcmp(stmt->value, "main") != 0) {
                    IRFunction *func = ir_function_create(stmt->value, IR_TYPE_INT, global_scope); // TODO: return type
                    // Add to module
                    func->next = module->functions;
                    module->functions = func;
                }
            }
        }
    }

    // Second pass: generate code for all functions
    for (IRFunction *func = module->functions; func != NULL; func = func->next) {
        ASTNode *func_ast = NULL;
        // Find the AST node for this function
        if (strcmp(func->name, "main") == 0) {
            func_ast = root; // Process whole program for main
        } else {
            for (ASTNode *stmt = root->left; stmt != NULL; stmt = stmt->next) {
                if (stmt->type == AST_FUNCTION_DECL && stmt->value && strcmp(stmt->value, func->name) == 0) {
                    func_ast = stmt;
                    break;
                }
            }
        }

        if (!func_ast) continue;

        // Handle parameters for non-main functions
        if (func_ast->type == AST_FUNCTION_DECL) {
            if (func_ast->children && func_ast->child_count > 0) {
                for (int i = 0; i < func_ast->child_count; i++) {
                    ASTNode *param = func_ast->children[i];
                    if (param && param->value) {
                        ir_symbol_table_add(func->sym_table, param->value, IR_TYPE_INT); // TODO type
                        func->param_count++;
                    }
                }
            }
            // Generate body
            if (func_ast->body) {
                ir_generate_from_ast_node(module, func, func_ast->body);
            }
        } else { // For main function, iterate through program
             for (ASTNode *stmt = root->left; stmt != NULL; stmt = stmt->next) {
                // Don't generate code for function declarations in main
                if (stmt->type != AST_FUNCTION_DECL) {
                    ir_generate_from_ast_node(module, func, stmt);
                }
            }
        }
        
        // Add implicit return
        IRInstruction *ret_instr = ir_instruction_create(IR_RETURN);
        ret_instr->src1 = ir_value_create_int(0);
        ret_instr->comment = strdup(func->name);
        ir_function_add_instruction(func, ret_instr);
    }
    
    ir_symbol_table_free(global_scope);
    return module;
}

static void ir_generate_from_ast_node(IRModule *module, IRFunction *func, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (ASTNode *stmt = node->body; stmt != NULL; stmt = stmt->next) {
                ir_generate_from_ast_node(module, func, stmt);
            }
            break;

        case AST_RETURN_STMT: {
            if (node->left) {
                ir_generate_from_ast_node(module, func, node->left);
            } else {
                IRInstruction *zero = ir_instruction_create(IR_CONST_INT);
                zero->src1 = ir_value_create_int(0);
                ir_function_add_instruction(func, zero);
            }
            IRInstruction *ret = ir_instruction_create(IR_RETURN);
            ret->comment = strdup(func->name);
            ir_function_add_instruction(func, ret);
            break;
        }
            
        case AST_VAR_DECL: {
            IRSymbol *sym = ir_symbol_table_add(func->sym_table, node->value, IR_TYPE_INT); // TODO: type
            func->local_count++;
            
            if (node->right) {
                ir_generate_from_ast_node(module, func, node->right);
                IRInstruction *store = ir_instruction_create(IR_STORE);
                store->dest = ir_value_create_int(sym->stack_offset);
                ir_function_add_instruction(func, store);
            }
            break;
        }
            
        case AST_CALL_EXPR: {
            if (node->value && strcmp(node->value, "print") == 0) {
                ASTNode *arg = node->left ? node->left : (node->child_count > 0 ? node->children[0] : NULL);
                if (arg) {
                    ir_generate_from_ast_node(module, func, arg);
                    IRInstruction *print = ir_instruction_create(IR_PRINT);
                    // Pass type info to print
                    if (arg->data_type == TYPE_STRING) {
                         print->src2 = ir_value_create_int(IR_TYPE_STRING);
                    } else {
                         print->src2 = ir_value_create_int(IR_TYPE_INT);
                    }
                    ir_function_add_instruction(func, print);
                }
            } else {
                // Push arguments onto stack in reverse order
                for (int i = node->child_count - 1; i >= 0; i--) {
                    ir_generate_from_ast_node(module, func, node->children[i]);
                    IRInstruction *push = ir_instruction_create(IR_PUSH);
                    ir_function_add_instruction(func, push);
                }
                
                IRInstruction *call = ir_instruction_create(IR_CALL);
                call->dest = ir_value_create_label(node->value);
                call->src1 = ir_value_create_int(node->child_count);
                ir_function_add_instruction(func, call);

                // After call, result is in RAX. If there were stack args, clean them up.
                if (node->child_count > 0) {
                    IRInstruction *add_rsp = ir_instruction_create(IR_ADD);
                    add_rsp->src1 = ir_value_create_reg(X64_REG_RSP, IR_TYPE_POINTER); // Not really how this works
                    add_rsp->src2 = ir_value_create_int(node->child_count * 8);
                    // This is a hack. We need a dedicated IR op for stack cleanup.
                    // For now, codegen will handle it.
                }
            }
            break;
        }
            
        case AST_BINARY_EXPR: {
            if (node->value && strcmp(node->value, "=") == 0) {
                if (node->left && node->left->type == AST_IDENTIFIER) {
                    IRSymbol *sym = ir_symbol_table_lookup(func->sym_table, node->left->value);
                    if (sym) {
                        ir_generate_from_ast_node(module, func, node->right);
                        IRInstruction *store = ir_instruction_create(IR_STORE);
                        store->dest = ir_value_create_int(sym->stack_offset);
                        ir_function_add_instruction(func, store);
                    } else {
                        fprintf(stderr, "Error: Assignment to undeclared variable '%s'\n", node->left->value);
                    }
                }
                break;
            }

            ir_generate_from_ast_node(module, func, node->left);
            IRInstruction *push = ir_instruction_create(IR_PUSH);
            ir_function_add_instruction(func, push);
            
            ir_generate_from_ast_node(module, func, node->right);
            
            IROpcode op = IR_ADD;
            if (node->value) {
                if (strcmp(node->value, "+") == 0) op = IR_ADD;
                else if (strcmp(node->value, "-") == 0) op = IR_SUB;
                else if (strcmp(node->value, "*") == 0) op = IR_MUL;
                else if (strcmp(node->value, "/") == 0) op = IR_DIV;
                else if (strcmp(node->value, "==") == 0) op = IR_EQ;
                else if (strcmp(node->value, "!=") == 0) op = IR_NE;
                else if (strcmp(node->value, "<") == 0) op = IR_LT;
                else if (strcmp(node->value, "<=") == 0) op = IR_LE;
                else if (strcmp(node->value, ">") == 0) op = IR_GT;
                else if (strcmp(node->value, ">=") == 0) op = IR_GE;
            }
            
            IRInstruction *bin_op = ir_instruction_create(op);
            ir_function_add_instruction(func, bin_op);
            break;
        }
            
        case AST_LITERAL: {
            IRInstruction *load_const = ir_instruction_create(IR_CONST_INT);
            if (node->data_type == TYPE_STRING) {
                load_const->src1 = ir_value_create_string_literal(module, node->value);
            } else { // INT, BOOL
                load_const->src1 = ir_value_create_int(atoll(node->value));
            }
            ir_function_add_instruction(func, load_const);
            break;
        }
            
        case AST_IDENTIFIER: {
            IRSymbol *sym = ir_symbol_table_lookup(func->sym_table, node->value);
            if (sym) {
                IRInstruction *load = ir_instruction_create(IR_LOAD);
                load->src1 = ir_value_create_int(sym->stack_offset);
                ir_function_add_instruction(func, load);
            } else {
                fprintf(stderr, "Error: Use of undeclared variable '%s'\n", node->value);
            }
            break;
        }

        case AST_IF_STMT: {
            static int if_counter = 0;
            int current_if = if_counter++;
            char else_label[32], end_label[32];
            sprintf(else_label, "L_ELSE_%d", current_if);
            sprintf(end_label, "L_END_IF_%d", current_if);

            ir_generate_from_ast_node(module, func, node->condition);
            
            IRInstruction *jump_if_false = ir_instruction_create(IR_JUMP_IF_NOT);
            jump_if_false->dest = ir_value_create_label(node->right ? else_label : end_label);
            ir_function_add_instruction(func, jump_if_false);
            
            ir_generate_from_ast_node(module, func, node->body);
            
            if (node->right) {
                IRInstruction *jump_to_end = ir_instruction_create(IR_JUMP);
                jump_to_end->dest = ir_value_create_label(end_label);
                ir_function_add_instruction(func, jump_to_end);

                IRInstruction *else_label_instr = ir_instruction_create(IR_LABEL);
                else_label_instr->dest = ir_value_create_label(else_label);
                ir_function_add_instruction(func, else_label_instr);
                
                ir_generate_from_ast_node(module, func, node->right);
            }
            
            IRInstruction *end_label_instr = ir_instruction_create(IR_LABEL);
            end_label_instr->dest = ir_value_create_label(end_label);
            ir_function_add_instruction(func, end_label_instr);
            break;
        }
            
        case AST_WHILE_STMT: {
            static int loop_counter = 0;
            int current_loop = loop_counter++;
            char start_label[32], end_label[32];
            sprintf(start_label, "L_WHILE_START_%d", current_loop);
            sprintf(end_label, "L_WHILE_END_%d", current_loop);

            IRInstruction *start_label_instr = ir_instruction_create(IR_LABEL);
            start_label_instr->dest = ir_value_create_label(start_label);
            ir_function_add_instruction(func, start_label_instr);
            
            ir_generate_from_ast_node(module, func, node->condition);
            
            IRInstruction *jump_if_false = ir_instruction_create(IR_JUMP_IF_NOT);
            jump_if_false->dest = ir_value_create_label(end_label);
            ir_function_add_instruction(func, jump_if_false);
            
            ir_generate_from_ast_node(module, func, node->body);
            
            IRInstruction *jump_to_start = ir_instruction_create(IR_JUMP);
            jump_to_start->dest = ir_value_create_label(start_label);
            ir_function_add_instruction(func, jump_to_start);
            
            IRInstruction *end_label_instr = ir_instruction_create(IR_LABEL);
            end_label_instr->dest = ir_value_create_label(end_label);
            ir_function_add_instruction(func, end_label_instr);
            break;
        }

        default:
            // Fallback for unhandled nodes
            if (node->left) ir_generate_from_ast_node(module, func, node->left);
            if (node->right) ir_generate_from_ast_node(module, func, node->right);
            for (int i = 0; i < node->child_count; i++) {
                ir_generate_from_ast_node(module, func, node->children[i]);
            }
            break;
    }
}

void ir_optimize(IRModule *module) {
    (void)module;
}

void ir_print(IRModule *module) {
    if (!module) return;
    
    printf("\n=== IR Module ===\n");
    printf("Entry point: %s\n", module->entry_point);
    
    for (int i = 0; i < module->string_count; i++) {
        printf("String %d (.LC%d): \"%s\"\n", i, i, module->string_literals[i]);
    }
    
    for (IRFunction *func = module->functions; func != NULL; func = func->next) {
        printf("\nFunction: %s (%d params, %d locals)\n", func->name, func->param_count, func->local_count);
        printf("  Instructions:\n");
        
        for (IRInstruction *instr = func->instructions; instr != NULL; instr = instr->next) {
            printf("    ");
            switch (instr->opcode) {
                case IR_ADD: printf("ADD"); break;
                case IR_SUB: printf("SUB"); break;
                case IR_MUL: printf("MUL"); break;
                case IR_DIV: printf("DIV"); break;
                case IR_CONST_INT: 
                    if (instr->src1->kind == IR_VAL_LABEL) {
                        printf("CONST_STR %s", instr->src1->data.label);
                    } else {
                        printf("CONST_INT %" PRId64, instr->src1->data.int_val); 
                    }
                    break;
                case IR_ALLOC: printf("ALLOC"); break; // No-op, just for sym table
                case IR_STORE: printf("STORE [rbp-%" PRId64 "]", -instr->dest->data.int_val); break;
                case IR_LOAD: printf("LOAD [rbp-%" PRId64 "]", -instr->src1->data.int_val); break;
                case IR_PUSH: printf("PUSH"); break;
                case IR_POP: printf("POP"); break;
                case IR_PRINT: printf("PRINT"); break;
                case IR_RETURN: printf("RETURN"); break;
                case IR_EQ: printf("EQ"); break;
                case IR_NE: printf("NE"); break;
                case IR_LT: printf("LT"); break;
                case IR_LE: printf("LE"); break;
                case IR_GT: printf("GT"); break;
                case IR_GE: printf("GE"); break;
                case IR_JUMP: printf("JUMP %s", instr->dest->data.label); break;
                case IR_JUMP_IF_NOT: printf("JUMP_IF_NOT %s", instr->dest->data.label); break;
                case IR_LABEL: printf("%s:", instr->dest->data.label); break;
                case IR_CALL: 
                    printf("CALL %s (%" PRId64 " args)", instr->dest->data.label, instr->src1 ? instr->src1->data.int_val : 0); 
                    break;
                default: printf("UNKNOWN (%d)", instr->opcode); break;
            }
            printf("\n");
        }
    }
}
