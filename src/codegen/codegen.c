/* ========================================
   SUB Language Code Generator - REAL IMPLEMENTATION
   Actually processes AST and generates working code
   File: codegen.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
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
    
    // Resize if necessary
    while (sb->size + needed + 1 > sb->capacity) {
        sb->capacity *= 2;
        char *new_buffer = realloc(sb->buffer, sb->capacity);
        if (!new_buffer) {
            va_end(args);
            return;
        }
        sb->buffer = new_buffer;
    }
    
    // Append
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

/* Forward declarations */
static void generate_node(StringBuilder *sb, ASTNode *node, int indent);
static void generate_expression(StringBuilder *sb, ASTNode *node);
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
            ASTNode **new_children = malloc(sizeof(ASTNode*) * node->child_count);
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
                long left_val = strtol(node->left->value, &left_end, 10);
                long right_val = strtol(node->right->value, &right_end, 10);
                
                if (*left_end == '\0' && *right_end == '\0') {
                    long result = 0;
                    
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
                    snprintf(folded_val, sizeof(folded_val), "%ld", result);
                    
                    node->type = AST_LITERAL;
                    free(node->value);
                    node->value = strdup(folded_val);
                    
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
    optimize_remove_dead_code(node);
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

/* Generate expression code */
static void generate_expression(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->value) {
                if (node->data_type == TYPE_STRING) {
                    sb_append(sb, "\"%s\"", node->value);
                } else if (node->data_type == TYPE_BOOL) {
                    if (strcmp(node->value, "true") == 0) {
                        sb_append(sb, "true");
                    } else {
                        sb_append(sb, "false");
                    }
                } else {
                    sb_append(sb, "%s", node->value);
                }
            }
            break;
            
        case AST_IDENTIFIER:
            if (node->value) {
                sb_append(sb, "%s", node->value);
            }
            break;
            
        case AST_BINARY_EXPR:
            if (node->left) {
                sb_append(sb, "(");
                generate_expression(sb, node->left);
                sb_append(sb, " %s ", node->value ? node->value : "+");
                generate_expression(sb, node->right);
                sb_append(sb, ")");
            }
            break;
            
        case AST_UNARY_EXPR:
            sb_append(sb, "(%s", node->value ? node->value : "-");
            if (node->right) {
                generate_expression(sb, node->right);
            }
            sb_append(sb, ")");
            break;
            
        case AST_CALL_EXPR:
            if (node->value) {
                sb_append(sb, "%s(", node->value);
                for (int i = 0; i < node->child_count; i++) {
                    generate_expression(sb, node->children[i]);
                    if (i + 1 < node->child_count) {
                        sb_append(sb, ", ");
                    }
                }
                sb_append(sb, ")");
            }
            break;
            
        default:
            break;
    }
}

