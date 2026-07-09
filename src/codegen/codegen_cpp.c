/* ========================================
   SUB Language - C++ Code Generator Implementation
   Generates modern C++17 code from AST
   File: codegen_cpp.c
   ======================================== */

#define _GNU_SOURCE
#include "codegen_cpp.h"
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
        sb->capacity *= 2;
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

/* Check if AST uses string types (to decide on #include <string>) */
static bool ast_needs_string(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_LITERAL && node->data_type == TYPE_STRING) return true;
    if (ast_needs_string(node->left)) return true;
    if (ast_needs_string(node->right)) return true;
    if (ast_needs_string(node->condition)) return true;
    if (ast_needs_string(node->body)) return true;
    if (ast_needs_string(node->next)) return true;
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            if (ast_needs_string(node->children[i])) return true;
        }
    }
    return false;
}

static bool ast_needs_vector(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_ARRAY_LITERAL) return true;
    if (ast_needs_vector(node->left)) return true;
    if (ast_needs_vector(node->right)) return true;
    if (ast_needs_vector(node->condition)) return true;
    if (ast_needs_vector(node->body)) return true;
    if (ast_needs_vector(node->next)) return true;
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            if (ast_needs_vector(node->children[i])) return true;
        }
    }
    return false;
}

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
            if (strcmp(fn, "print") == 0) {
                sb_append(sb, "std::cout << ");
                if (node->child_count > 0) {
                    generate_expr_cpp(sb, node->children[0]);
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

        case AST_ARRAY_LITERAL:
            sb_append(sb, "std::vector<auto>{");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_cpp(sb, node->children[i]);
            }
            sb_append(sb, "}");
            break;

        case AST_OBJECT_LITERAL:
            sb_append(sb, "std::map<std::string, auto>{");
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
            generate_expr_cpp(sb, node->left);
            sb_append(sb, "[");
            generate_expr_cpp(sb, node->right);
            sb_append(sb, "]");
            break;

        default:
            break;
    }
}

/* ========================================
   Statement/Node Code Generator
   ======================================== */

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
            sb_append(sb, "\nauto %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "auto %s",
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
            if (node->value && strcmp(node->value, "print") == 0) {
                sb_append(sb, "std::cout << ");
                if (node->child_count > 0) {
                    generate_expr_cpp(sb, node->children[0]);
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
    if (ast_needs_string(ast)) {
        sb_append(sb, "#include <string>\n");
    }
    if (ast_needs_vector(ast)) {
        sb_append(sb, "#include <vector>\n");
    }
    if (ast_needs_map(ast)) {
        sb_append(sb, "#include <map>\n");
    }
    sb_append(sb, "\n");

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

    sb_append(sb, "int main() {\n");
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
