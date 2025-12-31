/* ========================================
   SUB Language Enhanced Parser
   Properly handles: variables, functions, if/elif/else, loops, print, expressions
   File: parser_enhanced.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"

/* Parser state */
typedef struct {
    Token *tokens;
    int token_count;
    int current;
    int errors;
} ParserState;

/* Forward declarations */
static ASTNode* parse_statement(ParserState *state);
static ASTNode* parse_expression(ParserState *state);
static ASTNode* parse_block(ParserState *state);
static ASTNode* parse_primary(ParserState *state);
static ASTNode* parse_binary(ParserState *state, int precedence);

/* Create AST node */
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
    node->condition = NULL;
    node->body = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->metadata = NULL;
    return node;
}

/* Get current token */
static Token* current_token(ParserState *state) {
    if (!state || !state->tokens) return NULL;
    if (state->current >= state->token_count) {
        return &state->tokens[state->token_count - 1];
    }
    return &state->tokens[state->current];
}

/* Peek ahead n tokens */
static Token* peek_token(ParserState *state, int offset) __attribute__((unused));
static Token* peek_token(ParserState *state, int offset) {
    int pos = state->current + offset;
    if (pos >= state->token_count) {
        return &state->tokens[state->token_count - 1];
    }
    return &state->tokens[pos];
}

/* Advance to next token */
static void advance(ParserState *state) {
    if (state && state->current < state->token_count - 1) {
        state->current++;
    }
}

/* Check if current token matches type */
static bool match(ParserState *state, TokenType type) {
    Token *tok = current_token(state);
    return tok && tok->type == type;
}

/* Expect a specific token type */
static bool expect(ParserState *state, TokenType type) {
    if (match(state, type)) {
        advance(state);
        return true;
    }
    Token *tok = current_token(state);
    fprintf(stderr, "Parser Error: Expected token type %d, got %d at line %d\n", 
            type, tok ? tok->type : (TokenType)-1, tok ? tok->line : 0);
    state->errors++;
    return false;
}

/* Skip newlines and whitespace */
static void skip_newlines(ParserState *state) {
    while (match(state, TOKEN_NEWLINE)) {
        advance(state);
    }
}

/* Parse primary expression (literals, identifiers, parenthesized expressions) */
static ASTNode* parse_primary(ParserState *state) {
    Token *tok = current_token(state);
    if (!tok) return NULL;
    
    // Number literal
    if (match(state, TOKEN_NUMBER)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        node->data_type = TYPE_INT; // TODO: Detect float
        advance(state);
        return node;
    }
    
    // String literal
    if (match(state, TOKEN_STRING_LITERAL)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        node->data_type = TYPE_STRING;
        advance(state);
        return node;
    }
    
    // Boolean literals
    if (match(state, TOKEN_TRUE) || match(state, TOKEN_FALSE)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        node->data_type = TYPE_BOOL;
        advance(state);
        return node;
    }
    
    // Identifier or function call
    if (match(state, TOKEN_IDENTIFIER)) {
        char *name = strdup(tok->value);
        advance(state);
        
        // Check for function call
        if (match(state, TOKEN_LPAREN)) {
            advance(state);
            ASTNode *call = create_node(AST_CALL_EXPR, name);
            
            // Parse arguments (simplified for now)
            if (!match(state, TOKEN_RPAREN)) {
                call->left = parse_expression(state);
            }
            
            expect(state, TOKEN_RPAREN);
            free(name);
            return call;
        }
        
        // Just an identifier
        ASTNode *node = create_node(AST_IDENTIFIER, name);
        free(name);
        return node;
    }
    
    // Parenthesized expression
    if (match(state, TOKEN_LPAREN)) {
        advance(state);
        ASTNode *expr = parse_expression(state);
        expect(state, TOKEN_RPAREN);
        return expr;
    }
    
    return NULL;
}

/* Get precedence level for operator */
static int get_operator_precedence(const char *op) {
    if (!op) return -1;
    if (strcmp(op, "=") == 0) return 0;
    if (strcmp(op, "||") == 0) return 1;
    if (strcmp(op, "&&") == 0) return 2;
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) return 3;
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || 
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) return 4;
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0) return 5;
    if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0) return 6;
    return -1;
}