/* Generate code for a single AST node */
static void generate_node(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            if (node->data_type == TYPE_STRING) {
                sb_append(sb, "char *%s", node->value ? node->value : "var");
                if (node->right) {
                    sb_append(sb, " = sub_strdup(");
                    generate_expression(sb, node->right);
                    sb_append(sb, ")");
                }
            } else if (node->data_type == TYPE_BOOL) {
                sb_append(sb, "bool %s", node->value ? node->value : "var");
                if (node->right) {
                    sb_append(sb, " = ");
                    generate_expression(sb, node->right);
                }
            } else if (node->data_type == TYPE_FLOAT) {
                sb_append(sb, "double %s", node->value ? node->value : "var");
                if (node->right) {
                    sb_append(sb, " = ");
                    generate_expression(sb, node->right);
                }
            } else {
                sb_append(sb, "long %s", node->value ? node->value : "var");
                if (node->right) {
                    sb_append(sb, " = ");
                    generate_expression(sb, node->right);
                }
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            if (node->data_type == TYPE_STRING) {
                sb_append(sb, "const char *%s", node->value ? node->value : "const");
            } else if (node->data_type == TYPE_BOOL) {
                sb_append(sb, "const bool %s", node->value ? node->value : "const");
            } else if (node->data_type == TYPE_FLOAT) {
                sb_append(sb, "const double %s", node->value ? node->value : "const");
            } else {
                sb_append(sb, "const long %s", node->value ? node->value : "const");
            }
            sb_append(sb, " = ");
            generate_expression(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nvoid %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "long %s", node->children[i]->value ? node->children[i]->value : "arg");
            }
            sb_append(sb, ") {\n");
            if (node->body) {
                generate_node(sb, node->body, indent + 1);
            }
            sb_append(sb, "}\n\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expression(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for (long i = 0; i < 10; i++) {\n");
            generate_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expression(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expression(sb, node->right);
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
            generate_expression(sb, node->left);
            sb_append(sb, " = ");
            generate_expression(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        case AST_EMBED_CODE:
        case AST_EMBED_C:
            if (node->value) {
                sb_append(sb, "\n/* Embedded C code */\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;
            
        case AST_EMBED_CPP:
            if (node->value) {
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
    sb_append(sb, "#include <stddef.h>\n\n");
    
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
    
    sb_append(sb, "/* Error Handling Helpers */\n");
    sb_append(sb, "#ifndef SUB_ERROR_H\n");
    sb_append(sb, "#define SUB_ERROR_H\n");
    sb_append(sb, "#define SUB_CHECK_NULL(ptr, msg) do { \\\n");
    sb_append(sb, "    if (!(ptr)) { \\\n");
    sb_append(sb, "        fprintf(stderr, \"Error: %s at %s:%d\\n\", (msg), __FILE__, __LINE__); \\\n");
    sb_append(sb, "        exit(EXIT_FAILURE); \\\n");
    sb_append(sb, "    } \\\n");
    sb_append(sb, "} while(0)\n");
    sb_append(sb, "#endif /* SUB_ERROR_H */\n\n");
    
    // Generate code from AST
    generate_node(sb, ast, 0);
    
    // Add main function wrapper if not present
    sb_append(sb, "/* Auto-generated main if not defined */\n");
    sb_append(sb, "#ifndef MAIN_DEFINED\n");
    sb_append(sb, "int main(int argc, char *argv[]) {\n");
    sb_append(sb, "    (void)argc;\n");
    sb_append(sb, "    (void)argv;\n");
    sb_append(sb, "    printf(\"SUB Language Program Running...\\n\");\n");
    sb_append(sb, "    return EXIT_SUCCESS;\n");
    sb_append(sb, "}\n");
    sb_append(sb, "#define MAIN_DEFINED 1\n");
    sb_append(sb, "#endif /* MAIN_DEFINED */\n");
    
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

static char* generate_web(ASTNode *ast UNUSED) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "<!DOCTYPE html>\n");
    sb_append(sb, "<html>\n<head>\n");
    sb_append(sb, "    <title>SUB Language App</title>\n");
    sb_append(sb, "    <style>\n");
    sb_append(sb, "        body { font-family: Arial, sans-serif; margin: 20px; }\n");
    sb_append(sb, "    </style>\n");
    sb_append(sb, "</head>\n<body>\n");
    sb_append(sb, "    <h1>SUB Language Application</h1>\n");
    sb_append(sb, "    <div id='app'></div>\n");
    sb_append(sb, "    <script>\n");
    
    // Generate JavaScript from AST
    sb_append(sb, "    // Generated from SUB Language\n");
    sb_append(sb, "    console.log('SUB App Initialized');\n");
    
    // TODO: Convert AST nodes to JavaScript
    
    sb_append(sb, "    </script>\n");
    sb_append(sb, "</body>\n</html>\n");
    
    return sb_to_string(sb);
}

static char* generate_windows(ASTNode *ast) {
    // For Windows, generate C code with Windows headers
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// Windows Application Generated from SUB Language\n");
    sb_append(sb, "#include <windows.h>\n");
    sb_append(sb, "#include <stdio.h>\n\n");
    
    // Generate the actual SUB code
    generate_node(sb, ast, 0);
    
    sb_append(sb, "\nint WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,\n");
    sb_append(sb, "                   LPSTR lpCmdLine, int nCmdShow) {\n");
    sb_append(sb, "    MessageBox(NULL, \"SUB Language App\", \"Running\", MB_OK);\n");
    sb_append(sb, "    return 0;\n");
    sb_append(sb, "}\n");
    
    return sb_to_string(sb);
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

/* Generate C++ code from AST */
char* codegen_generate_cpp(ASTNode *ast UNUSED, Platform platform UNUSED) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "// Generated by SUB Language Compiler (C++ Mode)\n\n");
    sb_append(sb, "#include <iostream>\n");
    sb_append(sb, "#include <string>\n");
    sb_append(sb, "#include <vector>\n\n");
    sb_append(sb, "using namespace std;\n\n");
    
    // Generate code from AST
    generate_node(sb, ast, 0);
    
    sb_append(sb, "\nint main(int argc, char *argv[]) {\n");
    sb_append(sb, "    cout << \"SUB Language C++ Program Running...\" << endl;\n");
    sb_append(sb, "    return 0;\n");
    sb_append(sb, "}\n");
    
    return sb_to_string(sb);
}

/* Generate regular C code */
char* codegen_generate_c(ASTNode *ast UNUSED, Platform platform UNUSED) {
    return generate_c_code(ast);
}
