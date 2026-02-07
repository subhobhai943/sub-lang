/* ========================================
   SUB Language Semantic Analyzer
   File: semantic.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "type_system.h"
#include "windows_compat.h"

// Symbol table entry (local) - enhanced with DataType
typedef struct LocalSymbolEntry {
    char *name;
    char *type_str;      
    DataType data_type;  
    DataType return_type;
    DataType *param_types;
    int param_count;
    int scope_level;
    bool is_initialized;
    bool is_constant;
    bool is_function;
    struct LocalSymbolEntry *next;
} LocalSymbolEntry;

// Symbol table (local) - enhanced version
typedef struct {
    LocalSymbolEntry *head;
    int current_scope;
} LocalSymbolTable;

// Forward declaration
static DataType check_expression_type(ASTNode *node, LocalSymbolTable *table);
static void check_statement_type(ASTNode *node, LocalSymbolTable *table, LocalSymbolEntry *current_function);
/* static DataType get_symbol_type(LocalSymbolTable *table, const char *name); */
/* static void set_symbol_type(LocalSymbolTable *table, const char *name, DataType type); */
static LocalSymbolEntry* lookup_symbol_entry(LocalSymbolTable *table, const char *name);

// ========================================
// Symbol Table Management
// ========================================

static LocalSymbolTable* create_symbol_table() {
    LocalSymbolTable *table = malloc(sizeof(LocalSymbolTable));
    if (!table) {
        fprintf(stderr, "Semantic error: Failed to allocate symbol table\n");
        return NULL;
    }
    table->head = NULL;
    table->current_scope = 0;
    return table;
}

static LocalSymbolEntry* add_symbol(LocalSymbolTable *table, const char *name, const char *type_str, DataType data_type) {
    if (!table || !name) return NULL;
    LocalSymbolEntry *entry = calloc(1, sizeof(LocalSymbolEntry));
    if (!entry) {
        fprintf(stderr, "Semantic error: Failed to allocate symbol entry\n");
        return NULL;
    }
    entry->name = strdup(name);
    if (!entry->name) {
        free(entry);
        fprintf(stderr, "Semantic error: Failed to allocate symbol name\n");
        return NULL;
    }
    entry->type_str = type_str ? strdup(type_str) : NULL;
    if (type_str && !entry->type_str) {
        free(entry->name);
        free(entry);
        fprintf(stderr, "Semantic error: Failed to allocate symbol type string\n");
        return NULL;
    }
    entry->data_type = data_type;
    entry->return_type = TYPE_UNKNOWN;
    entry->param_types = NULL;
    entry->param_count = 0;
    entry->scope_level = table->current_scope;
    entry->is_initialized = false;
    entry->is_constant = false;
    entry->is_function = false;
    entry->next = table->head;
    table->head = entry;
    return entry;
}

static LocalSymbolEntry* lookup_symbol_entry(LocalSymbolTable *table, const char *name) {
    LocalSymbolEntry *current = table->head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static LocalSymbolEntry* lookup_symbol(LocalSymbolTable *table, const char *name) {
    return lookup_symbol_entry(table, name);
}

static void free_symbol_table(LocalSymbolTable *table) {
    LocalSymbolEntry *current = table->head;
    while (current) {
        LocalSymbolEntry *next = current->next;
        free(current->name);
        free(current->type_str);
        free(current->param_types);
        free(current);
        current = next;
    }
    free(table);
}

static void enter_scope(LocalSymbolTable *table) {
    if (!table) return;
    table->current_scope++;
}

static void exit_scope(LocalSymbolTable *table) {
    if (!table) return;
    int scope = table->current_scope;
    LocalSymbolEntry **current = &table->head;
    while (*current) {
        if ((*current)->scope_level == scope) {
            LocalSymbolEntry *to_remove = *current;
            *current = to_remove->next;
            free(to_remove->name);
            free(to_remove->type_str);
            free(to_remove->param_types);
            free(to_remove);
        } else {
            current = &(*current)->next;
        }
    }
    table->current_scope--;
}

// ========================================
// Type Conversion Utilities
// ========================================

static const char* data_type_to_string(DataType type) {
    switch (type) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_STRING: return "string";
        case TYPE_BOOL: return "bool";
        case TYPE_ARRAY: return "array";
        case TYPE_OBJECT: return "object";
        case TYPE_FUNCTION: return "function";
        case TYPE_NULL: return "null";
        case TYPE_VOID: return "void";
        case TYPE_AUTO: return "auto";
        default: return "unknown";
    }
}

