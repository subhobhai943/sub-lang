/* ========================================
   SUB Language - Rust Code Generator
   Implementation
   File: codegen_rust.c
   ======================================== */

#define _GNU_SOURCE
#include "codegen_rust.h"
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
    sb->size = 0;
    sb->buffer = malloc(sb->capacity);
    if (!sb->buffer) {
        free(sb);
        return NULL;
    }
    sb->buffer[0] = '\0';
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
    free(sb->buffer);
    free(sb);
    return result;
}

static void indent_code(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "    ");
    }
}

static void generate_expr_rust(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            sb_append(sb, "%s", node->value ? node->value : "()");
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_rust(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_rust(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            if (strcmp(fn, "print") == 0 || strcmp(fn, "printf") == 0) {
                sb_append(sb, "println!(\"{}\", ");
                if (node->left) {
                    generate_expr_rust(sb, node->left);
                } else if (node->children && node->child_count > 0) {
                    generate_expr_rust(sb, node->children[0]);
                }
                sb_append(sb, ")");
            } else {
                sb_append(sb, "%s(", fn);
                if (node->left) {
                    generate_expr_rust(sb, node->left);
                } else if (node->children && node->child_count > 0) {
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_rust(sb, node->children[i]);
                    }
                }
                sb_append(sb, ")");
            }
            break;
        }
        default:
            break;
    }
}

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
                sb_append(sb, "()");
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let %s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_rust(sb, node->right);
            } else {
                sb_append(sb, "()");
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfn %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children[i] && node->children[i]->value) {
                        sb_append(sb, "%s", node->children[i]->value);
                    }
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) {
                generate_node_rust(sb, node->body, indent + 1);
            }
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
                generate_node_rust(sb, stmt, indent + 1);
            }
            sb_append(sb, "}\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if ");
            if (node->condition) {
                generate_expr_rust(sb, node->condition);
            }
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
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for %s in 0..10 {\n", node->value ? node->value : "i");
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "loop {\n");
            if (node->condition) {
                generate_expr_rust(sb, node->condition);
                sb_append(sb, ";\n");
            }
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return ");
            if (node->left) {
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
            
        default:
            break;
    }
}

static char* extract_embedded_code(const char *source, const char *lang) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    char pattern_start[64];
    snprintf(pattern_start, sizeof(pattern_start), "#embed %s", lang);
    
    const char *ptr = source;
    while ((ptr = strstr(ptr, pattern_start)) != NULL) {
        ptr = strchr(ptr, '\n');
        if (!ptr) break;
        ptr++;
        
        const char *end = strstr(ptr, "#endembed");
        if (!end) break;
        
        while (ptr < end) {
            sb_append(sb, "%c", *ptr);
            ptr++;
        }
    }
    
    if (sb->size == 0) {
        sb_free(sb);
        return NULL;
    }
    
    return sb_to_string(sb);
}

char* codegen_rust(ASTNode *ast, const char *source) {
    char *embedded = extract_embedded_code(source, "rust");
    if (embedded) {
        StringBuilder *sb = sb_create();
        sb_append(sb, "// Generated by SUB Language Compiler\n\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
        return sb_to_string(sb);
    }
    
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");
    generate_node_rust(sb, ast, 0);
    sb_append(sb, "\nfn main() {\n}\n");
    
    return sb_to_string(sb);
}

RustContext* rust_context_create(FILE *output) {
    RustContext *ctx = calloc(1, sizeof(RustContext));
    if (!ctx) return NULL;
    ctx->output = output;
    ctx->indent_level = 0;
    ctx->label_counter = 0;
    return ctx;
}

void rust_context_free(RustContext *ctx) {
    if (ctx) free(ctx);
}

void rust_indent(RustContext *ctx) {
    for (int i = 0; i < ctx->indent_level; i++) {
        fprintf(ctx->output, "    ");
    }
}

const char* rust_get_type(DataType type) {
    switch (type) {
        case TYPE_INT: return "i32";
        case TYPE_FLOAT: return "f64";
        case TYPE_STRING: return "String";
        case TYPE_BOOL: return "bool";
        case TYPE_ARRAY: return "Vec";
        case TYPE_VOID: return "()";
        default: return "i32";
    }
}

void rust_emit(RustContext *ctx, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(ctx->output, format, args);
    va_end(args);
}

void rust_generate_node(RustContext *ctx, ASTNode *node) {
    if (!ctx || !node) return;
    (void)node;
}
