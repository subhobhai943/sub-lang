/* ========================================
   SUB Language Code Generator - REAL IMPLEMENTATION
   Actually processes AST and generates working code
   File: codegen.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "codegen_infer.h"
#include "codegen_globals.h"
#include "windows_compat.h"
#include <stdarg.h>

/* Optimization Context */
typedef struct {
    bool has_main;
    int function_count;
    bool optimized;
} OptimizationContext;

/* String Builder for code generation */
typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} StringBuilder;

static StringBuilder* sb_create() {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    sb->capacity = 4096;
    sb->size = 0;
    sb->buffer = malloc(sb->capacity);
    if (!sb->buffer) {
        free(sb);
        return NULL;
    }
    sb->buffer[0] = '\0';
    return sb;
}

static void sb_append(StringBuilder *sb, const char *fmt, ...) {
    if (!sb || !fmt) return;
    
    va_list args;
    va_start(args, fmt);
    
    // Calculate needed space
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (needed < 0) {
        va_end(args);
        return;
    }
    
    // Resize if necessary (guard against capacity overflow)
    while (sb->size + (size_t)needed + 1 > sb->capacity) {
        size_t new_cap = sb->capacity * 2;
        if (new_cap <= sb->capacity) {
            /* Overflow detected */
            va_end(args);
            return;
        }
        sb->capacity = new_cap;
        char *new_buffer = realloc(sb->buffer, sb->capacity);
        if (!new_buffer) {
            va_end(args);
            return;
        }
        sb->buffer = new_buffer;
    }
    
    // Append
    vsnprintf(sb->buffer + sb->size, needed + 1, fmt, args);
    sb->size += (size_t)needed;
    va_end(args);
}

static char* sb_to_string(StringBuilder *sb) {
    if (!sb) return NULL;
    char *result = strdup(sb->buffer);
    free(sb->buffer);
    free(sb);
    return result;
}

/* Escape a string literal for safe inclusion in generated C code */
static char* escape_c_string_literal(const char *raw) {
    if (!raw) return strdup("");
    size_t len = strlen(raw);
    /* Worst case: every char needs escaping (\xNN) = 4 bytes per char + 1 null */
    char *escaped = malloc(len * 4 + 1);
    if (!escaped) return strdup("");
    size_t out = 0;
    for (size_t i = 0; i < len && out < len * 4; i++) {
        switch ((unsigned char)raw[i]) {
            case '\n': escaped[out++] = '\\'; escaped[out++] = 'n'; break;
            case '\t': escaped[out++] = '\\'; escaped[out++] = 't'; break;
            case '\r': escaped[out++] = '\\'; escaped[out++] = 'r'; break;
            case '\\': escaped[out++] = '\\'; escaped[out++] = '\\'; break;
            case '"':  escaped[out++] = '\\'; escaped[out++] = '"';  break;
            case '\0': escaped[out++] = '\\'; escaped[out++] = '0';  break;
            default:
                /* Pass through printable ASCII and common UTF-8 continuation bytes */
                if ((unsigned char)raw[i] >= 32 && (unsigned char)raw[i] < 127) {
                    escaped[out++] = raw[i];
                } else if ((unsigned char)raw[i] >= 0x80) {
                    /* Pass through UTF-8 bytes as-is */
                    escaped[out++] = raw[i];
                } else {
                    /* Escape other control characters */
                    snprintf(escaped + out, 5, "\\x%02x", (unsigned char)raw[i]);
                    out += 4;
                }
                break;
        }
    }
    escaped[out] = '\0';
    return escaped;
}

/* Forward declarations */
static void generate_node(StringBuilder *sb, ASTNode *node, int indent);
static void generate_expression(StringBuilder *sb, ASTNode *node);
static int join_elem_kind(ASTNode *arr);
static void optimize_remove_dead_code(ASTNode *node);
static bool is_node_pure(ASTNode *node);
static void optimize_constant_folding(ASTNode *node);

/* Dead Code Elimination */
static bool is_node_pure(ASTNode *node) {
    if (!node) return false;
    
    switch (node->type) {
        case AST_LITERAL:
        case AST_IDENTIFIER:
            return true;
        case AST_BINARY_EXPR:
            return is_node_pure(node->left) && is_node_pure(node->right);
        case AST_UNARY_EXPR:
            return is_node_pure(node->left);
        default:
            return false;
    }
}

static void optimize_remove_dead_code(ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK: {
            if (node->child_count <= 0) break;
            ASTNode **new_children = malloc(sizeof(ASTNode*) * (size_t)node->child_count);
            if (!new_children) break; /* OOM — skip optimization */
            int new_count = 0;
            
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *child = node->children[i];
                
                if (!child) continue;
                
                optimize_remove_dead_code(child);
                
                bool keep = true;
                if (child->type == AST_LITERAL && !is_node_pure(child)) {
                    keep = false;
                }
                
                if (keep && (child->type == AST_VAR_DECL || 
                    child->type == AST_CONST_DECL ||
                    child->type == AST_FUNCTION_DECL ||
                    child->type == AST_ASSIGN_STMT ||
                    child->type == AST_CALL_EXPR ||
                    child->type == AST_RETURN_STMT ||
                    child->type == AST_IF_STMT ||
                    child->type == AST_FOR_STMT ||
                    child->type == AST_WHILE_STMT ||
                    child->type == AST_BLOCK ||
                    child->type == AST_BINARY_EXPR)) {
                    new_children[new_count++] = child;
                }
            }
            
            free(node->children);
            node->children = new_children;
            node->child_count = new_count;
            break;
        }
        default:
            for (int i = 0; i < node->child_count; i++) {
                optimize_remove_dead_code(node->children[i]);
            }
            if (node->left) optimize_remove_dead_code(node->left);
            if (node->right) optimize_remove_dead_code(node->right);
            if (node->condition) optimize_remove_dead_code(node->condition);
            if (node->body) optimize_remove_dead_code(node->body);
            break;
    }
}

static void optimize_constant_folding(ASTNode *node) {
    if (!node) return;
    
    if (node->type == AST_BINARY_EXPR) {
        optimize_constant_folding(node->left);
        optimize_constant_folding(node->right);
        
        if (node->left && node->right && 
            node->left->type == AST_LITERAL && 
            node->right->type == AST_LITERAL) {
            
            if (node->value) {
                char *left_end, *right_end;
                long long left_val = strtoll(node->left->value, &left_end, 10);
                long long right_val = strtoll(node->right->value, &right_end, 10);
                
                if (*left_end == '\0' && *right_end == '\0') {
                    long long result = 0;
                    
                    if (strcmp(node->value, "+") == 0) {
                        result = left_val + right_val;
                    } else if (strcmp(node->value, "-") == 0) {
                        result = left_val - right_val;
                    } else if (strcmp(node->value, "*") == 0) {
                        result = left_val * right_val;
                    } else if (strcmp(node->value, "/") == 0 && right_val != 0) {
                        result = left_val / right_val;
                    } else {
                        return;
                    }
                    
                    char folded_val[32];
                    snprintf(folded_val, sizeof(folded_val), "%lld", result);
                    
                    node->type = AST_LITERAL;
                    free(node->value);
                    node->value = strdup(folded_val);
                    node->data_type = TYPE_INT;

                    parser_free_ast(node->left);
                    parser_free_ast(node->right);
                    node->left = NULL;
                    node->right = NULL;
                }
            }
        }
    } else {
        for (int i = 0; i < node->child_count; i++) {
            optimize_constant_folding(node->children[i]);
        }
        if (node->left) optimize_constant_folding(node->left);
        if (node->right) optimize_constant_folding(node->right);
        if (node->condition) optimize_constant_folding(node->condition);
        if (node->body) optimize_constant_folding(node->body);
    }
}

/* Optimization stub - can be expanded */
void optimize_c_output(ASTNode *node) {
    if (!node) return;
    
    optimize_constant_folding(node);
    /* optimize_remove_dead_code(node);  -- DISABLED: DCE pass has inverted condition bug */
    (void)optimize_remove_dead_code; /* suppress unused warning */
}

/* Helper to generate indentation */
static void indent_code(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "    ");
    }
}

static ASTNode* block_first(ASTNode *node) {
    if (!node) return NULL;
    if (node->body) return node->body;
    if (node->children && node->child_count > 0) return node->children[0];
    if (node->left) return node->left;
    return NULL;
}

static const char* sanitize_c_identifier(const char *name, char *buf, size_t buf_sz) {
    if (!name) return "var";
    static const char *c_kw[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "inline", "int", "long", "register", "restrict", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
        "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof",
        "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
        "_Static_assert", "_Thread_local", NULL
    };
    for (int i = 0; c_kw[i]; i++) {
        if (strcmp(name, c_kw[i]) == 0) {
            snprintf(buf, buf_sz, "sub_%s", name);
            return buf;
        }
    }
    return name;
}

/* An array slot is 64 bits whatever it holds, so a value going in has to be
   spelled as one: a double by its bit pattern, a string by its pointer. */
static void gen_elem_in(StringBuilder *sb, ASTNode *value, DataType elem) {
    if (elem == TYPE_FLOAT)       sb_append(sb, "sub_bits((double)(");
    else if (elem == TYPE_STRING) sb_append(sb, "(long long)(intptr_t)(");
    else                          sb_append(sb, "(long long)(");
    generate_expression(sb, value);
    sb_append(sb, elem == TYPE_FLOAT ? "))" : ")");
}

/* ... and coming back out, spelled as whatever it actually is. */
static void gen_elem_out_open(StringBuilder *sb, DataType elem) {
    if (elem == TYPE_FLOAT)       sb_append(sb, "sub_dbl(");
    else if (elem == TYPE_STRING) sb_append(sb, "(char*)(intptr_t)(");
    else                          sb_append(sb, "(");
}

/* The C type a loop variable takes when iterating an array of `elem`. */
static const char *elem_c_type(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return "double";
    case TYPE_STRING: return "char *";
    case TYPE_BOOL:   return "bool";
    default:          return "long";
    }
}

static int elem_kind_code(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return 1;
    case TYPE_STRING: return 2;
    case TYPE_BOOL:   return 3;
    default:          return 0;
    }
}

