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
    sb->capacity = 8192;
    sb->buffer = malloc(sb->capacity);
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
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    while (sb->size + needed + 1 > sb->capacity) {
        sb->capacity *= 2;
        sb->buffer = realloc(sb->buffer, sb->capacity);
    }
    
    vsnprintf(sb->buffer + sb->size, needed + 1, fmt, args);
    sb->size += needed;
    va_end(args);
}

static char* sb_to_string(StringBuilder *sb) {
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

static void generate_node_rust(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *s = node->left; s; s = s->next) {
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
            sb_append(sb, "\nfn %s() {\n", node->value ? node->value : "func");
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
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->left) {
                sb_append(sb, " ");
                generate_expr_rust(sb, node->left);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_rust(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *s = node->body; s; s = s->next) {
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
        case AST_CALL_EXPR:
            if (strcmp(node->value, "print") == 0) {
                sb_append(sb, "println!(\"{}\", ");
                if (node->left) generate_expr_rust(sb, node->left);
                sb_append(sb, ")");
            } else {
                sb_append(sb, "%s()", node->value);
            }
            break;
        default:
            break;
    }
}

char* codegen_rust(ASTNode *ast, const char *source) {
    (void)source; // Source is not used yet in Rust codegen
    StringBuilder *sb = sb_create();
    sb_append(sb, "// Generated by SUB Language Compiler (Rust Target)\n\n");

    StringBuilder *main_sb = sb_create();

    if (ast->type == AST_PROGRAM) {
        for (ASTNode *stmt = ast->left; stmt != NULL; stmt = stmt->next) {
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
