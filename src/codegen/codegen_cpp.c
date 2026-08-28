/* ========================================
   SUB Language - C++ Code Generator Implementation
   Generates modern C++17 code from AST
   File: codegen_cpp.c
   ======================================== */

#define _GNU_SOURCE
#include "codegen_cpp.h"
#include "codegen_infer.h"
#include "codegen_switch.h"
#include "type_system.h"
#include "windows_compat.h"
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/* String Builder for code generation */
typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} StringBuilder;

/* SUB builtin conversions in C++ spelling. str() on something already a
   std::string must stay untouched - std::to_string has no string overload -
   so the caller checks the inferred argument type before using this. */
typedef struct { const char *sub_name, *prefix, *suffix; } CppBuiltin;

static const char* cpp_array_type(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return "std::vector<double>";
    case TYPE_STRING: return "std::vector<std::string>";
    case TYPE_BOOL:   return "std::vector<bool>";
    default:          return "std::vector<long long>";
    }
}

static const CppBuiltin* cpp_builtin(const char *name) {
    static const CppBuiltin table[] = {
        {"str","std::to_string(",")"},   {"to_string","std::to_string(",")"},
        {"int","(long long)(",")"},      {"float","(double)(",")"},
        {"bool","(bool)(",")"},          {"len","(long long)(",").size()"},
        {"length","(long long)(",").size()"},
        {"abs","std::abs(",")"},         {"sqrt","std::sqrt(",")"},
        {"upper","sub_upper(",")"},      {"lower","sub_lower(",")"},
        {"trim","sub_trim(",")"},
        /* SUB's floor/ceil/round return an integer, and its round() breaks
           ties away from zero - which is what std::round already does. */
        {"floor","(long long)std::floor(",")"},
        {"ceil","(long long)std::ceil(",")"},
        {"round","(long long)std::round(",")"},
        {NULL,NULL,NULL}
    };
    if (!name) return NULL;
    for (int i = 0; table[i].sub_name; i++)
        if (strcmp(table[i].sub_name, name) == 0) return &table[i];
    return NULL;
}

/* Builtins taking more than one argument; the table above wraps a single
   argument, which cannot express min(a, b). */
static const CppBuiltin* cpp_builtin_multi(const char *name) {
    static const CppBuiltin table[] = {
        {"min","std::min(",")"}, {"max","std::max(",")"}, {NULL,NULL,NULL}
    };
    if (!name) return NULL;
    for (int i = 0; table[i].sub_name; i++)
        if (strcmp(table[i].sub_name, name) == 0) return &table[i];
    return NULL;
}

static StringBuilder* sb_create(void) {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    sb->capacity = 16384;
    sb->buffer = malloc(sb->capacity);
    if (!sb->buffer) {
        free(sb);
        return NULL;
    }
    sb->buffer[0] = '\0';
    sb->size = 0;
    return sb;
}

static void sb_free(StringBuilder *sb) {
    if (sb) {
        free(sb->buffer);
        free(sb);
    }
}

