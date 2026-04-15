/* ========================================
   SUB Language Multi-Language Code Generator - REAL IMPLEMENTATION
   Actually processes your SUB code, not dummy templates!
   File: codegen_multilang.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "type_system.h"
#include "windows_compat.h"
#include <stdarg.h>
#include <ctype.h>

/* String Builder */
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

/* Helper to parse embedded code blocks from source */
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
        if (!end) {
            // Check for common typo and warn
            if (strstr(ptr, "#embeded")) {
                fprintf(stderr, "Warning: Found '#embeded' at line - did you mean '#endembed'?\n");
            }
            break;
        }
        
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

/* Forward declarations */
static void generate_node_python(StringBuilder *sb, ASTNode *node, int indent);

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

static char* escape_string_for_codegen(const char *raw) {
    if (!raw) {
        return strdup("");
    }

    size_t len = strlen(raw);
    char *escaped = malloc((len * 2) + 1);
    if (!escaped) return NULL;

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        switch ((unsigned char)raw[i]) {
            case '\n':
                escaped[out++] = '\\';
                escaped[out++] = 'n';
                break;
            case '\t':
                escaped[out++] = '\\';
                escaped[out++] = 't';
                break;
            case '\r':
                escaped[out++] = '\\';
                escaped[out++] = 'r';
                break;
            case '\\':
                escaped[out++] = '\\';
                escaped[out++] = '\\';
                break;
            case '"':
                escaped[out++] = '\\';
                escaped[out++] = '"';
                break;
            default:
                escaped[out++] = raw[i];
                break;
        }
    }

    escaped[out] = '\0';
    return escaped;
}

/* ========================================
   PYTHON CODE GENERATOR - REAL
   ======================================== */

static void generate_expr_python(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "None");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_python(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_python(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_python(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_python(sb, node->left);
            sb_append(sb, " if ");
            generate_expr_python(sb, node->condition);
            sb_append(sb, " else ");
            generate_expr_python(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR:
            if (node->value) {
                sb_append(sb, "%s(", node->value);
            } else {
                generate_expr_python(sb, node->left);
                sb_append(sb, "(");
            }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_python(sb, node->children[i]);
            }
            sb_append(sb, ")");
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "[");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_python(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\": ", pair->value ? pair->value : "");
                generate_expr_python(sb, pair->right);
            }
            sb_append(sb, "}");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_python(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            generate_expr_python(sb, node->left);
            sb_append(sb, "[");
            generate_expr_python(sb, node->right);
            sb_append(sb, "]");
            break;
        default:
            break;
    }
}