/* Parse binary expressions with precedence */
static ASTNode* parse_binary(ParserState *state, int precedence) {
    if (precedence >= 7) {
        return parse_primary(state);
    }
    
    ASTNode *left = parse_binary(state, precedence + 1);
    if (!left) return NULL;
    
    while (true) {
        Token *op_token = current_token(state);
        // Check if current token is operator
        if (!op_token || op_token->type != TOKEN_OPERATOR) {
            break;
        }

        // Check precedence match
        int op_prec = get_operator_precedence(op_token->value);
        if (op_prec != precedence) break;
        
        advance(state);
        
        ASTNode *right = parse_binary(state, precedence + 1); // Right-associative? No, left for standard, right for assign.
        // For assignment (=), we might want right associativity: a = b = c -> a = (b = c)
        // If so, we should call parse_binary(state, precedence) instead of precedence + 1?
        // But for mixed, precedence + 1 is fine if we are strict. 
        // Let's stick to standard recurse + 1 for now, effectively left-assoc, but grouped by level.
        // Wait, for `x = x - 1`:
        // Level 0 (=). Left (x) parsed. Op (=) matches prec 0. Eat. 
        // Right calls Level 1. Level 1..5 recurse. Level 5 parses (x - 1). Returns.
        // So `x = (x-1)`. Correct.
        
        ASTNode *binary = create_node(AST_BINARY_EXPR, op_token->value);
        binary->left = left;
        binary->right = right;
        left = binary;
    }
    
    return left;
}

/* Parse expression */
static ASTNode* parse_expression(ParserState *state) {
    return parse_binary(state, 0);
}

/* Parse block of statements */
static ASTNode* parse_block(ParserState *state) {
    ASTNode *block = create_node(AST_BLOCK, NULL);
    ASTNode *first_stmt = NULL;
    ASTNode *last_stmt = NULL;
    
    skip_newlines(state);
    
    while (!match(state, TOKEN_END) && !match(state, TOKEN_EOF) && 
           !match(state, TOKEN_ELIF) && !match(state, TOKEN_ELSE) && 
           !match(state, TOKEN_RBRACE)) {
        
        // Handle comments (lines starting with #)
        if (match(state, TOKEN_HASH)) {
            int line = state->tokens[state->current].line;
            while (!match(state, TOKEN_EOF) && state->tokens[state->current].line == line) {
                advance(state);
            }
            continue;
        }
        
        ASTNode *stmt = parse_statement(state);
        
        if (stmt) {
            if (!first_stmt) {
                first_stmt = stmt;
                last_stmt = stmt;
            } else {
                last_stmt->next = stmt;
                last_stmt = stmt;
            }
        } else {
             // If known end token, we will exit loop check next time
             // If unknown token, skip it
             if (!match(state, TOKEN_END) && !match(state, TOKEN_ELIF) && 
                 !match(state, TOKEN_ELSE) && !match(state, TOKEN_EOF)) {
                 advance(state);
             }
        }
        
        skip_newlines(state);
    }
    
    block->body = first_stmt;
    return block;
}

/* Parse print statement */
static ASTNode* parse_print(ParserState *state) __attribute__((unused));
static ASTNode* parse_print(ParserState *state) {
    advance(state); // skip 'print'
    
    ASTNode *print_node = create_node(AST_CALL_EXPR, "printf");
    
    if (match(state, TOKEN_LPAREN)) {
        advance(state);
        print_node->left = parse_expression(state);
        expect(state, TOKEN_RPAREN);
    }
    
    return print_node;
}

/* Parse if statement */
static ASTNode* parse_if(ParserState *state) {
    advance(state); // skip 'if'
    
    ASTNode *if_node = create_node(AST_IF_STMT, NULL);
    
    // Parse condition
    if_node->condition = parse_expression(state);
    skip_newlines(state);
    
    // Parse then block
    if_node->body = parse_block(state);
    
    // Consume closing brace if present
    if (match(state, TOKEN_RBRACE)) {
        advance(state);
    }
    
    skip_newlines(state);
    
    // Handle elif/else
    if (match(state, TOKEN_ELIF)) {
        // Recursively parse elif as nested if
        if_node->right = parse_if(state);
    } else if (match(state, TOKEN_ELSE)) {
        advance(state);
        skip_newlines(state);
        if_node->right = parse_block(state);
    }
    
    if (match(state, TOKEN_END)) {
        advance(state);
    }
    
    return if_node;
}

