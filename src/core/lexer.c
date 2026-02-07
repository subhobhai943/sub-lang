/* ========================================
   SUB Language Lexer Implementation
   File: lexer.c
   Aligned with sub_compiler.h v2.0
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"

#include <stdarg.h>

/* ── Character Classification ──────────────────────────────── */

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_ident_cont(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* ── Dynamic Token Array ───────────────────────────────────── */

typedef struct {
    Token *data;
    int    count;
    int    capacity;
} TokenArray;

static void token_array_init(TokenArray *a) {
    a->capacity = 256;
    a->count    = 0;
    a->data     = malloc(sizeof(Token) * a->capacity);
    if (!a->data) {
        fprintf(stderr, "Fatal: token array allocation failed\n");
        exit(1);
    }
}

static void token_array_push(TokenArray *a, Token tok) {
    if (a->count >= a->capacity) {
        a->capacity *= 2;
        a->data = realloc(a->data, sizeof(Token) * a->capacity);
        if (!a->data) {
            fprintf(stderr, "Fatal: token array realloc failed\n");
            exit(1);
        }
    }
    a->data[a->count++] = tok;
}

/* ── Token Constructors ────────────────────────────────────── */

static Token make_token(TokenType type, const char *value,
                        int line, int col) {
    Token t;
    t.type   = type;
    t.value  = value ? strdup(value) : NULL;
    t.line   = line;
    t.column = col;
    return t;
}

/* Build token from a (start, length) span — avoids strndup+free */
static Token make_token_span(TokenType type, const char *start, int len,
                             int line, int col) {
    Token t;
    t.type   = type;
    t.value  = strndup(start, len);
    t.line   = line;
    t.column = col;
    return t;
}

/* ── Lexer State ───────────────────────────────────────────── */

typedef struct {
    const char *source;
    const char *ptr;
    int         line;
    int         column;
    int         error_count;
} Lexer;

static char peek(Lexer *L)      { return *L->ptr; }
static char peek_next(Lexer *L) { return *L->ptr ? *(L->ptr + 1) : '\0'; }

static char advance(Lexer *L) {
    char c = *L->ptr++;
    L->column++;
    return c;
}

static int match(Lexer *L, char expected) {
    if (*L->ptr == expected) { advance(L); return 1; }
    return 0;
}

static void lex_error(Lexer *L, const char *fmt, ...) {
    L->error_count++;
    fprintf(stderr, "[line %d, col %d] Lexer error: ", L->line, L->column);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ── Keyword Table ─────────────────────────────────────────── 
   Every keyword from the TokenType enum in sub_compiler.h.
   Sorted roughly by frequency of use for marginal lookup speed. */

typedef struct { const char *word; TokenType type; } KWEntry;

static const KWEntry kw_table[] = {
    /* Core */
    {"var",         TOKEN_VAR},
    {"const",       TOKEN_CONST},
    {"let",         TOKEN_LET},
    {"function",    TOKEN_FUNCTION},
    {"return",      TOKEN_RETURN},
    {"if",          TOKEN_IF},
    {"elif",        TOKEN_ELIF},
    {"else",        TOKEN_ELSE},
    {"for",         TOKEN_FOR},
    {"while",       TOKEN_WHILE},
    {"do",          TOKEN_DO},
    {"end",         TOKEN_END},
    {"break",       TOKEN_BREAK},
    {"continue",    TOKEN_CONTINUE},

    /* Error handling */
    {"try",         TOKEN_TRY},
    {"catch",       TOKEN_CATCH},
    {"finally",     TOKEN_FINALLY},
    {"throw",       TOKEN_THROW},

    /* Embedded language names */
    {"embed",       TOKEN_EMBED},
    {"endembed",    TOKEN_ENDEMBED},
    {"cpp",         TOKEN_CPP},
    {"c",           TOKEN_C},
    {"python",      TOKEN_PYTHON},
    {"javascript",  TOKEN_JAVASCRIPT},
    {"rust",        TOKEN_RUST},

    /* UI */
    {"ui",          TOKEN_UI},

    /* OOP */
    {"class",       TOKEN_CLASS},
    {"extends",     TOKEN_EXTENDS},
    {"implements",  TOKEN_IMPLEMENTS},
    {"new",         TOKEN_NEW},
    {"this",        TOKEN_THIS},
    {"super",       TOKEN_SUPER},
    {"static",      TOKEN_STATIC},
    {"private",     TOKEN_PRIVATE},
    {"public",      TOKEN_PUBLIC},
    {"protected",   TOKEN_PROTECTED},

    /* Async */
    {"async",       TOKEN_ASYNC},
    {"await",       TOKEN_AWAIT},
    {"yield",       TOKEN_YIELD},

    /* Type keywords */
    {"int",         TOKEN_INT},
    {"float",       TOKEN_FLOAT},
    {"string",      TOKEN_STRING},
    {"bool",        TOKEN_BOOL},
    {"auto",        TOKEN_AUTO},
    {"void",        TOKEN_VOID},

    /* Literals */
    {"true",        TOKEN_TRUE},
    {"false",       TOKEN_FALSE},
    {"null",        TOKEN_NULL},

    {NULL, 0}
};

static TokenType lookup_keyword(const char *start, int len) {
    for (const KWEntry *e = kw_table; e->word; e++) {
        if ((int)strlen(e->word) == len &&
            memcmp(e->word, start, len) == 0)
            return e->type;
    }
    return TOKEN_IDENTIFIER;
}

/* ── Whitespace & Comment Skipping ─────────────────────────── */

static void skip_whitespace_and_comments(Lexer *L) {
    for (;;) {
        char c = peek(L);

        /* Spaces, tabs, carriage returns */
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(L);
            continue;
        }

        /* Single-line comment: // ... */
        if (c == '/' && peek_next(L) == '/') {
            while (peek(L) && peek(L) != '\n')
                advance(L);
            continue;
        }

        /* Block comment: supports nested slash-star ... star-slash blocks. */
        if (c == '/' && peek_next(L) == '*') {
            int start_line = L->line;
            advance(L);  /* '/' */
            advance(L);  /* '*' */
            int depth = 1;
            while (peek(L) && depth > 0) {
                if (peek(L) == '/' && peek_next(L) == '*') {
                    advance(L); advance(L);
                    depth++;
                } else if (peek(L) == '*' && peek_next(L) == '/') {
                    advance(L); advance(L);
                    depth--;
                } else {
                    if (peek(L) == '\n') {
                        L->line++;
                        L->column = 0;
                    }
                    advance(L);
                }
            }
            if (depth > 0)
                lex_error(L, "Unterminated block comment starting at line %d",
                          start_line);
            continue;
        }

        break;
    }
}

/* ── Escape Processing for Strings ─────────────────────────── */

static char* process_escapes(const char *raw, int len, Lexer *L) {
    char *buf = malloc(len + 1);
    int o = 0;
    for (int i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            i++;
            switch (raw[i]) {
                case 'n':  buf[o++] = '\n'; break;
                case 't':  buf[o++] = '\t'; break;
                case 'r':  buf[o++] = '\r'; break;
                case '\\': buf[o++] = '\\'; break;
                case '\'': buf[o++] = '\''; break;
                case '"':  buf[o++] = '"';  break;
                case '0':  buf[o++] = '\0'; break;
                case 'x': {
                    /* \xHH — two hex digits */
                    if (i + 2 < len &&
                        isxdigit((unsigned char)raw[i+1]) &&
                        isxdigit((unsigned char)raw[i+2])) {
                        char hex[3] = { raw[i+1], raw[i+2], '\0' };
                        buf[o++] = (char)strtol(hex, NULL, 16);
                        i += 2;
                    } else {
                        lex_error(L, "Invalid hex escape");
                        buf[o++] = 'x';
                    }
                    break;
                }
                default:
                    lex_error(L, "Unknown escape '\\%c'", raw[i]);
                    buf[o++] = raw[i];
                    break;
            }
        } else {
            buf[o++] = raw[i];
        }
    }
    buf[o] = '\0';
    return buf;
}

