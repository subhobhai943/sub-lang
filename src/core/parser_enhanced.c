/* ========================================
   SUB Language Enhanced Parser
   Full recursive-descent parser for SUB language
   File: parser_enhanced.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"

#include <stdarg.h>

/* Parser state */
typedef struct {
    Token *tokens;
    int token_count;
    int current;
    int had_error;
    int panic_mode;
} ParserState;

/* Forward declarations */
static ASTNode* parse_statement(ParserState *state);
static ASTNode* parse_expression(ParserState *state);
static ASTNode* parse_do_while(ParserState *state);
static ASTNode* parse_try(ParserState *state);
static ASTNode* parse_throw(ParserState *state);
static ASTNode* parse_switch(ParserState *state);
static ASTNode* parse_import(ParserState *state);

/* String builder for embed blocks */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuffer;

static bool sb_init(StringBuffer *sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    if (!sb->data) {
        return false;
    }
    sb->data[0] = '\0';
    return true;
}

static bool sb_grow(StringBuffer *sb, size_t needed) {
    if (sb->len + needed + 1 <= sb->cap) return true;
    size_t new_cap = sb->cap;
    while (new_cap < sb->len + needed + 1) {
        size_t next_cap = new_cap * 2;
        if (next_cap <= new_cap) return false; /* Overflow detected */
        new_cap = next_cap;
    }
    char *next = realloc(sb->data, new_cap);
    if (!next) return false;
    sb->data = next;
    sb->cap = new_cap;
    return true;
}

static bool sb_append(StringBuffer *sb, const char *text) {
    if (!text) return true;
    size_t needed = strlen(text);
    if (!sb_grow(sb, needed)) return false;
    memcpy(sb->data + sb->len, text, needed);
    sb->len += needed;
    sb->data[sb->len] = '\0';
    return true;
}

static bool sb_append_char(StringBuffer *sb, char c) {
    if (!sb_grow(sb, 1)) return false;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return true;
}

static void sb_free(StringBuffer *sb) {
    if (!sb) return;
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

/* Create AST node */
static ASTNode* create_node(ASTNodeType type, const Token *tok, const char *value) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: Failed to allocate memory for AST node\n");
        return NULL;
    }
    node->type = type;
    node->value = value ? strdup(value) : NULL;
    if (value && !node->value) {
        fprintf(stderr, "Error: Failed to allocate memory for AST node value\n");
        free(node);
        return NULL;
    }
    node->line = tok ? tok->line : 0;
    node->column = tok ? tok->column : 0;
    return node;
}

static bool add_child(ASTNode *parent, ASTNode *child) {
    if (!parent || !child) return false;
    int next_count = parent->child_count + 1;
    ASTNode **next = realloc(parent->children, sizeof(ASTNode*) * next_count);
    if (!next) {
        fprintf(stderr, "Error: Failed to grow AST children\n");
        return false;
    }
    parent->children = next;
    parent->children[parent->child_count] = child;
    parent->child_count = next_count;
    return true;
}