/* Parse for loop */
static ASTNode* parse_for(ParserState *state) {
    advance(state); // skip 'for'
    
    ASTNode *for_node = create_node(AST_FOR_STMT, NULL);
    
    // Parse loop variable
    if (match(state, TOKEN_IDENTIFIER)) {
        Token *var = current_token(state);
        for_node->value = strdup(var->value);
        advance(state);
    }
    
    // Parse 'in' (expect identifier 'in' since we don't have TOKEN_IN)
    Token *in_token = current_token(state);
    if (in_token && in_token->type == TOKEN_IDENTIFIER && strcmp(in_token->value, "in") == 0) {
        advance(state);
    }
    
    // Check for range(...) or collection
    Token *tok = current_token(state);
    if (tok && tok->type == TOKEN_IDENTIFIER && strcmp(tok->value, "range") == 0) {
        // Parse range expression
        advance(state);
        expect(state, TOKEN_LPAREN);
        
        ASTNode *range_node = create_node(AST_RANGE_EXPR, "range");
        range_node->left = parse_expression(state); // Start or Count
        
        if (match(state, TOKEN_COMMA)) {
            advance(state);
            range_node->right = parse_expression(state); // End
        }
        
        expect(state, TOKEN_RPAREN);
        
        // Store range node as first child
        for_node->children = malloc(sizeof(ASTNode*));
        for_node->children[0] = range_node;
        for_node->child_count = 1;
    } else {
        // Iterating over collection or variable
        ASTNode *collection = parse_expression(state);
        for_node->condition = collection; // Store valid collection in condition field if children not used
    }
    
    skip_newlines(state);
    for_node->body = parse_block(state);
    
    if (match(state, TOKEN_RBRACE)) {
        advance(state);
    }
    
    if (match(state, TOKEN_END)) {
        advance(state);
    }
    
    return for_node;
}

/* Parse while loop */
static ASTNode* parse_while(ParserState *state) {
    advance(state); // skip 'while'
    
    ASTNode *while_node = create_node(AST_WHILE_STMT, NULL);
    while_node->condition = parse_expression(state);
    skip_newlines(state);
    while_node->body = parse_block(state);
    
    if (match(state, TOKEN_RBRACE)) {
        advance(state);
    }
    
    if (match(state, TOKEN_END)) {
        advance(state);
    }
    
    return while_node;
}

/* Parse function declaration */
static ASTNode* parse_function(ParserState *state) {
    advance(state); // skip 'function'
    
    ASTNode *func = create_node(AST_FUNCTION_DECL, NULL);
    
    // Get function name
    if (match(state, TOKEN_IDENTIFIER)) {
        Token *name = current_token(state);
        func->value = strdup(name->value);
        advance(state);
    }
    
    // Parse parameters
    if (match(state, TOKEN_LPAREN)) {
        advance(state);
        
        // Allocate space for up to 32 parameters (simplified)
        func->children = malloc(sizeof(ASTNode*) * 32);
        func->child_count = 0;
        
        if (!match(state, TOKEN_RPAREN)) {
            while (true) {
                if (match(state, TOKEN_IDENTIFIER)) {
                    Token *param_token = current_token(state);
                    ASTNode *param = create_node(AST_PARAM_DECL, param_token->value);
                    advance(state);
                    
                    // Optional type annotation: param: type
                    if (match(state, TOKEN_COLON)) {
                        advance(state);
                        if (match(state, TOKEN_INT) || match(state, TOKEN_FLOAT) || 
                            match(state, TOKEN_STRING) || match(state, TOKEN_BOOL)) {
                            // TODO: Store type in param->data_type
                            advance(state);
                        } else if (match(state, TOKEN_IDENTIFIER)) {
                            advance(state);
                        }
                    }
                    
                    func->children[func->child_count++] = param;
                }
                
                if (match(state, TOKEN_COMMA)) {
                    advance(state);
                } else {
                    break;
                }
            }
        }
        
        expect(state, TOKEN_RPAREN);
    }
    
    skip_newlines(state);
    
    // Parse function body
    func->body = parse_block(state);
    
    if (match(state, TOKEN_RBRACE)) {
        advance(state);
    }
    
    if (match(state, TOKEN_END)) {
        advance(state);
    }
    
    return func;
}