static void sb_append(StringBuilder *sb, const char *fmt, ...) {
    if (!sb || !fmt) return;
    
    va_list args;
    va_start(args, fmt);
    
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (needed < 0) {
        va_end(args);
        return;
    }
    
    while (sb->size + needed + 1 > sb->capacity) {
        size_t new_cap = sb->capacity * 2;
        if (new_cap <= sb->capacity) { /* Overflow detected */
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
    
    vsnprintf(sb->buffer + sb->size, needed + 1, fmt, args);
    sb->size += needed;
    va_end(args);
}

static char* sb_to_string(StringBuilder *sb) {
    if (!sb) return NULL;
    char *result = strdup(sb->buffer);
    sb_free(sb);
    return result;
}

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

static char* escape_string_for_cpp(const char *raw) {
    if (!raw) return strdup("");
    size_t len = strlen(raw);
    char *escaped = malloc((len * 2) + 1);
    if (!escaped) return NULL;
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        switch ((unsigned char)raw[i]) {
            case '\n': escaped[out++] = '\\'; escaped[out++] = 'n'; break;
            case '\t': escaped[out++] = '\\'; escaped[out++] = 't'; break;
            case '\r': escaped[out++] = '\\'; escaped[out++] = 'r'; break;
            case '\\': escaped[out++] = '\\'; escaped[out++] = '\\'; break;
            case '"':  escaped[out++] = '\\'; escaped[out++] = '"'; break;
            default:   escaped[out++] = raw[i]; break;
        }
    }
    escaped[out] = '\0';
    return escaped;
}

/* Forward declarations */
static void generate_expr_cpp(StringBuilder *sb, ASTNode *node);
static void generate_node_cpp(StringBuilder *sb, ASTNode *node, int indent);

static bool ast_needs_map(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_OBJECT_LITERAL) return true;
    if (ast_needs_map(node->left)) return true;
    if (ast_needs_map(node->right)) return true;
    if (ast_needs_map(node->condition)) return true;
    if (ast_needs_map(node->body)) return true;
    if (ast_needs_map(node->next)) return true;
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            if (ast_needs_map(node->children[i])) return true;
        }
    }
    return false;
}



/* ========================================
   Expression Code Generator
   ======================================== */

