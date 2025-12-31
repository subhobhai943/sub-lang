/* ========================================
   SUB Language Compiler - Main Components
   Version 2.0 - C++ Support & Modern Features
   Implemented in C for performance
   File: sub_compiler.h
   ======================================== */

#ifndef SUB_COMPILER_H
#define SUB_COMPILER_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* Token Types for Lexical Analysis */
typedef enum {
    // Hash and Basic
    TOKEN_HASH,           // #
    
    // Keywords
    TOKEN_VAR,            // var
    TOKEN_CONST,          // const
    TOKEN_LET,            // let
    TOKEN_FUNCTION,       // function
    TOKEN_IF,             // if
    TOKEN_ELIF,           // elif
    TOKEN_ELSE,           // else
    TOKEN_FOR,            // for
    TOKEN_WHILE,          // while
    TOKEN_DO,             // do
    TOKEN_RETURN,         // return
    TOKEN_END,            // end
    TOKEN_BREAK,          // break
    TOKEN_CONTINUE,       // continue
    
    // Error Handling
    TOKEN_TRY,            // try
    TOKEN_CATCH,          // catch
    TOKEN_FINALLY,        // finally
    TOKEN_THROW,          // throw
    
    // Embedded Languages
    TOKEN_EMBED,          // embed
    TOKEN_ENDEMBED,       // endembed
    TOKEN_CPP,            // cpp (for #embed cpp)
    TOKEN_C,              // c (for #embed c)
    TOKEN_PYTHON,         // python
    TOKEN_JAVASCRIPT,     // javascript
    TOKEN_RUST,           // rust
    
    // UI Components
    TOKEN_UI,             // ui
    
    // OOP Keywords
    TOKEN_CLASS,          // class
    TOKEN_EXTENDS,        // extends
    TOKEN_IMPLEMENTS,     // implements
    TOKEN_NEW,            // new
    TOKEN_THIS,           // this
    TOKEN_SUPER,          // super
    TOKEN_STATIC,         // static
    TOKEN_PRIVATE,        // private
    TOKEN_PUBLIC,         // public
    TOKEN_PROTECTED,      // protected
    
    // Async Keywords
    TOKEN_ASYNC,          // async
    TOKEN_AWAIT,          // await
    TOKEN_YIELD,          // yield
    
    // Type Keywords
    TOKEN_INT,            // int
    TOKEN_FLOAT,          // float
    TOKEN_STRING,         // string (as type)
    TOKEN_BOOL,           // bool
    TOKEN_AUTO,           // auto
    TOKEN_VOID,           // void
    
    // Literals and Identifiers
    TOKEN_IDENTIFIER,     // variable names
    TOKEN_NUMBER,         // numeric literals
    TOKEN_STRING_LITERAL, // string literals
    TOKEN_TRUE,           // true
    TOKEN_FALSE,          // false
    TOKEN_NULL,           // null
    
    // Operators
    TOKEN_OPERATOR,       // +, -, *, /, =, ==, !=, <, >, etc.
    TOKEN_ARROW,          // => (arrow function)
    TOKEN_QUESTION,       // ? (ternary)
    TOKEN_COLON,          // : (ternary, type hints)
    TOKEN_SEMICOLON,      // ;
    
    // Delimiters
    TOKEN_LPAREN,         // (
    TOKEN_RPAREN,         // )
    TOKEN_LBRACE,         // {
    TOKEN_RBRACE,         // }
    TOKEN_LBRACKET,       // [
    TOKEN_RBRACKET,       // ]
    TOKEN_DOT,            // .
    TOKEN_COMMA,          // ,
    TOKEN_NEWLINE,        // \n
    
    // Special
    TOKEN_EOF             // End of file
} TokenType;

/* Token Structure */
typedef struct {
    TokenType type;
    char *value;
    int line;
    int column;
} Token;

/* Data Types */
typedef enum {
    TYPE_UNKNOWN,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_ARRAY,
    TYPE_OBJECT,
    TYPE_FUNCTION,
    TYPE_NULL,
    TYPE_AUTO,
    TYPE_VOID,
    TYPE_GENERIC
} DataType;

/* Abstract Syntax Tree Node Types */
typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_CONST_DECL,
    AST_FUNCTION_DECL,
    AST_ARROW_FUNCTION,
    AST_CLASS_DECL,
    AST_IF_STMT,
    AST_FOR_STMT,
    AST_WHILE_STMT,
    AST_DO_WHILE_STMT,
    AST_TRY_STMT,
    AST_CATCH_CLAUSE,
    AST_FINALLY_CLAUSE,
    AST_THROW_STMT,
    AST_RETURN_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_ASSIGN_STMT,
    AST_CALL_EXPR,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_TERNARY_EXPR,
    AST_IDENTIFIER,
    AST_LITERAL,
    AST_BLOCK,
    AST_UI_COMPONENT,
    AST_EMBED_CODE,
    AST_EMBED_CPP,
    AST_EMBED_C,
    AST_ARRAY_LITERAL,
    AST_OBJECT_LITERAL,
    AST_MEMBER_ACCESS,
    AST_ARRAY_ACCESS,
    AST_NEW_EXPR,         // new ClassName()
    AST_RANGE_EXPR,       // range(n) or range(start, end)
    AST_ARRAY_ITERATION,  // for item in collection
    AST_PARAM_DECL        // Function parameter declaration
} ASTNodeType;