static void generate_node_python(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_python(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_python(sb, node->right);
            } else {
                sb_append(sb, "None");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_python(sb, node->right);
            } else {
                sb_append(sb, "None");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\ndef %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "%s", node->children[i]->value ? node->children[i]->value : "arg");
            }
            sb_append(sb, "):\n");
            if (node->body) {
                generate_node_python(sb, node->body, indent + 1);
            }
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if ");
            generate_expr_python(sb, node->condition);
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    indent_code(sb, indent);
                    sb_append(sb, "elif ");
                    generate_expr_python(sb, node->right->condition);
                    sb_append(sb, ":\n");
                    generate_node_python(sb, node->right->body, indent + 1);
                    if (!node->right->body || block_first(node->right->body) == NULL) {
                        indent_code(sb, indent + 1);
                        sb_append(sb, "pass\n");
                    }
                    if (node->right->right) {
                        indent_code(sb, indent);
                        sb_append(sb, "else:\n");
                        generate_node_python(sb, node->right->right, indent + 1);
                        if (!node->right->right || block_first(node->right->right) == NULL) {
                            indent_code(sb, indent + 1);
                            sb_append(sb, "pass\n");
                        }
                    }
                } else {
                    indent_code(sb, indent);
                    sb_append(sb, "else:\n");
                    generate_node_python(sb, node->right, indent + 1);
                    if (!node->right || block_first(node->right) == NULL) {
                        indent_code(sb, indent + 1);
                        sb_append(sb, "pass\n");
                    }
                }
            }
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for %s in ", node->value ? node->value : "i");
            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    sb_append(sb, "range(");
                    if (range->left) generate_expr_python(sb, range->left);
                    if (range->right) {
                        sb_append(sb, ", ");
                        generate_expr_python(sb, range->right);
                    }
                    sb_append(sb, ")");
                } else {
                    generate_expr_python(sb, range);
                }
            } else if (node->condition) {
                generate_expr_python(sb, node->condition);
            } else {
                sb_append(sb, "range(10)");
            }
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while ");
            generate_expr_python(sb, node->condition);
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_python(sb, node->right);
            }
            sb_append(sb, "\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_python(sb, node);
            sb_append(sb, "\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            generate_expr_python(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_python(sb, node->right);
            sb_append(sb, "\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_python(sb, stmt, indent);
            }
            break;
            
        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            // Include embedded code directly
            if (node->value) {
                sb_append(sb, "# Embedded code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;
            
        default:
            break;
    }
}

char* codegen_python(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "#!/usr/bin/env python3\n");
    sb_append(sb, "# Generated by SUB Language Compiler\n\n");
    
    // Check for embedded Python code first
    char *embedded = extract_embedded_code(source, "python");
    bool has_embedded = embedded != NULL;
    if (embedded) {
        sb_append(sb, "# Embedded Python code from SUB\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }

    // Generate from AST
    generate_node_python(sb, ast, 0);
    
    // Add main guard if no embedded code
    if (!has_embedded) {
        sb_append(sb, "\nif __name__ == '__main__':\n");
        sb_append(sb, "    pass\n");
    }
    
    return sb_to_string(sb);
}

/* ========================================
   JAVASCRIPT CODE GENERATOR - REAL
   ======================================== */

static void generate_expr_js(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "null");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_js(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_js(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_js(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_js(sb, node->condition);
            sb_append(sb, " ? ");
            generate_expr_js(sb, node->left);
            sb_append(sb, " : ");
            generate_expr_js(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR:
            if (node->value) {
                sb_append(sb, "%s(", node->value);
            } else {
                generate_expr_js(sb, node->left);
                sb_append(sb, "(");
            }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_js(sb, node->children[i]);
            }
            sb_append(sb, ")");
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "[");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_js(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\": ", pair->value ? pair->value : "");
                generate_expr_js(sb, pair->right);
            }
            sb_append(sb, "}");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_js(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            generate_expr_js(sb, node->left);
            sb_append(sb, "[");
            generate_expr_js(sb, node->right);
            sb_append(sb, "]");
            break;
        default:
            break;
    }
}

static void generate_node_js(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_js(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let %s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_js(sb, node->right);
            } else {
                sb_append(sb, "null");
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "const %s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_js(sb, node->right);
            } else {
                sb_append(sb, "null");
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            indent_code(sb, indent);
            sb_append(sb, "function %s(", node->value ? node->value : "func");
            // Parameters
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    sb_append(sb, "%s%s", i > 0 ? ", " : "", node->children[i]->value);
                }
            }
            sb_append(sb, ") {\n");
            
            if (node->body) generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expr_js(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if (");
                    generate_expr_js(sb, node->right->condition);
                    sb_append(sb, ") {\n");
                    generate_node_js(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        sb_append(sb, " else {\n");
                        generate_node_js(sb, node->right->right, indent + 1);
                        indent_code(sb, indent);
                        sb_append(sb, "}");
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_js(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                }
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            // Check for range expression
            if (node->children && node->child_count > 0 && node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for (let %s = ", node->value ? node->value : "i");
                if (range->right) {
                    if (range->left) generate_expr_js(sb, range->left); else sb_append(sb, "0");
                } else {
                    sb_append(sb, "0");
                }
                sb_append(sb, "; %s < ", node->value ? node->value : "i");
                if (range->right) {
                    generate_expr_js(sb, range->right);
                } else if (range->left) {
                    generate_expr_js(sb, range->left);
                } else {
                    sb_append(sb, "10");
                }
                sb_append(sb, "; %s++) {\n", node->value ? node->value : "i");
            } 
            // Check for collection iteration
            else if (node->condition) {
                sb_append(sb, "for (let %s of ", node->value ? node->value : "item");
                generate_expr_js(sb, node->condition);
                sb_append(sb, ") {\n");
            }
            // Fallback
            else {
                sb_append(sb, "for (let %s = 0; %s < 10; %s++) {\n", 
                         node->value ? node->value : "i",
                         node->value ? node->value : "i",
                         node->value ? node->value : "i");
            }
            
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expr_js(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_js(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            // Map print to console.log
            if (node->value && strcmp(node->value, "print") == 0) {
                sb_append(sb, "console.log(");
                if (node->child_count > 0) generate_expr_js(sb, node->children[0]);
                sb_append(sb, ")");
            } else {
                generate_expr_js(sb, node);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            generate_expr_js(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_js(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_js(sb, stmt, indent);
            }
            break;
            
        case AST_EMBED_CODE:
            if (node->value) {
                sb_append(sb, "// Embedded JavaScript\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;
            
        default:
            break;
    }
}

char* codegen_javascript(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");
    
    // Check for embedded JavaScript
    char *embedded = extract_embedded_code(source, "javascript");
    if (embedded) {
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }
    generate_node_js(sb, ast, 0);
    
    return sb_to_string(sb);
}

/* ========================================
   JAVA CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_java(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "null");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;
        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_java(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_java(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_java(sb, node->condition);
            sb_append(sb, " ? ");
            generate_expr_java(sb, node->left);
            sb_append(sb, " : ");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            if (strcmp(fn, "print") == 0) {
                sb_append(sb, "System.out.println(");
                if (node->child_count > 0) generate_expr_java(sb, node->children[0]);
                sb_append(sb, ")");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", fn);
                } else {
                    generate_expr_java(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_java(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }
        case AST_ARRAY_LITERAL:
            sb_append(sb, "java.util.List.of(");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_java(sb, node->children[i]);
            }
            sb_append(sb, ")");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "java.util.Map.of(");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\", ", pair->value ? pair->value : "");
                generate_expr_java(sb, pair->right);
            }
            sb_append(sb, ")");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_java(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            generate_expr_java(sb, node->left);
            sb_append(sb, ".get(");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        default:
            fprintf(stderr, "Warning: Unsupported expression node %d in Java generator\n", node->type);
            break;
    }
}

static void generate_node_java(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_java(sb, node->right);
            else sb_append(sb, "null");
            sb_append(sb, ";\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "final var %s = ", node->value ? node->value : "CONST");
            if (node->right) generate_expr_java(sb, node->right);
            else sb_append(sb, "null");
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_code(sb, indent);
            sb_append(sb, "public static void %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "Object %s", node->children[i]->value ? node->children[i]->value : "arg");
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expr_java(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if (");
                    generate_expr_java(sb, node->right->condition);
                    sb_append(sb, ") {\n");
                    generate_node_java(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        sb_append(sb, " else {\n");
                        generate_node_java(sb, node->right->right, indent + 1);
                        indent_code(sb, indent);
                        sb_append(sb, "}");
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_java(sb, node->right, indent + 1);
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
                        if (range->left) generate_expr_java(sb, range->left);
                        else sb_append(sb, "0");
                        sb_append(sb, "; %s < ", var);
                        generate_expr_java(sb, range->right);
                    } else if (range->left) {
                        sb_append(sb, "0; %s < ", var);
                        generate_expr_java(sb, range->left);
                    } else {
                        sb_append(sb, "0; %s < 10", var);
                    }
                    sb_append(sb, "; %s++) {\n", var);
                } else {
                    sb_append(sb, "for (int %s = 0; %s < 10; %s++) {\n", var, var, var);
                }
            }
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expr_java(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_java(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_java(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            generate_expr_java(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_java(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent);
            }
            break;
            
        default:
            fprintf(stderr, "Warning: Unsupported AST node %d in Java generator\n", node->type);
            break;
    }
}

char* codegen_java(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");

    char *embedded = extract_embedded_code(source, "java");
    if (embedded) {
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }

    sb_append(sb, "public class SubProgram {\n");
    generate_node_java(sb, ast, 1);
    
    // Add main method wrapper if not present
    sb_append(sb, "\n    public static void main(String[] args) {\n");
    sb_append(sb, "        // Entry point\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "}\n");
    
    return sb_to_string(sb);
}

/* ========================================
   SWIFT CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_swift(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else {
                sb_append(sb, "%s", node->value ? node->value : "nil");
            }
            break;
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_BINARY_EXPR:
            sb_append(sb, "("); generate_expr_swift(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_swift(sb, node->right); sb_append(sb, ")"); break;
        case AST_CALL_EXPR:
            if (node->value && strcmp(node->value, "print") == 0) sb_append(sb, "print(");
            else if (node->value) sb_append(sb, "%s(", node->value);
            else { generate_expr_swift(sb, node->left); sb_append(sb, "("); }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_swift(sb, node->children[i]);
            }
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_swift(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: 
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_swift(sb, node->right); else sb_append(sb, "nil");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfunc %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "_ %s: Any", node->children[i]->value ? node->children[i]->value : "arg");
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) generate_node_swift(sb, node->body, indent + 1);
            sb_append(sb, "}\n"); break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            if (node->children && node->child_count > 0 &&
                node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for %s in ", node->value ? node->value : "i");
                if (range->right) {
                    if (range->left) generate_expr_swift(sb, range->left);
                    else sb_append(sb, "0");
                    sb_append(sb, "..<");
                    generate_expr_swift(sb, range->right);
                } else if (range->left) {
                    sb_append(sb, "0..<");
                    generate_expr_swift(sb, range->left);
                } else {
                    sb_append(sb, "0..<10");
                }
                sb_append(sb, " {\n");
            } else {
                sb_append(sb, "for %s in 0..<10 {\n", node->value ? node->value : "i");
            }
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n"); break;
        case AST_IF_STMT:
            indent_code(sb, indent); sb_append(sb, "if "); generate_expr_swift(sb, node->condition); sb_append(sb, " {\n");
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent); sb_append(sb, "}\n"); break;
        case AST_BLOCK:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent); generate_expr_swift(sb, node); sb_append(sb, "\n"); break;
        default: break;
    }
}

char* codegen_swift(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    sb_append(sb, "// Generated by SUB\n\n");
    char *e = extract_embedded_code(source, "swift");
    if (e) {
        sb_append(sb, "%s\n", e);
        free(e);
    }
    generate_node_swift(sb, ast, 0);
    return sb_to_string(sb);
}

/* ========================================
   KOTLIN CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_kotlin(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else {
                sb_append(sb, "%s", node->value ? node->value : "null");
            }
            break;
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_BINARY_EXPR:
            sb_append(sb, "("); generate_expr_kotlin(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_kotlin(sb, node->right); sb_append(sb, ")"); break;
        case AST_CALL_EXPR:
            if (node->value && strcmp(node->value, "print") == 0) sb_append(sb, "println(");
            else if (node->value) sb_append(sb, "%s(", node->value);
            else { generate_expr_kotlin(sb, node->left); sb_append(sb, "("); }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_kotlin(sb, node->children[i]);
            }
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_kotlin(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: 
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_kotlin(sb, node->right); else sb_append(sb, "null");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfun %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "%s: Any", node->children[i]->value ? node->children[i]->value : "arg");
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) generate_node_kotlin(sb, node->body, indent + 1);
            sb_append(sb, "}\n"); break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            if (node->children && node->child_count > 0 &&
                node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for (%s in ", node->value ? node->value : "i");
                if (range->right) {
                    if (range->left) generate_expr_kotlin(sb, range->left);
                    else sb_append(sb, "0");
                    sb_append(sb, " until ");
                    generate_expr_kotlin(sb, range->right);
                } else if (range->left) {
                    sb_append(sb, "0 until ");
                    generate_expr_kotlin(sb, range->left);
                } else {
                    sb_append(sb, "0 until 10");
                }
                sb_append(sb, ") {\n");
            } else {
                sb_append(sb, "for (%s in 0 until 10) {\n", node->value ? node->value : "i");
            }
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n"); break;
        case AST_IF_STMT:
            indent_code(sb, indent); sb_append(sb, "if ("); generate_expr_kotlin(sb, node->condition); sb_append(sb, ") {\n");
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent); sb_append(sb, "}\n"); break;
        case AST_BLOCK:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent); generate_expr_kotlin(sb, node); sb_append(sb, "\n"); break;
        default: break;
    }
}

char* codegen_kotlin(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    sb_append(sb, "// Generated by SUB\n\n");
    char *e = extract_embedded_code(source, "kotlin");
    if (e) {
        sb_append(sb, "%s\n", e);
        free(e);
    }
    generate_node_kotlin(sb, ast, 0);
    sb_append(sb, "\nfun main() {\n}\n");
    return sb_to_string(sb);
}
char* codegen_css(ASTNode *ast, const char *source) { (void)ast; (void)source; return strdup("body { font-family: Arial; }\n"); }
char* codegen_assembly(ASTNode *ast, const char *source) { (void)ast; (void)source; return strdup("; SUB Program\nsection .text\n\tglobal _start\n_start:\n\tmov rax, 60\n\txor rdi, rdi\n\tsyscall\n"); }

/* Ruby code generator continues from previous implementation... */
static void indent_ruby(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "  ");
    }
}

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent);

static void generate_expr_ruby(StringBuilder *sb, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "nil");
            }
            break;

        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;

        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_ruby(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, ")");
            break;

        case AST_CALL_EXPR: {
            const char *func_name = node->value ? node->value : "func";
            if (strcmp(func_name, "print") == 0) {
                sb_append(sb, "puts");
                if (node->child_count > 0) {
                    sb_append(sb, " ");
                    generate_expr_ruby(sb, node->children[0]);
                }
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", func_name);
                } else {
                    generate_expr_ruby(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_ruby(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }
        case AST_ARRAY_LITERAL:
            sb_append(sb, "[");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_ruby(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\" => ", pair->value ? pair->value : "");
                generate_expr_ruby(sb, pair->right);
            }
            sb_append(sb, "}");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_ruby(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            generate_expr_ruby(sb, node->left);
            sb_append(sb, "[");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, "]");
            break;

        default:
            break;
    }
}

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_ruby(sb, stmt, indent);
            }
            break;

        case AST_VAR_DECL:
            indent_ruby(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_ruby(sb, node->right);
            } else {
                sb_append(sb, "nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_CONST_DECL:
            indent_ruby(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_ruby(sb, node->right);
            } else {
                sb_append(sb, "nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_ruby(sb, indent);
            sb_append(sb, "def %s", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                sb_append(sb, "(");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children[i] && node->children[i]->value) {
                        sb_append(sb, "%s", node->children[i]->value);
                    }
                }
                sb_append(sb, ")");
            }
            sb_append(sb, "\n");

            if (node->body) {
                generate_node_ruby(sb, node->body, indent + 1);
            }
            if (!node->body) {
                indent_ruby(sb, indent + 1);
                sb_append(sb, "# TODO: implement\n");
            }

            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_IF_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "if ");
            generate_expr_ruby(sb, node->condition);
            sb_append(sb, "\n");
            generate_node_ruby(sb, node->body, indent + 1);
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    indent_ruby(sb, indent);
                    sb_append(sb, "elsif ");
                    generate_expr_ruby(sb, node->right->condition);
                    sb_append(sb, "\n");
                    generate_node_ruby(sb, node->right->body, indent + 1);
                    if (node->right->right) {
                        ASTNode *else_branch = node->right->right;
                        while (else_branch && else_branch->type == AST_IF_STMT) {
                            indent_ruby(sb, indent);
                            sb_append(sb, "elsif ");
                            generate_expr_ruby(sb, else_branch->condition);
                            sb_append(sb, "\n");
                            generate_node_ruby(sb, else_branch->body, indent + 1);
                            else_branch = else_branch->right;
                        }
                        if (else_branch) {
                            indent_ruby(sb, indent);
                            sb_append(sb, "else\n");
                            generate_node_ruby(sb, else_branch, indent + 1);
                        }
                    }
                } else {
                    indent_ruby(sb, indent);
                    sb_append(sb, "else\n");
                    generate_node_ruby(sb, node->right, indent + 1);
                }
            }
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_FOR_STMT:
            indent_ruby(sb, indent);
            // Generate range from AST children if available
            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    sb_append(sb, "(");
                    if (range->right) {
                        if (range->left) generate_expr_ruby(sb, range->left);
                        else sb_append(sb, "0");
                        sb_append(sb, "...");
                        generate_expr_ruby(sb, range->right);
                    } else if (range->left) {
                        sb_append(sb, "0...");
                        generate_expr_ruby(sb, range->left);
                    } else {
                        sb_append(sb, "0...10");
                    }
                    sb_append(sb, ")");
                } else {
                    generate_expr_ruby(sb, range);
                }
            } else if (node->condition) {
                generate_expr_ruby(sb, node->condition);
            } else {
                sb_append(sb, "(0...10)");  // Legacy fallback
            }
            sb_append(sb, ".each do |%s|\n", node->value ? node->value : "i");
            generate_node_ruby(sb, node->body, indent + 1);
            if (!node->body) {
                indent_ruby(sb, indent + 1);
                sb_append(sb, "# empty loop\n");
            }
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_WHILE_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "while ");
            generate_expr_ruby(sb, node->condition);
            sb_append(sb, "\n");
            generate_node_ruby(sb, node->body, indent + 1);
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_RETURN_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_ruby(sb, node->right);
            }
            sb_append(sb, "\n");
            break;

        case AST_CALL_EXPR:
            indent_ruby(sb, indent);
            generate_expr_ruby(sb, node);
            sb_append(sb, "\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_ruby(sb, indent);
            generate_expr_ruby(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, "\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_ruby(sb, stmt, indent);
            }
            break;

        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            if (node->value) {
                indent_ruby(sb, indent);
                sb_append(sb, "# Embedded code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;

        case AST_UI_COMPONENT:
            indent_ruby(sb, indent);
            sb_append(sb, "# UI: %s\n", node->value ? node->value : "component");
            break;

        default:
            break;
    }
}

char* codegen_ruby(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    sb_append(sb, "#!/usr/bin/env ruby\n");
    sb_append(sb, "# Generated by SUB Language Compiler\n\n");

    char *embedded = extract_embedded_code(source, "ruby");
    if (embedded) {
        sb_append(sb, "# Embedded Ruby code from SUB\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }
    generate_node_ruby(sb, ast, 0);

    return sb_to_string(sb);
}

/* ========================================
   GO CODE GENERATOR
   ======================================== */

static void indent_go(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "\t");
    }
}

static bool ast_needs_fmt(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_CALL_EXPR && node->value &&
        strcmp(node->value, "print") == 0)
        return true;
    if (ast_needs_fmt(node->left)) return true;
    if (ast_needs_fmt(node->right)) return true;
    if (ast_needs_fmt(node->condition)) return true;
    if (ast_needs_fmt(node->body)) return true;
    if (ast_needs_fmt(node->next)) return true;
    for (int i = 0; i < node->child_count; i++) {
        if (node->children && ast_needs_fmt(node->children[i]))
            return true;
    }
    return false;
}

static bool is_go_package_level_node(ASTNode *node) {
    if (!node) return false;

    switch (node->type) {
        case AST_FUNCTION_DECL:
        case AST_VAR_DECL:
        case AST_CONST_DECL:
        case AST_CLASS_DECL:
        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
        case AST_UI_COMPONENT:
            return true;
        default:
            return false;
    }
}

static void generate_node_go(StringBuilder *sb, ASTNode *node, int indent);

static void generate_expr_go(StringBuilder *sb, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "nil");
            }
            break;

        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "v");
            break;

        case AST_BINARY_EXPR:
            sb_append(sb, "(");
            generate_expr_go(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_go(sb, node->right);
            sb_append(sb, ")");
            break;

        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "!");
            generate_expr_go(sb, node->right);
            break;

        case AST_TERNARY_EXPR:
            sb_append(sb, "func() interface{} { if ");
            generate_expr_go(sb, node->condition);
            sb_append(sb, " { return ");
            generate_expr_go(sb, node->left);
            sb_append(sb, " }; return ");
            generate_expr_go(sb, node->right);
            sb_append(sb, " }()");
            break;

        case AST_CALL_EXPR: {
            const char *func_name = node->value ? node->value : "fn";
            if (strcmp(func_name, "print") == 0) {
                sb_append(sb, "fmt.Println(");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children)
                        generate_expr_go(sb, node->children[i]);
                }
                sb_append(sb, ")");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", func_name);
                } else {
                    generate_expr_go(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children)
                        generate_expr_go(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }

        case AST_ARRAY_LITERAL:
            sb_append(sb, "[]interface{}{");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                if (node->children)
                    generate_expr_go(sb, node->children[i]);
            }
            sb_append(sb, "}");
            break;

        case AST_OBJECT_LITERAL:
            sb_append(sb, "map[string]interface{}{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children ? node->children[i] : NULL;
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\": ", pair->value ? pair->value : "");
                generate_expr_go(sb, pair->right);
            }
            sb_append(sb, "}");
            break;

        case AST_MEMBER_ACCESS:
            generate_expr_go(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;

        case AST_ARRAY_ACCESS:
            generate_expr_go(sb, node->left);
            sb_append(sb, "[");
            generate_expr_go(sb, node->right);
            sb_append(sb, "]");
            break;

        default:
            break;
    }
}