/* Parse statement */
static ASTNode* parse_statement(ParserState *state) {
    if (!state) return NULL;
    skip_newlines(state);
    
    Token *tok = current_token(state);
    if (!tok) return NULL;
    
    // Variable declaration
    if (match(state, TOKEN_VAR)) {
        advance(state);
        ASTNode *var_decl = create_node(AST_VAR_DECL, NULL);
        
        if (match(state, TOKEN_IDENTIFIER)) {
            tok = current_token(state); // Refresh token
            var_decl->value = strdup(tok->value);
            advance(state);
            
            // Check for assignment
            if (match(state, TOKEN_OPERATOR)) {
                advance(state);
                var_decl->right = parse_expression(state);
            }
        }
        return var_decl;
    }
    
    // Constant declaration
    if (match(state, TOKEN_CONST)) {
        advance(state);
        ASTNode *const_decl = create_node(AST_CONST_DECL, NULL);
        
        if (match(state, TOKEN_IDENTIFIER)) {
            const_decl->value = strdup(tok->value);
            tok = current_token(state);
            advance(state);
            
            if (match(state, TOKEN_OPERATOR)) {
                advance(state);
                const_decl->right = parse_expression(state);
            }
        }
        return const_decl;
    }
    
    // Function declaration
    if (match(state, TOKEN_FUNCTION)) {
        return parse_function(state);
    }
    
    // If statement
    if (match(state, TOKEN_IF)) {
        return parse_if(state);
    }
    
    // For loop
    if (match(state, TOKEN_FOR)) {
        return parse_for(state);
    }
    
    // While loop
    if (match(state, TOKEN_WHILE)) {
        return parse_while(state);
    }
    
    // Return statement
    if (match(state, TOKEN_RETURN)) {
        advance(state);
        ASTNode *ret = create_node(AST_RETURN_STMT, NULL);
        ret->left = parse_expression(state);
        return ret;
    }
    
    // Print (handle as special function)
    // Expression statement (assignment, function call)
    if (match(state, TOKEN_IDENTIFIER) || match(state, TOKEN_NUMBER) || 
        match(state, TOKEN_STRING_LITERAL) || match(state, TOKEN_TRUE) || 
        match(state, TOKEN_FALSE) || match(state, TOKEN_LPAREN)) {
        return parse_expression(state);
    }
    
    // Skip unknown tokens
    // fprintf(stderr, "Warning: Unexpected token '%s'\n", tok ? tok->value : "EOF");
    advance(state);
    return NULL;
}

/* Main parser function */
ASTNode* parser_parse(Token *tokens, int token_count) {
    if (!tokens || token_count <= 0) {
        fprintf(stderr, "Error: Invalid tokens or token count\n");
        return NULL;
    }
    
    ParserState state = {tokens, token_count, 0, 0};
    ASTNode *root = create_node(AST_PROGRAM, "program");
    if (!root) return NULL;
    
    ASTNode *first_stmt = NULL;
    ASTNode *last_stmt = NULL;
    
    while (!match(&state, TOKEN_EOF)) {
        skip_newlines(&state);
        
        if (match(&state, TOKEN_EOF)) break;
        
        // Handle comments (lines starting with #)
        if (match(&state, TOKEN_HASH)) {
            int line = state.tokens[state.current].line;
            while (!match(&state, TOKEN_EOF) && state.tokens[state.current].line == line) {
                advance(&state);
            }
            continue;
        }
        
        ASTNode *stmt = parse_statement(&state);
        
        if (stmt) {
            if (!first_stmt) {
                first_stmt = stmt;
                last_stmt = stmt;
            } else {
                last_stmt->next = stmt;
                last_stmt = stmt;
            }
        } else {
            // If we couldn't parse a statement but aren't at EOF, skip token to avoid infinite loop
            // But only if parse_statement didn't already advance
             if (!match(&state, TOKEN_EOF)) {
                 // fprintf(stderr, "Warning: Skipping unexpected token '%s'\n", state.tokens[state.current].value);
                 advance(&state);
             }
        }
    }
    
    root->left = first_stmt;
    
    if (state.errors > 0) {
        fprintf(stderr, "Parser completed with %d errors\n", state.errors);
    }
    
    return root;
}

/* Free AST */
void parser_free_ast(ASTNode *node) {
    if (!node) return;
    
    if (node->value) free(node->value);
    if (node->left) parser_free_ast(node->left);
    if (node->right) parser_free_ast(node->right);
    if (node->next) parser_free_ast(node->next);
    if (node->condition) parser_free_ast(node->condition);
    if (node->body) parser_free_ast(node->body);
    
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            parser_free_ast(node->children[i]);
        }
        free(node->children);
    }
    
    free(node);
}