/* ── String Scanning ───────────────────────────────────────── */

static Token scan_string(Lexer *L) {
    int start_line = L->line;
    int start_col  = L->column;
    char quote = advance(L);          /* consume opening quote */
    const char *start = L->ptr;

    while (peek(L) && peek(L) != quote) {
        if (peek(L) == '\n') {
            L->line++;
            L->column = 0;
        }
        if (peek(L) == '\\' && peek_next(L))
            advance(L);               /* skip past backslash */
        advance(L);
    }

    int raw_len = (int)(L->ptr - start);

    if (!peek(L)) {
        lex_error(L, "Unterminated string starting at line %d, col %d",
                  start_line, start_col);
    } else {
        advance(L);                   /* consume closing quote */
    }

    /* NOTE: Emit TOKEN_STRING_LITERAL, not TOKEN_STRING.
       TOKEN_STRING is the "string" *type keyword* in the header. */
    char *val = process_escapes(start, raw_len, L);
    Token tok = make_token(TOKEN_STRING_LITERAL, val, start_line, start_col);
    free(val);
    return tok;
}

/* ── Number Scanning ───────────────────────────────────────── */

static Token scan_number(Lexer *L) {
    int start_col    = L->column;
    const char *start = L->ptr;

    /* Hex: 0x... */
    if (peek(L) == '0' && (peek_next(L) == 'x' || peek_next(L) == 'X')) {
        advance(L); advance(L);       /* 0x */
        if (!isxdigit((unsigned char)peek(L)))
            lex_error(L, "Expected hex digits after '0x'");
        while (isxdigit((unsigned char)peek(L)))
            advance(L);
        int len = (int)(L->ptr - start);
        return make_token_span(TOKEN_NUMBER, start, len, L->line, start_col);
    }

    /* Decimal integer or float */
    while (isdigit((unsigned char)peek(L)))
        advance(L);

    /* Fractional part — but not ".." range operator */
    if (peek(L) == '.' && peek_next(L) != '.') {
        advance(L);                   /* consume '.' */
        while (isdigit((unsigned char)peek(L)))
            advance(L);
    }

    /* Scientific notation */
    if (peek(L) == 'e' || peek(L) == 'E') {
        advance(L);
        if (peek(L) == '+' || peek(L) == '-')
            advance(L);
        if (!isdigit((unsigned char)peek(L)))
            lex_error(L, "Expected digit after exponent");
        while (isdigit((unsigned char)peek(L)))
            advance(L);
    }

    int len = (int)(L->ptr - start);
    return make_token_span(TOKEN_NUMBER, start, len, L->line, start_col);
}

