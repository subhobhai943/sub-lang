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
    int scope_level;
    bool is_initialized;
    bool is_constant;
    struct LocalSymbolEntry *next;
} LocalSymbolEntry;

// Symbol table (local) - enhanced version
typedef struct {
    LocalSymbolEntry *head;
    int current_scope;
} LocalSymbolTable;

// Forward declaration
static DataType check_expression_type(ASTNode *node, LocalSymbolTable *table);
static void check_statement_type(ASTNode *node, LocalSymbolTable *table);
static DataType get_symbol_type(LocalSymbolTable *table, const char *name);
static void set_symbol_type(LocalSymbolTable *table, const char *name, DataType type);
static LocalSymbolEntry* lookup_symbol_entry(LocalSymbolTable *table, const char *name);

// ========================================
// Symbol Table Management
// ========================================

static LocalSymbolTable* create_symbol_table() {
    LocalSymbolTable *table = malloc(sizeof(LocalSymbolTable));
    table->head = NULL;
    table->current_scope = 0;
    return table;
}

static void add_symbol(LocalSymbolTable *table, const char *name, const char *type_str, DataType data_type) {
    LocalSymbolEntry *entry = malloc(sizeof(LocalSymbolEntry));
    entry->name = strdup(name);
    entry->type_str = type_str ? strdup(type_str) : NULL;
    entry->data_type = data_type;
    entry->scope_level = table->current_scope;
    entry->is_initialized = false;
    entry->is_constant = false;
    entry->next = table->head;
    table->head = entry;
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
        free(current);
        current = next;
    }
    free(table);
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
            // Function calls - would need to look up function signature
            node->data_type = TYPE_UNKNOWN;
            return TYPE_UNKNOWN;
            
        case AST_ARRAY_LITERAL:
            node->data_type = TYPE_ARRAY;
            return TYPE_ARRAY;
            
        case AST_OBJECT_LITERAL:
            node->data_type = TYPE_OBJECT;
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

static void check_statement_type(ASTNode *node, LocalSymbolTable *table) {
    if (!node) return;
    
    DataType expr_type, var_type;
    LocalSymbolEntry *entry;
    
    switch (node->type) {
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            if (!node->value) {
                compile_error("Variable declaration missing name", node->line);
                return;
            }
            
            entry = lookup_symbol_entry(table, node->value);
            if (entry) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Variable '%s' already declared in this scope",
                         node->value);
                compile_error(error_msg, node->line);
                return;
            }
            
            // Add symbol to table
            add_symbol(table, node->value, NULL, TYPE_AUTO);
            
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
            if (!node->left || node->left->type != AST_IDENTIFIER) {
                compile_error("Assignment target must be an identifier", node->line);
                return;
            }
            
            entry = lookup_symbol(table, node->left->value);
            if (!entry) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Undefined variable '%s' in assignment",
                         node->left->value);
                compile_error(error_msg, node->line);
                return;
            }
            
            if (entry->is_constant) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Cannot assign to const variable '%s'",
                         node->left->value);
                compile_error(error_msg, node->line);
                return;
            }
            
            expr_type = check_expression_type(node->right, table);
            
            // Check type compatibility
            if (!data_types_are_compatible(entry->data_type, expr_type) &&
                entry->data_type != TYPE_AUTO && expr_type != TYPE_UNKNOWN) {
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg),
                         "Type error: Cannot assign %s to variable of type %s",
                         data_type_to_string(expr_type), data_type_to_string(entry->data_type));
                compile_error(error_msg, node->line);
                return;
            }
            
            // Update variable type if it was auto
            if (entry->data_type == TYPE_AUTO) {
                entry->data_type = expr_type;
            }
            
            entry->is_initialized = true;
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
            
            check_statement_type(node->body, table);
            check_statement_type(node->next, table);  // else/elif
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
            
            check_statement_type(node->body, table);
            break;
            
        case AST_FOR_STMT:
            table->current_scope++;
            
            if (node->left) {
                check_statement_type(node->left, table);
            }
            
            if (node->condition) {
                expr_type = check_expression_type(node->condition, table);
                if (expr_type != TYPE_BOOL && expr_type != TYPE_UNKNOWN) {
                    char error_msg[512];
                    snprintf(error_msg, sizeof(error_msg),
                             "Type error: For condition must be boolean, got %s",
                             data_type_to_string(expr_type));
                    compile_error(error_msg, node->line);
                }
            }
            
            if (node->right) {
                check_expression_type(node->right, table);
            }
            
            check_statement_type(node->body, table);
            table->current_scope--;
            break;
            
        case AST_RETURN_STMT:
            // Would need to check against function return type
            if (node->right) {
                check_expression_type(node->right, table);
            }
            break;
            
        case AST_FUNCTION_DECL:
            if (node->value) {
                add_symbol(table, node->value, "function", TYPE_FUNCTION);
            }
            
            table->current_scope++;
            
            if (node->left) {
                check_statement_type(node->left, table);
            }
            
            if (node->body) {
                check_statement_type(node->body, table);
            }
            
            table->current_scope--;
            break;
            
        case AST_BLOCK:
            table->current_scope++;
            {
                ASTNode *child = node->children ? node->children[0] : NULL;
                while (child) {
                    check_statement_type(child, table);
                    child = child->next;
                }
            }
            table->current_scope--;
            break;
            
        case AST_PROGRAM:
            {
                ASTNode *stmt = node->children ? node->children[0] : NULL;
                while (stmt) {
                    check_statement_type(stmt, table);
                    stmt = stmt->next;
                }
            }
            break;
            
        default:
            // Recursively check child nodes
            if (node->left) check_statement_type(node->left, table);
            if (node->right) check_statement_type(node->right, table);
            if (node->body) check_statement_type(node->body, table);
            if (node->condition) check_statement_type(node->condition, table);
            if (node->next) check_statement_type(node->next, table);
            
            if (node->children) {
                for (int i = 0; i < node->child_count; i++) {
                    check_statement_type(node->children[i], table);
                }
            }
            break;
    }
}