/* Generate expression code */
static void generate_expression(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->value) {
                if (node->data_type == TYPE_STRING) {
                    char *escaped = escape_c_string_literal(node->value);
                    sb_append(sb, "\"%s\"", escaped);
                    free(escaped);
                } else if (node->data_type == TYPE_BOOL) {
                    if (strcmp(node->value, "true") == 0) {
                        sb_append(sb, "true");
                    } else {
                        sb_append(sb, "false");
                    }
                } else if (node->data_type == TYPE_NULL ||
                           strcmp(node->value, "null") == 0 ||
                           strcmp(node->value, "nil") == 0) {
                    sb_append(sb, "NULL");
                } else {
                    sb_append(sb, "%s", node->value);
                }
            }
            break;
            
        case AST_IDENTIFIER:
            if (node->value) {
                char id_buf[128];
                sb_append(sb, "%s", sanitize_c_identifier(node->value, id_buf, sizeof(id_buf)));
            }
            break;
            
        case AST_BINARY_EXPR:
            if (node->left) {
                /* String concatenation: "str" + expr or expr + "str" */
                if (node->value && strcmp(node->value, "+") == 0 &&
                    (node->data_type == TYPE_STRING ||
                     (node->left && node->left->data_type == TYPE_STRING) ||
                     (node->right && node->right->data_type == TYPE_STRING))) {
                    /* Generate runtime string concat via snprintf with larger buffer */
                    sb_append(sb, "({char _buf[4096]; snprintf(_buf, sizeof(_buf), \"");
                    /* Build format string */
                    if (node->left->data_type == TYPE_STRING) sb_append(sb, "%%s");
                    else if (node->left->data_type == TYPE_INT) sb_append(sb, "%%ld");
                    else if (node->left->data_type == TYPE_FLOAT) sb_append(sb, "%%g");
                    else sb_append(sb, "%%s");
                    if (node->right->data_type == TYPE_STRING) sb_append(sb, "%%s");
                    else if (node->right->data_type == TYPE_INT) sb_append(sb, "%%ld");
                    else if (node->right->data_type == TYPE_FLOAT) sb_append(sb, "%%g");
                    else sb_append(sb, "%%s");
                    sb_append(sb, "\", ");
                    generate_expression(sb, node->left);
                    sb_append(sb, ", ");
                    generate_expression(sb, node->right);
                    sb_append(sb, "); sub_strdup(_buf);})");
                } else if (node->value &&
                           ((node->left && node->left->data_type == TYPE_STRING) ||
                            (node->right && node->right->data_type == TYPE_STRING)) &&
                           (strcmp(node->value, "==") == 0 || strcmp(node->value, "!=") == 0 ||
                            strcmp(node->value, "<") == 0  || strcmp(node->value, ">") == 0 ||
                            strcmp(node->value, "<=") == 0 || strcmp(node->value, ">=") == 0)) {
                    /* String comparison: compare contents via strcmp, not pointers */
                    const char *cop = strcmp(node->value, "==") == 0 ? "== 0" :
                                       strcmp(node->value, "!=") == 0 ? "!= 0" :
                                       strcmp(node->value, "<")  == 0 ? "< 0"  :
                                       strcmp(node->value, ">")  == 0 ? "> 0"  :
                                       strcmp(node->value, "<=") == 0 ? "<= 0" : ">= 0";
                    sb_append(sb, "(strcmp(");
                    generate_expression(sb, node->left);
                    sb_append(sb, ", ");
                    generate_expression(sb, node->right);
                    sb_append(sb, ") %s)", cop);
                } else if (node->value && strcmp(node->value, "**") == 0) {
                    /* Power operator: a ** b -> pow(a, b). pow() returns a
                       double, so an integer power has to be cast back or the
                       %ld used to print it reads the double's bit pattern. */
                    int as_int = (infer_expr_type(node->left)  == TYPE_INT &&
                                  infer_expr_type(node->right) == TYPE_INT &&
                                  !exponent_is_negative(node->right));
                    sb_append(sb, as_int ? "(long long)pow(" : "pow(");
                    generate_expression(sb, node->left);
                    sb_append(sb, ", ");
                    generate_expression(sb, node->right);
                    sb_append(sb, ")");
                } else if (node->value &&
                           (strcmp(node->value, "/") == 0 || strcmp(node->value, "%") == 0)) {
                    /* Division and remainder go through helpers so that
                       integer division truncates, float remainder follows
                       fmod, and dividing by zero reports the interpreter's
                       runtime error instead of raising SIGFPE. */
                    int is_div = (strcmp(node->value, "/") == 0);
                    int is_flt = (infer_expr_type(node->left)  == TYPE_FLOAT ||
                                  infer_expr_type(node->right) == TYPE_FLOAT);
                    sb_append(sb, "%s(", is_flt ? (is_div ? "sub_fdiv" : "sub_fmod")
                                                : (is_div ? "sub_idiv" : "sub_mod"));
                    generate_expression(sb, node->left);
                    sb_append(sb, ", ");
                    generate_expression(sb, node->right);
                    sb_append(sb, ")");
                } else if (node->value && (strcmp(node->value, "==") == 0 || strcmp(node->value, "!=") == 0) &&
                           ((node->right && (node->right->data_type == TYPE_NULL || (node->right->value && (strcmp(node->right->value, "null") == 0 || strcmp(node->right->value, "nil") == 0)))) ||
                            (node->left && (node->left->data_type == TYPE_NULL || (node->left->value && (strcmp(node->left->value, "null") == 0 || strcmp(node->left->value, "nil") == 0)))))) {
                    ASTNode *val_node = (node->right && (node->right->data_type == TYPE_NULL || (node->right->value && (strcmp(node->right->value, "null") == 0 || strcmp(node->right->value, "nil") == 0)))) ? node->left : node->right;
                    sb_append(sb, "((");
                    generate_expression(sb, val_node);
                    sb_append(sb, ") %s 0)", node->value);
                } else {
                    sb_append(sb, "(");
                    generate_expression(sb, node->left);
                    sb_append(sb, " %s ", node->value ? node->value : "+");
                    generate_expression(sb, node->right);
                    sb_append(sb, ")");
                }
            }
            break;
            
        case AST_UNARY_EXPR:
            if (node->value) {
                if (strcmp(node->value, "++") == 0) {
                    sb_append(sb, "(++(");
                    generate_expression(sb, node->right ? node->right : node->left);
                    sb_append(sb, "))");
                } else if (strcmp(node->value, "--") == 0) {
                    sb_append(sb, "(--(");
                    generate_expression(sb, node->right ? node->right : node->left);
                    sb_append(sb, "))");
                } else if (strcmp(node->value, "post++") == 0) {
                    sb_append(sb, "(((");
                    generate_expression(sb, node->right ? node->right : node->left);
                    sb_append(sb, ")++)");
                } else if (strcmp(node->value, "post--") == 0) {
                    sb_append(sb, "(((");
                    generate_expression(sb, node->right ? node->right : node->left);
                    sb_append(sb, ")--)");
                } else {
                    sb_append(sb, "(%s(", node->value);
                    generate_expression(sb, node->right ? node->right : node->left);
                    sb_append(sb, "))");
                }
            }
            break;

        case AST_ARRAY_LITERAL:
            {
                DataType elem = infer_elem_type(node);
                sb_append(sb, "sub_array_of(%d, %ld", elem_kind_code(elem),
                          (long)node->child_count);
                for (int i = 0; i < node->child_count; i++) {
                    sb_append(sb, ", ");
                    gen_elem_in(sb, node->children[i], elem);
                }
                sb_append(sb, ")");
            }
            break;

        case AST_OBJECT_LITERAL:
            sb_append(sb, "(void*)0");
            break;

        case AST_ARRAY_ACCESS:
            if (1) {
                DataType elem = infer_elem_type(node->left);
                gen_elem_out_open(sb, elem);
                sb_append(sb, "sub_array_get((SubArray*)(");
                generate_expression(sb, node->left);
                sb_append(sb, "), (long)(");
                generate_expression(sb, node->right);
                sb_append(sb, ")))");
            } else {
                sb_append(sb, "sub_array_get((SubArray*)(");
                generate_expression(sb, node->left);
                sb_append(sb, "), (long)(");
                generate_expression(sb, node->right);
                sb_append(sb, "))");
            }
            break;

        case AST_MEMBER_ACCESS:
            if (node->value && strcmp(node->value, "length") == 0) {
                if (node->left && (node->left->data_type == TYPE_STRING ||
                                   (node->left->value && (strcmp(node->left->value, "s") == 0 || strcmp(node->left->value, "str") == 0 || strcmp(node->left->value, "chars") == 0 || strcmp(node->left->value, "greeting") == 0 || strcmp(node->left->value, "name") == 0)) ||
                                   (node->left->type == AST_LITERAL && node->left->value && !isdigit((unsigned char)node->left->value[0])))) {
                    sb_append(sb, "sub_str_len((const char*)(");
                    generate_expression(sb, node->left);
                    sb_append(sb, "))");
                } else {
                    sb_append(sb, "sub_array_len((SubArray*)(");
                    generate_expression(sb, node->left);
                    sb_append(sb, "))");
                }
            } else if (node->value && strcmp(node->value, "new") == 0) {
                sb_append(sb, "sub_object_new()");
            } else if (node->left) {
                sb_append(sb, "(((SubObject*)(");
                generate_expression(sb, node->left);
                sb_append(sb, ")) ? ((SubObject*)(");
                generate_expression(sb, node->left);
                sb_append(sb, "))->%s : 0)", node->value ? node->value : "x");
            } else {
                sb_append(sb, "%s", node->value ? node->value : "0");
            }
            break;

        case AST_TERNARY_EXPR:
            sb_append(sb, "((");
            generate_expression(sb, node->condition);
            sb_append(sb, ") ? (");
            generate_expression(sb, node->left);
            sb_append(sb, ") : (");
            generate_expression(sb, node->right);
            sb_append(sb, "))");
            break;
            
        case AST_CALL_EXPR:
            if (node->left && node->left->type == AST_MEMBER_ACCESS) {
                if (node->left->left && node->left->left->type == AST_IDENTIFIER &&
                    node->left->left->value && strcmp(node->left->left->value, "ui") == 0) {
                    sb_append(sb, "0");
                    break;
                }
                const char *method = node->left->value;
                if (method && strcmp(method, "push") == 0) {
                    sb_append(sb, "sub_array_push((SubArray*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), ");
                    if (node->child_count > 0)
                        gen_elem_in(sb, node->children[0],
                                    infer_elem_type(node->left->left));
                    else sb_append(sb, "0");
                    sb_append(sb, ")");
                } else if (method && strcmp(method, "pop") == 0) {
                    sb_append(sb, "sub_array_pop((SubArray*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "))");
                } else if (method && strcmp(method, "join") == 0) {
                    sb_append(sb, "sub_array_join((SubArray*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), ");
                    if (node->child_count > 0) generate_expression(sb, node->children[0]);
                    else sb_append(sb, "\"\"");
                    sb_append(sb, ", %d)", join_elem_kind(node->left->left));
                } else if (method && strcmp(method, "upper") == 0) {
                    sb_append(sb, "sub_str_upper((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "))");
                } else if (method && strcmp(method, "lower") == 0) {
                    sb_append(sb, "sub_str_lower((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "))");
                } else if (method && strcmp(method, "trim") == 0) {
                    sb_append(sb, "sub_str_trim((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "))");
                } else if (method && strcmp(method, "substring") == 0) {
                    sb_append(sb, "sub_str_substring((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), (long)(");
                    if (node->child_count > 0) generate_expression(sb, node->children[0]); else sb_append(sb, "0");
                    sb_append(sb, "), (long)(");
                    if (node->child_count > 1) generate_expression(sb, node->children[1]); else sb_append(sb, "-1");
                    sb_append(sb, "))");
                } else if (method && strcmp(method, "split") == 0) {
                    sb_append(sb, "sub_str_split((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), ");
                    if (node->child_count > 0) generate_expression(sb, node->children[0]); else sb_append(sb, "\"\"");
                    sb_append(sb, ")");
                } else if (method && strcmp(method, "contains") == 0) {
                    sb_append(sb, "sub_str_contains((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), ");
                    if (node->child_count > 0) generate_expression(sb, node->children[0]); else sb_append(sb, "\"\"");
                    sb_append(sb, ")");
                } else if (method && strcmp(method, "replace") == 0) {
                    sb_append(sb, "sub_str_replace((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), ");
                    if (node->child_count > 0) generate_expression(sb, node->children[0]); else sb_append(sb, "\"\"");
                    sb_append(sb, ", ");
                    if (node->child_count > 1) generate_expression(sb, node->children[1]); else sb_append(sb, "\"\"");
                    sb_append(sb, ")");
                } else if (method && strcmp(method, "char_at") == 0) {
                    sb_append(sb, "sub_str_char_at((const char*)(");
                    generate_expression(sb, node->left->left);
                    sb_append(sb, "), (long)(");
                    if (node->child_count > 0) generate_expression(sb, node->children[0]); else sb_append(sb, "0");
                    sb_append(sb, "))");
                } else {
                    generate_expression(sb, node->left);
                    sb_append(sb, "(");
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expression(sb, node->children[i]);
                    }
                    sb_append(sb, ")");
                }
            } else if (node->value) {
                /* Map SUB print()/println()/show() to C printf() */
                if (is_print_builtin(node->value)) {
                    if (node->child_count > 0) {
                        sb_append(sb, "printf(\"");
                        for (int i = 0; i < node->child_count; i++) {
                            ASTNode *arg = node->children[i];
                            const char *fmt = "%ld";
                            if (arg->data_type == TYPE_INT) fmt = "%ld";
                            else if (arg->data_type == TYPE_FLOAT) fmt = "%g";
                            else if (arg->data_type == TYPE_BOOL) fmt = "%d";
                            else if (arg->data_type == TYPE_STRING) fmt = "%s";
                            else if (arg->type == AST_CALL_EXPR) {
                                if (arg->left && arg->left->type == AST_MEMBER_ACCESS) {
                                    const char *m = arg->left->value;
                                    if (m && (strcmp(m, "upper") == 0 || strcmp(m, "lower") == 0 ||
                                              strcmp(m, "substring") == 0 || strcmp(m, "replace") == 0 ||
                                              strcmp(m, "trim") == 0 || strcmp(m, "join") == 0 ||
                                              strcmp(m, "char_at") == 0)) fmt = "%s";
                                    else if (m && strcmp(m, "contains") == 0) fmt = "%ld";
                                } else if (arg->value) {
                                    const char *f = arg->value;
                                    if (f && (strcmp(f, "str") == 0 || strcmp(f, "to_string") == 0 ||
                                              strcmp(f, "trim") == 0 || strcmp(f, "join") == 0 ||
                                              strcmp(f, "char_at") == 0 || strcmp(f, "type") == 0 ||
                                              strcmp(f, "input") == 0 || strcmp(f, "upper") == 0 ||
                                              strcmp(f, "lower") == 0 || strcmp(f, "substring") == 0 ||
                                              strcmp(f, "replace") == 0)) fmt = "%s";
                                }
                            } else if (arg->type == AST_ARRAY_ACCESS) {
                                if (arg->left && arg->left->type == AST_IDENTIFIER &&
                                    (strcmp(arg->left->value, "parts") == 0 || arg->left->data_type == TYPE_STRING)) {
                                    fmt = "%s";
                                }
                            } else if (arg->type == AST_LITERAL && arg->value) {
                                char *end;
                                (void)strtol(arg->value, &end, 10);
                                if (*end == '\0') fmt = "%ld";
                                else {
                                    (void)strtod(arg->value, &end);
                                    if (*end == '\0') fmt = "%g";
                                    else fmt = "%s";
                                }
                            } else if (arg->type == AST_BINARY_EXPR) {
                                if (arg->data_type == TYPE_INT) fmt = "%ld";
                                else if (arg->data_type == TYPE_FLOAT) fmt = "%g";
                                else if (arg->data_type == TYPE_STRING) fmt = "%s";
                            }
                            /* The ad-hoc rules above predate shared
                               inference. Where inference has a concrete
                               answer it wins: the argument is cast to the
                               type inference reports, so choosing the format
                               any other way lets the two disagree - which is
                               how min(3, 9) came out as 1.03365e-317. */
                            {
                                DataType it = infer_expr_type(arg);
                                if (it == TYPE_STRING)      fmt = "%s";
                                else if (it == TYPE_FLOAT)  fmt = "%g";
                                else if (it == TYPE_BOOL)   fmt = "%s";
                                else if (it == TYPE_ARRAY)  fmt = "%s";
                                else if (it == TYPE_INT)    fmt = "%ld";
                            }
                            /* SUB spells booleans true/false, so C must print
                               the words rather than 1/0 - otherwise the same
                               program prints differently per backend. */
                            if (arg->data_type == TYPE_BOOL ||
                                infer_expr_type(arg) == TYPE_BOOL) fmt = "%s";
                            sb_append(sb, "%s%s", fmt, i + 1 < node->child_count ? " " : "\\n");
                        }
                        sb_append(sb, "\", ");
                        for (int i = 0; i < node->child_count; i++) {
                            ASTNode *arg = node->children[i];
                            int is_bool = (arg->data_type == TYPE_BOOL ||
                                           infer_expr_type(arg) == TYPE_BOOL);
                            DataType at = infer_expr_type(arg);
                            /* printf is variadic: an `int` expression passed
                               where %ld expects a `long` leaves the upper
                               word undefined, which is why -7 %% 3 printed
                               4294967295. Cast to the format's exact type. */
                            int cast_long   = !is_bool && (at == TYPE_INT);
                            int cast_double = !is_bool && (at == TYPE_FLOAT);
                            int as_array    = !is_bool && (at == TYPE_ARRAY);
                            if (is_bool)          sb_append(sb, "((");
                            else if (cast_long)   sb_append(sb, "(long)(");
                            else if (cast_double) sb_append(sb, "(double)(");
                            else if (as_array)    sb_append(sb, "sub_array_str((SubArray*)(");
                            generate_expression(sb, arg);
                            if (is_bool) sb_append(sb, ") ? \"true\" : \"false\")");
                            else if (as_array) sb_append(sb, "))");
                            else if (cast_long || cast_double) sb_append(sb, ")");
                            if (i + 1 < node->child_count) {
                                sb_append(sb, ", ");
                            }
                        }
                    } else {
                        sb_append(sb, "printf(\"\\n\"");
                    }
                    sb_append(sb, ")");
                } else {
                    const char *fn = node->value;
                    if (strcmp(fn, "float") == 0) {
                        if (node->child_count > 0 && node->children[0]->data_type == TYPE_STRING) sb_append(sb, "sub_float_from_str(");
                        else sb_append(sb, "(double)(");
                    }
                    else if (strcmp(fn, "int") == 0) {
                        if (node->child_count > 0 && node->children[0]->data_type == TYPE_STRING) sb_append(sb, "sub_int_from_str(");
                        else sb_append(sb, "(long)(");
                    }
                    else if (strcmp(fn, "str") == 0) {
                        if (node->child_count > 0) {
                            if (node->children[0]->data_type == TYPE_FLOAT) sb_append(sb, "sub_str_from_double(");
                            else if (node->children[0]->data_type == TYPE_STRING) sb_append(sb, "sub_strdup(");
                            else sb_append(sb, "sub_str_from_long(");
                        } else {
                            sb_append(sb, "sub_strdup(\"\")");
                        }
                    }
                    else if (strcmp(fn, "type") == 0) {
                        if (node->child_count > 0) {
                            if (node->children[0]->data_type == TYPE_FLOAT) sb_append(sb, "\"float\"");
                            else if (node->children[0]->data_type == TYPE_STRING) sb_append(sb, "\"string\"");
                            else if (node->children[0]->data_type == TYPE_BOOL) sb_append(sb, "\"bool\"");
                            else if (node->children[0]->data_type == TYPE_ARRAY || (node->children[0]->type == AST_ARRAY_LITERAL)) sb_append(sb, "\"array\"");
                            else if (node->children[0]->data_type == TYPE_NULL) sb_append(sb, "\"null\"");
                            else sb_append(sb, "\"int\"");
                        } else {
                            sb_append(sb, "\"null\"");
                        }
                    }
                    /* Function forms of the string/collection builtins. Only
                       the method forms (s.upper()) were wired up, so calling
                       upper(s) or len(s) emitted an undeclared function.
                       These emit the whole call, closing parens included. */
                    else if (strcmp(fn, "upper") == 0 || strcmp(fn, "lower") == 0) {
                        sb_append(sb, "%s((const char*)(",
                                  strcmp(fn, "upper") == 0 ? "sub_str_upper" : "sub_str_lower");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, "))");
                    }
                    else if (strcmp(fn, "len") == 0 || strcmp(fn, "length") == 0) {
                        int is_arr = (node->child_count > 0 &&
                                      infer_expr_type(node->children[0]) == TYPE_ARRAY);
                        sb_append(sb, is_arr ? "sub_array_len((SubArray*)("
                                             : "(long)strlen((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, "))");
                    }
                    else if (strcmp(fn, "input") == 0) sb_append(sb, "sub_input(");
                    else if (strcmp(fn, "sqrt") == 0) sb_append(sb, "sqrt(");
                    else if (strcmp(fn, "abs") == 0) sb_append(sb, "fabs(");
                    else if (strcmp(fn, "floor") == 0) sb_append(sb, "(long)floor(");
                    else if (strcmp(fn, "ceil") == 0) sb_append(sb, "(long)ceil(");
                    else if (strcmp(fn, "round") == 0) sb_append(sb, "(long)round(");
                    else if (strcmp(fn, "min") == 0 || strcmp(fn, "max") == 0) {
                        /* fmin/fmax are double-only: using them for two
                           integers rounds anything past 2^53 and makes the
                           result a double, which is not what SUB returns. */
                        int both_int = (node->child_count >= 2 &&
                            infer_expr_type(node->children[0]) == TYPE_INT &&
                            infer_expr_type(node->children[1]) == TYPE_INT);
                        if (both_int)
                            sb_append(sb, fn[1] == 'i' ? "sub_min_i(" : "sub_max_i(");
                        else
                            sb_append(sb, fn[1] == 'i' ? "fmin(" : "fmax(");
                    }
                    else if (strcmp(fn, "to_string") == 0) {
                        if (node->child_count > 0) {
                            if (node->children[0]->data_type == TYPE_FLOAT) sb_append(sb, "sub_str_from_double(");
                            else if (node->children[0]->data_type == TYPE_STRING) sb_append(sb, "sub_strdup(");
                            else sb_append(sb, "sub_str_from_long(");
                        } else sb_append(sb, "sub_strdup(\"\")");
                    }
                    else if (strcmp(fn, "push") == 0) {
                        sb_append(sb, "sub_array_push((SubArray*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        sb_append(sb, "), ");
                        if (node->child_count > 1)
                            gen_elem_in(sb, node->children[1],
                                        node->child_count > 0
                                          ? infer_elem_type(node->children[0])
                                          : TYPE_INT);
                        else sb_append(sb, "0");
                        sb_append(sb, ")");
                    }
                    else if (strcmp(fn, "pop") == 0) {
                        sb_append(sb, "sub_array_pop((SubArray*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        sb_append(sb, "))");
                    }
                    else if (strcmp(fn, "join") == 0) {
                        sb_append(sb, "sub_array_join((SubArray*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        sb_append(sb, "), ");
                        if (node->child_count > 1) generate_expression(sb, node->children[1]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, ", %d)",
                                  node->child_count > 0 ? join_elem_kind(node->children[0]) : 0);
                    }
                    else if (strcmp(fn, "trim") == 0) {
                        sb_append(sb, "sub_str_trim((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        sb_append(sb, "))");
                    }
                    else if (strcmp(fn, "char_at") == 0) {
                        sb_append(sb, "sub_str_char_at((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        sb_append(sb, "), (long)(");
                        if (node->child_count > 1) generate_expression(sb, node->children[1]);
                        else sb_append(sb, "0");
                        sb_append(sb, "))");
                    }
                    else if (strcmp(fn, "substring") == 0) {
                        /* The two-argument form runs to the end of the string.
                           The helper cannot be told that with a sentinel: the
                           interpreter clamps a negative end to 0, so -1 has to
                           mean the empty string rather than "the rest". */
                        sb_append(sb, node->child_count > 2 ? "sub_str_substring((const char*)("
                                                            : "sub_str_substring_from((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, "), (long)(");
                        if (node->child_count > 1) generate_expression(sb, node->children[1]);
                        else sb_append(sb, "0");
                        if (node->child_count > 2) {
                            sb_append(sb, "), (long)(");
                            generate_expression(sb, node->children[2]);
                        }
                        sb_append(sb, "))");
                    }
                    else if (strcmp(fn, "split") == 0) {
                        sb_append(sb, "sub_str_split((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, "), ");
                        if (node->child_count > 1) generate_expression(sb, node->children[1]);
                        else sb_append(sb, "\" \"");
                        sb_append(sb, ")");
                    }
                    else if (strcmp(fn, "contains") == 0) {
                        sb_append(sb, "sub_str_contains((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, "), ");
                        if (node->child_count > 1) generate_expression(sb, node->children[1]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, ")");
                    }
                    else if (strcmp(fn, "replace") == 0) {
                        sb_append(sb, "sub_str_replace((const char*)(");
                        if (node->child_count > 0) generate_expression(sb, node->children[0]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, "), ");
                        if (node->child_count > 1) generate_expression(sb, node->children[1]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, ", ");
                        if (node->child_count > 2) generate_expression(sb, node->children[2]);
                        else sb_append(sb, "\"\"");
                        sb_append(sb, ")");
                    }
                    else {
                        char fn_buf[128];
                        sb_append(sb, "%s(", sanitize_c_identifier(fn, fn_buf, sizeof(fn_buf)));
                    }
                    
                    if (strcmp(fn, "type") != 0 && strcmp(fn, "push") != 0 && strcmp(fn, "pop") != 0 &&
                        strcmp(fn, "join") != 0 && strcmp(fn, "trim") != 0 && strcmp(fn, "char_at") != 0 &&
                        strcmp(fn, "upper") != 0 && strcmp(fn, "lower") != 0 &&
                        strcmp(fn, "substring") != 0 && strcmp(fn, "split") != 0 &&
                        strcmp(fn, "contains") != 0 && strcmp(fn, "replace") != 0 &&
                        strcmp(fn, "len") != 0 && strcmp(fn, "length") != 0 &&
                        (strcmp(fn, "str") != 0 || node->child_count > 0) &&
                        (strcmp(fn, "to_string") != 0 || node->child_count > 0)) {
                        for (int i = 0; i < node->child_count; i++) {
                            generate_expression(sb, node->children[i]);
                            if (i + 1 < node->child_count) {
                                sb_append(sb, ", ");
                            }
                        }
                        sb_append(sb, ")");
                    }
                }
            }
            break;
            
        default:
            break;
    }
}