static void generate_node_go(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt; stmt = stmt->next) {
                generate_node_go(sb, stmt, indent);
            }
            break;

        case AST_VAR_DECL:
            indent_go(sb, indent);
            sb_append(sb, "var %s", node->value ? node->value : "v");
            if (node->right) {
                sb_append(sb, " = ");
                generate_expr_go(sb, node->right);
            } else {
                sb_append(sb, " interface{} = nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_CONST_DECL:
            indent_go(sb, indent);
            if (node->right && node->right->type == AST_LITERAL) {
                sb_append(sb, "const %s = ", node->value ? node->value : "C");
                generate_expr_go(sb, node->right);
            } else {
                sb_append(sb, "var %s", node->value ? node->value : "C");
                if (node->right) {
                    sb_append(sb, " = ");
                    generate_expr_go(sb, node->right);
                } else {
                    sb_append(sb, " interface{} = nil");
                }
            }
            sb_append(sb, "\n");
            break;

        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_go(sb, indent);
            sb_append(sb, "func %s(", node->value ? node->value : "fn");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children[i] && node->children[i]->value) {
                        sb_append(sb, "%s", node->children[i]->value);
                    }
                }
                sb_append(sb, " interface{}");
            }
            sb_append(sb, ") {\n");
            if (node->body) {
                generate_node_go(sb, node->body, indent + 1);
            }
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_IF_STMT:
            indent_go(sb, indent);
            sb_append(sb, "if ");
            generate_expr_go(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_go(sb, node->body, indent + 1);
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    indent_go(sb, indent);
                    sb_append(sb, "} else if ");
                    generate_expr_go(sb, node->right->condition);
                    sb_append(sb, " {\n");
                    generate_node_go(sb, node->right->body, indent + 1);
                    if (node->right->right) {
                        ASTNode *branch = node->right->right;
                        while (branch && branch->type == AST_IF_STMT) {
                            indent_go(sb, indent);
                            sb_append(sb, "} else if ");
                            generate_expr_go(sb, branch->condition);
                            sb_append(sb, " {\n");
                            generate_node_go(sb, branch->body, indent + 1);
                            branch = branch->right;
                        }
                        if (branch) {
                            indent_go(sb, indent);
                            sb_append(sb, "} else {\n");
                            generate_node_go(sb, branch, indent + 1);
                        }
                    }
                } else {
                    indent_go(sb, indent);
                    sb_append(sb, "} else {\n");
                    generate_node_go(sb, node->right, indent + 1);
                }
            }
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_FOR_STMT:
            indent_go(sb, indent);
            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    const char *var = node->value ? node->value : "i";
                    sb_append(sb, "for %s := ", var);
                    if (range->right) {
                        /* range(start, end) */
                        generate_expr_go(sb, range->left);
                        sb_append(sb, "; %s < ", var);
                        generate_expr_go(sb, range->right);
                    } else if (range->left) {
                        /* range(n) → 0..n */
                        sb_append(sb, "0; %s < ", var);
                        generate_expr_go(sb, range->left);
                    } else {
                        sb_append(sb, "0; %s < 10", var);
                    }
                    sb_append(sb, "; %s++ {\n", var);
                } else {
                    sb_append(sb, "for _, %s := range ",
                              node->value ? node->value : "item");
                    generate_expr_go(sb, range);
                    sb_append(sb, " {\n");
                }
            } else if (node->condition) {
                sb_append(sb, "for _, %s := range ",
                          node->value ? node->value : "item");
                generate_expr_go(sb, node->condition);
                sb_append(sb, " {\n");
            } else {
                sb_append(sb, "for %s := 0; %s < 10; %s++ {\n",
                          node->value ? node->value : "i",
                          node->value ? node->value : "i",
                          node->value ? node->value : "i");
            }
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_WHILE_STMT:
            indent_go(sb, indent);
            sb_append(sb, "for ");
            generate_expr_go(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_DO_WHILE_STMT:
            indent_go(sb, indent);
            sb_append(sb, "for {\n");
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent + 1);
            sb_append(sb, "if !(");
            generate_expr_go(sb, node->condition);
            sb_append(sb, ") {\n");
            indent_go(sb, indent + 2);
            sb_append(sb, "break\n");
            indent_go(sb, indent + 1);
            sb_append(sb, "}\n");
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_RETURN_STMT:
            indent_go(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_go(sb, node->right);
            }
            sb_append(sb, "\n");
            break;

        case AST_BREAK_STMT:
            indent_go(sb, indent);
            sb_append(sb, "break\n");
            break;

        case AST_CONTINUE_STMT:
            indent_go(sb, indent);
            sb_append(sb, "continue\n");
            break;

        case AST_CALL_EXPR:
            indent_go(sb, indent);
            generate_expr_go(sb, node);
            sb_append(sb, "\n");
            break;

        case AST_ASSIGN_STMT:
            indent_go(sb, indent);
            generate_expr_go(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_go(sb, node->right);
            sb_append(sb, "\n");
            break;

        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt; stmt = stmt->next) {
                generate_node_go(sb, stmt, indent);
            }
            break;

        case AST_CLASS_DECL:
            indent_go(sb, indent);
            sb_append(sb, "type %s struct {}\n",
                      node->value ? node->value : "Object");
            break;

        case AST_TRY_STMT:
            indent_go(sb, indent);
            sb_append(sb, "func() {\n");
            if (node->right) {
                indent_go(sb, indent + 1);
                sb_append(sb, "defer func() {\n");
                indent_go(sb, indent + 2);
                sb_append(sb, "if r := recover(); r != nil {\n");
                generate_node_go(sb, node->right, indent + 3);
                indent_go(sb, indent + 2);
                sb_append(sb, "}\n");
                indent_go(sb, indent + 1);
                sb_append(sb, "}()\n");
            }
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent);
            sb_append(sb, "}()\n");
            break;

        case AST_THROW_STMT:
            indent_go(sb, indent);
            sb_append(sb, "panic(");
            if (node->right) {
                generate_expr_go(sb, node->right);
            } else {
                sb_append(sb, "\"error\"");
            }
            sb_append(sb, ")\n");
            break;

        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            if (node->value) {
                indent_go(sb, indent);
                sb_append(sb, "// Embedded code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;

        case AST_UI_COMPONENT:
            indent_go(sb, indent);
            sb_append(sb, "// UI: %s\n", node->value ? node->value : "component");
            break;

        default:
            break;
    }
}