/* ── Identifier / Keyword Scanning ─────────────────────────── */

static Token scan_identifier(Lexer *L) {
    int start_col    = L->column;
    const char *start = L->ptr;

    while (is_ident_cont(peek(L)))
        advance(L);

    int len = (int)(L->ptr - start);
    TokenType type = lookup_keyword(start, len);

    return make_token_span(type, start, len, L->line, start_col);
}

/* ── Operator / Punctuation Scanning ───────────────────────── */

static Token scan_operator_or_punct(Lexer *L) {
    int start_col = L->column;
    char c = advance(L);
    char n = peek(L);

    switch (c) {
        /* ── Punctuation ── */
        case '(': return make_token(TOKEN_LPAREN,    "(", L->line, start_col);
        case ')': return make_token(TOKEN_RPAREN,    ")", L->line, start_col);
        case '{': return make_token(TOKEN_LBRACE,    "{", L->line, start_col);
        case '}': return make_token(TOKEN_RBRACE,    "}", L->line, start_col);
        case '[': return make_token(TOKEN_LBRACKET,  "[", L->line, start_col);
        case ']': return make_token(TOKEN_RBRACKET,  "]", L->line, start_col);
        case ',': return make_token(TOKEN_COMMA,     ",", L->line, start_col);
        case ';': return make_token(TOKEN_SEMICOLON, ";", L->line, start_col);
        case '?': return make_token(TOKEN_QUESTION,  "?", L->line, start_col);
        case ':': return make_token(TOKEN_COLON,     ":", L->line, start_col);
        case '#': return make_token(TOKEN_HASH,      "#", L->line, start_col);
        case '@': return make_token(TOKEN_OPERATOR,  "@", L->line, start_col);

        /* ── Dot or ".." range ── */
        case '.':
            if (n == '.') {
                advance(L);
                return make_token(TOKEN_OPERATOR, "..", L->line, start_col);
            }
            return make_token(TOKEN_DOT, ".", L->line, start_col);

        /* ── Compound operators ── */
        case '+': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "+=", L->line, start_col); }
            if (n == '+') { advance(L); return make_token(TOKEN_OPERATOR, "++", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "+", L->line, start_col);
        }
        case '-': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "-=", L->line, start_col); }
            if (n == '-') { advance(L); return make_token(TOKEN_OPERATOR, "--", L->line, start_col); }
            if (n == '>') { advance(L); return make_token(TOKEN_OPERATOR, "->", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "-", L->line, start_col);
        }
        case '*': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "*=", L->line, start_col); }
            if (n == '*') { advance(L); return make_token(TOKEN_OPERATOR, "**", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "*", L->line, start_col);
        }
        case '/': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "/=", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "/", L->line, start_col);
        }
        case '%': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "%=", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "%", L->line, start_col);
        }
        case '=': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "==", L->line, start_col); }
            if (n == '>') { advance(L); return make_token(TOKEN_ARROW,    "=>", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "=", L->line, start_col);
        }
        case '!': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "!=", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "!", L->line, start_col);
        }
        case '<': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "<=", L->line, start_col); }
            if (n == '<') { advance(L); return make_token(TOKEN_OPERATOR, "<<", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "<", L->line, start_col);
        }
        case '>': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, ">=", L->line, start_col); }
            if (n == '>') { advance(L); return make_token(TOKEN_OPERATOR, ">>", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, ">", L->line, start_col);
        }
        case '&': {
            if (n == '&') { advance(L); return make_token(TOKEN_OPERATOR, "&&", L->line, start_col); }
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "&=", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "&", L->line, start_col);
        }
        case '|': {
            if (n == '|') { advance(L); return make_token(TOKEN_OPERATOR, "||", L->line, start_col); }
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "|=", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "|", L->line, start_col);
        }
        case '^': {
            if (n == '=') { advance(L); return make_token(TOKEN_OPERATOR, "^=", L->line, start_col); }
            return make_token(TOKEN_OPERATOR, "^", L->line, start_col);
        }
        case '~':
            return make_token(TOKEN_OPERATOR, "~", L->line, start_col);

        default:
            break;
    }

    /* Should not reach here — caller checks before calling */
    char buf[2] = {c, '\0'};
    return make_token(TOKEN_OPERATOR, buf, L->line, start_col);
}