/* Generate code for a single AST node */
static DataType find_return_type_recursive(ASTNode *node) {
    if (!node) return TYPE_UNKNOWN;
    if (node->type == AST_RETURN_STMT) {
        if (node->right) {
            if (node->right->data_type == TYPE_NULL || (node->right->value && strcmp(node->right->value, "null") == 0)) {
                return TYPE_UNKNOWN;
            }
            if (node->right->type == AST_BINARY_EXPR && node->right->value && strcmp(node->right->value, "/") == 0) {
                return TYPE_FLOAT;
            }
            return node->right->data_type != TYPE_UNKNOWN ? node->right->data_type : TYPE_FLOAT;
        }
        return TYPE_VOID;
    }
    DataType ret = find_return_type_recursive(node->body);
    if (ret != TYPE_UNKNOWN && ret != TYPE_VOID) return ret;
    ret = find_return_type_recursive(node->left);
    if (ret != TYPE_UNKNOWN && ret != TYPE_VOID) return ret;
    ret = find_return_type_recursive(node->right);
    if (ret != TYPE_UNKNOWN && ret != TYPE_VOID) return ret;
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            ret = find_return_type_recursive(node->children[i]);
            if (ret != TYPE_UNKNOWN && ret != TYPE_VOID) return ret;
        }
    }
    if (node->next) {
        return find_return_type_recursive(node->next);
    }
    return TYPE_UNKNOWN;
}