static DataType data_type_from_string(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    if (strcmp(name, "int") == 0) return TYPE_INT;
    if (strcmp(name, "float") == 0) return TYPE_FLOAT;
    if (strcmp(name, "string") == 0) return TYPE_STRING;
    if (strcmp(name, "bool") == 0) return TYPE_BOOL;
    if (strcmp(name, "void") == 0) return TYPE_VOID;
    if (strcmp(name, "auto") == 0) return TYPE_AUTO;
    return TYPE_UNKNOWN;
}

static bool data_types_are_compatible(DataType expected, DataType actual) {
    if (expected == TYPE_AUTO || actual == TYPE_AUTO) return true;
    if (expected == TYPE_UNKNOWN || actual == TYPE_UNKNOWN) return false;
    if (expected == actual) return true;
    
    // Numeric types are compatible (int can be promoted to float)
    if (expected == TYPE_FLOAT && actual == TYPE_INT) return true;
    if (expected == TYPE_INT && actual == TYPE_FLOAT) return true;
    
    return false;
}

// ========================================
// Expression Type Checking
// ========================================

static DataType check_expression_type(ASTNode *node, LocalSymbolTable *table) {
    if (!node) return TYPE_UNKNOWN;
    
    DataType left_type, right_type, result_type;
    
    switch (node->type) {
        case AST_LITERAL:
            if (!node->value) {
                compile_error_with_col("Literal has no value", node->line, node->column);
                return TYPE_UNKNOWN;
            }
            
            if (node->value[0] == '"' || node->value[0] == '\'') {
                node->data_type = TYPE_STRING;
                return TYPE_STRING;
            }
            
            if (strcmp(node->value, "true") == 0 || strcmp(node->value, "false") == 0) {
                node->data_type = TYPE_BOOL;
                return TYPE_BOOL;
            }
            
            if (strcmp(node->value, "null") == 0 || strcmp(node->value, "nil") == 0) {
                node->data_type = TYPE_NULL;
                return TYPE_NULL;
            }
            
            // Check if numeric
            bool has_dot = false;
            bool is_number = true;
            const char *p = node->value;
            
            if (*p == '-' || *p == '+') p++;
            
            while (*p) {
                if (isdigit(*p)) {
                    // OK
                } else if (*p == '.' && !has_dot) {
                    has_dot = true;
                } else {
                    is_number = false;
                    break;
                }
                p++;
            }
            
            if (is_number && strlen(node->value) > 0) {
                node->data_type = has_dot ? TYPE_FLOAT : TYPE_INT;
                return node->data_type;
            }
            
            node->data_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
            
        case AST_IDENTIFIER:
            if (!node->value) {
                compile_error("Identifier has no name", node->line);
                return TYPE_UNKNOWN;
            }
            
            {
                LocalSymbolEntry *entry = lookup_symbol(table, node->value);
                if (!entry) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg), 
                             "Undefined variable '%s'", node->value);
                    compile_error(error_msg, node->line);
                    return TYPE_UNKNOWN;
                }
                if (!entry->is_initialized) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                             "Variable '%s' used before initialization", node->value);
                    compile_error(error_msg, node->line);
                }
                
                node->data_type = entry->data_type;
                return entry->data_type;
            }
            
        case AST_BINARY_EXPR:
            if (!node->value) {
                compile_error("Binary expression has no operator", node->line);
                return TYPE_UNKNOWN;
            }
            
            left_type = check_expression_type(node->left, table);
            right_type = check_expression_type(node->right, table);
            
            const char *op = node->value;
            
            // Arithmetic operators
            if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
                strcmp(op, "%") == 0) {
                
                // String concatenation
                if (strcmp(op, "+") == 0) {
                    if (left_type == TYPE_STRING || right_type == TYPE_STRING) {
                        if (left_type != TYPE_STRING && left_type != TYPE_INT && left_type != TYPE_FLOAT && left_type != TYPE_BOOL) {
                            char error_msg[512];
                            snprintf(error_msg, sizeof(error_msg),
                                     "Type error: Cannot convert %s to string for concatenation",
                                     data_type_to_string(left_type));
                            compile_error(error_msg, node->line);
                        }
                        node->data_type = TYPE_STRING;
                        return TYPE_STRING;
                    }
                }
                
                // Numeric operations
                if (left_type == TYPE_INT || left_type == TYPE_FLOAT) {
                    if (right_type == TYPE_INT || right_type == TYPE_FLOAT) {
                        node->data_type = (left_type == TYPE_FLOAT || right_type == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
                        return node->data_type;
                    }
                }
                
                // Type mismatch for arithmetic
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Cannot apply operator '%s' to %s and %s",
                         op, data_type_to_string(left_type), data_type_to_string(right_type));
                compile_error(error_msg, node->line);
                node->data_type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            
            // Comparison operators
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
                
                // String comparison
                if (left_type == TYPE_STRING && right_type == TYPE_STRING) {
                    node->data_type = TYPE_BOOL;
                    return TYPE_BOOL;
                }
                
                // Numeric comparison
                if ((left_type == TYPE_INT || left_type == TYPE_FLOAT) &&
                    (right_type == TYPE_INT || right_type == TYPE_FLOAT)) {
                    node->data_type = TYPE_BOOL;
                    return TYPE_BOOL;
                }
                
                // Type mismatch for comparison
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Cannot compare %s with %s",
                         data_type_to_string(left_type), data_type_to_string(right_type));
                compile_error(error_msg, node->line);
                node->data_type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            
            // Logical operators
            if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 ||
                strcmp(op, "and") == 0 || strcmp(op, "or") == 0) {
                
                if (left_type != TYPE_BOOL) {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type error: Logical operator '%s' requires boolean, got %s on left side",
                             op, data_type_to_string(left_type));
                    compile_error(error_msg, node->line);
                }
                
                if (right_type != TYPE_BOOL) {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type error: Logical operator '%s' requires boolean, got %s on right side",
                             op, data_type_to_string(right_type));
                    compile_error(error_msg, node->line);
                }
                
                node->data_type = TYPE_BOOL;
                return TYPE_BOOL;
            }
            
            compile_error("Unknown binary operator", node->line);
            node->data_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
            
        case AST_UNARY_EXPR:
            if (!node->value) {
                compile_error("Unary expression has no operator", node->line);
                return TYPE_UNKNOWN;
            }
            
            right_type = check_expression_type(node->right, table);
            
            if (strcmp(node->value, "!") == 0 || strcmp(node->value, "not") == 0) {
                if (right_type != TYPE_BOOL && right_type != TYPE_UNKNOWN) {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type error: Logical NOT requires boolean, got %s",
                             data_type_to_string(right_type));
                    compile_error(error_msg, node->line);
                }
                node->data_type = TYPE_BOOL;
                return TYPE_BOOL;
            }
            
            if (strcmp(node->value, "-") == 0) {
                if (right_type != TYPE_INT && right_type != TYPE_FLOAT && right_type != TYPE_UNKNOWN) {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type error: Unary minus requires numeric type, got %s",
                             data_type_to_string(right_type));
                    compile_error(error_msg, node->line);
                }
                node->data_type = right_type;
                return right_type;
            }
            
            compile_error("Unknown unary operator", node->line);
            return TYPE_UNKNOWN;
            
        case AST_CALL_EXPR:
            if (!node->value && (!node->left || node->left->type != AST_IDENTIFIER)) {
                node->data_type = TYPE_UNKNOWN;
                return TYPE_UNKNOWN;
            }
            {
                const char *fn_name = node->value ? node->value : node->left->value;
                LocalSymbolEntry *entry = lookup_symbol(table, fn_name);
                if (!entry || !entry->is_function) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                             "Undefined function '%s'", fn_name ? fn_name : "<anonymous>");
                    compile_error(error_msg, node->line);
                    node->data_type = TYPE_UNKNOWN;
                    return TYPE_UNKNOWN;
                }

                if (entry->param_count >= 0 && entry->param_types) {
                    if (node->child_count != entry->param_count) {
                        char error_msg[256];
                        snprintf(error_msg, sizeof(error_msg),
                                 "Function '%s' expects %d arguments, got %d",
                                 fn_name, entry->param_count, node->child_count);
                        compile_error(error_msg, node->line);
                    } else {
                        for (int i = 0; i < node->child_count; i++) {
                            DataType arg_type = check_expression_type(node->children[i], table);
                            DataType param_type = entry->param_types[i];
                            if (param_type != TYPE_UNKNOWN && param_type != TYPE_AUTO &&
                                !data_types_are_compatible(param_type, arg_type) &&
                                arg_type != TYPE_UNKNOWN) {
                                char error_msg[512];
                                snprintf(error_msg, sizeof(error_msg),
                                         "Type error: Argument %d to '%s' expects %s, got %s",
                                         i + 1, fn_name,
                                         data_type_to_string(param_type),
                                         data_type_to_string(arg_type));
                                compile_error(error_msg, node->line);
                            }
                        }
                    }
                } else {
                    for (int i = 0; i < node->child_count; i++) {
                        check_expression_type(node->children[i], table);
                    }
                }

                node->data_type = entry->return_type != TYPE_UNKNOWN ? entry->return_type : TYPE_UNKNOWN;
                return node->data_type;
            }
            
        case AST_ARRAY_LITERAL:
            node->data_type = TYPE_ARRAY;
            if (node->child_count > 0 && node->children) {
                DataType elem_type = check_expression_type(node->children[0], table);
                for (int i = 1; i < node->child_count; i++) {
                    DataType next_type = check_expression_type(node->children[i], table);
                    if (!data_types_are_compatible(elem_type, next_type) &&
                        elem_type != TYPE_UNKNOWN && next_type != TYPE_UNKNOWN) {
                        char error_msg[512];
                        snprintf(error_msg, sizeof(error_msg),
                                 "Type error: Array literal contains incompatible types %s and %s",
                                 data_type_to_string(elem_type), data_type_to_string(next_type));
                        compile_error(error_msg, node->line);
                    }
                }
            }
            return TYPE_ARRAY;
            
        case AST_OBJECT_LITERAL:
            node->data_type = TYPE_OBJECT;
            if (node->children) {
                for (int i = 0; i < node->child_count; i++) {
                    ASTNode *pair = node->children[i];
                    if (pair && pair->right) {
                        check_expression_type(pair->right, table);
                    }
                }
            }
            return TYPE_OBJECT;
            
        case AST_ARRAY_ACCESS:
            left_type = check_expression_type(node->left, table);
            right_type = check_expression_type(node->right, table);
            
            if (left_type != TYPE_ARRAY && left_type != TYPE_STRING && left_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Cannot index into non-array type %s",
                         data_type_to_string(left_type));
                compile_error(error_msg, node->line);
            }
            
            if (right_type != TYPE_INT && right_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Array index must be integer, got %s",
                         data_type_to_string(right_type));
                compile_error(error_msg, node->line);
            }
            
            // For string indexing, result is string; for array, element type (unknown for now)
            node->data_type = (left_type == TYPE_STRING) ? TYPE_STRING : TYPE_UNKNOWN;
            return node->data_type;
            
        case AST_MEMBER_ACCESS:
            // Object property access
            node->data_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
            
        case AST_TERNARY_EXPR:
            if (!node->condition) {
                compile_error("Ternary expression missing condition", node->line);
                return TYPE_UNKNOWN;
            }
            
            result_type = check_expression_type(node->condition, table);
            if (result_type != TYPE_BOOL && result_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Ternary condition must be boolean, got %s",
                         data_type_to_string(result_type));
                compile_error(error_msg, node->line);
            }
            
            left_type = check_expression_type(node->left, table);
            right_type = check_expression_type(node->right, table);
            
            if (!data_types_are_compatible(left_type, right_type) && 
                left_type != TYPE_UNKNOWN && right_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Ternary branches have incompatible types %s and %s",
                         data_type_to_string(left_type), data_type_to_string(right_type));
                compile_error(error_msg, node->line);
            }
            
            node->data_type = left_type;
            return left_type;
            
        default:
            node->data_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
    }
}

