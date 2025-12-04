#ifndef SUB_FRONTEND_FFI_H
#define SUB_FRONTEND_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Token types from Rust frontend
typedef enum {
    TOKEN_HASH, TOKEN_VAR, TOKEN_CONST, TOKEN_FUNCTION,
    TOKEN_IF, TOKEN_ELIF, TOKEN_ELSE,
    TOKEN_FOR, TOKEN_WHILE, TOKEN_DO, TOKEN_RETURN,
    TOKEN_END, TOKEN_BREAK, TOKEN_CONTINUE,
    TOKEN_TRY, TOKEN_CATCH, TOKEN_FINALLY, TOKEN_THROW,
    TOKEN_EMBED, TOKEN_ENDEMBED,
    TOKEN_CPP, TOKEN_C, TOKEN_PYTHON, TOKEN_JAVASCRIPT, TOKEN_RUST,
    TOKEN_UI, TOKEN_CLASS, TOKEN_EXTENDS, TOKEN_NEW, TOKEN_THIS,
    TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_STRING_LIT,
    TOKEN_TRUE, TOKEN_FALSE, TOKEN_NULL,
    TOKEN_OPERATOR, TOKEN_ARROW,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_DOT, TOKEN_COMMA, TOKEN_COLON, TOKEN_SEMICOLON,
    TOKEN_NEWLINE, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType token_type;
    char* value;
    int32_t line;
    int32_t column;
} Token;

typedef struct {
    Token* tokens;
    size_t count;
} TokenArray;

// FFI functions exported from Rust
void* sub_frontend_tokenize(const char* source);
void sub_frontend_free_tokens(void* token_array);

#ifdef __cplusplus
}
#endif

#endif // SUB_FRONTEND_FFI_H