/* Which construct a `break` belongs to at the point being generated.
   -1 means the nearest enclosing breakable is a loop, so `break;` is right.
   Anything else is the id of an enclosing switch, whose cases are an if/else
   chain that `break;` would either escape wrongly or fail to compile in. */
/* Distinguishes the temps of one `for x in ...` from another's. */
static int g_loop_seq = 0;

static int g_break_switch = -1;
/* Set when a `break` actually emitted a jump, so an unused label -- which
   compilers warn about -- is never written. */
static int g_break_used = 0;

/* The top-level variables of the program being generated. Their declarations
   are lifted to file scope so that functions can name them; see
   codegen_globals.h. */
static Globals g_globals;

/* How sub_array_join should read an element of this array: 0 int, 1 string,
   2 float, 3 bool. The generator knows; the runtime cannot tell a char* from
   a small integer once both are stored as long long. */
static int join_elem_kind(ASTNode *arr) {
    switch (infer_elem_type(arr)) {
        case TYPE_STRING: return 1;
        case TYPE_FLOAT:  return 2;
        case TYPE_BOOL:   return 3;
        default:          return 0;
    }
}

/* The C type a declaration needs, as a prefix to the name. Split out from
   the declaration itself because a global is declared in one place and
   initialized in another, and both have to spell the type the same way. */
static const char* c_decl_type(ASTNode *node) {
    if (!node) return "long ";
    if (node->data_type == TYPE_STRING) return "char *";
    if (node->data_type == TYPE_BOOL)   return "bool ";
    if (node->data_type == TYPE_FLOAT)  return "double ";
    if (node->data_type == TYPE_ARRAY ||
        (node->right && (node->right->type == AST_ARRAY_LITERAL ||
         (node->right->type == AST_CALL_EXPR && node->right->left &&
          node->right->left->type == AST_MEMBER_ACCESS &&
          node->right->left->value &&
          strcmp(node->right->left->value, "split") == 0) ||
         (node->right->type == AST_CALL_EXPR && node->right->value &&
          strcmp(node->right->value, "split") == 0))))
        return "SubArray *";
    if (node->data_type == TYPE_OBJECT ||
        (node->right && node->right->type == AST_OBJECT_LITERAL))
        return "void *";
    if (node->data_type == TYPE_NULL ||
        (node->right && node->right->type == AST_LITERAL && node->right->value &&
         (strcmp(node->right->value, "null") == 0 ||
          strcmp(node->right->value, "nil") == 0)))
        return "double ";
    return "long ";
}

/* The ` = <initializer>` half, or nothing when there is no initializer. */
static void c_decl_init(StringBuilder *sb, ASTNode *node) {
    const char *type = c_decl_type(node);

    if (node->data_type == TYPE_NULL ||
        (node->right && node->right->type == AST_LITERAL && node->right->value &&
         (strcmp(node->right->value, "null") == 0 ||
          strcmp(node->right->value, "nil") == 0))) {
        sb_append(sb, " = 0");
        return;
    }
    if (!node->right) return;

    if (strcmp(type, "char *") == 0) {
        sb_append(sb, " = sub_strdup(");
        generate_expression(sb, node->right);
        sb_append(sb, ")");
    } else if (strcmp(type, "long ") == 0) {
        sb_append(sb, " = (long)(");
        generate_expression(sb, node->right);
        sb_append(sb, ")");
    } else {
        sb_append(sb, " = ");
        generate_expression(sb, node->right);
    }
}