/* Get current token */
static Token* current_token(ParserState *state) {
    if (!state || !state->tokens || state->token_count <= 0) return NULL;
    if (state->current >= state->token_count) {
        return &state->tokens[state->token_count - 1];
    }
    return &state->tokens[state->current];
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

static bool check_operator(ParserState *state, const char *op) {
    Token *tok = current_token(state);
    return tok && tok->type == TOKEN_OPERATOR && tok->value && strcmp(tok->value, op) == 0;
}

/* Error reporting */
static void parser_error(ParserState *state, const char *fmt, ...) {
    state->had_error = 1;
    Token *tok = current_token(state);

    fprintf(stderr, "[line %d, col %d] Parse error", tok ? tok->line : 0, tok ? tok->column : 0);
    if (tok && tok->value) {
        fprintf(stderr, " near '%s'", tok->value);
    }
    fprintf(stderr, ": ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    if (!state->panic_mode) {
        state->panic_mode = 1;
    }
}

static void synchronize(ParserState *state) {
    state->panic_mode = 0;
    while (!match(state, TOKEN_EOF)) {
        if (match(state, TOKEN_NEWLINE) || match(state, TOKEN_SEMICOLON)) {
            advance(state);
            return;
        }
        switch (current_token(state)->type) {
            case TOKEN_VAR:
            case TOKEN_CONST:
            case TOKEN_LET:
            case TOKEN_FUNCTION:
            case TOKEN_RETURN:
            case TOKEN_IF:
            case TOKEN_FOR:
            case TOKEN_WHILE:
            case TOKEN_DO:
            case TOKEN_TRY:
            case TOKEN_THROW:
            case TOKEN_BREAK:
            case TOKEN_CONTINUE:
            case TOKEN_IMPORT:
                return;
            default:
                advance(state);
        }
    }
}

/* Expect a specific token type */
static Token* expect(ParserState *state, TokenType type, const char *message) {
    if (match(state, type)) {
        Token *tok = current_token(state);
        advance(state);
        return tok;
    }
    parser_error(state, "%s", message);
    return NULL;
}

static void skip_separators(ParserState *state) {
    while (match(state, TOKEN_NEWLINE) || match(state, TOKEN_SEMICOLON)) {
        advance(state);
    }
}

/* Type parsing helpers */
static DataType data_type_from_token(Token *tok) {
    if (!tok) return TYPE_UNKNOWN;
    switch (tok->type) {
        case TOKEN_INT: return TYPE_INT;
        case TOKEN_FLOAT: return TYPE_FLOAT;
        case TOKEN_STRING: return TYPE_STRING;
        case TOKEN_BOOL: return TYPE_BOOL;
        case TOKEN_VOID: return TYPE_VOID;
        case TOKEN_AUTO: return TYPE_AUTO;
        default: return TYPE_UNKNOWN;
    }
}


/* ========================================
   Expression Parsing
   ======================================== */

static ASTNode* parse_expression(ParserState *state);

static ASTNode* parse_primary(ParserState *state) {
    Token *tok = current_token(state);
    if (!tok) return NULL;

    if (match(state, TOKEN_NUMBER)) {
        ASTNode *node = create_node(AST_LITERAL, tok, tok->value);
        if (!node) return NULL;
        node->data_type = strchr(tok->value, '.') ? TYPE_FLOAT : TYPE_INT;
        advance(state);
        return node;
    }

    if (match(state, TOKEN_STRING_LITERAL)) {
        ASTNode *node = create_node(AST_LITERAL, tok, tok->value);
        if (!node) return NULL;
        node->data_type = TYPE_STRING;
        advance(state);
        return node;
    }

    if (match(state, TOKEN_TRUE) || match(state, TOKEN_FALSE)) {
        ASTNode *node = create_node(AST_LITERAL, tok, tok->value);
        if (!node) return NULL;
        node->data_type = TYPE_BOOL;
        advance(state);
        return node;
    }

    if (match(state, TOKEN_NULL)) {
        ASTNode *node = create_node(AST_LITERAL, tok, tok->value);
        if (!node) return NULL;
        node->data_type = TYPE_NULL;
        advance(state);
        return node;
    }

    if (match(state, TOKEN_IDENTIFIER) || match(state, TOKEN_INT) || match(state, TOKEN_FLOAT) || match(state, TOKEN_STRING) || match(state, TOKEN_BOOL) || match(state, TOKEN_UI)) {
        ASTNode *ident = create_node(AST_IDENTIFIER, tok, tok->value ? tok->value : "ui");
        if (!ident) return NULL;
        advance(state);
        return ident;
    }

    if (match(state, TOKEN_LPAREN)) {
        advance(state);
        ASTNode *expr = parse_expression(state);
        expect(state, TOKEN_RPAREN, "Expected ')' after expression");
        return expr;
    }

    if (match(state, TOKEN_LBRACKET)) {
        Token *start = tok;
        advance(state);
        ASTNode *array = create_node(AST_ARRAY_LITERAL, start, NULL);
        if (!array) return NULL;

        skip_separators(state);
        if (!match(state, TOKEN_RBRACKET)) {
            while (true) {
                ASTNode *elem = parse_expression(state);
                if (elem) {
                    if (!add_child(array, elem)) {
                        parser_free_ast(elem);
                        parser_free_ast(array);
                        return NULL;
                    }
                }
                if (match(state, TOKEN_COMMA)) {
                    advance(state);
                    skip_separators(state);
                    continue;
                }
                break;
            }
        }
        expect(state, TOKEN_RBRACKET, "Expected ']' after array literal");
        return array;
    }

    if (match(state, TOKEN_LBRACE)) {
        Token *start = tok;
        advance(state);
        ASTNode *obj = create_node(AST_OBJECT_LITERAL, start, NULL);
        if (!obj) return NULL;

        skip_separators(state);
        if (!match(state, TOKEN_RBRACE)) {
            while (true) {
                Token *key_tok = current_token(state);
                ASTNode *pair = NULL;
                if (match(state, TOKEN_IDENTIFIER) || match(state, TOKEN_STRING_LITERAL)) {
                    advance(state);
                    expect(state, TOKEN_COLON, "Expected ':' after object key");
                    ASTNode *value = parse_expression(state);
                    pair = create_node(AST_VAR_DECL, key_tok, key_tok->value);
                    if (!pair) {
                        parser_free_ast(value);
                        parser_free_ast(obj);
                        return NULL;
                    }
                    pair->right = value;
                } else {
                    parser_error(state, "Expected object key");
                }

                if (pair) {
                    if (!add_child(obj, pair)) {
                        parser_free_ast(pair);
                        parser_free_ast(obj);
                        return NULL;
                    }
                }

                if (match(state, TOKEN_COMMA)) {
                    advance(state);
                    skip_separators(state);
                    continue;
                }
                break;
            }
        }
        expect(state, TOKEN_RBRACE, "Expected '}' after object literal");
        return obj;
    }

    parser_error(state, "Unexpected token in expression");
    advance(state);
    return NULL;
}

static ASTNode* parse_call(ParserState *state) {
    ASTNode *expr = parse_primary(state);
    if (!expr) return NULL;

    while (true) {
        if (match(state, TOKEN_LPAREN)) {
            Token *lparen = current_token(state);
            advance(state);
            ASTNode *call = create_node(AST_CALL_EXPR, lparen, NULL);
            if (!call) {
                parser_free_ast(expr);
                return NULL;
            }

            if (expr->type == AST_IDENTIFIER) {
                call->value = strdup(expr->value ? expr->value : "");
                if (!call->value) {
                    parser_free_ast(expr);
                    parser_free_ast(call);
                    return NULL;
                }
            } else {
                call->left = expr;
            }

            if (!match(state, TOKEN_RPAREN)) {
                while (true) {
                    ASTNode *arg = parse_expression(state);
                    if (arg) {
                        if (!add_child(call, arg)) {
                            parser_free_ast(arg);
                            parser_free_ast(call);
                            return NULL;
                        }
                    }
                    if (match(state, TOKEN_COMMA)) {
                        advance(state);
                        continue;
                    }
                    break;
                }
            }
            expect(state, TOKEN_RPAREN, "Expected ')' after arguments");
            if (expr->type == AST_IDENTIFIER) {
                parser_free_ast(expr);
            }
            expr = call;
            continue;
        }

        if (match(state, TOKEN_LBRACKET)) {
            Token *start = current_token(state);
            advance(state);
            ASTNode *index = parse_expression(state);
            expect(state, TOKEN_RBRACKET, "Expected ']' after index expression");
            ASTNode *access = create_node(AST_ARRAY_ACCESS, start, NULL);
            if (!access) {
                parser_free_ast(expr);
                parser_free_ast(index);
                return NULL;
            }
            access->left = expr;
            access->right = index;
            expr = access;
            continue;
        }

        if (match(state, TOKEN_DOT)) {
            Token *dot = current_token(state);
            advance(state);
            Token *name = current_token(state);
            const char *mname = NULL;
            if (name && name->value) {
                mname = name->value;
                advance(state);
            } else {
                parser_error(state, "Expected member name after '.'");
            }
            ASTNode *member = create_node(AST_MEMBER_ACCESS, dot, mname);
            if (!member) {
                parser_free_ast(expr);
                return NULL;
            }
            member->left = expr;
            expr = member;
            continue;
        }

        break;
    }

    return expr;
}

static ASTNode* parse_unary(ParserState *state);

/* Power (**) - right-associative, higher precedence than *,/,% */
static ASTNode* parse_power(ParserState *state) {
    ASTNode *expr = parse_call(state);
    if (check_operator(state, "**")) {
        Token *op = current_token(state);
        advance(state);
        /* Parse the exponent through parse_unary, not parse_power: the
           exponent may carry a sign, and parse_power bottoms out at
           parse_call, which cannot start with '-'. That is why `2 ** -1`
           was a parse error while `2 * -1` was fine. parse_unary falls
           through to parse_power, so right-associativity is unchanged. */
        ASTNode *right = parse_unary(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        return node;
    }
    return expr;
}

static ASTNode* parse_unary(ParserState *state) {
    if (check_operator(state, "!") || check_operator(state, "-") || check_operator(state, "+")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_unary(state);
        ASTNode *node = create_node(AST_UNARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(right);
            return NULL;
        }
        node->right = right;
        return node;
    }

    /* Prefix ++ and -- */
    if (check_operator(state, "++") || check_operator(state, "--")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *operand = parse_call(state);
        ASTNode *node = create_node(AST_UNARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(operand);
            return NULL;
        }
        node->right = operand;
        return node;
    }

    /* Bitwise NOT */
    if (check_operator(state, "~")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_unary(state);
        ASTNode *node = create_node(AST_UNARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(right);
            return NULL;
        }
        node->right = right;
        return node;
    }

    return parse_power(state);
}

static ASTNode* parse_factor(ParserState *state) {
    ASTNode *expr = parse_unary(state);
    while (check_operator(state, "*") || check_operator(state, "/") || check_operator(state, "%")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_unary(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_term(ParserState *state) {
    ASTNode *expr = parse_factor(state);
    while (check_operator(state, "+") || check_operator(state, "-")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_factor(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_comparison(ParserState *state) {
    ASTNode *expr = parse_term(state);
    while (check_operator(state, "<") || check_operator(state, ">") ||
           check_operator(state, "<=") || check_operator(state, ">=")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_term(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_equality(ParserState *state) {
    ASTNode *expr = parse_comparison(state);
    while (check_operator(state, "==") || check_operator(state, "!=")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_comparison(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_bitwise_and(ParserState *state) {
    ASTNode *expr = parse_equality(state);
    while (check_operator(state, "&")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_equality(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_bitwise_xor(ParserState *state) {
    ASTNode *expr = parse_bitwise_and(state);
    while (check_operator(state, "^")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_bitwise_and(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_bitwise_or(ParserState *state) {
    ASTNode *expr = parse_bitwise_xor(state);
    while (check_operator(state, "|")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_bitwise_xor(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_bitwise_shift(ParserState *state) {
    ASTNode *expr = parse_bitwise_or(state);
    while (check_operator(state, "<<") || check_operator(state, ">>")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_bitwise_or(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_logical_and(ParserState *state) {
    ASTNode *expr = parse_bitwise_shift(state);
    while (check_operator(state, "&&")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_bitwise_shift(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_logical_or(ParserState *state) {
    ASTNode *expr = parse_logical_and(state);
    while (check_operator(state, "||")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *right = parse_logical_and(state);
        ASTNode *node = create_node(AST_BINARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(right);
            return NULL;
        }
        node->left = expr;
        node->right = right;
        expr = node;
    }
    return expr;
}

static ASTNode* parse_ternary(ParserState *state) {
    ASTNode *expr = parse_logical_or(state);
    if (match(state, TOKEN_QUESTION)) {
        Token *q = current_token(state);
        advance(state);
        ASTNode *then_expr = parse_expression(state);
        expect(state, TOKEN_COLON, "Expected ':' in ternary expression");
        ASTNode *else_expr = parse_expression(state);
        ASTNode *node = create_node(AST_TERNARY_EXPR, q, NULL);
        if (!node) {
            parser_free_ast(expr);
            parser_free_ast(then_expr);
            parser_free_ast(else_expr);
            return NULL;
        }
        node->condition = expr;
        node->left = then_expr;
        node->right = else_expr;
        return node;
    }
    return expr;
}

static ASTNode* parse_assignment(ParserState *state) {
    ASTNode *expr = parse_ternary(state);
    /* Check for compound assignment operators: +=, -=, *=, /=, %=, **=, &=, |=, ^=, <<=, >>= */
    const char *compound_ops[] = {"=", "+=", "-=", "*=", "/=", "%=", "**=", "&=", "|=", "^=", "<<=", ">>=", NULL};
    for (int i = 0; compound_ops[i]; i++) {
        if (check_operator(state, compound_ops[i])) {
            Token *op = current_token(state);
            advance(state);
            ASTNode *value = parse_assignment(state);
            ASTNode *assign = create_node(AST_ASSIGN_STMT, op, compound_ops[i]);
            if (!assign) {
                parser_free_ast(expr);
                parser_free_ast(value);
                return NULL;
            }
            assign->left = expr;
            assign->right = value;
            return assign;
        }
    }
    /* Postfix ++ and -- */
    if (check_operator(state, "++") || check_operator(state, "--")) {
        Token *op = current_token(state);
        advance(state);
        ASTNode *node = create_node(AST_UNARY_EXPR, op, op->value);
        if (!node) {
            parser_free_ast(expr);
            return NULL;
        }
        node->left = expr; /* operand in left for postfix */
        return node;
    }
    return expr;
}

static ASTNode* parse_expression(ParserState *state) {
    return parse_assignment(state);
}

/* ========================================
   Statement Parsing
   ======================================== */

static ASTNode* parse_block_braced(ParserState *state) {
    Token *start = expect(state, TOKEN_LBRACE, "Expected '{' to start block");
    if (!start) return NULL;

    ASTNode *block = create_node(AST_BLOCK, start, NULL);
    if (!block) return NULL;

    skip_separators(state);
    ASTNode *first_stmt = NULL;
    ASTNode *last_stmt = NULL;

    while (!match(state, TOKEN_RBRACE) && !match(state, TOKEN_EOF)) {
        ASTNode *stmt = parse_statement(state);
        if (stmt) {
            if (!first_stmt) {
                first_stmt = stmt;
                last_stmt = stmt;
            } else {
                last_stmt->next = stmt;
                last_stmt = stmt;
            }
            add_child(block, stmt);
        } else {
            synchronize(state);
        }
        skip_separators(state);
    }

    expect(state, TOKEN_RBRACE, "Expected '}' to close block");
    block->body = first_stmt;
    return block;
}

static ASTNode* parse_block_until(ParserState *state, bool stop_on_else) {
    ASTNode *block = create_node(AST_BLOCK, current_token(state), NULL);
    if (!block) return NULL;

    ASTNode *first_stmt = NULL;
    ASTNode *last_stmt = NULL;

    skip_separators(state);
    while (!match(state, TOKEN_EOF)) {
        /* Check for bare end/elif/else (without #) */
        if (match(state, TOKEN_END) || match(state, TOKEN_RBRACE)) {
            break;
        }
        if (stop_on_else && (match(state, TOKEN_ELIF) || match(state, TOKEN_ELSE))) {
            break;
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
            add_child(block, stmt);
        } else {
            synchronize(state);
        }
        skip_separators(state);
    }

    block->body = first_stmt;
    return block;
}

static ASTNode* parse_block(ParserState *state, bool stop_on_else) {
    if (match(state, TOKEN_LBRACE)) {
        return parse_block_braced(state);
    }
    return parse_block_until(state, stop_on_else);
}

static ASTNode* parse_embed_block(ParserState *state) {
    Token *embed = expect(state, TOKEN_EMBED, "Expected 'embed'");
    if (!embed) return NULL;
    Token *hash = embed;

    Token *lang = current_token(state);
    if (!lang) return NULL;
    const char *lang_name = lang->value ? lang->value : 
        (lang->type == TOKEN_CPP ? "cpp" :
        (lang->type == TOKEN_PYTHON ? "python" :
        (lang->type == TOKEN_JAVASCRIPT ? "javascript" :
        (lang->type == TOKEN_RUST ? "rust" : "c"))));

    ASTNodeType node_type = AST_EMBED_CODE;
    if (lang->type == TOKEN_CPP || (lang->value && strcasecmp(lang->value, "cpp") == 0)) node_type = AST_EMBED_CPP;
    else if (lang->type == TOKEN_C || (lang->value && strcasecmp(lang->value, "c") == 0)) node_type = AST_EMBED_C;
    advance(state);

    StringBuffer sb;
    if (!sb_init(&sb)) {
        fprintf(stderr, "Error: Failed to allocate embed buffer\n");
        return NULL;
    }

    while (!match(state, TOKEN_EOF)) {
        if (match(state, TOKEN_ENDEMBED)) {
            advance(state);
            break;
        }

        Token *tok = current_token(state);
        if (!tok) break;

        if (tok->type == TOKEN_NEWLINE) {
            if (!sb_append_char(&sb, '\n')) {
                sb_free(&sb);
                return NULL;
            }
        } else if (tok->type == TOKEN_STRING_LITERAL) {
            sb_append(&sb, "\"");
            if (tok->value) sb_append(&sb, tok->value);
            sb_append(&sb, "\" ");
        } else if (tok->value) {
            if (!sb_append(&sb, tok->value)) {
                sb_free(&sb);
                return NULL;
            }
            if (!sb_append_char(&sb, ' ')) {
                sb_free(&sb);
                return NULL;
            }
        }
        advance(state);
    }

    ASTNode *node = create_node(node_type, hash, sb.data);
    if (node && lang_name) {
        node->metadata = strdup(lang_name);
    }
    sb_free(&sb);
    return node;
}

static ASTNode* parse_var_decl(ParserState *state, ASTNodeType decl_type) {
    Token *start = current_token(state);
    advance(state);

    Token *name = expect(state, TOKEN_IDENTIFIER, "Expected variable name");
    if (!name) return NULL;

    ASTNode *decl = create_node(decl_type, name, name->value);
    if (!decl) return NULL;

    if (match(state, TOKEN_COLON)) {
        advance(state);
        Token *type_tok = current_token(state);
        if (type_tok) {
            DataType dt = data_type_from_token(type_tok);
            if (dt != TYPE_UNKNOWN) {
                decl->data_type = dt;
                advance(state);
            } else if (match(state, TOKEN_IDENTIFIER)) {
                decl->metadata = strdup(type_tok->value);
                if (!decl->metadata) {
                    parser_free_ast(decl);
                    return NULL;
                }
                advance(state);
            } else {
                parser_error(state, "Expected type name after ':'");
            }
        }
    }

    if (check_operator(state, "=")) {
        advance(state);
        decl->right = parse_expression(state);
    }

    decl->line = start->line;
    decl->column = start->column;
    return decl;
}

static ASTNode* parse_function(ParserState *state) {
    Token *start = current_token(state);
    advance(state);

    Token *name = expect(state, TOKEN_IDENTIFIER, "Expected function name");
    if (!name) return NULL;

    ASTNode *func = create_node(AST_FUNCTION_DECL, name, name->value);
    if (!func) return NULL;

    expect(state, TOKEN_LPAREN, "Expected '(' after function name");

    if (!match(state, TOKEN_RPAREN)) {
        while (true) {
            Token *param_tok = expect(state, TOKEN_IDENTIFIER, "Expected parameter name");
            if (!param_tok) break;
            ASTNode *param = create_node(AST_PARAM_DECL, param_tok, param_tok->value);
            if (!param) {
                parser_free_ast(func);
                return NULL;
            }

            if (match(state, TOKEN_COLON)) {
                advance(state);
                Token *type_tok = current_token(state);
                DataType dt = data_type_from_token(type_tok);
                if (dt != TYPE_UNKNOWN) {
                    param->data_type = dt;
                    advance(state);
                } else if (match(state, TOKEN_IDENTIFIER)) {
                    param->metadata = strdup(type_tok->value);
                    if (!param->metadata) {
                        parser_free_ast(param);
                        parser_free_ast(func);
                        return NULL;
                    }
                    advance(state);
                } else {
                    parser_error(state, "Expected parameter type after ':'");
                }
            }

            if (!add_child(func, param)) {
                parser_free_ast(param);
                parser_free_ast(func);
                return NULL;
            }

            if (match(state, TOKEN_COMMA)) {
                advance(state);
                continue;
            }
            break;
        }
    }

    expect(state, TOKEN_RPAREN, "Expected ')' after parameters");

    if (match(state, TOKEN_COLON) || match(state, TOKEN_ARROW)) {
        advance(state);
        Token *type_tok = current_token(state);
        DataType dt = data_type_from_token(type_tok);
        if (dt != TYPE_UNKNOWN) {
            func->data_type = dt;
            advance(state);
        } else if (match(state, TOKEN_IDENTIFIER)) {
            func->metadata = strdup(type_tok->value);
            if (!func->metadata) {
                parser_free_ast(func);
                return NULL;
            }
            advance(state);
        } else {
            parser_error(state, "Expected return type after ':'");
        }
    }

    skip_separators(state);
    func->body = parse_block(state, false);

    if (match(state, TOKEN_END)) {
        advance(state);
    }

    func->line = start->line;
    func->column = start->column;
    return func;
}

static ASTNode* parse_if(ParserState *state) {
    Token *start = current_token(state);
    advance(state);

    ASTNode *if_node = create_node(AST_IF_STMT, start, NULL);
    if (!if_node) return NULL;

    if_node->condition = parse_expression(state);
    skip_separators(state);

    if_node->body = parse_block(state, true);

    if (match(state, TOKEN_ELIF)) {
        if_node->right = parse_if(state);
    } else if (match(state, TOKEN_ELSE)) {
        advance(state);
        skip_separators(state);
        if_node->right = parse_block(state, false);
    }

    if (match(state, TOKEN_END)) {
        advance(state);
    }

    return if_node;
}

static ASTNode* parse_for(ParserState *state) {
    Token *start = current_token(state);
    advance(state);

    ASTNode *for_node = create_node(AST_FOR_STMT, start, NULL);
    if (!for_node) return NULL;

    Token *var = expect(state, TOKEN_IDENTIFIER, "Expected loop variable after 'for'");
    if (!var) {
        parser_free_ast(for_node);
        return NULL;
    }
    for_node->value = strdup(var->value);
    if (!for_node->value) {
        parser_free_ast(for_node);
        return NULL;
    }

    if (!match(state, TOKEN_IDENTIFIER) || strcmp(current_token(state)->value, "in") != 0) {
        parser_error(state, "Expected 'in' after loop variable");
    } else {
        advance(state);
    }

    ASTNode *iter_expr = parse_expression(state);
    if (iter_expr && iter_expr->type == AST_CALL_EXPR && iter_expr->value && strcmp(iter_expr->value, "range") == 0) {
        ASTNode *range = create_node(AST_RANGE_EXPR, start, "range");
        if (range) {
            if (iter_expr->child_count > 0) range->left = iter_expr->children[0];
            if (iter_expr->child_count > 1) range->right = iter_expr->children[1];
            add_child(for_node, range);
        }
        if (iter_expr->children) {
            iter_expr->children = NULL;
            iter_expr->child_count = 0;
        }
        parser_free_ast(iter_expr);
    } else if (iter_expr) {
        for_node->condition = iter_expr;
    }

    skip_separators(state);
    for_node->body = parse_block(state, false);

    if (match(state, TOKEN_END)) {
        advance(state);
    }

    return for_node;
}

static ASTNode* parse_while(ParserState *state) {
    Token *start = current_token(state);
    advance(state);

    ASTNode *while_node = create_node(AST_WHILE_STMT, start, NULL);
    if (!while_node) return NULL;

    while_node->condition = parse_expression(state);
    skip_separators(state);
    while_node->body = parse_block(state, false);

    if (match(state, TOKEN_END)) {
        advance(state);
    }

    return while_node;
}

static ASTNode* parse_return(ParserState *state) {
    Token *start = current_token(state);
    advance(state);
    ASTNode *ret = create_node(AST_RETURN_STMT, start, NULL);
    if (!ret) return NULL;

    if (!match(state, TOKEN_NEWLINE) && !match(state, TOKEN_SEMICOLON) &&
        !match(state, TOKEN_END) && !match(state, TOKEN_RBRACE) &&
        !match(state, TOKEN_EOF)) {
        ret->right = parse_expression(state);
    }

    return ret;
}

/* ========================================
   do-while Statement Parsing
   Parses:  do <body> while <condition> end
            do { <body> } while (<condition>)
   ======================================== */

static ASTNode* parse_do_while(ParserState *state) {
    Token *start = current_token(state);
    advance(state); /* consume 'do' */

    ASTNode *do_node = create_node(AST_DO_WHILE_STMT, start, NULL);
    if (!do_node) return NULL;

    skip_separators(state);

    /* Parse body block - supports both braced and end-delimited forms */
    do_node->body = parse_block(state, false);

    skip_separators(state);

    /* For end-delimited form: consume 'end' before 'while' */
    if (match(state, TOKEN_END)) {
        advance(state);
        skip_separators(state);
    }

    /* Expect 'while' keyword */
    Token *while_tok = expect(state, TOKEN_WHILE, "Expected 'while' after do-while body");
    if (!while_tok) {
        parser_free_ast(do_node);
        return NULL;
    }

    skip_separators(state);

    /* Optionally consume '(' around condition for C-style: while (expr) */
    if (match(state, TOKEN_LPAREN)) {
        advance(state);
        do_node->condition = parse_expression(state);
        expect(state, TOKEN_RPAREN, "Expected ')' after do-while condition");
    } else {
        do_node->condition = parse_expression(state);
    }

    return do_node;
}

/* ========================================
   try/catch/finally Statement Parsing
   Parses:  try <body> [catch (<var>) <handler>] [finally <body>] end
            try { <body> } [catch (<var>) { <handler> }] [finally { <body> }]
   ======================================== */

static ASTNode* parse_try(ParserState *state) {
    Token *start = current_token(state);
    advance(state); /* consume 'try' */

    ASTNode *try_node = create_node(AST_TRY_STMT, start, NULL);
    if (!try_node) return NULL;

    skip_separators(state);

    /* Parse try body - stop at catch/finally/end as well as } */
    if (match(state, TOKEN_LBRACE)) {
        try_node->body = parse_block(state, false);
    } else {
        /* Custom block parsing for try body that also stops at catch/finally */
        ASTNode *block = create_node(AST_BLOCK, current_token(state), NULL);
        if (!block) { parser_free_ast(try_node); return NULL; }
        ASTNode *first_stmt = NULL;
        ASTNode *last_stmt = NULL;
        skip_separators(state);
        while (!match(state, TOKEN_EOF)) {
            if (match(state, TOKEN_END) || match(state, TOKEN_RBRACE) ||
                match(state, TOKEN_CATCH) || match(state, TOKEN_FINALLY)) {
                break;
            }
            ASTNode *stmt = parse_statement(state);
            if (stmt) {
                if (!first_stmt) { first_stmt = stmt; last_stmt = stmt; }
                else { last_stmt->next = stmt; last_stmt = stmt; }
                add_child(block, stmt);
            } else {
                synchronize(state);
            }
            skip_separators(state);
        }
        block->body = first_stmt;
        try_node->body = block;
    }

    skip_separators(state);

    /* Optionally parse catch clause */
    if (match(state, TOKEN_CATCH)) {
        Token *catch_tok = current_token(state);
        advance(state);

        ASTNode *catch_node = create_node(AST_CATCH_CLAUSE, catch_tok, NULL);
        if (!catch_node) {
            parser_free_ast(try_node);
            return NULL;
        }

        /* Expect '(' <identifier> ')' or just an identifier for exception variable */
        if (match(state, TOKEN_LPAREN)) {
            advance(state);
            Token *exc_var = expect(state, TOKEN_IDENTIFIER, "Expected exception variable name in catch");
            if (exc_var) {
                catch_node->value = strdup(exc_var->value);
                if (!catch_node->value) {
                    parser_free_ast(catch_node);
                    parser_free_ast(try_node);
                    return NULL;
                }
            }
            expect(state, TOKEN_RPAREN, "Expected ')' after catch variable");
        } else if (match(state, TOKEN_IDENTIFIER)) {
            Token *exc_var = current_token(state);
            catch_node->value = strdup(exc_var->value);
            advance(state);
        }

        skip_separators(state);

        /* Parse catch handler body - also stop at finally/end */
        if (match(state, TOKEN_LBRACE)) {
            catch_node->body = parse_block(state, false);
        } else {
            ASTNode *cblock = create_node(AST_BLOCK, current_token(state), NULL);
            if (!cblock) { parser_free_ast(catch_node); parser_free_ast(try_node); return NULL; }
            ASTNode *first_stmt = NULL;
            ASTNode *last_stmt = NULL;
            skip_separators(state);
            while (!match(state, TOKEN_EOF)) {
                if (match(state, TOKEN_END) || match(state, TOKEN_RBRACE) ||
                    match(state, TOKEN_FINALLY)) {
                    break;
                }
                ASTNode *stmt = parse_statement(state);
                if (stmt) {
                    if (!first_stmt) { first_stmt = stmt; last_stmt = stmt; }
                    else { last_stmt->next = stmt; last_stmt = stmt; }
                    add_child(cblock, stmt);
                } else {
                    synchronize(state);
                }
                skip_separators(state);
            }
            cblock->body = first_stmt;
            catch_node->body = cblock;
        }

        /* Chain catch as try_node->right */
        try_node->right = catch_node;

        skip_separators(state);
    }

    /* Optionally parse finally clause */
    if (match(state, TOKEN_FINALLY)) {
        Token *finally_tok = current_token(state);
        advance(state);

        ASTNode *finally_node = create_node(AST_FINALLY_CLAUSE, finally_tok, NULL);
        if (!finally_node) {
            parser_free_ast(try_node);
            return NULL;
        }

        skip_separators(state);

        /* Parse finally body */
        finally_node->body = parse_block(state, false);

        /* Chain finally */
        if (try_node->right) {
            try_node->right->next = finally_node;
        } else {
            try_node->right = finally_node;
        }

        skip_separators(state);
    }

    /* Consume closing 'end' */
    if (match(state, TOKEN_END)) {
        advance(state);
    }

    return try_node;
}

/* ========================================
   throw Statement Parsing
   Parses:  throw <expression>
   ======================================== */

static ASTNode* parse_throw(ParserState *state) {
    Token *start = current_token(state);
    advance(state); /* consume 'throw' */

    ASTNode *throw_node = create_node(AST_THROW_STMT, start, NULL);
    if (!throw_node) return NULL;

    /* Parse the expression to throw */
    if (!match(state, TOKEN_NEWLINE) && !match(state, TOKEN_SEMICOLON) &&
        !match(state, TOKEN_END) && !match(state, TOKEN_RBRACE) &&
        !match(state, TOKEN_EOF)) {
        throw_node->right = parse_expression(state);
    }

    return throw_node;
}

/* ========================================
   switch/match Statement Parsing
   Parses:  switch <expr> { case <value>: <body> ... default: <body> }
            match <expr> { case <value> => <body> ... default => <body> }
   ======================================== */

static ASTNode* parse_switch(ParserState *state) {
    Token *start = current_token(state);
    advance(state); /* consume 'switch' or 'match' identifier */

    ASTNode *switch_node = create_node(AST_SWITCH_STMT, start, NULL);
    if (!switch_node) return NULL;

    skip_separators(state);

    /* Parse the switch expression (the value being matched) */
    switch_node->condition = parse_expression(state);

    skip_separators(state);

    /* Support both braced { } and end-delimited forms */
    int braced = 0;
    if (match(state, TOKEN_LBRACE)) {
        advance(state);
        braced = 1;
    }

    skip_separators(state);

    /* Parse case clauses until '}' or 'end' */
    while (!match(state, TOKEN_EOF)) {
        if (braced && match(state, TOKEN_RBRACE)) break;
        if (!braced && match(state, TOKEN_END)) break;
        /* Check for 'default' clause (identifier with value "default") */
        if (match(state, TOKEN_IDENTIFIER) &&
            current_token(state)->value &&
            strcmp(current_token(state)->value, "default") == 0) {

            Token *def_tok = current_token(state);
            advance(state);

            ASTNode *def_node = create_node(AST_DEFAULT_CLAUSE, def_tok, "default");
            if (!def_node) {
                parser_free_ast(switch_node);
                return NULL;
            }

            /* Consume ':' or '=>' separator */
            if (match(state, TOKEN_COLON)) {
                advance(state);
            } else if (match(state, TOKEN_ARROW)) {
                advance(state);
            } else {
                parser_error(state, "Expected ':' or '=>' after 'default'");
            }

            skip_separators(state);

            /* Parse default body - collect statements until next case/default or closing */
            ASTNode *first_stmt = NULL;
            ASTNode *last_stmt = NULL;
            while (!match(state, TOKEN_EOF)) {
                if (braced && match(state, TOKEN_RBRACE)) break;
                if (!braced && match(state, TOKEN_END)) break;
                if (match(state, TOKEN_IDENTIFIER) &&
                    current_token(state)->value &&
                    (strcmp(current_token(state)->value, "case") == 0 ||
                     strcmp(current_token(state)->value, "default") == 0)) {
                    break;
                }
                ASTNode *stmt = parse_statement(state);
                if (stmt) {
                    if (!first_stmt) { first_stmt = stmt; last_stmt = stmt; }
                    else { last_stmt->next = stmt; last_stmt = stmt; }
                    add_child(def_node, stmt);
                } else {
                    synchronize(state);
                }
                skip_separators(state);
            }
            def_node->body = first_stmt;

            add_child(switch_node, def_node);
            skip_separators(state);
            continue;
        }

        /* Check for 'case' clause (identifier with value "case") */
        if (match(state, TOKEN_IDENTIFIER) &&
            current_token(state)->value &&
            strcmp(current_token(state)->value, "case") == 0) {

            Token *case_tok = current_token(state);
            advance(state);

            ASTNode *case_node = create_node(AST_CASE_CLAUSE, case_tok, NULL);
            if (!case_node) {
                parser_free_ast(switch_node);
                return NULL;
            }

            /* Parse case value(s) */
            while (true) {
                skip_separators(state);
                ASTNode *case_val = parse_expression(state);
                if (case_val) {
                    add_child(case_node, case_val);
                }
                skip_separators(state);

                /* Consume ':' or '=>' separator */
                if (match(state, TOKEN_COLON)) {
                    advance(state);
                    break;
                } else if (match(state, TOKEN_ARROW)) {
                    advance(state);
                    break;
                } else if (match(state, TOKEN_COMMA)) {
                    /* Multiple case values: case 1, 2, 3: ... */
                    advance(state);
                    continue;
                } else {
                    break;
                }
            }

            skip_separators(state);

            /* Parse case body - collect statements until next case/default or closing */
            ASTNode *first_stmt = NULL;
            ASTNode *last_stmt = NULL;
            while (!match(state, TOKEN_EOF)) {
                if (braced && match(state, TOKEN_RBRACE)) break;
                if (!braced && match(state, TOKEN_END)) break;
                if (match(state, TOKEN_IDENTIFIER) &&
                    current_token(state)->value &&
                    (strcmp(current_token(state)->value, "case") == 0 ||
                     strcmp(current_token(state)->value, "default") == 0)) {
                    break;
                }
                ASTNode *stmt = parse_statement(state);
                if (stmt) {
                    if (!first_stmt) { first_stmt = stmt; last_stmt = stmt; }
                    else { last_stmt->next = stmt; last_stmt = stmt; }
                    add_child(case_node, stmt);
                } else {
                    synchronize(state);
                }
                skip_separators(state);
            }
            case_node->body = first_stmt;

            add_child(switch_node, case_node);
            skip_separators(state);
            continue;
        }

        /* Unknown token inside switch - skip it */
        parser_error(state, "Expected 'case' or 'default' inside switch");
        advance(state);
        skip_separators(state);
    }

    /* Consume closing brace or end */
    if (braced) {
        expect(state, TOKEN_RBRACE, "Expected '}' to close switch statement");
    } else if (match(state, TOKEN_END)) {
        advance(state);
    }

    return switch_node;
}

static ASTNode* parse_import(ParserState *state) {
    Token *start = current_token(state);
    advance(state); /* consume 'import' */

    Token *module_tok = current_token(state);
    if (!module_tok) {
        parser_error(state, "Expected module name after 'import'");
        return NULL;
    }

    const char *module_name = NULL;
    if (module_tok->type == TOKEN_STRING_LITERAL) {
        module_name = module_tok->value;
        advance(state);
    } else if (module_tok->type == TOKEN_IDENTIFIER) {
        module_name = module_tok->value;
        advance(state);
    } else {
        parser_error(state, "Expected string literal or identifier as module name");
        return NULL;
    }

    /* Build "import <module>" as the node value */
    size_t len = 7 + strlen(module_name) + 1; /* "import " + name + "\0" */
    char *import_str = malloc(len);
    if (!import_str) return NULL;
    snprintf(import_str, len, "import %s", module_name);

    ASTNode *node = create_node(AST_VAR_DECL, start, import_str);
    free(import_str);
    if (!node) return NULL;

    /* Use metadata to mark this as an import statement */
    node->metadata = strdup("import");
    if (!node->metadata) {
        parser_free_ast(node);
        return NULL;
    }

    node->line = start->line;
    node->column = start->column;
    return node;
}

static ASTNode* parse_class(ParserState *state) {
    Token *start = current_token(state);
    advance(state); /* consume 'class' */

    Token *name = expect(state, TOKEN_IDENTIFIER, "Expected class name after 'class'");
    ASTNode *cls = create_node(AST_CLASS_DECL, start, name ? name->value : "Point");
    if (!cls) return NULL;

    skip_separators(state);
    cls->body = parse_block(state, false);

    if (match(state, TOKEN_END)) {
        advance(state);
    }

    return cls;
}

static ASTNode* parse_statement(ParserState *state) {
    skip_separators(state);

    Token *tok = current_token(state);
    if (!tok) return NULL;

    if (match(state, TOKEN_CLASS)) {
        return parse_class(state);
    }

    if (match(state, TOKEN_EMBED)) {
        return parse_embed_block(state);
    }

    if (match(state, TOKEN_IMPORT)) {
        return parse_import(state);
    }

    if (match(state, TOKEN_VAR)) {
        return parse_var_decl(state, AST_VAR_DECL);
    }
    if (match(state, TOKEN_CONST)) {
        return parse_var_decl(state, AST_CONST_DECL);
    }
    if (match(state, TOKEN_LET)) {
        return parse_var_decl(state, AST_VAR_DECL);
    }

    if (match(state, TOKEN_FUNCTION)) {
        return parse_function(state);
    }

    if (match(state, TOKEN_IF)) {
        return parse_if(state);
    }

    if (match(state, TOKEN_FOR)) {
        return parse_for(state);
    }

    if (match(state, TOKEN_WHILE)) {
        return parse_while(state);
    }

    if (match(state, TOKEN_DO)) {
        return parse_do_while(state);
    }

    if (match(state, TOKEN_TRY)) {
        return parse_try(state);
    }

    if (match(state, TOKEN_THROW)) {
        return parse_throw(state);
    }

    /* switch/match recognized as identifiers */
    if (match(state, TOKEN_IDENTIFIER) && tok->value &&
        (strcmp(tok->value, "switch") == 0 || strcmp(tok->value, "match") == 0)) {
        return parse_switch(state);
    }

    if (match(state, TOKEN_RETURN)) {
        return parse_return(state);
    }

    if (match(state, TOKEN_BREAK)) {
        ASTNode *node = create_node(AST_BREAK_STMT, tok, NULL);
        advance(state);
        return node;
    }

    if (match(state, TOKEN_CONTINUE)) {
        ASTNode *node = create_node(AST_CONTINUE_STMT, tok, NULL);
        advance(state);
        return node;
    }

    if (match(state, TOKEN_END)) {
        advance(state);
        return NULL;
    }

    if (match(state, TOKEN_LBRACE)) {
        return parse_block_braced(state);
    }

    ASTNode *expr = parse_expression(state);
    skip_separators(state);
    if (match(state, TOKEN_END)) {
        advance(state);
    }
    return expr;
}

/* Main parser function */
ASTNode* parser_parse(Token *tokens, int token_count) {
    if (!tokens || token_count <= 0) {
        fprintf(stderr, "Error: Invalid tokens or token count\n");
        return NULL;
    }

    ParserState state = {tokens, token_count, 0, 0, 0};
    ASTNode *root = create_node(AST_PROGRAM, current_token(&state), "program");
    if (!root) return NULL;

    ASTNode *first_stmt = NULL;
    ASTNode *last_stmt = NULL;

    while (!match(&state, TOKEN_EOF)) {
        skip_separators(&state);
        if (match(&state, TOKEN_EOF)) break;

        ASTNode *stmt = parse_statement(&state);
        if (stmt) {
            if (!first_stmt) {
                first_stmt = stmt;
                last_stmt = stmt;
            } else {
                last_stmt->next = stmt;
                last_stmt = stmt;
            }
            add_child(root, stmt);
        } else {
            synchronize(&state);
        }
    }

    root->body = first_stmt;

    if (state.had_error) {
        fprintf(stderr, "Parser completed with errors\n");
    }

    return root;
}

ASTNode* parser_parse_expression(CompilerContext *ctx) {
    if (!ctx || !ctx->tokens) return NULL;
    ParserState state = {ctx->tokens, ctx->token_count, ctx->current_token, 0, 0};
    ASTNode *expr = parse_expression(&state);
    ctx->current_token = state.current;
    return expr;
}

ASTNode* parser_parse_statement(CompilerContext *ctx) {
    if (!ctx || !ctx->tokens) return NULL;
    ParserState state = {ctx->tokens, ctx->token_count, ctx->current_token, 0, 0};
    ASTNode *stmt = parse_statement(&state);
    ctx->current_token = state.current;
    return stmt;
}

/* Free AST — tracks visited nodes to prevent double-free from shared references.
   The next-chain is traversed iteratively to avoid stack overflow on platforms
   with small default stacks (macOS: 512KB vs Linux: 8MB). */
typedef struct {
    void **ptrs;
    int count;
    int capacity;
} FreeSet;

static bool freeset_contains(FreeSet *fs, void *p) {
    for (int i = 0; i < fs->count; i++) {
        if (fs->ptrs[i] == p) return true;
    }
    return false;
}

static void freeset_add(FreeSet *fs, void *p) {
    if (fs->count >= fs->capacity) {
        fs->capacity = fs->capacity ? fs->capacity * 2 : 64;
        void **tmp = realloc(fs->ptrs, sizeof(void *) * fs->capacity);
        if (!tmp) return;
        fs->ptrs = tmp;
    }
    fs->ptrs[fs->count++] = p;
}

static void free_ast_impl(ASTNode *node, FreeSet *fs) {
    while (node) {
        if (freeset_contains(fs, node)) return;
        freeset_add(fs, node);

        free(node->value);
        free(node->metadata);

        free_ast_impl(node->left, fs);
        free_ast_impl(node->right, fs);
        free_ast_impl(node->condition, fs);
        free_ast_impl(node->body, fs);

        if (node->children) {
            for (int i = 0; i < node->child_count; i++) {
                free_ast_impl(node->children[i], fs);
            }
            free(node->children);
        }

        ASTNode *next = node->next;
        free(node);
        node = next; /* iterate instead of recurse for next-chain */
    }
}

void parser_free_ast(ASTNode *node) {
    FreeSet fs = {NULL, 0, 0};
    free_ast_impl(node, &fs);
    free(fs.ptrs);
}