/* AST Node Structure */
typedef struct ASTNode {
    ASTNodeType type;
    char *value;
    DataType data_type;
    struct ASTNode *left;
    struct ASTNode *right;
    struct ASTNode *next;
    struct ASTNode *condition;  // For if/while/ternary
    struct ASTNode *body;       // For functions/loops/try
    struct ASTNode **children;
    int child_count;
    void *metadata;
    int line;
    int column;
} ASTNode;

/* Target Platform Enum */
typedef enum {
    PLATFORM_ANDROID,
    PLATFORM_IOS,
    PLATFORM_WINDOWS,
    PLATFORM_MACOS,
    PLATFORM_LINUX,
    PLATFORM_WEB,
    PLATFORM_WASM         // WebAssembly target
} Platform;

/* Compilation Options */
typedef struct {
    bool optimize;              // Enable optimizations
    int optimization_level;     // 0-3
    bool debug_symbols;         // Include debug info
    bool minify;                // Minify output
    bool warnings_as_errors;    // Treat warnings as errors
    bool verbose;               // Verbose output
    bool use_cpp;               // Use C++ compiler instead of C
    bool enable_simd;           // Enable SIMD optimizations
    bool parallel_compile;      // Parallel compilation
} CompilationOptions;

/* Symbol Table Entry */
typedef struct SymbolTableEntry {
    char *name;
    DataType type;
    int scope_level;
    bool is_constant;
    bool is_initialized;
    struct SymbolTableEntry *next;
} SymbolTableEntry;

/* Symbol Table */
typedef struct {
    SymbolTableEntry **buckets;
    int size;
    int scope_level;
} SymbolTable;

/* Class field information */
typedef struct ClassField {
    char *name;
    DataType type;
    int offset;
    struct ClassField *next;
} ClassField;

/* Class definition */
typedef struct ClassDef {
    char *name;
    ClassField *fields;
    int field_count;
    int size;
    struct ClassDef *next;
} ClassDef;

/* Compiler Context */
typedef struct {
    Token *tokens;
    int token_count;
    int current_token;
    ASTNode *ast;
    SymbolTable *symbol_table;
    ClassDef *classes;
    Platform target_platform;
    CompilationOptions options;
    char *output_path;
    char *source_file;
    int error_count;
    int warning_count;
} CompilerContext;

/* Function Declarations */

// Lexical Analysis
Token* lexer_tokenize(const char *source, int *token_count);
void lexer_free_tokens(Token *tokens, int count);
const char* token_type_to_string(TokenType type);

// Parser
ASTNode* parser_parse(Token *tokens, int token_count);
void parser_free_ast(ASTNode *node);
ASTNode* parser_parse_expression(CompilerContext *ctx);
ASTNode* parser_parse_statement(CompilerContext *ctx);

// Semantic Analysis
int semantic_analyze(ASTNode *ast);
int semantic_check_types(ASTNode *ast);
bool semantic_type_check(ASTNode *node);
DataType semantic_infer_type(ASTNode *node);

// Symbol Table
SymbolTable* symbol_table_create(int size);
void symbol_table_free(SymbolTable *table);
bool symbol_table_insert(SymbolTable *table, const char *name, DataType type);
SymbolTableEntry* symbol_table_lookup(SymbolTable *table, const char *name);
void symbol_table_enter_scope(SymbolTable *table);
void symbol_table_exit_scope(SymbolTable *table);

// Class management
ClassDef* class_def_create(const char *name);
void class_def_add_field(ClassDef *cls, const char *field_name, DataType type);
ClassDef* class_def_lookup(ClassDef *classes, const char *name);
void class_def_free(ClassDef *cls);

// Code Generation
char* codegen_generate(ASTNode *ast, Platform platform);
char* codegen_generate_cpp(ASTNode *ast, Platform platform);
char* codegen_generate_c(ASTNode *ast, Platform platform);
char* codegen_embed_cpp(const char *cpp_code);
char* codegen_embed_c(const char *c_code);
void optimize_c_output(ASTNode *node);

// Optimization
void optimizer_optimize(ASTNode *ast, int level);
void optimizer_constant_folding(ASTNode *ast);
void optimizer_dead_code_elimination(ASTNode *ast);
void optimizer_inline_expansion(ASTNode *ast);

// Utility Functions
char* read_file(const char *filename);
void write_file(const char *filename, const char *content);
void compile_error(const char *message, int line);
void compile_error_with_col(const char *message, int line, int column);
void compile_warning(const char *message, int line);

// Compiler Interface
CompilerContext* compiler_create(const char *source_file);
void compiler_free(CompilerContext *ctx);
bool compiler_compile(CompilerContext *ctx);
char* compiler_get_output(CompilerContext *ctx);

// Platform-specific helpers
const char* platform_to_string(Platform platform);
bool platform_supports_cpp(Platform platform);
const char* platform_get_compiler(Platform platform, bool use_cpp);

#endif /* SUB_COMPILER_H */