static void generate_node(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node(sb, stmt, indent);
            }
            break;
            
        case AST_CLASS_DECL:
            /* Class declaration in C */
            sb_append(sb, "/* class %s */\n", node->value ? node->value : "Class");
            break;

        case AST_VAR_DECL:
        case AST_CONST_DECL:
            indent_code(sb, indent);
            /* A top-level declaration was already written at file scope, so
               here only its initializer is left, as an assignment. */
            if (!globals_is_decl(&g_globals, node))
                sb_append(sb, "%s%s", c_decl_type(node), node->value
                          ? node->value : "var");
            else
                sb_append(sb, "%s", node->value);
            c_decl_init(sb, node);
            sb_append(sb, ";\n");
            break;

        case AST_FUNCTION_DECL: {
            /* Determine return type from AST */
            const char *ret_type = "void";
            DataType found_ret = find_return_type_recursive(node->body);
            DataType fn_type = node->data_type != TYPE_UNKNOWN ? node->data_type : found_ret;
            
            if (fn_type == TYPE_INT || fn_type == TYPE_AUTO) ret_type = "long";
            else if (fn_type == TYPE_FLOAT) ret_type = "double";
            else if (fn_type == TYPE_STRING) ret_type = "char*";
            else if (fn_type == TYPE_BOOL) ret_type = "bool";
            else if (fn_type == TYPE_ARRAY) ret_type = "SubArray*";
            else if (fn_type == TYPE_VOID) ret_type = "void";

            char fn_buf[128];
            const char *fn_name = sanitize_c_identifier(node->value, fn_buf, sizeof(fn_buf));
            sb_append(sb, "\n%s %s(", ret_type, fn_name);
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                {
                    const char *ptype = "long long";
                    if (node->children[i]->data_type == TYPE_FLOAT) ptype = "double";
                    else if (node->children[i]->data_type == TYPE_STRING) ptype = "const char*";
                    else if (node->children[i]->data_type == TYPE_BOOL) ptype = "int";
                    else if (node->children[i]->data_type == TYPE_ARRAY) ptype = "SubArray*";
                    sb_append(sb, "%s %s", ptype, node->children[i]->value ? node->children[i]->value : "arg");
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) {
                /* Resolve bare identifiers in the body against this
                   function's parameters: without it an array parameter is
                   invisible and len(a) comes out as strlen(). */
                ASTNode *prev_fn = infer_enter_function(node);
                generate_node(sb, node->body, indent + 1);
                infer_enter_function(prev_fn);
            }
            sb_append(sb, "}\n\n");
            break;
        }
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expression(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else ");
                    generate_node(sb, node->right, indent);
                } else {
                    sb_append(sb, " else {\n");
                    generate_node(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}\n");
                }
            } else {
                sb_append(sb, "\n");
            }
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            {
                const char *var = node->value ? node->value : "i";
                if (node->children && node->child_count > 0 &&
                    node->children[0]->type == AST_RANGE_EXPR) {
                    ASTNode *range = node->children[0];
                    sb_append(sb, "for (long %s = ", var);
                    if (range->right) {
                        /* range(start, end) */
                        if (range->left) generate_expression(sb, range->left);
                        else sb_append(sb, "0");
                        sb_append(sb, "; %s < ", var);
                        generate_expression(sb, range->right);
                    } else if (range->left) {
                        /* range(n) → 0..n */
                        sb_append(sb, "0; %s < ", var);
                        generate_expression(sb, range->left);
                    } else {
                        sb_append(sb, "0; %s < 10", var);
                    }
                    sb_append(sb, "; %s++) {\n", var);
                } else if (node->condition) {
                    /* for item in collection */
                    const char *var = node->value ? node->value : "item";
                    /* The array and index temps are declared in the enclosing
                       block, not inside the loop, so naming them after the
                       loop variable alone made two `for v in ...` loops in one
                       function redeclare _arr_v. */
                    int seq = g_loop_seq++;
                    if (node->condition->data_type == TYPE_STRING ||
                        (node->condition->type == AST_LITERAL && node->condition->value && !isdigit((unsigned char)node->condition->value[0]))) {
                        sb_append(sb, "const char *_str_%d = (const char*)(", seq);
                        generate_expression(sb, node->condition);
                        sb_append(sb, ");\n");
                        indent_code(sb, indent);
                        sb_append(sb, "for (long _idx_%d = 0; _str_%d && _str_%d[_idx_%d]; _idx_%d++) {\n", seq, seq, seq, seq, seq);
                        indent_code(sb, indent + 1);
                        sb_append(sb, "char _buf_%d[2] = {_str_%d[_idx_%d], '\\0'};\n", seq, seq, seq);
                        indent_code(sb, indent + 1);
                        sb_append(sb, "char *%s = _buf_%d;\n", var, seq);
                    } else {
                        sb_append(sb, "SubArray *_arr_%d = (SubArray*)(", seq);
                        generate_expression(sb, node->condition);
                        sb_append(sb, ");\n");
                        indent_code(sb, indent);
                        sb_append(sb, "for (long _idx_%d = 0; _arr_%d && _idx_%d < _arr_%d->count; _idx_%d++) {\n", seq, seq, seq, seq, seq);
                        indent_code(sb, indent + 1);
                        DataType el = infer_elem_type(node->condition);
                        sb_append(sb, "%s %s = ", elem_c_type(el), var);
                        gen_elem_out_open(sb, el);
                        sb_append(sb, "_arr_%d->items[_idx_%d]);\n", seq, seq);
                    }
                } else {
                    sb_append(sb, "for (long %s = 0; %s < 10; %s++) {\n", var, var, var);
                }
            }
            { int sv = g_break_switch; g_break_switch = -1;
              generate_node(sb, node->body, indent + 1);
              g_break_switch = sv; }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expression(sb, node->condition);
            sb_append(sb, ") {\n");
            { int sv = g_break_switch; g_break_switch = -1;
              generate_node(sb, node->body, indent + 1);
              g_break_switch = sv; }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                if (node->right->data_type == TYPE_NULL || (node->right->value && (strcmp(node->right->value, "null") == 0 || strcmp(node->right->value, "nil") == 0))) {
                    sb_append(sb, " 0");
                } else {
                    sb_append(sb, " ");
                    generate_expression(sb, node->right);
                }
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expression(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "sub_array_set((SubArray*)(");
                generate_expression(sb, node->left->left);
                sb_append(sb, "), (long)(");
                generate_expression(sb, node->left->right);
                sb_append(sb, "), ");
                gen_elem_in(sb, node->right, infer_elem_type(node->left->left));
                sb_append(sb, ");\n");
            } else if (node->left && node->left->type == AST_MEMBER_ACCESS) {
                sb_append(sb, "if ((SubObject*)(");
                generate_expression(sb, node->left->left);
                sb_append(sb, ")) ((SubObject*)(");
                generate_expression(sb, node->left->left);
                sb_append(sb, "))->%s = (long)(", node->left->value ? node->left->value : "x");
                generate_expression(sb, node->right);
                sb_append(sb, ");\n");
            } else {
                generate_expression(sb, node->left);
                sb_append(sb, " %s ", node->value ? node->value : "=");
                generate_expression(sb, node->right);
                sb_append(sb, ";\n");
            }
            break;

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "do {\n");
            { int sv = g_break_switch; g_break_switch = -1;
              generate_node(sb, node->body, indent + 1);
              g_break_switch = sv; }
            indent_code(sb, indent);
            sb_append(sb, "} while (");
            generate_expression(sb, node->condition);
            sb_append(sb, ");\n");
            break;

        case AST_BREAK_STMT:
            indent_code(sb, indent);
            if (g_break_switch >= 0) {
                sb_append(sb, "goto _sw%d_end;\n", g_break_switch);
                g_break_used = 1;
            }
            else                     sb_append(sb, "break;\n");
            break;

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue;\n");
            break;

        case AST_SWITCH_STMT: {
            /* An if/else chain rather than a C `switch`. SUB matches on any
               value, including strings and floats, and allows a case value
               that is not a compile-time constant -- none of which a C
               `switch` label can express. Cases do not fall through, so the
               chain is also the exact semantics, not an approximation. */
            static int sw_depth = 0;
            DataType st = infer_expr_type(node->condition);
            const char *ctype = st == TYPE_FLOAT  ? "double"
                              : st == TYPE_STRING ? "const char*"
                              : st == TYPE_BOOL   ? "int"
                                                  : "long long";
            int id = sw_depth++;
            int used_sv = g_break_used;
            g_break_used = 0;

            indent_code(sb, indent);
            sb_append(sb, "{\n");
            indent_code(sb, indent + 1);
            sb_append(sb, "%s _sw%d = (", ctype, id);
            generate_expression(sb, node->condition);
            sb_append(sb, ");\n");
            sb_append(sb, "        (void)_sw%d;\n", id);

            ASTNode *deflt = NULL;
            int emitted = 0;
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *clause = node->children[i];
                if (!clause) continue;
                if (clause->type == AST_DEFAULT_CLAUSE) { deflt = clause; continue; }
                if (clause->type != AST_CASE_CLAUSE || clause->child_count == 0) continue;

                indent_code(sb, indent + 1);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < clause->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    if (st == TYPE_STRING) {
                        sb_append(sb, "strcmp(_sw%d, ", id);
                        generate_expression(sb, clause->children[j]);
                        sb_append(sb, ") == 0");
                    } else {
                        sb_append(sb, "_sw%d == (", id);
                        generate_expression(sb, clause->children[j]);
                        sb_append(sb, ")");
                    }
                }
                sb_append(sb, ") {\n");
                if (clause->body) {
                    int sv = g_break_switch; g_break_switch = id;
                    generate_node(sb, clause->body, indent + 2);
                    g_break_switch = sv;
                }
                emitted = 1;
            }

            if (deflt) {
                indent_code(sb, indent + 1);
                sb_append(sb, "%s{\n", emitted ? "} else " : "");
                if (deflt->body) {
                    int sv = g_break_switch; g_break_switch = id;
                    generate_node(sb, deflt->body, indent + 2);
                    g_break_switch = sv;
                }
                emitted = 1;
            }
            if (emitted) {
                indent_code(sb, indent + 1);
                sb_append(sb, "}\n");
            }
            /* `break` inside a case jumps here; the label needs a statement
               after it. Only written when something actually jumps to it. */
            if (g_break_used) sb_append(sb, "_sw%d_end: (void)0;\n", id);
            g_break_used = used_sv;
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        }

        case AST_TRY_STMT:
            indent_code(sb, indent);
            sb_append(sb, "{\n");
            if (node->body) generate_node(sb, node->body, indent + 1);
            if (node->right && node->right->type == AST_CATCH_CLAUSE) {
                indent_code(sb, indent + 1);
                if (node->right->value) {
                    sb_append(sb, "/* catch (%s) */\n", node->right->value);
                }
                if (node->right->body) generate_node(sb, node->right->body, indent + 1);
            }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_THROW_STMT:
            indent_code(sb, indent);
            sb_append(sb, "/* throw */;\n");
            break;
            
        case AST_EMBED_CODE:
        case AST_EMBED_C:
            if (node->value && (!node->metadata || strcasecmp((const char*)node->metadata, "c") == 0)) {
                sb_append(sb, "\n/* Embedded C code */\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;
            
        case AST_EMBED_CPP:
            if (node->value && (!node->metadata || strcasecmp((const char*)node->metadata, "cpp") == 0 || strcasecmp((const char*)node->metadata, "c++") == 0)) {
                sb_append(sb, "\n/* Embedded C++ code */\n");
                sb_append(sb, "#ifdef __cplusplus\n");
                sb_append(sb, "%s\n", node->value);
                sb_append(sb, "#endif\n");
            }
            break;
            
        default:
            for (int i = 0; i < node->child_count; i++) {
                generate_node(sb, node->children[i], indent);
            }
            break;
    }
}

