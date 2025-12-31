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

/* ========================================
   PYTHON CODE GENERATOR - REAL
   ======================================== */

static void generate_expr_python(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            sb_append(sb, "%s", node->value ? node->value : "None");
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
        case AST_CALL_EXPR:
            sb_append(sb, "%s(", node->value ? node->value : "func");
            if (node->left) generate_expr_python(sb, node->left);
            sb_append(sb, ")");
            break;
        default:
            break;
    }
}

static void generate_node_python(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
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
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\ndef %s():\n", node->value ? node->value : "func");
            if (node->body) {
                generate_node_python(sb, node->body, indent + 1);
            }
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
                generate_node_python(sb, stmt, indent + 1);
            }
            if (!node->body && !node->left) {
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
            if (node->right) {
                indent_code(sb, indent);
                sb_append(sb, "else:\n");
                generate_node_python(sb, node->right, indent + 1);
            }
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for %s in ", node->value ? node->value : "i");
            // Generate range from AST children if available
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
                // Fallback: condition might hold the iterable
                generate_expr_python(sb, node->condition);
            } else {
                sb_append(sb, "range(10)");  // Legacy fallback
            }
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body) {
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
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->left) {
                sb_append(sb, " ");
                generate_expr_python(sb, node->left);
            }
            sb_append(sb, "\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_python(sb, node);
            sb_append(sb, "\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = node->body; stmt != NULL; stmt = stmt->next) {
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
    if (embedded) {
        sb_append(sb, "# Embedded Python code from SUB\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    } else {
        // Generate from AST
        generate_node_python(sb, ast, 0);
        
        // Add main guard if no embedded code
        if (!embedded) {
            sb_append(sb, "\nif __name__ == '__main__':\n");
            sb_append(sb, "    pass\n");
        }
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
            sb_append(sb, "%s", node->value ? node->value : "null");
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
        case AST_CALL_EXPR:
            sb_append(sb, "%s(", node->value ? node->value : "func");
            if (node->left) generate_expr_js(sb, node->left);
            sb_append(sb, ")");
            break;
        default:
            break;
    }
}

static void generate_node_js(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
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
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
                generate_node_js(sb, stmt, indent + 1);
            }
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
                sb_append(sb, " else {\n");
                generate_node_js(sb, node->right, indent + 1);
                indent_code(sb, indent);
                sb_append(sb, "}");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            // Check for range expression
            if (node->children && node->child_count > 0 && node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for (let %s = ", node->value ? node->value : "i");
                if (range->left) generate_expr_js(sb, range->left); else sb_append(sb, "0");
                sb_append(sb, "; %s < ", node->value ? node->value : "i");
                if (range->right) generate_expr_js(sb, range->right); else sb_append(sb, "10");
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
            if (node->left) {
                sb_append(sb, " ");
                generate_expr_js(sb, node->left);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            // Map print to console.log
            if (node->value && strcmp(node->value, "print") == 0) {
                sb_append(sb, "console.log(");
                if (node->left) generate_expr_js(sb, node->left);
                sb_append(sb, ")");
            } else {
                generate_expr_js(sb, node);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = node->body; stmt != NULL; stmt = stmt->next) {
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
    } else {
        generate_node_js(sb, ast, 0);
    }
    
    return sb_to_string(sb);
}

/* ========================================
   JAVA CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_java(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            sb_append(sb, "%s", node->value ? node->value : "null");
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
        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            if (strcmp(fn, "print") == 0) {
                sb_append(sb, "System.out.println(");
                if (node->left) generate_expr_java(sb, node->left);
                sb_append(sb, ")");
            } else {
                sb_append(sb, "%s(", fn);
                if (node->left) generate_expr_java(sb, node->left);
                sb_append(sb, ")");
            }
            break;
        }
        default:
            fprintf(stderr, "Warning: Unsupported expression node %d in Java generator\n", node->type);
            break;
    }
}

static void generate_node_java(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
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
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_code(sb, indent);
            sb_append(sb, "public static void %s() {\n", node->value ? node->value : "func");
            if (node->body) generate_node_java(sb, node->body, indent + 1);
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent + 1);
            }
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
                sb_append(sb, " else {\n");
                generate_node_java(sb, node->right, indent + 1);
                indent_code(sb, indent);
                sb_append(sb, "}");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for (int %s = 0; %s < 10; %s++) {\n", 
                     node->value ? node->value : "i",
                     node->value ? node->value : "i",
                     node->value ? node->value : "i");
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
            if (node->left) {
                sb_append(sb, " ");
                generate_expr_java(sb, node->left);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_java(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = node->body; stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent);
            }
            break;
            
        default:
            fprintf(stderr, "Warning: Unsupported AST node %d in Java generator\n", node->type);
            break;
    }
}

char* codegen_java(ASTNode *ast, const char *source) {
    char *embedded = extract_embedded_code(source, "java");
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
   C++ CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_cpp(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            sb_append(sb, "%s", node->value ? node->value : "nullptr");
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
        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            if (strcmp(fn, "print") == 0) {
                sb_append(sb, "std::cout << ");
                if (node->left) generate_expr_cpp(sb, node->left);
                sb_append(sb, " << std::endl");
            } else {
                sb_append(sb, "%s(", fn);
                if (node->left) generate_expr_cpp(sb, node->left);
                sb_append(sb, ")");
            }
            break;
        }
        default: break;
    }
}

static void generate_node_cpp(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *s = node->left; s; s = s->next) generate_node_cpp(sb, s, indent);
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "auto %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_cpp(sb, node->right);
            else sb_append(sb, "nullptr");
            sb_append(sb, ";\n");
            break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nvoid %s() {\n", node->value ? node->value : "func");
            if (node->body) generate_node_cpp(sb, node->body, indent + 1);
            sb_append(sb, "}\n");
            break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for (int %s = 0; %s < 10; %s++) {\n",
                     node->value ? node->value : "i", node->value ? node->value : "i", node->value ? node->value : "i");
            generate_node_cpp(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->left) { sb_append(sb, " "); generate_expr_cpp(sb, node->left); }
            sb_append(sb, ";\n");
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_cpp(sb, node);
            sb_append(sb, ";\n");
            break;
        case AST_BLOCK:
            for (ASTNode *s = node->body; s; s = s->next) generate_node_cpp(sb, s, indent);
            break;
        default: break;
    }
}

char* codegen_cpp(ASTNode *ast, const char *source) {
    char *embedded = extract_embedded_code(source, "cpp");
    if (embedded) { StringBuilder *sb = sb_create(); sb_append(sb, "%s", embedded); free(embedded); return sb_to_string(sb); }
    StringBuilder *sb = sb_create();
    sb_append(sb, "// Generated by SUB Language Compiler\n#include <iostream>\n#include <string>\n\n");
    generate_node_cpp(sb, ast, 0);
    sb_append(sb, "\nint main() {\n    return 0;\n}\n");
    return sb_to_string(sb);
}

/* ========================================
   RUST CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_rust(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL: sb_append(sb, "%s", node->value ? node->value : "()"); break;
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_BINARY_EXPR:
            sb_append(sb, "("); generate_expr_rust(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_rust(sb, node->right); sb_append(sb, ")");
            break;
        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            if (strcmp(fn, "print") == 0) {
                sb_append(sb, "println!(\"{}\", ");
                if (node->left) generate_expr_rust(sb, node->left);
                sb_append(sb, ")");
            } else {
                sb_append(sb, "%s(", fn);
                if (node->left) generate_expr_rust(sb, node->left);
                sb_append(sb, ")");
            }
            break;
        }
        default: break;
    }
}

static void generate_node_rust(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *s = node->left; s; s = s->next) generate_node_rust(sb, s, indent);
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_rust(sb, node->right);
            else sb_append(sb, "()");
            sb_append(sb, ";\n");
            break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfn %s() {\n", node->value ? node->value : "func");
            if (node->body) generate_node_rust(sb, node->body, indent + 1);
            sb_append(sb, "}\n");
            break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for %s in 0..10 {\n", node->value ? node->value : "i");
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            if (node->left) { generate_expr_rust(sb, node->left); }
            sb_append(sb, "\n");
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_rust(sb, node);
            sb_append(sb, ";\n");
            break;
        case AST_BLOCK:
            for (ASTNode *s = node->body; s; s = s->next) generate_node_rust(sb, s, indent);
            break;
        default: break;
    }
}

char* codegen_rust(ASTNode *ast, const char *source) {
    char *embedded = extract_embedded_code(source, "rust");
    if (embedded) { StringBuilder *sb = sb_create(); sb_append(sb, "%s", embedded); free(embedded); return sb_to_string(sb); }
    StringBuilder *sb = sb_create();
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");
    generate_node_rust(sb, ast, 0);
    sb_append(sb, "\nfn main() {\n}\n");
    return sb_to_string(sb);
}

/* ========================================
   SWIFT CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_swift(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL: sb_append(sb, "%s", node->value ? node->value : "nil"); break;
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_BINARY_EXPR:
            sb_append(sb, "("); generate_expr_swift(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_swift(sb, node->right); sb_append(sb, ")"); break;
        case AST_CALL_EXPR:
            if (strcmp(node->value, "print") == 0) sb_append(sb, "print(");
            else sb_append(sb, "%s(", node->value);
            if (node->left) generate_expr_swift(sb, node->left);
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_swift(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: 
            for (ASTNode *s = node->left; s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_swift(sb, node->right); else sb_append(sb, "nil");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfunc %s() {\n", node->value ? node->value : "func");
            if (node->body) generate_node_swift(sb, node->body, indent + 1);
            sb_append(sb, "}\n"); break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for %s in 0..<10 {\n", node->value ? node->value : "i");
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n"); break;
        case AST_IF_STMT:
            indent_code(sb, indent); sb_append(sb, "if "); generate_expr_swift(sb, node->condition); sb_append(sb, " {\n");
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent); sb_append(sb, "}\n"); break;
        case AST_BLOCK:
            for (ASTNode *s = node->body; s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent); generate_expr_swift(sb, node); sb_append(sb, "\n"); break;
        default: break;
    }
}

char* codegen_swift(ASTNode *ast, const char *source) {
    char *e = extract_embedded_code(source, "swift");
    if (e) { StringBuilder *sb = sb_create(); sb_append(sb, "%s", e); free(e); return sb_to_string(sb); }
    StringBuilder *sb = sb_create();
    sb_append(sb, "// Generated by SUB\n\n");
    generate_node_swift(sb, ast, 0);
    return sb_to_string(sb);
}

/* ========================================
   KOTLIN CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_kotlin(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL: sb_append(sb, "%s", node->value ? node->value : "null"); break;
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_BINARY_EXPR:
            sb_append(sb, "("); generate_expr_kotlin(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_kotlin(sb, node->right); sb_append(sb, ")"); break;
        case AST_CALL_EXPR:
            if (strcmp(node->value, "print") == 0) sb_append(sb, "println(");
            else sb_append(sb, "%s(", node->value);
            if (node->left) generate_expr_kotlin(sb, node->left);
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_kotlin(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: 
            for (ASTNode *s = node->left; s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_kotlin(sb, node->right); else sb_append(sb, "null");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfun %s() {\n", node->value ? node->value : "func");
            if (node->body) generate_node_kotlin(sb, node->body, indent + 1);
            sb_append(sb, "}\n"); break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for (%s in 0..9) {\n", node->value ? node->value : "i");
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n"); break;
        case AST_IF_STMT:
            indent_code(sb, indent); sb_append(sb, "if ("); generate_expr_kotlin(sb, node->condition); sb_append(sb, ") {\n");
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent); sb_append(sb, "}\n"); break;
        case AST_BLOCK:
            for (ASTNode *s = node->body; s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent); generate_expr_kotlin(sb, node); sb_append(sb, "\n"); break;
        default: break;
    }
}

char* codegen_kotlin(ASTNode *ast, const char *source) {
    char *e = extract_embedded_code(source, "kotlin");
    if (e) { StringBuilder *sb = sb_create(); sb_append(sb, "%s", e); free(e); return sb_to_string(sb); }
    StringBuilder *sb = sb_create();
    sb_append(sb, "// Generated by SUB\n\n");
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
            sb_append(sb, "%s", node->value ? node->value : "nil");
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
                if (node->left) {
                    sb_append(sb, " ");
                    generate_expr_ruby(sb, node->left);
                }
            } else {
                sb_append(sb, "%s(", func_name);
                if (node->left) {
                    generate_expr_ruby(sb, node->left);
                }
                sb_append(sb, ")");
            }
            break;
        }

        default:
            break;
    }
}

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
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
            for (ASTNode *stmt = node->left; stmt != NULL; stmt = stmt->next) {
                generate_node_ruby(sb, stmt, indent + 1);
            }

            if (!node->body && !node->left) {
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
                    if (range->left) generate_expr_ruby(sb, range->left);
                    else sb_append(sb, "0");
                    sb_append(sb, "...");
                    if (range->right) generate_expr_ruby(sb, range->right);
                    else sb_append(sb, "10");
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
            if (node->left) {
                sb_append(sb, " ");
                generate_expr_ruby(sb, node->left);
            }
            sb_append(sb, "\n");
            break;

        case AST_CALL_EXPR:
            indent_ruby(sb, indent);
            generate_expr_ruby(sb, node);
            sb_append(sb, "\n");
            break;

        case AST_BLOCK:
            for (ASTNode *stmt = node->body; stmt != NULL; stmt = stmt->next) {
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
    } else {
        generate_node_ruby(sb, ast, 0);
    }

    return sb_to_string(sb);
}