// ========================================
// Statement Type Checking
// ========================================

static void check_statement_type(ASTNode *node, LocalSymbolTable *table, LocalSymbolEntry *current_function) {
    if (!node) return;
    
    DataType expr_type;
    LocalSymbolEntry *entry;
    
    switch (node->type) {
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            if (!node->value) {
                compile_error("Variable declaration missing name", node->line);
                return;
            }
            
            entry = lookup_symbol_entry(table, node->value);
            if (entry && entry->scope_level == table->current_scope) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Variable '%s' already declared in this scope",
                         node->value);
                compile_error(error_msg, node->line);
                return;
            }
            
            // Add symbol to table
            {
                DataType declared = node->data_type != TYPE_UNKNOWN ? node->data_type : TYPE_AUTO;
                add_symbol(table, node->value, NULL, declared);
            }
            
            // Check initializer if present
            if (node->right) {
                expr_type = check_expression_type(node->right, table);
                
                // Update the symbol's type
                entry = lookup_symbol_entry(table, node->value);
                if (entry) {
                    entry->data_type = expr_type;
                    entry->is_initialized = true;
                    entry->is_constant = (node->type == AST_CONST_DECL);
                    node->data_type = expr_type;
                }
            } else if (node->type == AST_CONST_DECL) {
                compile_error("Const declaration requires initializer", node->line);
            }
            break;
            
        case AST_ASSIGN_STMT:
            if (!node->left) {
                compile_error("Assignment missing target", node->line);
                return;
            }

            if (node->left->type == AST_IDENTIFIER) {
                entry = lookup_symbol(table, node->left->value);
                if (!entry) {
                    char error_msg[256];
                    snprintf(error_msg, sizeof(error_msg),
                             "Undefined variable '%s' in assignment",
                             node->left->value);
                    compile_error(error_msg, node->line);
                    return;
                }
            } else if (node->left->type == AST_MEMBER_ACCESS || node->left->type == AST_ARRAY_ACCESS) {
                entry = NULL;
            } else {
                compile_error("Assignment target must be identifier or member access", node->line);
                return;
            }
            
            if (entry && entry->is_constant) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Cannot assign to const variable '%s'",
                         node->left->value);
                compile_error(error_msg, node->line);
                return;
            }
            
            expr_type = check_expression_type(node->right, table);
            
            // Check type compatibility
            if (entry) {
                if (!data_types_are_compatible(entry->data_type, expr_type) &&
                    entry->data_type != TYPE_AUTO && expr_type != TYPE_UNKNOWN) {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type error: Cannot assign %s to variable of type %s",
                             data_type_to_string(expr_type), data_type_to_string(entry->data_type));
                    compile_error(error_msg, node->line);
                    return;
                }
            }
            
            // Update variable type if it was auto
            if (entry) {
                if (entry->data_type == TYPE_AUTO) {
                    entry->data_type = expr_type;
                }
                entry->is_initialized = true;
            }
            node->data_type = expr_type;
            break;
            
        case AST_IF_STMT:
            if (!node->condition) {
                compile_error("If statement missing condition", node->line);
                return;
            }
            
            expr_type = check_expression_type(node->condition, table);
            if (expr_type != TYPE_BOOL && expr_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: If condition must be boolean, got %s",
                         data_type_to_string(expr_type));
                compile_error(error_msg, node->line);
            }
            
            check_statement_type(node->body, table, current_function);
            check_statement_type(node->next, table, current_function);  // else/elif
            break;
            
        case AST_WHILE_STMT:
        case AST_DO_WHILE_STMT:
            if (!node->condition) {
                compile_error("While statement missing condition", node->line);
                return;
            }
            
            expr_type = check_expression_type(node->condition, table);
            if (expr_type != TYPE_BOOL && expr_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: While condition must be boolean, got %s",
                         data_type_to_string(expr_type));
                compile_error(error_msg, node->line);
            }
            
            check_statement_type(node->body, table, current_function);
            break;
            
        case AST_FOR_STMT:
            enter_scope(table);

            if (node->value) {
                LocalSymbolEntry *loop_var = add_symbol(table, node->value, NULL, TYPE_AUTO);
                if (loop_var) {
                    loop_var->is_initialized = true;
                }
            }

            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    if (range->left) check_expression_type(range->left, table);
                    if (range->right) check_expression_type(range->right, table);
                }
            } else if (node->condition) {
                check_expression_type(node->condition, table);
            }

            check_statement_type(node->body, table, current_function);
            exit_scope(table);
            break;
            
        case AST_RETURN_STMT:
            // Would need to check against function return type
            if (node->right) {
                DataType return_type = check_expression_type(node->right, table);
                if (current_function) {
                    if (current_function->return_type == TYPE_UNKNOWN || current_function->return_type == TYPE_AUTO) {
                        current_function->return_type = return_type;
                    } else if (!data_types_are_compatible(current_function->return_type, return_type) &&
                               return_type != TYPE_UNKNOWN) {
                        char error_msg[512];
                        snprintf(error_msg, sizeof(error_msg),
                                 "Type error: Return type %s does not match function return type %s",
                                 data_type_to_string(return_type),
                                 data_type_to_string(current_function->return_type));
                        compile_error(error_msg, node->line);
                    }
                }
            } else if (current_function && current_function->return_type == TYPE_UNKNOWN) {
                current_function->return_type = TYPE_VOID;
            }
            break;
            
        case AST_FUNCTION_DECL:
            if (node->value) {
                LocalSymbolEntry *func_entry = add_symbol(table, node->value, "function", TYPE_FUNCTION);
                if (func_entry) {
                    func_entry->is_function = true;
                    func_entry->return_type = node->data_type != TYPE_UNKNOWN ? node->data_type : TYPE_UNKNOWN;
                    func_entry->param_count = node->child_count;
                    if (node->child_count > 0) {
                        func_entry->param_types = calloc(node->child_count, sizeof(DataType));
                        if (!func_entry->param_types) {
                            compile_error("Failed to allocate function parameter types", node->line);
                        }
                    }

                    for (int i = 0; i < node->child_count; i++) {
                        ASTNode *param = node->children[i];
                        DataType param_type = param ? param->data_type : TYPE_UNKNOWN;
                        if (param && param_type == TYPE_UNKNOWN && param->metadata) {
                            param_type = data_type_from_string(param->metadata);
                        }
                        if (func_entry->param_types) {
                            func_entry->param_types[i] = param_type;
                        }
                    }
                }

                enter_scope(table);
                if (node->children) {
                    for (int i = 0; i < node->child_count; i++) {
                        ASTNode *param = node->children[i];
                        if (param && param->value) {
                            LocalSymbolEntry *param_entry = add_symbol(table, param->value, NULL,
                                                                       param->data_type != TYPE_UNKNOWN ? param->data_type : TYPE_AUTO);
                            if (param_entry) {
                                param_entry->is_initialized = true;
                            }
                        }
                    }
                }

                if (node->body) {
                    check_statement_type(node->body, table, func_entry);
                }
                exit_scope(table);
            }
            break;
            
        case AST_BLOCK:
            enter_scope(table);
            {
                ASTNode *child = node->body ? node->body : (node->children ? node->children[0] : NULL);
                while (child) {
                    check_statement_type(child, table, current_function);
                    child = child->next;
                }
            }
            exit_scope(table);
            break;
            
        case AST_PROGRAM:
            {
                ASTNode *stmt = node->body ? node->body : (node->children ? node->children[0] : NULL);
                while (stmt) {
                    check_statement_type(stmt, table, current_function);
                    stmt = stmt->next;
                }
            }
            break;
            
        default:
            // Recursively check child nodes
            if (node->left) check_statement_type(node->left, table, current_function);
            if (node->right) check_statement_type(node->right, table, current_function);
            if (node->body) check_statement_type(node->body, table, current_function);
            if (node->condition) check_statement_type(node->condition, table, current_function);
            if (node->next) check_statement_type(node->next, table, current_function);
            
            if (node->children) {
                for (int i = 0; i < node->child_count; i++) {
                    check_statement_type(node->children[i], table, current_function);
                }
            }
            break;
    }
}