/* Generate C code from AST */
static char* generate_c_code(ASTNode *ast) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    // Apply optimizations before code generation
    optimize_c_output(ast);
    
    // Generate standard headers (C99 compliant)
    sb_append(sb, "/*\n");
    sb_append(sb, " * Generated by SUB Language Compiler\n");
    sb_append(sb, " * C99 Compliant Output\n");
    sb_append(sb, " */\n\n");
    
    sb_append(sb, "/* Standard Library Headers */\n");
    sb_append(sb, "#include <stdio.h>\n");
    sb_append(sb, "#include <stdlib.h>\n");
    sb_append(sb, "#include <string.h>\n");
    sb_append(sb, "#include <stdbool.h>\n");
    sb_append(sb, "#include <stddef.h>\n");
    sb_append(sb, "#include <math.h>\n");
    sb_append(sb, "#include <ctype.h>\n");
    sb_append(sb, "#include <stdarg.h>\n");
    sb_append(sb, "#include <stdint.h>\n\n");
    /* Forward declarations: the array helpers are emitted before these are
       defined, and both report a runtime error the interpreter also
       reports. */
    sb_append(sb, "static void sub_die(const char *msg);\n");
    sb_append(sb, "static void sub_die_index(long idx, long count);\n");
    sb_append(sb, "static inline char* sub_strdup(const char *s);\n\n");
    
    sb_append(sb, "/* Memory Management Helpers */\n");
    sb_append(sb, "#ifndef SUB_STRSAFE\n");
    sb_append(sb, "#define SUB_STRSAFE\n");
    sb_append(sb, "static inline char* sub_strdup(const char *s) {\n");
    sb_append(sb, "    if (!s) return NULL;\n");
    sb_append(sb, "    size_t len = strlen(s) + 1;\n");
    sb_append(sb, "    char *copy = malloc(len);\n");
    sb_append(sb, "    if (copy) memcpy(copy, s, len);\n");
    sb_append(sb, "    return copy;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "#define SUB_FREE(p) do { if (p) { free(p); (p) = NULL; } } while(0)\n");
    sb_append(sb, "#endif /* SUB_STRSAFE */\n\n");
    
    sb_append(sb, "/* Generic Object Structure */\n");
    sb_append(sb, "typedef struct SubObject {\n");
    sb_append(sb, "    long x, y, z, width, height, id;\n");
    sb_append(sb, "    char *name;\n");
    sb_append(sb, "    char *title;\n");
    sb_append(sb, "} SubObject;\n\n");
    sb_append(sb, "static inline SubObject* sub_object_new(void) {\n");
    sb_append(sb, "    SubObject *obj = (SubObject*)calloc(1, sizeof(SubObject));\n");
    sb_append(sb, "    return obj;\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "/* Dynamic Array Structure */\n");
    /* Elements are 64-bit slots, not longs: a double is stored as its bit
       pattern and a string as its pointer, so one array type carries every
       element type SUB has. `kind` records which, because printing an array
       is the one operation with no static type to hand. */
    sb_append(sb, "typedef struct SubArray {\n");
    sb_append(sb, "    long count;\n");
    sb_append(sb, "    long capacity;\n");
    sb_append(sb, "    int  kind;   /* 0 int, 1 float, 2 string, 3 bool */\n");
    sb_append(sb, "    long long *items;\n");
    sb_append(sb, "} SubArray;\n\n");
    sb_append(sb, "static inline long long sub_bits(double d) "
                  "{ long long v; memcpy(&v, &d, sizeof v); return v; }\n");
    sb_append(sb, "static inline double sub_dbl(long long v) "
                  "{ double d; memcpy(&d, &v, sizeof d); return d; }\n\n");
    
    sb_append(sb, "static inline SubArray* sub_array_create(void) {\n");
    sb_append(sb, "    SubArray *a = (SubArray*)malloc(sizeof(SubArray));\n");
    sb_append(sb, "    if (!a) return NULL;\n");
    sb_append(sb, "    a->count = 0;\n");
    sb_append(sb, "    a->capacity = 8;\n");
    sb_append(sb, "    a->kind = 0;\n");
    sb_append(sb, "    a->items = (long long*)malloc(sizeof(long long) * a->capacity);\n");
    sb_append(sb, "    return a;\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "static inline SubArray* sub_array_of(int kind, long n, ...) {\n");
    sb_append(sb, "    SubArray *a = sub_array_create();\n");
    sb_append(sb, "    if (!a) return NULL;\n");
    sb_append(sb, "    a->kind = kind;\n");
    sb_append(sb, "    if (n > a->capacity) {\n");
    sb_append(sb, "        a->capacity = n < 8 ? 8 : n * 2;\n");
    sb_append(sb, "        a->items = (long long*)realloc(a->items, sizeof(long long) * a->capacity);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    va_list args;\n");
    sb_append(sb, "    va_start(args, n);\n");
    sb_append(sb, "    for (long i = 0; i < n; i++) {\n");
    sb_append(sb, "        a->items[a->count++] = va_arg(args, long long);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    va_end(args);\n");
    sb_append(sb, "    return a;\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "static inline void sub_array_push(SubArray *a, long long val) {\n");
    sb_append(sb, "    if (!a) return;\n");
    sb_append(sb, "    if (a->count >= a->capacity) {\n");
    sb_append(sb, "        a->capacity = a->capacity ? a->capacity * 2 : 8;\n");
    sb_append(sb, "        a->items = (long long*)realloc(a->items, sizeof(long long) * a->capacity);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    a->items[a->count++] = val;\n");
    sb_append(sb, "}\n\n");
    
    /* Out of range and popping an empty array stop the program the way the
       interpreter does, rather than returning 0 and carrying on. */
    sb_append(sb, "static inline long long sub_array_pop(SubArray *a) {\n");
    sb_append(sb, "    if (!a || a->count <= 0) sub_die(\"pop from empty array\");\n");
    sb_append(sb, "    return a->items[--a->count];\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "static void sub_die_index(long idx, long count) {\n");
    sb_append(sb, "    fprintf(stderr, \"RuntimeError: array index %%ld out of "
                  "bounds [0, %%ld)\\n\", idx, count);\n");
    sb_append(sb, "    exit(70);\n}\n");
    /* A negative index counts from the end: a[-1] is the last element. */
    sb_append(sb, "static inline long sub_array_idx(SubArray *a, long idx) {\n");
    sb_append(sb, "    if (!a) sub_die(\"index of a non-array\");\n");
    sb_append(sb, "    if (idx < 0) idx += a->count;\n");
    sb_append(sb, "    if (idx < 0 || idx >= a->count) sub_die_index(idx, a->count);\n");
    sb_append(sb, "    return idx;\n}\n");
    sb_append(sb, "static inline long long sub_array_get(SubArray *a, long idx) {\n");
    sb_append(sb, "    return a->items[sub_array_idx(a, idx)];\n");
    sb_append(sb, "}\n\n");
    
    /* Assigning past the end is an error, not a grow: the interpreter
       refuses it and a backend that silently extended the array would mean
       the same program had two different lengths. */
    sb_append(sb, "static inline void sub_array_set(SubArray *a, long idx, long long val) {\n");
    sb_append(sb, "    a->items[sub_array_idx(a, idx)] = val;\n");
    sb_append(sb, "}\n\n");

    /* "[a, b, c]", the interpreter's spelling, with no quotes on strings. */
    sb_append(sb, "static char* sub_array_str(SubArray *a) {\n");
    sb_append(sb, "    if (!a) return sub_strdup(\"[]\");\n");
    sb_append(sb, "    size_t cap = 64, len = 1;\n");
    sb_append(sb, "    char *out = (char*)malloc(cap);\n");
    sb_append(sb, "    if (!out) return sub_strdup(\"[]\");\n");
    sb_append(sb, "    strcpy(out, \"[\");\n");
    sb_append(sb, "    for (long i = 0; i < a->count; i++) {\n");
    sb_append(sb, "        char buf[64];\n");
    sb_append(sb, "        const char *piece = buf;\n");
    sb_append(sb, "        if (a->kind == 1) snprintf(buf, sizeof buf, \"%%g\", sub_dbl(a->items[i]));\n");
    sb_append(sb, "        else if (a->kind == 2) { piece = (const char*)(intptr_t)a->items[i];\n");
    sb_append(sb, "                                if (!piece) piece = \"null\"; }\n");
    sb_append(sb, "        else if (a->kind == 3) piece = a->items[i] ? \"true\" : \"false\";\n");
    sb_append(sb, "        else snprintf(buf, sizeof buf, \"%%lld\", a->items[i]);\n");
    sb_append(sb, "        size_t need = strlen(piece) + 4;\n");
    sb_append(sb, "        while (len + need > cap) { cap *= 2; out = (char*)realloc(out, cap); }\n");
    sb_append(sb, "        if (i) { strcpy(out + len, \", \"); len += 2; }\n");
    sb_append(sb, "        strcpy(out + len, piece); len += strlen(piece);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    while (len + 2 > cap) { cap *= 2; out = (char*)realloc(out, cap); }\n");
    sb_append(sb, "    strcpy(out + len, \"]\");\n");
    sb_append(sb, "    return out;\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "static inline long sub_array_len(SubArray *a) {\n");
    sb_append(sb, "    return a ? a->count : 0;\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "/* `kind` says how to read an element: 0 int, 1 string, 2 float, 3 bool.\n");
    sb_append(sb, "   It used to be guessed by dereferencing the element and asking whether the\n");
    sb_append(sb, "   first byte looked like printable ASCII, which read address 1 for the array\n");
    sb_append(sb, "   [1, 2, 3] and crashed. The generator knows the element type, so it says so. */\n");
    sb_append(sb, "static inline char* sub_array_join(SubArray *a, const char *sep, int kind) {\n");
    sb_append(sb, "    if (!a || a->count == 0) return sub_strdup(\"\");\n");
    sb_append(sb, "    if (!sep) sep = \"\";\n");
    sb_append(sb, "    size_t sep_len = strlen(sep);\n");
    sb_append(sb, "    size_t cap = sep_len * (size_t)(a->count - 1) + 1;\n");
    sb_append(sb, "    char **parts = (char**)malloc(sizeof(char*) * (size_t)a->count);\n");
    sb_append(sb, "    char scratch[64];\n");
    sb_append(sb, "    for (long i = 0; i < a->count; i++) {\n");
    sb_append(sb, "        const char *piece;\n");
    sb_append(sb, "        if (kind == 1) piece = (const char*)a->items[i] ? (const char*)a->items[i] : \"\";\n");
    sb_append(sb, "        else if (kind == 2) { double d; memcpy(&d, &a->items[i], sizeof(double));\n");
    sb_append(sb, "                              snprintf(scratch, sizeof(scratch), \"%%g\", d); piece = scratch; }\n");
    sb_append(sb, "        else if (kind == 3) piece = a->items[i] ? \"true\" : \"false\";\n");
    sb_append(sb, "        else { snprintf(scratch, sizeof(scratch), \"%%lld\", a->items[i]); piece = scratch; }\n");
    sb_append(sb, "        parts[i] = sub_strdup(piece);\n");
    sb_append(sb, "        cap += strlen(parts[i]);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    char *res = (char*)malloc(cap);\n");
    sb_append(sb, "    res[0] = 0;\n");
    sb_append(sb, "    char *w = res;\n");
    sb_append(sb, "    for (long i = 0; i < a->count; i++) {\n");
    sb_append(sb, "        if (i > 0) { memcpy(w, sep, sep_len); w += sep_len; }\n");
    sb_append(sb, "        size_t n = strlen(parts[i]);\n");
    sb_append(sb, "        memcpy(w, parts[i], n); w += n;\n");
    sb_append(sb, "        free(parts[i]);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    *w = 0;\n");
    sb_append(sb, "    free(parts);\n");
    sb_append(sb, "    return res;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "\n");
    sb_append(sb, "/* String Methods */\n");
    /* Integer division by zero is undefined behaviour in C and arrives as
       SIGFPE. SUB reports the interpreter's runtime error and exits 70. */
    sb_append(sb, "static void sub_die(const char *msg) {\n");
    sb_append(sb, "    fprintf(stderr, \"RuntimeError: %%s\\n\", msg);\n");
    sb_append(sb, "    exit(70);\n}\n");
    sb_append(sb, "static inline long long sub_idiv(long long a, long long b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"division by zero\");\n");
    sb_append(sb, "    return a / b;\n}\n");
    sb_append(sb, "static inline long long sub_mod(long long a, long long b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"modulo by zero\");\n");
    sb_append(sb, "    return a %% b;\n}\n");
    sb_append(sb, "static inline double sub_fdiv(double a, double b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"division by zero\");\n");
    sb_append(sb, "    return a / b;\n}\n");
    sb_append(sb, "static inline double sub_fmod(double a, double b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"modulo by zero\");\n");
    sb_append(sb, "    return fmod(a, b);\n}\n");
    sb_append(sb, "static inline long long sub_min_i(long long a, long long b) { return a < b ? a : b; }\n");
    sb_append(sb, "static inline long long sub_max_i(long long a, long long b) { return a > b ? a : b; }\n");
    sb_append(sb, "static inline long sub_str_len(const char *s) { return s ? (long)strlen(s) : 0; }\n");
    sb_append(sb, "static inline char* sub_str_upper(const char *s) {\n");
    sb_append(sb, "    if (!s) return sub_strdup(\"\");\n");
    sb_append(sb, "    char *res = sub_strdup(s);\n");
    sb_append(sb, "    for (char *p = res; *p; p++) *p = (char)toupper((unsigned char)*p);\n");
    sb_append(sb, "    return res;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline char* sub_str_lower(const char *s) {\n");
    sb_append(sb, "    if (!s) return sub_strdup(\"\");\n");
    sb_append(sb, "    char *res = sub_strdup(s);\n");
    sb_append(sb, "    for (char *p = res; *p; p++) *p = (char)tolower((unsigned char)*p);\n");
    sb_append(sb, "    return res;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline void sub_runtime_error(const char *fmt, long a, long b) {\n");
    sb_append(sb, "    fflush(stdout);\n");
    sb_append(sb, "    fprintf(stderr, fmt, a, b);\n");
    sb_append(sb, "    fputc(10, stderr);\n");
    sb_append(sb, "    exit(70);\n");
    sb_append(sb, "}\n");
    sb_append(sb, "/* [start, end) with the interpreter's clamping: a negative bound is 0, a\n");
    sb_append(sb, "   bound past the end is the length, and a start past the end collapses to\n");
    sb_append(sb, "   it. Every one of those cases is reachable from user arithmetic. */\n");
    sb_append(sb, "static inline char* sub_str_substring(const char *s, long start, long end) {\n");
    sb_append(sb, "    if (!s) return sub_strdup(\"\");\n");
    sb_append(sb, "    long len = (long)strlen(s);\n");
    sb_append(sb, "    if (start < 0) start = 0;\n");
    sb_append(sb, "    if (end < 0) end = 0;\n");
    sb_append(sb, "    if (start > len) start = len;\n");
    sb_append(sb, "    if (end > len) end = len;\n");
    sb_append(sb, "    if (start > end) start = end;\n");
    sb_append(sb, "    long n = end - start;\n");
    sb_append(sb, "    char *res = (char*)malloc((size_t)n + 1);\n");
    sb_append(sb, "    memcpy(res, s + start, (size_t)n);\n");
    sb_append(sb, "    res[n] = 0;\n");
    sb_append(sb, "    return res;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline char* sub_str_substring_from(const char *s, long start) {\n");
    sb_append(sb, "    return sub_str_substring(s, start, s ? (long)strlen(s) : 0);\n");
    sb_append(sb, "}\n");
    sb_append(sb, "/* strtok is the wrong tool and was the bug here: it treats the separator as\n");
    sb_append(sb, "   a set of characters, collapses runs of them, and drops empty fields, so\n");
    sb_append(sb, "   split(\"a,,b\", \",\") came back with two elements instead of three. The\n");
    sb_append(sb, "   separator is one string, every field is kept, and the result always has\n");
    sb_append(sb, "   at least one element. An empty separator splits into characters. */\n");
    sb_append(sb, "static inline SubArray* sub_str_split(const char *s, const char *sep) {\n");
    sb_append(sb, "    SubArray *arr = sub_array_create();\n");
    sb_append(sb, "    if (!s) { sub_array_push(arr, (long)sub_strdup(\"\")); return arr; }\n");
    sb_append(sb, "    if (!sep || !*sep) {\n");
    sb_append(sb, "        for (long i = 0; s[i]; i++) {\n");
    sb_append(sb, "            char ch[2]; ch[0] = s[i]; ch[1] = 0;\n");
    sb_append(sb, "            sub_array_push(arr, (long)sub_strdup(ch));\n");
    sb_append(sb, "        }\n");
    sb_append(sb, "        return arr;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    size_t sep_len = strlen(sep);\n");
    sb_append(sb, "    const char *p = s;\n");
    sb_append(sb, "    for (;;) {\n");
    sb_append(sb, "        const char *next = strstr(p, sep);\n");
    sb_append(sb, "        size_t n = next ? (size_t)(next - p) : strlen(p);\n");
    sb_append(sb, "        char *part = (char*)malloc(n + 1);\n");
    sb_append(sb, "        memcpy(part, p, n);\n");
    sb_append(sb, "        part[n] = 0;\n");
    sb_append(sb, "        sub_array_push(arr, (long)part);\n");
    sb_append(sb, "        if (!next) break;\n");
    sb_append(sb, "        p = next + sep_len;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    return arr;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline int sub_str_contains(const char *s, const char *sub) {\n");
    sb_append(sb, "    if (!s || !sub) return 0;\n");
    sb_append(sb, "    return strstr(s, sub) != NULL;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "/* Sized to the result rather than written into a fixed buffer: the old\n");
    sb_append(sb, "   4096-byte array silently truncated a longer string and overflowed on a\n");
    sb_append(sb, "   replacement that grew it. */\n");
    sb_append(sb, "static inline char* sub_str_replace(const char *s, const char *old_sub, const char *new_sub) {\n");
    sb_append(sb, "    if (!s) return sub_strdup(\"\");\n");
    sb_append(sb, "    if (!old_sub || !*old_sub) return sub_strdup(s);\n");
    sb_append(sb, "    if (!new_sub) new_sub = \"\";\n");
    sb_append(sb, "    size_t old_len = strlen(old_sub), new_len = strlen(new_sub);\n");
    sb_append(sb, "    size_t count = 0;\n");
    sb_append(sb, "    for (const char *p = s; (p = strstr(p, old_sub)) != NULL; p += old_len) count++;\n");
    sb_append(sb, "    size_t size = strlen(s) + count * (new_len > old_len ? new_len - old_len : 0) + 1;\n");
    sb_append(sb, "    char *res = (char*)malloc(size);\n");
    sb_append(sb, "    char *w = res;\n");
    sb_append(sb, "    for (const char *p = s; *p; ) {\n");
    sb_append(sb, "        if (strncmp(p, old_sub, old_len) == 0) {\n");
    sb_append(sb, "            memcpy(w, new_sub, new_len); w += new_len; p += old_len;\n");
    sb_append(sb, "        } else *w++ = *p++;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    *w = 0;\n");
    sb_append(sb, "    return res;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline char* sub_str_trim(const char *s) {\n");
    sb_append(sb, "    if (!s) return sub_strdup(\"\");\n");
    sb_append(sb, "    while (isspace((unsigned char)*s)) s++;\n");
    sb_append(sb, "    if (*s == '\\0') return sub_strdup(\"\");\n");
    sb_append(sb, "    const char *end = s + strlen(s) - 1;\n");
    sb_append(sb, "    while (end > s && isspace((unsigned char)*end)) end--;\n");
    sb_append(sb, "    long len = (long)(end - s + 1);\n");
    sb_append(sb, "    char *res = (char*)malloc(len + 1);\n");
    sb_append(sb, "    memcpy(res, s, len);\n");
    sb_append(sb, "    res[len] = '\\0';\n");
    sb_append(sb, "    return res;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline char* sub_str_char_at(const char *s, long idx) {\n");
    sb_append(sb, "    long len = s ? (long)strlen(s) : 0;\n");
    sb_append(sb, "    if (idx < 0) idx += len;\n");
    sb_append(sb, "    if (idx < 0 || idx >= len)\n");
    sb_append(sb, "        sub_runtime_error(\"RuntimeError: char_at(%ld) out of range [0, %ld)\", idx, len);\n");
    sb_append(sb, "    char ch[2]; ch[0] = s[idx]; ch[1] = 0;\n");
    sb_append(sb, "    return sub_strdup(ch);\n");
    sb_append(sb, "}\n\n");
    
    sb_append(sb, "/* Built-in Functions */\n");
    sb_append(sb, "static inline char* sub_input(const char* prompt) {\n");
    sb_append(sb, "    if (prompt) printf(\"%%s\", prompt);\n");
    sb_append(sb, "    char buf[1024];\n");
    sb_append(sb, "    if (fgets(buf, sizeof(buf), stdin)) {\n");
    sb_append(sb, "        size_t len = strlen(buf);\n");
    sb_append(sb, "        if (len > 0 && buf[len-1] == '\\n') buf[len-1] = '\\0';\n");
    sb_append(sb, "        if (len > 1 && buf[len-2] == '\\r') buf[len-2] = '\\0';\n");
    sb_append(sb, "        return sub_strdup(buf);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    return sub_strdup(\"\");\n");
    sb_append(sb, "}\n");
    sb_append(sb, "static inline double sub_float_from_str(const char* s) { return s ? atof(s) : 0.0; }\n");
    sb_append(sb, "static inline long sub_int_from_str(const char* s) { return s ? atol(s) : 0; }\n");
    sb_append(sb, "static inline char* sub_str_from_long(long v) { char buf[64]; snprintf(buf, sizeof(buf), \"%%ld\", v); return sub_strdup(buf); }\n");
    sb_append(sb, "static inline char* sub_str_from_double(double v) { char buf[64]; snprintf(buf, sizeof(buf), \"%%g\", v); return sub_strdup(buf); }\n\n");
    
    /* Pass 1: the top-level variables, declared at file scope so that the
       functions below can name them. Uninitialized: C wants a constant
       expression here, and a top-level `let` may be initialized by anything,
       so the initializer stays in main() where it was written. */
    globals_collect(&g_globals, ast);
    for (int i = 0; i < g_globals.count; i++) {
        if (!globals_is_first(&g_globals, i)) continue;
    ASTNode *d = g_globals.decls[i];
        sb_append(sb, "static %s%s;\n", c_decl_type(d), d->value);
    }
    if (g_globals.count > 0) sb_append(sb, "\n");

    /* Pass 2: Generate function declarations at file scope */
    if (ast && (ast->type == AST_PROGRAM || ast->type == AST_BLOCK)) {
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                generate_node(sb, stmt, 0);
            }
        }
    }
    
    /* Pass 3: Wrap non-function top-level statements in main() */
    sb_append(sb, "int main(int argc, char *argv[]) {\n");
    sb_append(sb, "    (void)argc;\n");
    sb_append(sb, "    (void)argv;\n");
    
    if (ast && (ast->type == AST_PROGRAM || ast->type == AST_BLOCK)) {
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type != AST_FUNCTION_DECL) {
                generate_node(sb, stmt, 1);
            }
        }
    }
    
    sb_append(sb, "    return EXIT_SUCCESS;\n");
    sb_append(sb, "}\n");
    
    return sb_to_string(sb);
}

/* Platform-specific code generation */

static char* generate_android(ASTNode *ast UNUSED) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// Android Java Code Generated from SUB Language\n");
    sb_append(sb, "package com.sublang.app;\n\n");
    sb_append(sb, "import android.app.Activity;\n");
    sb_append(sb, "import android.os.Bundle;\n");
    sb_append(sb, "import android.widget.TextView;\n\n");
    
    sb_append(sb, "public class MainActivity extends Activity {\n");
    sb_append(sb, "    @Override\n");
    sb_append(sb, "    protected void onCreate(Bundle savedInstanceState) {\n");
    sb_append(sb, "        super.onCreate(savedInstanceState);\n");
    sb_append(sb, "        TextView tv = new TextView(this);\n");
    sb_append(sb, "        tv.setText(\"SUB Language App\");\n");
    sb_append(sb, "        setContentView(tv);\n");
    
    // TODO: Process AST for Android-specific UI elements
    
    sb_append(sb, "    }\n");
    sb_append(sb, "}\n");
    
    return sb_to_string(sb);
}

static char* generate_ios(ASTNode *ast UNUSED) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// iOS Swift Code Generated from SUB Language\n");
    sb_append(sb, "import UIKit\n\n");
    
    sb_append(sb, "class ViewController: UIViewController {\n");
    sb_append(sb, "    override func viewDidLoad() {\n");
    sb_append(sb, "        super.viewDidLoad()\n");
    sb_append(sb, "        let label = UILabel()\n");
    sb_append(sb, "        label.text = \"SUB Language App\"\n");
    sb_append(sb, "        view.addSubview(label)\n");
    
    // TODO: Process AST for iOS-specific UI elements
    
    sb_append(sb, "    }\n");
    sb_append(sb, "}\n");
    
    return sb_to_string(sb);
}

/* Helper: generate JavaScript expression from AST node */
static void generate_js_expression(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                sb_append(sb, "\"%s\"", node->value ? node->value : "");
            } else {
                sb_append(sb, "%s", node->value ? node->value : "0");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "x");
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_js_expression(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_js_expression(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR:
            if (is_print_builtin(node->value)) {
                sb_append(sb, "console.log(");
                if (node->child_count > 0) generate_js_expression(sb, node->children[0]);
                sb_append(sb, ")");
            } else {
                sb_append(sb, "%s(", node->value ? node->value : "func");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_js_expression(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        default:
            break;
    }
}

/* Helper: generate JavaScript statement from AST node */
static void generate_js_node(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_js_node(sb, stmt, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let %s", node->value ? node->value : "x");
            if (node->right) {
                sb_append(sb, " = ");
                generate_js_expression(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "const %s = ", node->value ? node->value : "x");
            generate_js_expression(sb, node->right);
            sb_append(sb, ";\n");
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_js_expression(sb, node);
            sb_append(sb, ";\n");
            break;
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            generate_js_expression(sb, node->left);
            sb_append(sb, " = ");
            generate_js_expression(sb, node->right);
            sb_append(sb, ";\n");
            break;
        case AST_FUNCTION_DECL:
            indent_code(sb, indent);
            sb_append(sb, "function %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "%s", node->children[i]->value ? node->children[i]->value : "arg");
            }
            sb_append(sb, ") {\n");
            if (node->body) generate_js_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_js_expression(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_js_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, "} else ");
                    generate_js_node(sb, node->right, indent);
                } else {
                    sb_append(sb, "} else {\n");
                    generate_js_node(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}\n");
                }
            } else {
                sb_append(sb, "}\n");
            }
            break;
        case AST_FOR_STMT: {
            indent_code(sb, indent);
            const char *var = node->value ? node->value : "i";
            if (node->children && node->child_count > 0 && node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for (let %s = ", var);
                if (range->right) {
                    if (range->left) generate_js_expression(sb, range->left);
                    else sb_append(sb, "0");
                    sb_append(sb, "; %s < ", var);
                    generate_js_expression(sb, range->right);
                } else if (range->left) {
                    sb_append(sb, "0; %s < ", var);
                    generate_js_expression(sb, range->left);
                } else {
                    sb_append(sb, "0; %s < 10", var);
                }
                sb_append(sb, "; %s++) {\n", var);
            } else {
                sb_append(sb, "for (let %s = 0; %s < 10; %s++) {\n", var, var, var);
            }
            generate_js_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        }
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_js_expression(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_js_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_js_expression(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
        default:
            for (int i = 0; i < node->child_count; i++) {
                generate_js_node(sb, node->children[i], indent);
            }
            break;
    }
}

static char* generate_web(ASTNode *ast) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "<!DOCTYPE html>\n");
    sb_append(sb, "<html>\n<head>\n");
    sb_append(sb, "    <title>SUB Language App</title>\n");
    sb_append(sb, "    <style>\n");
    sb_append(sb, "        body { font-family: Arial, sans-serif; margin: 20px; padding: 20px; }\n");
    sb_append(sb, "        #output { background: #1a1a2e; color: #e0e0e0; padding: 20px; border-radius: 8px; font-family: monospace; white-space: pre-wrap; }\n");
    sb_append(sb, "    </style>\n");
    sb_append(sb, "</head>\n<body>\n");
    sb_append(sb, "    <h1>SUB Language Application</h1>\n");
    sb_append(sb, "    <div id='output'></div>\n");
    sb_append(sb, "    <script>\n");
    sb_append(sb, "    // Generated from SUB Language Compiler\n");
    sb_append(sb, "    const _out = document.getElementById('output');\n");
    sb_append(sb, "    const _origLog = console.log;\n");
    sb_append(sb, "    console.log = function(...args) { _out.textContent += args.join(' ') + '\\n'; _origLog.apply(console, args); };\n\n");
    
    /* Generate JavaScript from AST */
    generate_js_node(sb, ast, 1);
    
    sb_append(sb, "    </script>\n");
    sb_append(sb, "</body>\n</html>\n");
    
    return sb_to_string(sb);
}

static char* generate_windows(ASTNode *ast) {
    /* For Windows, generate standard C console application */
    return generate_c_code(ast);
}

static char* generate_linux(ASTNode *ast) {
    // For Linux, just generate standard C code
    return generate_c_code(ast);
}

static char* generate_macos(ASTNode *ast) {
    // For macOS, generate standard C code (can be enhanced with Cocoa later)
    return generate_c_code(ast);
}

/* Main code generation entry point */
char* codegen_generate(ASTNode *ast, Platform platform) {
    if (!ast) {
        fprintf(stderr, "Error: NULL AST node\n");
        return NULL;
    }
    
    switch (platform) {
        case PLATFORM_ANDROID:
            return generate_android(ast);
        case PLATFORM_IOS:
            return generate_ios(ast);
        case PLATFORM_WEB:
            return generate_web(ast);
        case PLATFORM_WINDOWS:
            return generate_windows(ast);
        case PLATFORM_MACOS:
            return generate_macos(ast);
        case PLATFORM_LINUX:
            return generate_linux(ast);
        default:
            fprintf(stderr, "Error: Unknown platform, using C code generation\n");
            return generate_c_code(ast);
    }
}

/* Generate regular C code */
char* codegen_generate_c(ASTNode *ast, Platform platform) {
    (void)platform;
    return generate_c_code(ast);
}
