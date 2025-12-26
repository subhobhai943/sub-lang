/**
 * SUB Language Frontend FFI
 * C header for interfacing with Rust frontend
 */

#ifndef SUB_FRONTEND_FFI_H
#define SUB_FRONTEND_FFI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types
typedef struct SubLexer SubLexer;
typedef struct SubParser SubParser;
typedef struct SubToken SubToken;
typedef struct SubAST SubAST;
typedef struct SubSourceFile SubSourceFile;

// Token types
typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_KEYWORD,
    TOKEN_OPERATOR,
    TOKEN_PUNCTUATION,
    TOKEN_COMMENT,
    TOKEN_WHITESPACE,
    TOKEN_EOF,
    TOKEN_ERROR
} SubTokenType;

// Error handling
typedef struct {
    const char* message;
    uint32_t line;
    uint32_t column;
    const char* file;
} SubError;

// Lexer functions
SubLexer* sub_lexer_new(const char* source, size_t source_len);
void sub_lexer_free(SubLexer* lexer);
SubToken* sub_lexer_next_token(SubLexer* lexer);
bool sub_lexer_has_errors(const SubLexer* lexer);
const SubError* sub_lexer_get_error(const SubLexer* lexer);

// Token functions
void sub_token_free(SubToken* token);
SubTokenType sub_token_get_type(const SubToken* token);
const char* sub_token_get_value(const SubToken* token);
uint32_t sub_token_get_line(const SubToken* token);
uint32_t sub_token_get_column(const SubToken* token);

// Parser functions
SubParser* sub_parser_new(SubLexer* lexer);
void sub_parser_free(SubParser* parser);
SubAST* sub_parser_parse(SubParser* parser);
bool sub_parser_has_errors(const SubParser* parser);
const SubError* sub_parser_get_error(const SubParser* parser);

// AST functions
void sub_ast_free(SubAST* ast);
const char* sub_ast_to_string(const SubAST* ast);
size_t sub_ast_get_node_count(const SubAST* ast);

// Source file handling
SubSourceFile* sub_source_file_new(const char* path);
void sub_source_file_free(SubSourceFile* file);
const char* sub_source_file_get_content(const SubSourceFile* file);
size_t sub_source_file_get_size(const SubSourceFile* file);
bool sub_source_file_is_valid(const SubSourceFile* file);

// Utility functions
const char* sub_get_version(void);
void sub_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif // SUB_FRONTEND_FFI_H
