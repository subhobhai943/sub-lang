/* ========================================
   SUB Language Parser Implementation
   File: parser.c (improved)
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ── Parser State ─────────────────────────────────────────────

typedef struct {
    Token *tokens;
    int    token_count;
    int    current;
    int    had_error;
    int    panic_mode;
} ParserState;

// ── Forward declarations ─────────────────────────────────────

static ASTNode* parse_statement(ParserState *state);
static ASTNode* parse_expression(ParserState *state);
static ASTNode* parse_expression_precedence(ParserState *state, int min_prec);
static ASTNode* parse_block(ParserState *state);

// ── Helpers ──────────────────────────────────────────────────

static ASTNode* create_node(ASTNodeType type, const char *value) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: Failed to allocate AST node\n");
        return NULL;
    }
    node->type        = type;
    node->value       = value ? strdup(value) : NULL;
    node->left        = NULL;
    node->right       = NULL;
    node->next        = NULL;
    node->children    = NULL;
    node->child_count = 0;
    node->metadata    = NULL;
    return node;
}

static void add_child(ASTNode *parent, ASTNode *child) {
    if (!parent || !child) return;
    parent->child_count++;
    parent->children = realloc(parent->children,
                               sizeof(ASTNode*) * parent->child_count);
    parent->children[parent->child_count - 1] = child;
}

static Token* current_token(ParserState *state) {
    if (!state || !state->tokens) return NULL;
    if (state->current >= state->token_count)
        return &state->tokens[state->token_count - 1]; // EOF sentinel
    return &state->tokens[state->current];
}

static Token* peek_ahead(ParserState *state, int offset) {
    int idx = state->current + offset;
    if (idx >= state->token_count) idx = state->token_count - 1;
    if (idx < 0) idx = 0;
    return &state->tokens[idx];
}

static void advance(ParserState *state) {
    if (state && state->current < state->token_count - 1)
        state->current++;
}

static int match(ParserState *state, TokenType type) {
    Token *tok = current_token(state);
    return tok && tok->type == type;
}

static int check_operator(ParserState *state, const char *op) {
    Token *tok = current_token(state);
    return tok && tok->type == TOKEN_OPERATOR && tok->value
           && strcmp(tok->value, op) == 0;
}

static int check_punctuation(ParserState *state, const char *punc) {
    Token *tok = current_token(state);
    return tok && tok->type == TOKEN_PUNCTUATION && tok->value
           && strcmp(tok->value, punc) == 0;
}

// ── Error reporting ──────────────────────────────────────────

static void parser_error(ParserState *state, const char *fmt, ...) {
    state->had_error = 1;
    Token *tok = current_token(state);

    fprintf(stderr, "[line %d] Parse error", tok ? tok->line : 0);
    if (tok && tok->value)
        fprintf(stderr, " near '%s'", tok->value);
    fprintf(stderr, ": ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* Consume a token of the expected type, or report an error. */
static Token* expect(ParserState *state, TokenType type, const char *msg) {
    if (match(state, type)) {
        Token *tok = current_token(state);
        advance(state);
        return tok;
    }
    parser_error(state, "%s", msg);
    return NULL;
}

static Token* expect_operator(ParserState *state, const char *op, const char *msg) {
    if (check_operator(state, op)) {
        Token *tok = current_token(state);
        advance(state);
        return tok;
    }
    parser_error(state, "%s", msg);
    return NULL;
}

static Token* expect_punctuation(ParserState *state, const char *punc, const char *msg) {
    if (check_punctuation(state, punc)) {
        Token *tok = current_token(state);
        advance(state);
        return tok;
    }
    parser_error(state, "%s", msg);
    return NULL;
}

static void skip_newlines(ParserState *state) {
    while (match(state, TOKEN_NEWLINE))
        advance(state);
}

static void synchronize(ParserState *state) {
    /* Skip tokens until we reach something that looks like
       the beginning of a new statement. */
    state->panic_mode = 0;
    while (!match(state, TOKEN_EOF)) {
        if (match(state, TOKEN_NEWLINE)) { advance(state); return; }
        switch (current_token(state)->type) {
            case TOKEN_VAR:
            case TOKEN_FUNCTION:
            case TOKEN_RETURN:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_PRINT:
                return;
            default:
                advance(state);
        }
    }
}

// ── Operator precedence table ────────────────────────────────

static int get_precedence(const char *op) {
    if (!op) return -1;
    if (strcmp(op, "=") == 0)                            return 1;
    if (strcmp(op, "||") == 0)                           return 2;
    if (strcmp(op, "&&") == 0)                           return 3;
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0)  return 4;
    if (strcmp(op, "<") == 0  || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0)  return 5;
    if (strcmp(op, "+") == 0  || strcmp(op, "-") == 0)   return 6;
    if (strcmp(op, "*") == 0  || strcmp(op, "/") == 0 ||
        strcmp(op, "%") == 0)                            return 7;
    return -1;
}

static int is_right_assoc(const char *op) {
    return op && strcmp(op, "=") == 0;
}

// ── Expression parsing (Pratt-style) ────────────────────────

/* Parse a "primary": literals, identifiers, function calls,
   grouped expressions, unary operators.                       */
