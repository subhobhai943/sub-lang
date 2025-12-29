/* ========================================
   SUB Language Lexer Implementation
   File: lexer.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "windows_compat.h"

// Helper function to check if character is valid identifier start
static int is_identifier_start(char c) {
    return isalpha(c) || c == '_';
}

// Helper function to check if character is valid identifier continuation
static int is_identifier_continue(char c) {
    return isalnum(c) || c == '_';
}

// Helper function to create a token
static Token create_token(TokenType type, const char *value, int line, int column) {
    Token token;
    token.type = type;
    token.value = value ? strdup(value) : NULL;
    token.line = line;
    token.column = column;
    return token;
}

// Tokenize source code
Token* lexer_tokenize(const char *source, int *token_count) {
    int capacity = 1000;
    Token *tokens = malloc(sizeof(Token) * capacity);
    int count = 0;
    
    int line = 1;
    int column = 1;
    const char *ptr = source;
    
    while (*ptr) {
        // Skip whitespace (except newlines)
        if (*ptr == ' ' || *ptr == '\t' || *ptr == '\r') {
            ptr++;
            column++;
            continue;
        }
        
        // Handle newlines
        if (*ptr == '\n') {
            tokens[count++] = create_token(TOKEN_NEWLINE, NULL, line, column);
            ptr++;
            line++;
            column = 1;
            continue;
        }
        
        // Handle comments (// style)
        if (*ptr == '/' && *(ptr + 1) == '/') {
            while (*ptr && *ptr != '\n') ptr++;
            continue;
        }
        
        // Handle hash symbol (#) - blockchain method marker
        if (*ptr == '#') {
            tokens[count++] = create_token(TOKEN_HASH, "#", line, column);
            ptr++;
            column++;
            continue;
        }
        
        // Handle string literals
        if (*ptr == '"' || *ptr == '\'') {
            char quote = *ptr;
            ptr++;
            column++;
            const char *start = ptr;
            
            while (*ptr && *ptr != quote) {
                if (*ptr == '\\') ptr++; // Skip escaped characters
                ptr++;
                column++;
            }
            
            char *str_value = strndup(start, ptr - start);
            tokens[count++] = create_token(TOKEN_STRING, str_value, line, column);
            free(str_value);
            
            if (*ptr) {
                ptr++;
                column++;
            }
            continue;
        }
        
        // Handle numbers
        if (isdigit(*ptr)) {
            const char *start = ptr;
            while (isdigit(*ptr) || *ptr == '.') {
                ptr++;
                column++;
            }
            
            char *num_value = strndup(start, ptr - start);
            tokens[count++] = create_token(TOKEN_NUMBER, num_value, line, column);
            free(num_value);
            continue;
        }
        
        // Handle identifiers and keywords
        if (is_identifier_start(*ptr)) {
            const char *start = ptr;
            while (is_identifier_continue(*ptr)) {
                ptr++;
                column++;
            }
            
            char *identifier = strndup(start, ptr - start);
            
            // Check for keywords
            TokenType type = TOKEN_IDENTIFIER;
            if (strcmp(identifier, "var") == 0) type = TOKEN_VAR;
            else if (strcmp(identifier, "function") == 0) type = TOKEN_FUNCTION;
            else if (strcmp(identifier, "if") == 0) type = TOKEN_IF;
            else if (strcmp(identifier, "elif") == 0) type = TOKEN_ELIF;
            else if (strcmp(identifier, "else") == 0) type = TOKEN_ELSE;
            else if (strcmp(identifier, "for") == 0) type = TOKEN_FOR;
            else if (strcmp(identifier, "while") == 0) type = TOKEN_WHILE;
            else if (strcmp(identifier, "return") == 0) type = TOKEN_RETURN;
            else if (strcmp(identifier, "end") == 0) type = TOKEN_END;
            else if (strcmp(identifier, "embed") == 0) type = TOKEN_EMBED;
            else if (strcmp(identifier, "endembed") == 0) type = TOKEN_ENDEMBED;
            else if (strcmp(identifier, "ui") == 0) type = TOKEN_UI;
            
            tokens[count++] = create_token(type, identifier, line, column);
            free(identifier);
            continue;
        }
        
        // Handle operators and punctuation
        switch (*ptr) {
            case '(': tokens[count++] = create_token(TOKEN_LPAREN, "(", line, column); break;
            case ')': tokens[count++] = create_token(TOKEN_RPAREN, ")", line, column); break;
            case '{': tokens[count++] = create_token(TOKEN_LBRACE, "{", line, column); break;
            case '}': tokens[count++] = create_token(TOKEN_RBRACE, "}", line, column); break;
            case '.': tokens[count++] = create_token(TOKEN_DOT, ".", line, column); break;
            case ',': tokens[count++] = create_token(TOKEN_COMMA, ",", line, column); break;
            case '+':
            case '-':
            case '*':
            case '/':
            case '=':
            case '<':
            case '>':
            case '!': {
                char op[3] = {*ptr, '\0', '\0'};
                if (*(ptr + 1) == '=') {
                    op[1] = '=';
                    op[2] = '\0';
                    ptr++;
                    column++;
                }
                tokens[count++] = create_token(TOKEN_OPERATOR, op, line, column);
                break;
            }
            default:
                fprintf(stderr, "Unexpected character: %c at line %d, column %d\n", *ptr, line, column);
        }
        
        ptr++;
        column++;
        
        // Expand token array if needed
        if (count >= capacity) {
            capacity *= 2;
            tokens = realloc(tokens, sizeof(Token) * capacity);
        }
    }
    
    tokens[count++] = create_token(TOKEN_EOF, NULL, line, column);
    *token_count = count;
    
    return tokens;
}

// Free token array
void lexer_free_tokens(Token *tokens, int count) {
    for (int i = 0; i < count; i++) {
        if (tokens[i].value) {
            free(tokens[i].value);
        }
    }
    free(tokens);
}
