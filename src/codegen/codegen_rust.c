/* ========================================
   SUB Language - Rust Code Generator
   Implementation
   File: codegen_rust.c
   ======================================== */

#define _GNU_SOURCE
#include "codegen_rust.hh"
#include "windows_compat.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} StringBuilder;

static StringBuilder* sb_create(void) {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    sb->capacity = 8192;
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
        char *next = realloc(sb->buffer, sb->capacity);
        if (!next) {
            va_end(args);
            return;
        }
        sb->buffer = next;
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

static void generate_expr_rust(StringBuilder *sb, ASTNode *node);

static ASTNode* block_first(ASTNode *node) {
    if (!node) return NULL;
    if (node->body) return node->body;
    if (node->children && node->child_count > 0) return node->children[0];
    if (node->left) return node->left;
    return NULL;
}

static bool ast_contains_object(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_OBJECT_LITERAL) return true;
    if (node->left && ast_contains_object(node->left)) return true;
    if (node->right && ast_contains_object(node->right)) return true;
    if (node->condition && ast_contains_object(node->condition)) return true;
    if (node->body && ast_contains_object(node->body)) return true;
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            if (ast_contains_object(node->children[i])) return true;
        }
    }
    return false;
}

static void generate_node_rust(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_rust(sb, s, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let mut %s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_rust(sb, node->right);
            } else {
                sb_append(sb, "0"); // Default value
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfn %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "%s: i64", node->children[i]->value ? node->children[i]->value : "arg");
            }
            sb_append(sb, ") {\n");
            if (node->body) {
                generate_node_rust(sb, node->body, indent + 1);
            }
            sb_append(sb, "}\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if ");
            generate_expr_rust(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                sb_append(sb, " else {\n");
                generate_node_rust(sb, node->right, indent + 1);
                indent_code(sb, indent);
                sb_append(sb, "}");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while ");
            generate_expr_rust(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_FOR_STMT:
            indent_code(sb, indent);
            if (node->children && node->child_count > 0 && node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for %s in ", node->value ? node->value : "i");
                if (range->right) {
                    generate_expr_rust(sb, range->left);
                    sb_append(sb, "..");
                    generate_expr_rust(sb, range->right);
                } else {
                    sb_append(sb, "0..");
                    generate_expr_rust(sb, range->left);
                }
                sb_append(sb, " {\n");
            } else if (node->condition) {
                sb_append(sb, "for %s in ", node->value ? node->value : "item");
                generate_expr_rust(sb, node->condition);
                sb_append(sb, " {\n");
            } else {
                sb_append(sb, "for %s in 0..10 {\n", node->value ? node->value : "i");
            }
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_rust(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_rust(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_rust(sb, s, indent);
            }
            break;

        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            generate_expr_rust(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_rust(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        default:
            break;
    }
}

static void generate_expr_rust(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                sb_append(sb, "String::from(\"%s\")", node->value);
            } else {
                sb_append(sb, "%s", node->value ? node->value : "0");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value);
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_rust(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_rust(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_rust(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "if ");
            generate_expr_rust(sb, node->condition);
            sb_append(sb, " { ");
            generate_expr_rust(sb, node->left);
            sb_append(sb, " } else { ");
            generate_expr_rust(sb, node->right);
            sb_append(sb, " }");
            break;
        case AST_CALL_EXPR:
            if (node->value && strcmp(node->value, "print") == 0) {
                sb_append(sb, "println!(\"{}\", ");
                if (node->child_count > 0) generate_expr_rust(sb, node->children[0]);
                sb_append(sb, ")");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", node->value);
                } else {
                    generate_expr_rust(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_rust(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "vec![");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_rust(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "HashMap::from([");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "(String::from(\"%s\"), ", pair->value ? pair->value : "");
                generate_expr_rust(sb, pair->right);
                sb_append(sb, ")");
            }
            sb_append(sb, "])");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_rust(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            generate_expr_rust(sb, node->left);
            sb_append(sb, "[");
            generate_expr_rust(sb, node->right);
            sb_append(sb, "]");
            break;
        default:
            break;
    }
}

char* codegen_rust(ASTNode *ast, const char *source) {
    (void)source; // Source is not used yet in Rust codegen
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    sb_append(sb, "// Generated by SUB Language Compiler (Rust Target)\n\n");
    if (ast_contains_object(ast)) {
        sb_append(sb, "use std::collections::HashMap;\n\n");
    }

    StringBuilder *main_sb = sb_create();
    if (!main_sb) {
        sb_free(sb);
        return NULL;
    }

    if (ast->type == AST_PROGRAM) {
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                generate_node_rust(sb, stmt, 0);
            } else {
                generate_node_rust(main_sb, stmt, 1);
            }
        }
    }

    sb_append(sb, "fn main() {\n");
    sb_append(sb, "%s", main_sb->buffer);
    sb_append(sb, "}\n");

    sb_free(main_sb);
    return sb_to_string(sb);
}
