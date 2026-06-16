/* ========================================
   SUB Language Parser Implementation
   Full recursive-descent parser for SUB language
   File: parser.c
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
    int parse_depth;  /* recursion depth guard */
} ParserState;

#define MAX_PARSE_DEPTH 1000
#define PARSE_DEPTH_CHECK(s, ret) \
    do { if (++(s)->parse_depth > MAX_PARSE_DEPTH) { \
        fprintf(stderr, "Error: nesting too deep (max %d levels)\n", MAX_PARSE_DEPTH); \
        (s)->had_error = 1; --(s)->parse_depth; return (ret); \
    } } while(0)
#define PARSE_DEPTH_EXIT(s) (--(s)->parse_depth)

/* Forward declarations */
static ASTNode* parse_statement(ParserState *state);
static ASTNode* parse_expression(ParserState *state);

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
        new_cap *= 2;
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

/* ---- Token utilities ---- */

static Token* current_token(ParserState *state) {
    if (state->current >= state->token_count) return NULL;
    return &state->tokens[state->current];
}

static Token* peek(ParserState *state, int offset) {
    int idx = state->current + offset;
    if (idx < 0 || idx >= state->token_count) return NULL;
    return &state->tokens[idx];
}

static bool check(ParserState *state, TokenType type) {
    Token *t = current_token(state);
    return t && t->type == type;
}

static bool check_keyword(ParserState *state, const char *kw) {
    Token *t = current_token(state);
    return t && t->type == TOKEN_KEYWORD && t->value && strcmp(t->value, kw) == 0;
}

static Token* advance(ParserState *state) {
    if (state->current < state->token_count) state->current++;
    return &state->tokens[state->current - 1];
}

static bool match(ParserState *state, TokenType type) {
    if (!check(state, type)) return false;
    advance(state);
    return true;
}

static bool match_keyword(ParserState *state, const char *kw) {
    if (!check_keyword(state, kw)) return false;
    advance(state);
    return true;
}