static void generate_expr_cpp(StringBuilder *sb, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_cpp(node->value ? node->value : "");
                sb_append(sb, "std::string(\"%s\")", escaped ? escaped : "");
                free(escaped);
            } else if (expr_is_null_literal(node)) {
                /* C++ has no `null`; SUB's null in a numeric slot is NaN,
                   a value real arithmetic never produces. */
                sb_append(sb, "std::nan(\"\")");
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "0");
            }
            break;

        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;

        case AST_BINARY_EXPR:
            /* `x == null` / `x != null` become NaN tests, matching how the
               null sentinel is represented above. */
            if (node->value && (strcmp(node->value, "==") == 0 ||
                                strcmp(node->value, "!=") == 0) &&
                (expr_is_null_literal(node->left) || expr_is_null_literal(node->right))) {
                ASTNode *val = expr_is_null_literal(node->left) ? node->right : node->left;
                sb_append(sb, "%sstd::isnan(", strcmp(node->value, "!=") == 0 ? "!" : "");
                generate_expr_cpp(sb, val);
                sb_append(sb, ")");
                break;
            }
            if (node->value &&
                (strcmp(node->value, "/") == 0 || strcmp(node->value, "%") == 0)) {
                int is_div = (strcmp(node->value, "/") == 0);
                int is_flt = (infer_expr_type(node->left)  == TYPE_FLOAT ||
                              infer_expr_type(node->right) == TYPE_FLOAT);
                sb_append(sb, "%s(", is_flt ? (is_div ? "sub_fdiv" : "sub_fmod")
                                            : (is_div ? "sub_idiv" : "sub_mod"));
                generate_expr_cpp(sb, node->left);
                sb_append(sb, ", ");
                generate_expr_cpp(sb, node->right);
                sb_append(sb, ")");
                break;
            }
            /* C++ has no ** operator; emitting it verbatim parsed as a double
               dereference and failed to compile. */
            if (node->value && strcmp(node->value, "**") == 0) {
                int as_int = (infer_expr_type(node->left)  == TYPE_INT &&
                              infer_expr_type(node->right) == TYPE_INT &&
                              !exponent_is_negative(node->right));
                sb_append(sb, as_int ? "(long long)std::pow(" : "std::pow(");
                generate_expr_cpp(sb, node->left);
                sb_append(sb, ", ");
                generate_expr_cpp(sb, node->right);
                sb_append(sb, ")");
                break;
            }
            sb_append(sb, "(");
            generate_expr_cpp(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_cpp(sb, node->right);
            sb_append(sb, ")");
            break;

        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_cpp(sb, node->right);
            break;

        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_cpp(sb, node->condition);
            sb_append(sb, " ? ");
            generate_expr_cpp(sb, node->left);
            sb_append(sb, " : ");
            generate_expr_cpp(sb, node->right);
            sb_append(sb, ")");
            break;

        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            {
                const CppBuiltin *cb = cpp_builtin(node->value);
                if (cb && node->child_count == 1) {
                    /* std::to_string(std::string) does not exist; a string
                       argument to str() is already the answer. */
                    if (strcmp(cb->sub_name, "str") == 0 &&
                        infer_expr_type(node->children[0]) == TYPE_STRING) {
                        generate_expr_cpp(sb, node->children[0]);
                        break;
                    }
                    sb_append(sb, "%s", cb->prefix);
                    generate_expr_cpp(sb, node->children[0]);
                    sb_append(sb, "%s", cb->suffix);
                    break;
                }
                cb = cpp_builtin_multi(node->value);
                if (cb && node->child_count >= 2) {
                    sb_append(sb, "%s", cb->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_cpp(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", cb->suffix);
                    break;
                }
            }
            /* push/pop/len/str of an array want the array helpers, not the
               string spellings in the builtin table. */
            if (node->value && node->child_count >= 1 && node->children[0] &&
                infer_expr_type(node->children[0]) == TYPE_ARRAY) {
                if ((!strcmp(node->value, "push") || !strcmp(node->value, "append")) &&
                    node->child_count >= 2) {
                    sb_append(sb, "sub_push(");
                    generate_expr_cpp(sb, node->children[0]);
                    sb_append(sb, ", ");
                    if (infer_elem_type(node->children[0]) == TYPE_STRING)
                        sb_append(sb, "std::string(");
                    generate_expr_cpp(sb, node->children[1]);
                    if (infer_elem_type(node->children[0]) == TYPE_STRING)
                        sb_append(sb, ")");
                    sb_append(sb, ")");
                    break;
                }
                if (!strcmp(node->value, "pop")) {
                    sb_append(sb, "sub_pop(");
                    generate_expr_cpp(sb, node->children[0]);
                    sb_append(sb, ")");
                    break;
                }
                if (!strcmp(node->value, "len") || !strcmp(node->value, "length")) {
                    sb_append(sb, "(long long)(");
                    generate_expr_cpp(sb, node->children[0]);
                    sb_append(sb, ").size()");
                    break;
                }
                if (!strcmp(node->value, "str") || !strcmp(node->value, "to_string")) {
                    sb_append(sb, "sub_arr_str(");
                    generate_expr_cpp(sb, node->children[0]);
                    sb_append(sb, ")");
                    break;
                }
            }
            if (is_print_builtin(fn)) {
                sb_append(sb, "std::cout << ");
                if (node->child_count > 0) {
                    int arr = (infer_expr_type(node->children[0]) == TYPE_ARRAY);
                    if (arr) sb_append(sb, "sub_arr_str(");
                    generate_expr_cpp(sb, node->children[0]);
                    if (arr) sb_append(sb, ")");
                } else {
                    sb_append(sb, "\"\"");
                }
                sb_append(sb, " << std::endl");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", fn);
                } else {
                    generate_expr_cpp(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_cpp(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }

        case AST_ARRAY_LITERAL: {
            /* The element type was hard-coded to std::string, so a list of
               integers did not compile at all. */
            DataType elem = infer_elem_type(node);
            sb_append(sb, "%s{", cpp_array_type(elem));
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                if (elem == TYPE_STRING) sb_append(sb, "std::string(");
                generate_expr_cpp(sb, node->children[i]);
                if (elem == TYPE_STRING) sb_append(sb, ")");
            }
            sb_append(sb, "}");
            break;
        }

        case AST_OBJECT_LITERAL:
            sb_append(sb, "std::map<std::string, std::string>{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "{\"%s\", ", pair->value ? pair->value : "");
                generate_expr_cpp(sb, pair->right);
                sb_append(sb, "}");
            }
            sb_append(sb, "}");
            break;

        case AST_MEMBER_ACCESS:
            generate_expr_cpp(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;

        case AST_ARRAY_ACCESS:
            sb_append(sb, "sub_at(");
            generate_expr_cpp(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_cpp(sb, node->right);
            sb_append(sb, ")");
            break;

        default:
            break;
    }
}

/* ========================================
   Statement/Node Code Generator
   ======================================== */

static void generate_node_cpp(StringBuilder *sb, ASTNode *node, int indent);

/* See gen_clause_python. */
static void gen_clause_cpp(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_code(sb, indent);
        sb_append(sb, "do {\n");
        indent++;
    }
    generate_node_cpp(sb, clause->body, indent);
    if (loop) {
        indent_code(sb, indent - 1);
        sb_append(sb, "} while (false);\n");
    }
}

static void generate_node_cpp(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_cpp(sb, stmt, indent);
            }
            break;

        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "auto %s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_cpp(sb, node->right);
            } else {
                sb_append(sb, "0");
            }
            sb_append(sb, ";\n");
            break;

        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "const auto %s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_cpp(sb, node->right);
            } else {
                sb_append(sb, "0");
            }
            sb_append(sb, ";\n");
            break;

        case AST_FUNCTION_DECL:
            /* `auto` parameter types require C++20 abbreviated function
               templates, which callers don't reliably compile with
               (no -std=c++20 in the documented/CI build commands). Use an
               explicit template instead, which works under any C++11+
               default. */
            if (node->children && node->child_count > 0) {
                sb_append(sb, "\ntemplate<");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "typename T%d", i);
                }
                sb_append(sb, ">\n");
            } else {
                sb_append(sb, "\n");
            }
            sb_append(sb, "auto %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "T%d %s", i,
                              node->children[i]->value ? node->children[i]->value : "arg");
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) {
                generate_node_cpp(sb, node->body, indent + 1);
            }
            sb_append(sb, "}\n");
            break;

        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expr_cpp(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_cpp(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if (");
                    generate_expr_cpp(sb, node->right->condition);
                    sb_append(sb, ") {\n");
                    generate_node_cpp(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        ASTNode *branch = node->right->right;
                        while (branch && branch->type == AST_IF_STMT) {
                            sb_append(sb, " else if (");
                            generate_expr_cpp(sb, branch->condition);
                            sb_append(sb, ") {\n");
                            generate_node_cpp(sb, branch->body, indent + 1);
                            indent_code(sb, indent);
                            sb_append(sb, "}");
                            branch = branch->right;
                        }
                        if (branch) {
                            sb_append(sb, " else {\n");
                            generate_node_cpp(sb, branch, indent + 1);
                            indent_code(sb, indent);
                            sb_append(sb, "}");
                        }
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_cpp(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                }
            }
            sb_append(sb, "\n");
            break;

        case AST_FOR_STMT:
            indent_code(sb, indent);
            {
                const char *var = node->value ? node->value : "i";
                if (node->children && node->child_count > 0 &&
                    node->children[0]->type == AST_RANGE_EXPR) {
                    ASTNode *range = node->children[0];
                    sb_append(sb, "for (int %s = ", var);
                    if (range->right) {
                        if (range->left) generate_expr_cpp(sb, range->left);
                        else sb_append(sb, "0");
                        sb_append(sb, "; %s < ", var);
                        generate_expr_cpp(sb, range->right);
                    } else if (range->left) {
                        sb_append(sb, "0; %s < ", var);
                        generate_expr_cpp(sb, range->left);
                    } else {
                        sb_append(sb, "0; %s < 10", var);
                    }
                    sb_append(sb, "; %s++) {\n", var);
                } else if (node->condition) {
                    sb_append(sb, "for (auto& %s : ", var);
                    generate_expr_cpp(sb, node->condition);
                    sb_append(sb, ") {\n");
                } else {
                    sb_append(sb, "for (int %s = 0; %s < 10; %s++) {\n", var, var, var);
                }
            }
            generate_node_cpp(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expr_cpp(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_cpp(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "do {\n");
            generate_node_cpp(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "} while (");
            generate_expr_cpp(sb, node->condition);
            sb_append(sb, ");\n");
            break;

        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_cpp(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;


        case AST_SWITCH_STMT: {
            /* An if/else chain rather than a C++ `switch`: SUB matches on
               strings and floats and on values that are not constants, and a
               case label can be none of those. */
            static int sw_cpp = 0;
            int id = sw_cpp++;
            ASTNode *deflt = switch_default_clause(node);
            int emitted = 0;

            indent_code(sb, indent);
            sb_append(sb, "{\n");
            indent_code(sb, indent + 1);
            sb_append(sb, "auto _sw%d = ", id);
            generate_expr_cpp(sb, node->condition);
            sb_append(sb, ";\n");
            indent_code(sb, indent + 1);
            sb_append(sb, "(void)_sw%d;\n", id);

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_code(sb, indent + 1);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    sb_append(sb, "_sw%d == (", id);
                    generate_expr_cpp(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, ") {\n");
                gen_clause_cpp(sb, c, indent + 2);
                emitted = 1;
            }
            if (deflt) {
                if (emitted) {
                    indent_code(sb, indent + 1);
                    sb_append(sb, "} else {\n");
                    gen_clause_cpp(sb, deflt, indent + 2);
                } else {
                    gen_clause_cpp(sb, deflt, indent + 1);
                }
            }
            if (emitted) {
                indent_code(sb, indent + 1);
                sb_append(sb, "}\n");
            }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        }
        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break;\n");
            break;

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue;\n");
            break;

        case AST_CALL_EXPR:
            indent_code(sb, indent);
            if (is_print_builtin(node->value)) {
                sb_append(sb, "std::cout << ");
                if (node->child_count > 0) {
                    /* std::ostream has no operator<< for a vector. */
                    int arr = (infer_expr_type(node->children[0]) == TYPE_ARRAY);
                    if (arr) sb_append(sb, "sub_arr_str(");
                    generate_expr_cpp(sb, node->children[0]);
                    if (arr) sb_append(sb, ")");
                } else {
                    sb_append(sb, "\"\"");
                }
                sb_append(sb, " << std::endl;\n");
            } else {
                generate_expr_cpp(sb, node);
                sb_append(sb, ";\n");
            }
            break;

        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "sub_at(");
                generate_expr_cpp(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_cpp(sb, node->left->right);
                sb_append(sb, ") = ");
                generate_expr_cpp(sb, node->right);
                sb_append(sb, ";\n");
                break;
            }
            generate_expr_cpp(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_cpp(sb, node->right);
            sb_append(sb, ";\n");
            break;

        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_cpp(sb, stmt, indent);
            }
            break;

        case AST_CLASS_DECL:
            indent_code(sb, indent);
            sb_append(sb, "class %s {\npublic:\n", node->value ? node->value : "Object");
            if (node->body) {
                generate_node_cpp(sb, node->body, indent + 1);
            }
            indent_code(sb, indent);
            sb_append(sb, "};\n");
            break;

        case AST_TRY_STMT:
            indent_code(sb, indent);
            sb_append(sb, "try {\n");
            generate_node_cpp(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                sb_append(sb, " catch (const std::exception& e) {\n");
                generate_node_cpp(sb, node->right, indent + 1);
                indent_code(sb, indent);
                sb_append(sb, "}");
            }
            sb_append(sb, "\n");
            break;

        case AST_THROW_STMT:
            indent_code(sb, indent);
            sb_append(sb, "throw std::runtime_error(");
            if (node->right) {
                generate_expr_cpp(sb, node->right);
            } else {
                sb_append(sb, "\"error\"");
            }
            sb_append(sb, ");\n");
            break;

        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            if (node->value) {
                indent_code(sb, indent);
                sb_append(sb, "// Embedded code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;

        default:
            break;
    }
}

/* ========================================
   Public API
   ======================================== */

void codegen_cpp_get_default_options(CPPVersion version, CPPCodegenOptions *options) {
    if (!options) return;
    options->version = version;
    options->use_std_string = true;
    options->use_auto = (version >= CPP_VER_11);
    options->use_range_based_for = (version >= CPP_VER_11);
    options->use_constexpr = (version >= CPP_VER_11);
    options->use_concepts = (version >= CPP_VER_20);
    options->use_modules = (version >= CPP_VER_20);
}

const char* codegen_cpp_version_to_string(CPPVersion version) {
    switch (version) {
        case CPP_VER_11: return "C++11";
        case CPP_VER_14: return "C++14";
        case CPP_VER_17: return "C++17";
        case CPP_VER_20: return "C++20";
        case CPP_VER_23: return "C++23";
        default:         return "C++17";
    }
}

CPPVersion codegen_cpp_parse_version(const char *version_str) {
    if (!version_str) return CPP_VER_17;
    if (strstr(version_str, "23")) return CPP_VER_23;
    if (strstr(version_str, "20")) return CPP_VER_20;
    if (strstr(version_str, "17")) return CPP_VER_17;
    if (strstr(version_str, "14")) return CPP_VER_14;
    if (strstr(version_str, "11")) return CPP_VER_11;
    return CPP_VER_17;
}

char* codegen_cpp(ASTNode *ast, const char *source, CPPCodegenOptions *options) {
    (void)source; /* Reserved for embedded code extraction */
    (void)options; /* Options reserved for future fine-tuning */

    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    sb_append(sb, "// Generated by SUB Language Compiler (C++17 Target)\n\n");

    /* Emit includes based on AST analysis */
    sb_append(sb, "#include <iostream>\n");
    sb_append(sb, "#include <cmath>\n");
    sb_append(sb, "#include <algorithm>\n");
    sb_append(sb, "#include <cctype>\n");
    sb_append(sb, "#include <cstdlib>\n");
    /* <string> and <vector> unconditionally: the array helpers below are
       templates over std::vector<T> and are emitted whether or not the
       program uses an array, and a template still has to name its types. */
    sb_append(sb, "#include <string>\n");
    sb_append(sb, "#include <vector>\n");
    if (ast_needs_map(ast)) {
        sb_append(sb, "#include <map>\n");
    }
    sb_append(sb, "\n");

    /* Integer division by zero is undefined behaviour in C++ and lands as
       SIGFPE in practice. SUB reports the interpreter's runtime error and
       exits 70, so every backend ends a divide by zero the same way. */
    sb_append(sb, "[[noreturn]] static void sub_die(const char *msg) {\n");
    sb_append(sb, "    std::cerr << \"RuntimeError: \" << msg << std::endl;\n");
    sb_append(sb, "    std::exit(70);\n}\n");
    sb_append(sb, "static long long sub_idiv(long long a, long long b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"division by zero\");\n");
    sb_append(sb, "    return a / b;\n}\n");
    sb_append(sb, "static long long sub_mod(long long a, long long b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"modulo by zero\");\n");
    sb_append(sb, "    return a %% b;\n}\n");
    /* `%` is not defined for doubles in C++ at all, so a float remainder
       has to go through std::fmod or the program will not compile. */
    sb_append(sb, "static double sub_fdiv(double a, double b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"division by zero\");\n");
    sb_append(sb, "    return a / b;\n}\n");
    sb_append(sb, "static double sub_fmod(double a, double b) {\n");
    sb_append(sb, "    if (b == 0) sub_die(\"modulo by zero\");\n");
    sb_append(sb, "    return std::fmod(a, b);\n}\n\n");

    /* Arrays. Templates, so one set of helpers covers every element type.
       sub_at returns a reference so it serves reads and writes alike. */
    sb_append(sb, "[[noreturn]] static void sub_die_index(long long i, size_t n) {\n");
    sb_append(sb, "    std::cerr << \"RuntimeError: array index \" << i\n");
    sb_append(sb, "              << \" out of bounds [0, \" << n << \")\" << std::endl;\n");
    sb_append(sb, "    std::exit(70);\n}\n");
    sb_append(sb, "template <class T> T& sub_at(std::vector<T> &a, long long i) {\n");
    sb_append(sb, "    if (i < 0) i += (long long)a.size();   /* a[-1] is the last */\n");
    sb_append(sb, "    if (i < 0 || (size_t)i >= a.size()) sub_die_index(i, a.size());\n");
    sb_append(sb, "    return a[(size_t)i];\n}\n");
    /* Two template parameters: with one, push_back(a, 4) deduces T as both
       long long (from the vector) and int (from the literal) and fails. */
    sb_append(sb, "template <class T, class U> void sub_push(std::vector<T> &a, U v) "
                  "{ a.push_back(T(v)); }\n");
    sb_append(sb, "template <class T> T sub_pop(std::vector<T> &a) {\n");
    sb_append(sb, "    if (a.empty()) sub_die(\"pop from empty array\");\n");
    sb_append(sb, "    T v = a.back(); a.pop_back(); return v;\n}\n");
    sb_append(sb, "static std::string sub_elem_str(const std::string &v) { return v; }\n");
    sb_append(sb, "static std::string sub_elem_str(bool v) "
                  "{ return v ? \"true\" : \"false\"; }\n");
    sb_append(sb, "static std::string sub_elem_str(double v) {\n");
    sb_append(sb, "    char b[64]; snprintf(b, sizeof b, \"%%g\", v); return b;\n}\n");
    sb_append(sb, "static std::string sub_elem_str(long long v) "
                  "{ return std::to_string(v); }\n");
    sb_append(sb, "template <class T> std::string sub_arr_str(const std::vector<T> &a) {\n");
    sb_append(sb, "    std::string s = \"[\";\n");
    sb_append(sb, "    for (size_t i = 0; i < a.size(); i++) {\n");
    sb_append(sb, "        if (i) s += \", \";\n");
    sb_append(sb, "        s += sub_elem_str(a[i]);\n");
    sb_append(sb, "    }\n    return s + \"]\";\n}\n\n");

    /* Two-pass approach: functions first, then main() with top-level statements */
    StringBuilder *main_sb = sb_create();
    if (!main_sb) {
        sb_free(sb);
        return NULL;
    }

    if (ast->type == AST_PROGRAM) {
        /* Pass 1: emit function declarations at file scope */
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL ||
                stmt->type == AST_CLASS_DECL) {
                generate_node_cpp(sb, stmt, 0);
            }
        }
        /* Pass 2: collect all non-function top-level statements for main() */
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type != AST_FUNCTION_DECL &&
                stmt->type != AST_CLASS_DECL) {
                generate_node_cpp(main_sb, stmt, 1);
            }
        }
    }

    /* SUB exposes upper/lower/trim as plain functions; C++ has no such free
       functions for std::string, so generate them. */
    sb_append(sb, "\nstatic std::string sub_upper(std::string s) {\n");
    sb_append(sb, "    std::transform(s.begin(), s.end(), s.begin(), ::toupper);\n");
    sb_append(sb, "    return s;\n}\n");
    sb_append(sb, "\nstatic std::string sub_lower(std::string s) {\n");
    sb_append(sb, "    std::transform(s.begin(), s.end(), s.begin(), ::tolower);\n");
    sb_append(sb, "    return s;\n}\n");
    sb_append(sb, "\nstatic std::string sub_trim(std::string s) {\n");
    sb_append(sb, "    size_t b = s.find_first_not_of(\" \\t\\n\\r\");\n");
    sb_append(sb, "    size_t e = s.find_last_not_of(\" \\t\\n\\r\");\n");
    sb_append(sb, "    return b == std::string::npos ? \"\" : s.substr(b, e - b + 1);\n}\n");
    sb_append(sb, "int main() {\n");
    /* SUB spells booleans true/false; C++ streams default to 1/0, which made
       the same program print differently than the interpreter. */
    sb_append(sb, "    std::cout << std::boolalpha;\n");
    sb_append(sb, "%s", main_sb->buffer);
    sb_append(sb, "    return 0;\n");
    sb_append(sb, "}\n");

    sb_free(main_sb);
    return sb_to_string(sb);
}

char* codegen_cpp_generate(ASTNode *ast, const char *source) {
    CPPCodegenOptions options;
    codegen_cpp_get_default_options(CPP_VER_17, &options);
    return codegen_cpp(ast, source, &options);
}