// ========================================
// Symbol Type Helpers
// ========================================

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

// ========================================
// Legacy Analysis Functions (kept for compatibility)
// ========================================

static int analyze_node(ASTNode *node, LocalSymbolTable *table) {
    if (!node) return 1;
    
    switch (node->type) {
        case AST_VAR_DECL:
            if (node->value) {
                if (lookup_symbol(table, node->value)) {
                    fprintf(stderr, "Semantic error: Variable '%s' already declared\n", node->value);
                    return 0;
                }
                add_symbol(table, node->value, NULL, TYPE_AUTO);
            }
            if (node->right) {
                return analyze_node(node->right, table);
            }
            break;
            
        case AST_IDENTIFIER:
            if (node->value) {
                if (!lookup_symbol(table, node->value)) {
                    fprintf(stderr, "Semantic error: Undefined variable '%s'\n", node->value);
                    return 0;
                }
            }
            break;
            
        case AST_FUNCTION_DECL:
            if (node->value) {
                add_symbol(table, node->value, "function", TYPE_FUNCTION);
            }
            table->current_scope++;
            if (node->left) {
                if (!analyze_node(node->left, table)) return 0;
            }
            table->current_scope--;
            break;
            
        default:
            if (node->left && !analyze_node(node->left, table)) return 0;
            if (node->right && !analyze_node(node->right, table)) return 0;
            if (node->next && !analyze_node(node->next, table)) return 0;
            break;
    }
    
    return 1;
}

// ========================================
// Main Entry Points
// ========================================

int semantic_analyze(ASTNode *ast) {
    if (!ast) {
        fprintf(stderr, "Semantic error: NULL AST\n");
        return 0;
    }
    
    LocalSymbolTable *table = create_symbol_table();
    int result = analyze_node(ast, table);
    free_symbol_table(table);
    
    return result;
}

// Strict type checking pass - validates all types before IR generation
int semantic_check_types(ASTNode *ast) {
    if (!ast) {
        fprintf(stderr, "Semantic error: NULL AST\n");
        return 0;
    }
    
    printf("[Type Check] Running strict type checking...\n");
    
    LocalSymbolTable *table = create_symbol_table();
    
    // Perform type checking on all statements
    check_statement_type(ast, table);
    
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