/* ── Character dispatch helpers ────────────────────────────── */

static int is_operator_or_punct_start(char c) {
    return  c == '(' || c == ')' || c == '{' || c == '}' ||
            c == '[' || c == ']' || c == ',' || c == ';' ||
            c == '?' || c == ':' || c == '#' || c == '@' ||
            c == '.' || c == '+' || c == '-' || c == '*' ||
            c == '/' || c == '%' || c == '=' || c == '!' ||
            c == '<' || c == '>' || c == '&' || c == '|' ||
            c == '^' || c == '~';
}

/* ── Main Tokenizer ────────────────────────────────────────── */

Token* lexer_tokenize(const char *source, int *token_count) {
    if (!source || !token_count) {
        if (token_count) *token_count = 0;
        return NULL;
    }

    Lexer L = {
        .source      = source,
        .ptr         = source,
        .line        = 1,
        .column      = 1,
        .error_count = 0
    };

    TokenArray arr;
    token_array_init(&arr);

    while (peek(&L)) {
        /* Skip whitespace and comments first */
        skip_whitespace_and_comments(&L);
        if (!peek(&L)) break;

        char c = peek(&L);

        /* ── Newlines ── */
        if (c == '\n') {
            token_array_push(&arr,
                make_token(TOKEN_NEWLINE, NULL, L.line, L.column));
            advance(&L);
            L.line++;
            L.column = 1;
            continue;
        }

        /* ── String literals ── */
        if (c == '"' || c == '\'' || c == '`') {
            token_array_push(&arr, scan_string(&L));
            continue;
        }

        /* ── Numbers ── */
        if (isdigit((unsigned char)c)) {
            token_array_push(&arr, scan_number(&L));
            continue;
        }

        /* ── Leading-dot float: .5 ── */
        if (c == '.' && isdigit((unsigned char)peek_next(&L))) {
            token_array_push(&arr, scan_number(&L));
            continue;
        }

        /* ── Identifiers and keywords ── */
        if (is_ident_start(c)) {
            token_array_push(&arr, scan_identifier(&L));
            continue;
        }

        /* ── Operators and punctuation ── */
        if (is_operator_or_punct_start(c)) {
            token_array_push(&arr, scan_operator_or_punct(&L));
            continue;
        }

        /* ── Unknown character ── */
        lex_error(&L, "Unexpected character '%c' (0x%02X)",
                  c, (unsigned char)c);
        advance(&L);
    }

    /* EOF sentinel */
    token_array_push(&arr, make_token(TOKEN_EOF, NULL, L.line, L.column));

    *token_count = arr.count;

    if (L.error_count > 0)
        fprintf(stderr, "Lexer finished with %d error(s)\n", L.error_count);

    return arr.data;
}