static ASTNode* parse_primary(ParserState *state) {
    Token *tok = current_token(state);
    if (!tok) return NULL;

    // Number literal
    if (match(state, TOKEN_NUMBER)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        advance(state);
        return node;
    }

    // String literal
    if (match(state, TOKEN_STRING)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        advance(state);
        return node;
    }

    // Boolean literals (if your lexer emits them as identifiers)
    if (match(state, TOKEN_IDENTIFIER) &&
        (strcmp(tok->value, "true") == 0 || strcmp(tok->value, "false") == 0)) {
        ASTNode *node = create_node(AST_LITERAL, tok->value);
        advance(state);
        return node;
    }

    // Identifier — possibly a function call
    if (match(state, TOKEN_IDENTIFIER)) {
        ASTNode *node = create_node(AST_IDENTIFIER, tok->value);
        advance(state);

        // Function call: identifier followed by '('
        if (check_punctuation(state, "(")) {
            ASTNode *call = create_node(AST_FUNCTION_CALL, node->value);
            parser_free_ast(node);       // replace with call node
            advance(state);              // consume '('

            // Parse argument list
            if (!check_punctuation(state, ")")) {
                ASTNode *arg = parse_expression(state);
                if (arg) add_child(call, arg);

                while (check_punctuation(state, ",")) {
                    advance(state);      // consume ','
                    arg = parse_expression(state);
                    if (arg) add_child(call, arg);
                }
            }
            expect_punctuation(state, ")", "Expected ')' after arguments");
            return call;
        }

        // Array index: identifier followed by '['
        if (check_punctuation(state, "[")) {
            advance(state);
            ASTNode *index_expr = parse_expression(state);
            expect_punctuation(state, "]", "Expected ']' after index");
            ASTNode *idx_node = create_node(AST_INDEX_ACCESS, node->value);
            idx_node->left = node;
            idx_node->right = index_expr;
            return idx_node;
        }

        return node;
    }

    // Grouped expression: '(' expr ')'
    if (check_punctuation(state, "(")) {
        advance(state);
        ASTNode *expr = parse_expression(state);
        expect_punctuation(state, ")", "Expected ')' after expression");
        return expr;
    }

    // Unary minus / not
    if (check_operator(state, "-") || check_operator(state, "!")) {
        const char *op = tok->value;
        advance(state);
        ASTNode *operand = parse_primary(state);
        ASTNode *unary = create_node(AST_UNARY_OP, op);
        unary->left = operand;
        return unary;
    }

    parser_error(state, "Unexpected token '%s'", tok->value ? tok->value : "?");
    advance(state);
    return NULL;
}

/* Precedence-climbing expression parser. */
static ASTNode* parse_expression_precedence(ParserState *state, int min_prec) {
    ASTNode *left = parse_primary(state);
    if (!left) return NULL;

    while (match(state, TOKEN_OPERATOR)) {
        Token *op_tok = current_token(state);
        int prec = get_precedence(op_tok->value);
        if (prec < min_prec) break;

        char *op = strdup(op_tok->value);
        advance(state);

        int next_prec = is_right_assoc(op) ? prec : prec + 1;

        ASTNode *right = parse_expression_precedence(state, next_prec);
        ASTNode *bin   = create_node(AST_BINARY_OP, op);
        bin->left  = left;
        bin->right = right;
        left = bin;
        free(op);
    }

    return left;
}

static ASTNode* parse_expression(ParserState *state) {
    return parse_expression_precedence(state, 1);
}

// ── Block parsing (sequence of statements between { }) ──────

static ASTNode* parse_block(ParserState *state) {
    expect_punctuation(state, "{", "Expected '{' to begin block");
    skip_newlines(state);

    ASTNode *block = create_node(AST_BLOCK, NULL);

    while (!check_punctuation(state, "}") && !match(state, TOKEN_EOF)) {
        ASTNode *stmt = parse_statement(state);
        if (stmt) {
            add_child(block, stmt);
        } else {
            synchronize(state);
        }
        skip_newlines(state);
    }

    expect_punctuation(state, "}", "Expected '}' to end block");
    return block;
}

// ── Statement parsing ────────────────────────────────────────

static ASTNode* parse_var_declaration(ParserState *state) {
    advance(state); // consume 'var'

    Token *name_tok = expect(state, TOKEN_IDENTIFIER,
                             "Expected variable name after 'var'");
    if (!name_tok) return NULL;

    ASTNode *node = create_node(AST_VAR_DECL, name_tok->value);

    // Optional type annotation:  var x: int = ...
    if (check_punctuation(state, ":")) {
        advance(state);
        Token *type_tok = expect(state, TOKEN_IDENTIFIER,
                                 "Expected type after ':'");
        if (type_tok)
            node->metadata = strdup(type_tok->value);
    }

    // Optional initializer
    if (check_operator(state, "=")) {
        advance(state);
        node->right = parse_expression(state);
    }

    return node;
}

static ASTNode* parse_function_declaration(ParserState *state) {
    advance(state); // consume 'fun' / 'function'

    Token *name_tok = expect(state, TOKEN_IDENTIFIER,
                             "Expected function name");
    if (!name_tok) return NULL;

    ASTNode *func = create_node(AST_FUNCTION_DECL, name_tok->value);

    // Parameter list: (a, b, c)
    expect_punctuation(state, "(", "Expected '(' after function name");

    ASTNode *params = create_node(AST_PARAM_LIST, NULL);
    if (!check_punctuation(state, ")")) {
        Token *p = expect(state, TOKEN_IDENTIFIER, "Expected parameter name");
        if (p) add_child(params, create_node(