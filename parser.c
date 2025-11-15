/* ========================================
   SUB Language Parser Implementation
   File: parser.c
   ======================================== */

#include "sub_compiler.h"

// Parser state
typedef struct {
    Token *tokens;
    int token_count;
    int current;
} ParserState;

// Forward declarations
static ASTNode* parse_statement(ParserState *state);
static ASTNode* parse_expression(ParserState *state);

// Create AST node
static ASTNode* create_node(ASTNodeType type, const char *value) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: Failed to allocate memory for AST node\n");
        return NULL;
    }
    node->type = type;
    node->value = value ? strdup(value) : NULL;
    node->left = NULL;
    node->right = NULL;
    node->next = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->metadata = NULL;
    return node;
}

// Get current token
static Token* current_token(ParserState *state) {
    if (!state || !state->tokens) return NULL;
    if (state->current >= state->token_count) {
        return &state->tokens[state->token_count - 1];
    }
    return &state->tokens[state->current];
}

// Advance to next token
static void advance(ParserState *state) {
    if (state && state->current < state->token_count - 1) {
        state->current++;
    }
}

// Check if current token matches type
static int match(ParserState *state, TokenType type) {
    Token *tok = current_token(state);
    return tok && tok->type == type;
}

// Skip newlines
static void skip_newlines(ParserState *state) {
    while (match(state, TOKEN_NEWLINE)) {
        advance(state);
    }
}

// Parse expression (simplified)
static ASTNode* parse_expression(ParserState *state) {
    Token *tok = current_token(state);
    if (!tok) return NULL;
    
    if (match(state, TOKEN_NUMBER)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        advance(state);
        return node;
    }
    
    if (match(state, TOKEN_STRING)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        advance(state);
        return node;
    }
    
    if (match(state, TOKEN_IDENTIFIER)) {
        ASTNode *node = create_node(AST_IDENTIFIER, tok->value);
        advance(state);
        return node;
    }
    
    return NULL;
}

// Parse statement
static ASTNode* parse_statement(ParserState *state) {
    if (!state) return NULL;
    skip_newlines(state);
    
    Token *tok = current_token(state);
    if (!tok) return NULL;
    
    if (match(state, TOKEN_VAR)) {
        advance(state);
        ASTNode *node = create_node(AST_VAR_DECL, NULL);
        if (!node) return NULL;
        
        if (match(state, TOKEN_IDENTIFIER)) {
            tok = current_token(state);
            if (tok && tok->value) {
                node->value = strdup(tok->value);
            }
            advance(state);
        }
        if (match(state, TOKEN_OPERATOR)) {
            advance(state);
            node->right = parse_expression(state);
        }
        return node;
    }
    
    if (match(state, TOKEN_FUNCTION)) {
        advance(state);
        ASTNode *node = create_node(AST_FUNCTION_DECL, NULL);
        if (!node) return NULL;
        
        if (match(state, TOKEN_IDENTIFIER)) {
            tok = current_token(state);
            if (tok && tok->value) {
                node->value = strdup(tok->value);
            }
            advance(state);
        }
        // Skip parameters for now
        while (!match(state, TOKEN_NEWLINE) && !match(state, TOKEN_EOF)) {
            advance(state);
        }
        return node;
    }
    
    if (match(state, TOKEN_RETURN)) {
        advance(state);
        ASTNode *node = create_node(AST_RETURN_STMT, NULL);
        if (!node) return NULL;
        node->left = parse_expression(state);
        return node;
    }
    
    // Skip unknown tokens
    advance(state);
    return NULL;
}

// Main parser function
ASTNode* parser_parse(Token *tokens, int token_count) {
    if (!tokens || token_count <= 0) {
        fprintf(stderr, "Error: Invalid tokens or token count\n");
        return NULL;
    }
    
    ParserState state = {tokens, token_count, 0};
    ASTNode *root = create_node(AST_PROGRAM, "program");
    if (!root) return NULL;
    
    ASTNode *last_stmt = NULL;
    
    while (!match(&state, TOKEN_EOF)) {
        if (match(&state, TOKEN_HASH)) {
            advance(&state);
            ASTNode *stmt = parse_statement(&state);
            if (stmt) {
                if (!root->left) {
                    root->left = stmt;
                    last_stmt = stmt;
                } else if (last_stmt) {
                    last_stmt->next = stmt;
                    last_stmt = stmt;
                }
            }
        } else {
            advance(&state);
        }
    }
    
    return root;
}

// Free AST
void parser_free_ast(ASTNode *node) {
    if (!node) return;
    
    if (node->value) free(node->value);
    if (node->left) parser_free_ast(node->left);
    if (node->right) parser_free_ast(node->right);
    if (node->next) parser_free_ast(node->next);
    
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            parser_free_ast(node->children[i]);
        }
        free(node->children);
    }
    
    free(node);
}
