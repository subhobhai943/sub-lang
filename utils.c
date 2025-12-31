/* ========================================
   SUB Language Utility Functions
   File: utils.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include <stdarg.h>

// Utility: Print compilation error
void compile_error(const char *message, int line) {
    fprintf(stderr, "Compilation error at line %d: %s\n", line, message);
}

void compile_error_with_col(const char *message, int line, int column) {
    fprintf(stderr, "Compilation error at line %d, column %d: %s\n", line, column, message);
}

// String utilities
char* string_concat(const char *s1, const char *s2) {
    size_t len = strlen(s1) + strlen(s2) + 1;
    char *result = malloc(len);
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

// Print token (for debugging)
void print_token(Token *token) {
    const char *type_names[] = {
        "HASH", "VAR", "FUNCTION", "IF", "ELIF", "ELSE",
        "FOR", "WHILE", "RETURN", "END", "EMBED", "ENDEMBED",
        "UI", "IDENTIFIER", "NUMBER", "STRING", "OPERATOR",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "DOT", "COMMA",
        "NEWLINE", "EOF"
    };
    
    if (token->type < sizeof(type_names) / sizeof(char*)) {
        printf("Token: %-12s ", type_names[token->type]);
        if (token->value) {
            printf("Value: '%s'", token->value);
        }
        printf(" at line %d, col %d\n", token->line, token->column);
    }
}

// Print AST (for debugging)
void print_ast(ASTNode *node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    
    const char *type_names[] = {
        "PROGRAM", "VAR_DECL", "FUNCTION_DECL", "IF_STMT",
        "FOR_STMT", "WHILE_STMT", "RETURN_STMT", "ASSIGN_STMT",
        "CALL_EXPR", "BINARY_EXPR", "IDENTIFIER", "LITERAL",
        "BLOCK", "UI_COMPONENT", "EMBED_CODE"
    };
    
    if (node->type < sizeof(type_names) / sizeof(char*)) {
        printf("%s", type_names[node->type]);
        if (node->value) {
            printf(": %s", node->value);
        }
        printf("\n");
    }
    
    if (node->left) print_ast(node->left, depth + 1);
    if (node->right) print_ast(node->right, depth + 1);
    if (node->next) print_ast(node->next, depth);
}