static void parser_error(ParserState *state, const char *fmt, ...) {
    if (state->panic_mode) return;
    state->panic_mode = 1;
    state->had_error  = 1;
    Token *t = current_token(state);
    int line = t ? t->line : -1;
    fprintf(stderr, "[line %d] Parse error: ", line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static bool expect(ParserState *state, TokenType type, const char *msg) {
    if (check(state, type)) { advance(state); return true; }
    parser_error(state, "%s", msg);
    return false;
}

static bool expect_keyword(ParserState *state, const char *kw, const char *msg) {
    if (check_keyword(state, kw)) { advance(state); return true; }
    parser_error(state, "%s", msg);
    return false;
}

static bool is_at_end(ParserState *state) {
    Token *t = current_token(state);
    return !t || t->type == TOKEN_EOF;
}

static void synchronize(ParserState *state) {
    state->panic_mode = 0;
    while (!is_at_end(state)) {
        Token *t = current_token(state);
        if (!t) break;
        if (t->type == TOKEN_NEWLINE) { advance(state); return; }
        if (t->type == TOKEN_KEYWORD) {
            const char *kw = t->value;
            if (kw && (strcmp(kw, "var") == 0 || strcmp(kw, "const") == 0 ||
                       strcmp(kw, "function") == 0 || strcmp(kw, "if") == 0 ||
                       strcmp(kw, "while") == 0 || strcmp(kw, "for") == 0 ||
                       strcmp(kw, "return") == 0 || strcmp(kw, "class") == 0))
                return;
        }
        advance(state);
    }
}

/* ---- Node allocation ---- */

static ASTNode* make_node(ASTNodeType type) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    if (!n) return NULL;
    n->type = type;
    return n;
}

static void node_add_child(ASTNode *parent, ASTNode *child) {
    if (!parent || !child) return;
    parent->children = realloc(parent->children, (parent->child_count + 1) * sizeof(ASTNode*));
    if (!parent->children) return;
    parent->children[parent->child_count++] = child;
}

/* ---- Parsing ---- */

static ASTNode* parse_primary(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    Token *t = current_token(state);
    if (!t) { PARSE_DEPTH_EXIT(state); return NULL; }

    /* Integer literal */
    if (t->type == TOKEN_INTEGER) {
        advance(state);
        ASTNode *n = make_node(AST_LITERAL);
        n->value = t->value ? strdup(t->value) : NULL;
        n->data_type = TYPE_INT;
        PARSE_DEPTH_EXIT(state); return n;
    }
    /* Float literal */
    if (t->type == TOKEN_FLOAT) {
        advance(state);
        ASTNode *n = make_node(AST_LITERAL);
        n->value = t->value ? strdup(t->value) : NULL;
        n->data_type = TYPE_FLOAT;
        PARSE_DEPTH_EXIT(state); return n;
    }
    /* String literal */
    if (t->type == TOKEN_STRING) {
        advance(state);
        ASTNode *n = make_node(AST_LITERAL);
        n->value = t->value ? strdup(t->value) : NULL;
        n->data_type = TYPE_STRING;
        PARSE_DEPTH_EXIT(state); return n;
    }
    /* Boolean literals */
    if (t->type == TOKEN_KEYWORD && t->value &&
        (strcmp(t->value, "true") == 0 || strcmp(t->value, "false") == 0)) {
        advance(state);
        ASTNode *n = make_node(AST_LITERAL);
        n->value = strdup(t->value);
        n->data_type = TYPE_BOOL;
        PARSE_DEPTH_EXIT(state); return n;
    }
    /* null literal */
    if (t->type == TOKEN_KEYWORD && t->value && strcmp(t->value, "null") == 0) {
        advance(state);
        ASTNode *n = make_node(AST_LITERAL);
        n->value = strdup("null");
        n->data_type = TYPE_NULL;
        PARSE_DEPTH_EXIT(state); return n;
    }
    /* Grouped expression */
    if (t->type == TOKEN_LPAREN) {
        advance(state);
        ASTNode *expr = parse_expression(state);
        expect(state, TOKEN_RPAREN, "Expected ')'");
        PARSE_DEPTH_EXIT(state); return expr;
    }
    /* Array literal */
    if (t->type == TOKEN_LBRACKET) {
        advance(state);
        ASTNode *arr = make_node(AST_ARRAY_LITERAL);
        while (!check(state, TOKEN_RBRACKET) && !is_at_end(state)) {
            ASTNode *elem = parse_expression(state);
            if (elem) node_add_child(arr, elem);
            if (!match(state, TOKEN_COMMA)) break;
        }
        expect(state, TOKEN_RBRACKET, "Expected ']'");
        PARSE_DEPTH_EXIT(state); return arr;
    }
    /* Dict/object literal */
    if (t->type == TOKEN_LBRACE) {
        advance(state);
        ASTNode *obj = make_node(AST_OBJECT_LITERAL);
        while (!check(state, TOKEN_RBRACE) && !is_at_end(state)) {
            Token *key = current_token(state);
            if (!key) break;
            advance(state);
            ASTNode *kn = make_node(AST_LITERAL);
            kn->value = key->value ? strdup(key->value) : NULL;
            kn->data_type = TYPE_STRING;
            expect(state, TOKEN_COLON, "Expected ':'");
            ASTNode *value = parse_expression(state);
            ASTNode *pair = make_node(AST_KEY_VALUE);
            pair->left  = kn;
            pair->right = value;
            node_add_child(obj, pair);
            if (!match(state, TOKEN_COMMA)) break;
        }
        expect(state, TOKEN_RBRACE, "Expected '}'");
        PARSE_DEPTH_EXIT(state); return obj;
    }
    /* Identifier */
    if (t->type == TOKEN_IDENTIFIER) {
        advance(state);
        ASTNode *n = make_node(AST_IDENTIFIER);
        n->value = t->value ? strdup(t->value) : NULL;
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* Unknown token — skip and return NULL */
    PARSE_DEPTH_EXIT(state);
    return NULL;
}

static ASTNode* parse_call(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *expr = parse_primary(state);

    while (true) {
        if (check(state, TOKEN_LPAREN)) {
            advance(state);
            ASTNode *call = make_node(AST_CALL_EXPR);
            if (expr && expr->type == AST_IDENTIFIER)
                call->value = expr->value ? strdup(expr->value) : NULL;
            call->left = expr;
            while (!check(state, TOKEN_RPAREN) && !is_at_end(state)) {
                ASTNode *arg = parse_expression(state);
                if (arg) node_add_child(call, arg);
                if (!match(state, TOKEN_COMMA)) break;
            }
            expect(state, TOKEN_RPAREN, "Expected ')'");
            expr = call;
        } else if (check(state, TOKEN_DOT)) {
            advance(state);
            Token *prop = current_token(state);
            if (prop && (prop->type == TOKEN_IDENTIFIER || prop->type == TOKEN_KEYWORD)) {
                advance(state);
                ASTNode *access = make_node(AST_MEMBER_EXPR);
                access->left  = expr;
                access->value = prop->value ? strdup(prop->value) : NULL;
                expr = access;
            }
        } else if (check(state, TOKEN_LBRACKET)) {
            advance(state);
            ASTNode *index = parse_expression(state);
            expect(state, TOKEN_RBRACKET, "Expected ']'");
            ASTNode *idx_expr = make_node(AST_INDEX_EXPR);
            idx_expr->left  = expr;
            idx_expr->right = index;
            expr = idx_expr;
        } else {
            break;
        }
    }
    PARSE_DEPTH_EXIT(state); return expr;
}

static ASTNode* parse_unary(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    if (check(state, TOKEN_MINUS) || check(state, TOKEN_BANG)) {
        Token *op = advance(state);
        ASTNode *right = parse_unary(state);
        ASTNode *n = make_node(AST_UNARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->right = right;
        PARSE_DEPTH_EXIT(state); return n;
    }
    ASTNode *r = parse_call(state);
    PARSE_DEPTH_EXIT(state); return r;
}

static ASTNode* parse_factor(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_unary(state);
    while (check(state, TOKEN_STAR) || check(state, TOKEN_SLASH) || check(state, TOKEN_PERCENT)) {
        Token *op = advance(state);
        ASTNode *right = parse_unary(state);
        ASTNode *n = make_node(AST_BINARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->left  = left;
        n->right = right;
        left = n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_term(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_factor(state);
    while (check(state, TOKEN_PLUS) || check(state, TOKEN_MINUS)) {
        Token *op = advance(state);
        ASTNode *right = parse_factor(state);
        ASTNode *n = make_node(AST_BINARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->left  = left;
        n->right = right;
        left = n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_comparison(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_term(state);
    while (check(state, TOKEN_LT) || check(state, TOKEN_GT) ||
           check(state, TOKEN_LTE) || check(state, TOKEN_GTE)) {
        Token *op = advance(state);
        ASTNode *right = parse_term(state);
        ASTNode *n = make_node(AST_BINARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->left  = left;
        n->right = right;
        left = n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_equality(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_comparison(state);
    while (check(state, TOKEN_EQ_EQ) || check(state, TOKEN_BANG_EQ)) {
        Token *op = advance(state);
        ASTNode *right = parse_comparison(state);
        ASTNode *n = make_node(AST_BINARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->left  = left;
        n->right = right;
        left = n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_logical_and(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_equality(state);
    while (check(state, TOKEN_AND)) {
        Token *op = advance(state);
        ASTNode *right = parse_equality(state);
        ASTNode *n = make_node(AST_BINARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->left  = left;
        n->right = right;
        left = n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_logical_or(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_logical_and(state);
    while (check(state, TOKEN_OR)) {
        Token *op = advance(state);
        ASTNode *right = parse_logical_and(state);
        ASTNode *n = make_node(AST_BINARY_EXPR);
        n->value = op->value ? strdup(op->value) : NULL;
        n->left  = left;
        n->right = right;
        left = n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_ternary(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *cond = parse_logical_or(state);
    if (check(state, TOKEN_QUESTION)) {
        advance(state);
        ASTNode *then_expr = parse_expression(state);
        expect(state, TOKEN_COLON, "Expected ':' in ternary");
        ASTNode *else_expr = parse_expression(state);
        ASTNode *n = make_node(AST_TERNARY_EXPR);
        n->condition = cond;
        n->left      = then_expr;
        n->right     = else_expr;
        PARSE_DEPTH_EXIT(state); return n;
    }
    PARSE_DEPTH_EXIT(state); return cond;
}

static ASTNode* parse_assignment(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *left = parse_ternary(state);
    if (check(state, TOKEN_ASSIGN)) {
        advance(state);
        ASTNode *right = parse_assignment(state);
        ASTNode *n = make_node(AST_ASSIGN_STMT);
        n->left  = left;
        n->right = right;
        if (left && left->value) n->value = strdup(left->value);
        PARSE_DEPTH_EXIT(state); return n;
    }
    PARSE_DEPTH_EXIT(state); return left;
}

static ASTNode* parse_expression(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);
    ASTNode *r = parse_assignment(state);
    PARSE_DEPTH_EXIT(state); return r;
}

static ASTNode* parse_block_braced(ParserState *state) {
    /* consume '{' */
    expect(state, TOKEN_LBRACE, "Expected '{'");
    ASTNode *block = make_node(AST_BLOCK);
    while (!check(state, TOKEN_RBRACE) && !is_at_end(state)) {
        while (match(state, TOKEN_NEWLINE) || match(state, TOKEN_SEMICOLON)) {}
        if (check(state, TOKEN_RBRACE)) break;
        ASTNode *stmt = parse_statement(state);
        if (stmt) node_add_child(block, stmt);
        if (state->had_error && !state->panic_mode) synchronize(state);
    }
    expect(state, TOKEN_RBRACE, "Expected '}'");
    return block;
}

static ASTNode* parse_block_until(ParserState *state, bool stop_on_else) {
    ASTNode *block = make_node(AST_BLOCK);
    while (!is_at_end(state)) {
        while (match(state, TOKEN_NEWLINE) || match(state, TOKEN_SEMICOLON)) {}
        if (is_at_end(state)) break;
        /* Stop on end/else/elif */
        if (check_keyword(state, "end")) break;
        if (stop_on_else && (check_keyword(state, "else") || check_keyword(state, "elif"))) break;
        ASTNode *stmt = parse_statement(state);
        if (stmt) node_add_child(block, stmt);
        if (state->had_error && !state->panic_mode) synchronize(state);
    }
    return block;
}

static ASTNode* parse_block(ParserState *state, bool stop_on_else) {
    if (check(state, TOKEN_LBRACE))
        return parse_block_braced(state);
    return parse_block_until(state, stop_on_else);
}

/* ---- Statements ---- */

static ASTNode* parse_statement(ParserState *state) {
    PARSE_DEPTH_CHECK(state, NULL);

    /* Skip blank lines */
    while (match(state, TOKEN_NEWLINE) || match(state, TOKEN_SEMICOLON)) {}
    if (is_at_end(state)) { PARSE_DEPTH_EXIT(state); return NULL; }

    Token *t = current_token(state);
    if (!t) { PARSE_DEPTH_EXIT(state); return NULL; }

    /* var / let / const declaration */
    if (t->type == TOKEN_KEYWORD && t->value &&
        (strcmp(t->value, "var") == 0 || strcmp(t->value, "let") == 0 || strcmp(t->value, "const") == 0)) {
        bool is_const = strcmp(t->value, "const") == 0;
        advance(state);
        Token *name = current_token(state);
        if (!name || name->type != TOKEN_IDENTIFIER) {
            parser_error(state, "Expected variable name");
            PARSE_DEPTH_EXIT(state); return NULL;
        }
        advance(state);
        ASTNode *n = make_node(is_const ? AST_CONST_DECL : AST_VAR_DECL);
        n->value = name->value ? strdup(name->value) : NULL;
        /* Optional type annotation: var x: int = ... */
        if (check(state, TOKEN_COLON)) {
            advance(state);
            advance(state); /* skip type name */
        }
        if (match(state, TOKEN_ASSIGN)) {
            n->right = parse_expression(state);
        }
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* function declaration */
    if (check_keyword(state, "function") || check_keyword(state, "fn") || check_keyword(state, "def")) {
        advance(state);
        Token *name = current_token(state);
        if (!name || name->type != TOKEN_IDENTIFIER) {
            parser_error(state, "Expected function name");
            PARSE_DEPTH_EXIT(state); return NULL;
        }
        advance(state);
        ASTNode *fn = make_node(AST_FUNCTION_DECL);
        fn->value = name->value ? strdup(name->value) : NULL;
        /* Parameters */
        expect(state, TOKEN_LPAREN, "Expected '(' after function name");
        fn->param_count = 0;
        fn->params = NULL;
        while (!check(state, TOKEN_RPAREN) && !is_at_end(state)) {
            Token *param = current_token(state);
            if (!param) break;
            advance(state);
            fn->params = realloc(fn->params, (fn->param_count + 1) * sizeof(char*));
            fn->params[fn->param_count++] = param->value ? strdup(param->value) : strdup("");
            /* optional type: param: type */
            if (check(state, TOKEN_COLON)) { advance(state); advance(state); }
            if (!match(state, TOKEN_COMMA)) break;
        }
        expect(state, TOKEN_RPAREN, "Expected ')'");
        /* optional return type annotation */
        if (check(state, TOKEN_ARROW) || check(state, TOKEN_COLON)) { advance(state); advance(state); }
        fn->body = parse_block(state, false);
        if (check_keyword(state, "end")) advance(state);
        PARSE_DEPTH_EXIT(state); return fn;
    }

    /* if statement */
    if (check_keyword(state, "if")) {
        advance(state);
        ASTNode *n = make_node(AST_IF_STMT);
        n->condition = parse_expression(state);
        /* optional 'then' */
        if (check_keyword(state, "then")) advance(state);
        n->body = parse_block(state, true);
        if (check_keyword(state, "elif")) {
            /* treat elif as nested else-if */
            ASTNode *elif_node = parse_statement(state);
            n->right = elif_node;
        } else if (check_keyword(state, "else")) {
            advance(state);
            n->right = parse_block(state, false);
            if (check_keyword(state, "end")) advance(state);
        } else if (check_keyword(state, "end")) {
            advance(state);
        }
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* while loop */
    if (check_keyword(state, "while")) {
        advance(state);
        ASTNode *n = make_node(AST_WHILE_STMT);
        n->condition = parse_expression(state);
        /* optional 'do' */
        if (check_keyword(state, "do")) advance(state);
        n->body = parse_block(state, false);
        if (check_keyword(state, "end")) advance(state);
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* for loop */
    if (check_keyword(state, "for")) {
        advance(state);
        ASTNode *n = make_node(AST_FOR_STMT);
        Token *iter = current_token(state);
        if (iter && iter->type == TOKEN_IDENTIFIER) {
            n->value = strdup(iter->value);
            advance(state);
        }
        /* expect 'in' */
        if (check_keyword(state, "in")) advance(state);
        /* range expression */
        ASTNode *range_node = make_node(AST_RANGE_EXPR);
        /* try to detect range(start, end) or range(end) or start..end */
        if (check_keyword(state, "range") || (current_token(state) && current_token(state)->type == TOKEN_IDENTIFIER
            && current_token(state)->value && strcmp(current_token(state)->value, "range") == 0)) {
            advance(state);
            expect(state, TOKEN_LPAREN, "Expected '('");
            ASTNode *a = parse_expression(state);
            if (match(state, TOKEN_COMMA)) {
                ASTNode *b = parse_expression(state);
                range_node->left  = a;
                range_node->right = b;
            } else {
                range_node->left = a;
            }
            expect(state, TOKEN_RPAREN, "Expected ')'");
        } else {
            /* fallback: parse as expression */
            range_node->left = parse_expression(state);
        }
        node_add_child(n, range_node);
        /* optional 'do' */
        if (check_keyword(state, "do")) advance(state);
        n->body = parse_block(state, false);
        if (check_keyword(state, "end")) advance(state);
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* return statement */
    if (check_keyword(state, "return")) {
        advance(state);
        ASTNode *n = make_node(AST_RETURN_STMT);
        if (!is_at_end(state) && !check(state, TOKEN_NEWLINE) && !check(state, TOKEN_SEMICOLON))
            n->right = parse_expression(state);
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* break / continue */
    if (check_keyword(state, "break")) {
        advance(state);
        PARSE_DEPTH_EXIT(state);
        return make_node(AST_BREAK_STMT);
    }
    if (check_keyword(state, "continue")) {
        advance(state);
        PARSE_DEPTH_EXIT(state);
        return make_node(AST_CONTINUE_STMT);
    }

    /* import statement */
    if (check_keyword(state, "import")) {
        advance(state);
        ASTNode *n = make_node(AST_IMPORT_STMT);
        Token *mod = current_token(state);
        if (mod) { n->value = mod->value ? strdup(mod->value) : NULL; advance(state); }
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* class definition */
    if (check_keyword(state, "class")) {
        advance(state);
        ASTNode *n = make_node(AST_CLASS_DECL);
        Token *cname = current_token(state);
        if (cname && cname->type == TOKEN_IDENTIFIER) {
            n->value = cname->value ? strdup(cname->value) : NULL;
            advance(state);
        }
        /* optional extends */
        if (check_keyword(state, "extends")) {
            advance(state);
            Token *base = current_token(state);
            if (base) { advance(state); }
        }
        n->body = parse_block(state, false);
        if (check_keyword(state, "end")) advance(state);
        PARSE_DEPTH_EXIT(state); return n;
    }

    /* print / show as statement keyword (optional — also handled as call) */
    if (check_keyword(state, "print") || check_keyword(state, "show")) {
        Token *kw = advance(state);
        ASTNode *call = make_node(AST_CALL_EXPR);
        call->value = kw->value ? strdup(kw->value) : NULL;
        if (check(state, TOKEN_LPAREN)) {
            advance(state);
            while (!check(state, TOKEN_RPAREN) && !is_at_end(state)) {
                ASTNode *arg = parse_expression(state);
                if (arg) node_add_child(call, arg);
                if (!match(state, TOKEN_COMMA)) break;
            }
            expect(state, TOKEN_RPAREN, "Expected ')'");
        } else {
            /* bare print x */
            ASTNode *arg = parse_expression(state);
            if (arg) node_add_child(call, arg);
        }
        PARSE_DEPTH_EXIT(state); return call;
    }

    /* Fallthrough: expression statement */
    ASTNode *expr = parse_expression(state);
    PARSE_DEPTH_EXIT(state); return expr;
}

/* ---- Top-level parser ---- */

ASTNode* parser_parse(Token *tokens, int token_count) {
    if (!tokens || token_count == 0) return make_node(AST_PROGRAM);

    ParserState state = {
        .tokens      = tokens,
        .token_count = token_count,
        .current     = 0,
        .had_error   = 0,
        .panic_mode  = 0,
        .parse_depth = 0,
    };

    ASTNode *program = make_node(AST_PROGRAM);

    while (!is_at_end(&state)) {
        while (match(&state, TOKEN_NEWLINE) || match(&state, TOKEN_SEMICOLON)) {}
        if (is_at_end(&state)) break;
        ASTNode *stmt = parse_statement(&state);
        if (stmt) node_add_child(program, stmt);
        if (state.had_error && !state.panic_mode) synchronize(&state);
    }

    return program;
}

void parser_free_ast(ASTNode *node) {
    if (!node) return;
    free(node->value);
    parser_free_ast(node->left);
    parser_free_ast(node->right);
    parser_free_ast(node->condition);
    parser_free_ast(node->body);
    for (int i = 0; i < node->child_count; i++)
        parser_free_ast(node->children[i]);
    free(node->children);
    for (int i = 0; i < node->param_count; i++)
        free(node->params[i]);
    free(node->params);
    free(node);
}