// ========================================
// Symbol Type Helpers
// ========================================

#if 0
static DataType get_symbol_type(LocalSymbolTable *table, const char *name) {
    LocalSymbolEntry *entry = lookup_symbol(table, name);
    if (!entry) return TYPE_UNKNOWN;
    return entry->data_type;
}

static void set_symbol_type(LocalSymbolTable *table, const char *name, DataType type) {
    LocalSymbolEntry *entry = lookup_symbol(table, name);
    if (entry) {
        entry->data_type = type;
    }
}
#endif

// ========================================
// Legacy Analysis Functions (disabled)
// ========================================
#if 0
static int analyze_node(ASTNode *node, LocalSymbolTable *table) {
    if (!node) return 1;
    (void)table;
    return 1;
}
#endif

// ========================================
// Main Entry Points
// ========================================

int semantic_analyze(ASTNode *ast) {
    if (!ast) {
        fprintf(stderr, "Semantic error: NULL AST\n");
        return 0;
    }
    
    LocalSymbolTable *table = create_symbol_table();
    if (!table) return 0;
    check_statement_type(ast, table, NULL);
    free_symbol_table(table);
    
    return 1;
}

// Strict type checking pass - validates all types before IR generation
int semantic_check_types(ASTNode *ast) {
    if (!ast) {
        fprintf(stderr, "Semantic error: NULL AST\n");
        return 0;
    }
    
    printf("[Type Check] Running strict type checking...\n");
    
    LocalSymbolTable *table = create_symbol_table();
    if (!table) return 0;
    
    // Perform type checking on all statements
    check_statement_type(ast, table, NULL);
    
    free_symbol_table(table);
    
    printf("[Type Check] Type checking complete\n");
    return 1;
}