/* ── Cleanup ───────────────────────────────────────────────── */

void lexer_free_tokens(Token *tokens, int count) {
    if (!tokens) return;
    for (int i = 0; i < count; i++) {
        free(tokens[i].value);   /* free(NULL) is safe per C standard */
    }
    free(tokens);
}

/* ── Debug helper ──────────────────────────────────────────── */

const char* token_type_to_string(TokenType type) {
    switch (type) {
        case TOKEN_HASH:           return "HASH";
        case TOKEN_VAR:            return "VAR";
        case TOKEN_CONST:          return "CONST";
        case TOKEN_LET:            return "LET";
        case TOKEN_FUNCTION:       return "FUNCTION";
        case TOKEN_IF:             return "IF";
        case TOKEN_ELIF:           return "ELIF";
        case TOKEN_ELSE:           return "ELSE";
        case TOKEN_FOR:            return "FOR";
        case TOKEN_WHILE:          return "WHILE";
        case TOKEN_DO:             return "DO";
        case TOKEN_RETURN:         return "RETURN";
        case TOKEN_END:            return "END";
        case TOKEN_BREAK:          return "BREAK";
        case TOKEN_CONTINUE:       return "CONTINUE";
        case TOKEN_TRY:            return "TRY";
        case TOKEN_CATCH:          return "CATCH";
        case TOKEN_FINALLY:        return "FINALLY";
        case TOKEN_THROW:          return "THROW";
        case TOKEN_EMBED:          return "EMBED";
        case TOKEN_ENDEMBED:       return "ENDEMBED";
        case TOKEN_CPP:            return "CPP";
        case TOKEN_C:              return "C";
        case TOKEN_PYTHON:         return "PYTHON";
        case TOKEN_JAVASCRIPT:     return "JAVASCRIPT";
        case TOKEN_RUST:           return "RUST";
        case TOKEN_UI:             return "UI";
        case TOKEN_CLASS:          return "CLASS";
        case TOKEN_EXTENDS:        return "EXTENDS";
        case TOKEN_IMPLEMENTS:     return "IMPLEMENTS";
        case TOKEN_NEW:            return "NEW";
        case TOKEN_THIS:           return "THIS";
        case TOKEN_SUPER:          return "SUPER";
        case TOKEN_STATIC:         return "STATIC";
        case TOKEN_PRIVATE:        return "PRIVATE";
        case TOKEN_PUBLIC:         return "PUBLIC";
        case TOKEN_PROTECTED:      return "PROTECTED";
        case TOKEN_ASYNC:          return "ASYNC";
        case TOKEN_AWAIT:          return "AWAIT";
        case TOKEN_YIELD:          return "YIELD";
        case TOKEN_INT:            return "INT";
        case TOKEN_FLOAT:          return "FLOAT";
        case TOKEN_STRING:         return "STRING_TYPE";
        case TOKEN_BOOL:           return "BOOL";
        case TOKEN_AUTO:           return "AUTO";
        case TOKEN_VOID:           return "VOID";
        case TOKEN_IDENTIFIER:     return "IDENTIFIER";
        case TOKEN_NUMBER:         return "NUMBER";
        case TOKEN_STRING_LITERAL: return "STRING_LITERAL";
        case TOKEN_TRUE:           return "TRUE";
        case TOKEN_FALSE:          return "FALSE";
        case TOKEN_NULL:           return "NULL";
        case TOKEN_OPERATOR:       return "OPERATOR";
        case TOKEN_ARROW:          return "ARROW";
        case TOKEN_QUESTION:       return "QUESTION";
        case TOKEN_COLON:          return "COLON";
        case TOKEN_SEMICOLON:      return "SEMICOLON";
        case TOKEN_LPAREN:         return "LPAREN";
        case TOKEN_RPAREN:         return "RPAREN";
        case TOKEN_LBRACE:         return "LBRACE";
        case TOKEN_RBRACE:         return "RBRACE";
        case TOKEN_LBRACKET:       return "LBRACKET";
        case TOKEN_RBRACKET:       return "RBRACKET";
        case TOKEN_DOT:            return "DOT";
        case TOKEN_COMMA:          return "COMMA";
        case TOKEN_NEWLINE:        return "NEWLINE";
        case TOKEN_EOF:            return "EOF";
        default:                   return "UNKNOWN";
    }
}