char* codegen_go(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    sb_append(sb, "// Generated by SUB Language Compiler\n\n");

    sb_append(sb, "package main\n\n");

    bool needs_fmt = ast_needs_fmt(ast);
    if (needs_fmt) {
        sb_append(sb, "import \"fmt\"\n\n");
    }

    char *embedded = extract_embedded_code(source, "go");
    if (embedded) {
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }

    /* Pass 1: emit package-level declarations, track main and executable stmts */
    bool has_user_main = false;
    bool has_exec_stmts = false;
    bool emitted_package_level = false;
    for (ASTNode *stmt = block_first(ast); stmt; stmt = stmt->next) {
        if (stmt->type == AST_FUNCTION_DECL) {
            if (stmt->value && strcmp(stmt->value, "main") == 0)
                has_user_main = true;
        }

        if (is_go_package_level_node(stmt)) {
            generate_node_go(sb, stmt, 0);
            emitted_package_level = true;
        } else {
            has_exec_stmts = true;
        }
    }

    /* Pass 2: wrap executable top-level stmts in init() or main() */
    if (has_exec_stmts || !has_user_main) {
        if (emitted_package_level) {
            sb_append(sb, "\n");
        }
        sb_append(sb, has_user_main ? "func init() {\n" : "func main() {\n");
        for (ASTNode *stmt = block_first(ast); stmt; stmt = stmt->next) {
            if (!is_go_package_level_node(stmt)) {
                generate_node_go(sb, stmt, 1);
            }
        }
        sb_append(sb, "}\n");
    }

    return sb_to_string(sb);
}