// Infer type from AST node (helper for code generation)
DataType semantic_infer_type(ASTNode *node) {
    if (!node) return TYPE_UNKNOWN;
    
    // If node already has data_type set by type checker, use it
    if (node->data_type != TYPE_UNKNOWN) {
        return node->data_type;
    }
    
    // Otherwise, infer based on node type
    switch (node->type) {
        case AST_LITERAL:
            if (!node->value) return TYPE_NULL;
            
            if (node->value[0] == '"' || node->value[0] == '\'') return TYPE_STRING;
            if (strcmp(node->value, "true") == 0 || strcmp(node->value, "false") == 0) return TYPE_BOOL;
            if (strcmp(node->value, "null") == 0 || strcmp(node->value, "nil") == 0) return TYPE_NULL;
            
            // Check if numeric
            bool has_dot = false;
            bool is_number = true;
            const char *p = node->value;
            
            if (*p == '-' || *p == '+') p++;
            
            while (*p) {
                if (isdigit(*p)) {
                    // OK
                } else if (*p == '.' && !has_dot) {
                    has_dot = true;
                } else {
                    is_number = false;
                    break;
                }
                p++;
            }
            
            if (is_number) return has_dot ? TYPE_FLOAT : TYPE_INT;
            return TYPE_UNKNOWN;
            
        case AST_ARRAY_LITERAL:
            return TYPE_ARRAY;
            
        case AST_OBJECT_LITERAL:
            return TYPE_OBJECT;
            
        default:
            return TYPE_UNKNOWN;
    }
}
