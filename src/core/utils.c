/* ========================================
   SUB Language Utility Functions
   File: utils.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "codegen_cpp.h"
#include "windows_compat.h"
#include <stdarg.h>

// Utility: Print compilation error
void compile_error(const char *message, int line) {
    fprintf(stderr, "Compilation error at line %d: %s\n", line, message);
}

void compile_error_with_col(const char *message, int line, int column) {
    fprintf(stderr, "Compilation error at line %d, column %d: %s\n", line, column, message);
}

// String utilities
char* string_concat(const char *s1, const char *s2) {
    size_t len = strlen(s1) + strlen(s2) + 1;
    char *result = malloc(len);
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

// Print token (for debugging)
void print_token(Token *token) {
    const char *type_names[] = {
        "HASH", "VAR", "FUNCTION", "IF", "ELIF", "ELSE",
        "FOR", "WHILE", "RETURN", "END", "EMBED", "ENDEMBED",
        "UI", "IDENTIFIER", "NUMBER", "STRING", "OPERATOR",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "DOT", "COMMA",
        "NEWLINE", "EOF"
    };
    
    if (token->type < sizeof(type_names) / sizeof(char*)) {
        printf("Token: %-12s ", type_names[token->type]);
        if (token->value) {
            printf("Value: '%s'", token->value);
        }
        printf(" at line %d, col %d\n", token->line, token->column);
    }
}

// Print AST (for debugging)
void print_ast(ASTNode *node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    
    const char *type_names[] = {
        "PROGRAM", "VAR_DECL", "FUNCTION_DECL", "IF_STMT",
        "FOR_STMT", "WHILE_STMT", "RETURN_STMT", "ASSIGN_STMT",
        "CALL_EXPR", "BINARY_EXPR", "IDENTIFIER", "LITERAL",
        "BLOCK", "UI_COMPONENT", "EMBED_CODE"
    };
    
    if (node->type < sizeof(type_names) / sizeof(char*)) {
        printf("%s", type_names[node->type]);
        if (node->value) {
            printf(": %s", node->value);
        }
        printf("\n");
    }
    
    if (node->left) print_ast(node->left, depth + 1);
    if (node->right) print_ast(node->right, depth + 1);
    if (node->next) print_ast(node->next, depth);
}

/* ========================================
   Symbol Table Implementation
   ======================================== */

// Simple hash function
static int hash(const char *str) {
    int h = 5381;
    int c;
    while ((c = *str++)) {
        h = ((h << 5) + h) + c;
    }
    return h < 0 ? -h : h;
}

// Create a new symbol table
SymbolTable* symbol_table_create(int size) {
    if (size <= 0) size = 64;
    
    SymbolTable *table = calloc(1, sizeof(SymbolTable));
    if (!table) {
        fprintf(stderr, "Error: Failed to allocate symbol table\n");
        return NULL;
    }
    
    table->buckets = calloc(size, sizeof(SymbolTableEntry*));
    if (!table->buckets) {
        fprintf(stderr, "Error: Failed to allocate symbol table buckets\n");
        free(table);
        return NULL;
    }
    
    table->size = size;
    table->scope_level = 0;
    
    return table;
}

// Free symbol table
void symbol_table_free(SymbolTable *table) {
    if (!table) return;
    
    for (int i = 0; i < table->size; i++) {
        SymbolTableEntry *entry = table->buckets[i];
        while (entry) {
            SymbolTableEntry *next = entry->next;
            free(entry->name);
            free(entry);
            entry = next;
        }
    }
    
    free(table->buckets);
    free(table);
}

// Insert a symbol into the table
bool symbol_table_insert(SymbolTable *table, const char *name, DataType type) {
    if (!table || !name) return false;
    
    // Check if symbol already exists in current scope
    int bucket = hash(name) % table->size;
    SymbolTableEntry *entry = table->buckets[bucket];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0 && entry->scope_level == table->scope_level) {
            // Symbol already exists in current scope
            return false;
        }
        entry = entry->next;
    }
    
    // Create new entry
    SymbolTableEntry *new_entry = calloc(1, sizeof(SymbolTableEntry));
    if (!new_entry) return false;
    
    new_entry->name = strdup(name);
    if (!new_entry->name) {
        free(new_entry);
        return false;
    }
    
    new_entry->type = type;
    new_entry->scope_level = table->scope_level;
    new_entry->is_initialized = false;
    new_entry->is_constant = false;
    
    // Insert at head of bucket
    new_entry->next = table->buckets[bucket];
    table->buckets[bucket] = new_entry;
    
    return true;
}

// Lookup a symbol in the table
SymbolTableEntry* symbol_table_lookup(SymbolTable *table, const char *name) {
    if (!table || !name) return NULL;
    
    int bucket = hash(name) % table->size;
    SymbolTableEntry *entry = table->buckets[bucket];
    
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

// Enter a new scope
void symbol_table_enter_scope(SymbolTable *table) {
    if (!table) return;
    table->scope_level++;
}

// Exit current scope
void symbol_table_exit_scope(SymbolTable *table) {
    if (!table) return;
    
    int current_level = table->scope_level;
    
    for (int i = 0; i < table->size; i++) {
        SymbolTableEntry **ptr = &table->buckets[i];
        while (*ptr) {
            if ((*ptr)->scope_level == current_level) {
                SymbolTableEntry *to_remove = *ptr;
                *ptr = to_remove->next;
                free(to_remove->name);
                free(to_remove);
            } else {
                ptr = &(*ptr)->next;
            }
        }
    }
    
    table->scope_level--;
}

/* ========================================
   Class Definition Implementation
   ======================================== */

// Create a new class definition
ClassDef* class_def_create(const char *name) {
    if (!name) return NULL;
    
    ClassDef *cls = calloc(1, sizeof(ClassDef));
    if (!cls) {
        fprintf(stderr, "Error: Failed to allocate class definition\n");
        return NULL;
    }
    
    cls->name = strdup(name);
    if (!cls->name) {
        free(cls);
        return NULL;
    }
    
    cls->fields = NULL;
    cls->field_count = 0;
    cls->size = 0;
    cls->next = NULL;
    
    return cls;
}

// Add a field to a class
void class_def_add_field(ClassDef *cls, const char *field_name, DataType type) {
    if (!cls || !field_name) return;
    
    ClassField *field = calloc(1, sizeof(ClassField));
    if (!field) return;
    
    field->name = strdup(field_name);
    field->type = type;
    field->offset = cls->size;
    
    // Determine size based on type
    switch (type) {
        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_BOOL:
            cls->size += 8;
            break;
        case TYPE_STRING:
            cls->size += 8; // Pointer
            break;
        default:
            cls->size += 8;
            break;
    }
    
    // Add to fields list
    field->next = cls->fields;
    cls->fields = field;
    cls->field_count++;
}

// Lookup a class definition
ClassDef* class_def_lookup(ClassDef *classes, const char *name) {
    ClassDef *current = classes;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Free class definition
void class_def_free(ClassDef *cls) {
    if (!cls) return;
    
    // Free fields
    ClassField *field = cls->fields;
    while (field) {
        ClassField *next = field->next;
        free(field->name);
        free(field);
        field = next;
    }
    
    free(cls->name);
    free(cls);
}

/* ========================================
   Compiler Interface Implementation
   ======================================== */

// Create a compiler context
CompilerContext* compiler_create(const char *source_file) {
    CompilerContext *ctx = calloc(1, sizeof(CompilerContext));
    if (!ctx) {
        fprintf(stderr, "Error: Failed to allocate compiler context\n");
        return NULL;
    }
    
    ctx->tokens = NULL;
    ctx->token_count = 0;
    ctx->current_token = 0;
    ctx->ast = NULL;
    ctx->symbol_table = symbol_table_create(64);
    ctx->classes = NULL;
    ctx->target_platform = PLATFORM_LINUX;
    
    // Set default options
    ctx->options.optimize = false;
    ctx->options.optimization_level = 0;
    ctx->options.debug_symbols = false;
    ctx->options.minify = false;
    ctx->options.warnings_as_errors = false;
    ctx->options.verbose = false;
    ctx->options.use_cpp = false;
    ctx->options.enable_simd = false;
    ctx->options.parallel_compile = false;
    
    ctx->output_path = NULL;
    ctx->source_file = source_file ? strdup(source_file) : NULL;
    ctx->error_count = 0;
    ctx->warning_count = 0;
    
    return ctx;
}

// Free compiler context
void compiler_free(CompilerContext *ctx) {
    if (!ctx) return;
    
    if (ctx->tokens) {
        lexer_free_tokens(ctx->tokens, ctx->token_count);
    }
    
    if (ctx->ast) {
        parser_free_ast(ctx->ast);
    }
    
    if (ctx->symbol_table) {
        symbol_table_free(ctx->symbol_table);
    }
    
    // Free classes
    ClassDef *cls = ctx->classes;
    while (cls) {
        ClassDef *next = cls->next;
        class_def_free(cls);
        cls = next;
    }
    
    free(ctx->output_path);
    free(ctx->source_file);
    free(ctx);
}

// Compile a source file
bool compiler_compile(CompilerContext *ctx) {
    if (!ctx || !ctx->source_file) {
        return false;
    }
    
    // Read source file
    char *source = read_file(ctx->source_file);
    if (!source) {
        ctx->error_count++;
        return false;
    }
    
    // Lexical analysis
    ctx->tokens = lexer_tokenize(source, &ctx->token_count);
    if (!ctx->tokens) {
        ctx->error_count++;
        free(source);
        return false;
    }
    
    // Parsing
    ctx->ast = parser_parse(ctx->tokens, ctx->token_count);
    if (!ctx->ast) {
        ctx->error_count++;
        free(source);
        return false;
    }
    
    // Semantic analysis
    if (!semantic_analyze(ctx->ast)) {
        ctx->error_count++;
        free(source);
        return false;
    }
    
    // Code generation
    char *output = codegen_generate(ctx->ast, ctx->target_platform);
    if (!output) {
        ctx->error_count++;
        free(source);
        return false;
    }
    
    // Write output
    if (ctx->output_path) {
        write_file(ctx->output_path, output);
    }
    
    free(output);
    free(source);
    
    return ctx->error_count == 0;
}

// Get compiler output
char* compiler_get_output(CompilerContext *ctx) {
    (void)ctx;
    // This would return the generated code
    // For now, return NULL as the output is written to file
    return NULL;
}

/* ========================================
   StringBuilder Helpers
   ======================================== */

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} StringBuilder;

static StringBuilder* sb_create(void) {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    sb->capacity = 4096;
    sb->size = 0;
    sb->buffer = malloc(sb->capacity);
    if (!sb->buffer) {
        free(sb);
        return NULL;
    }
    sb->buffer[0] = '\0';
    return sb;
}

static void sb_append(StringBuilder *sb, const char *fmt, ...) {
    if (!sb || !fmt) return;

    va_list args;
    va_start(args, fmt);

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return;
    }

    while (sb->size + needed + 1 > sb->capacity) {
        sb->capacity *= 2;
        char *new_buffer = realloc(sb->buffer, sb->capacity);
        if (!new_buffer) {
            va_end(args);
            return;
        }
        sb->buffer = new_buffer;
    }

    vsnprintf(sb->buffer + sb->size, needed + 1, fmt, args);
    sb->size += needed;
    va_end(args);
}

static char* sb_to_string(StringBuilder *sb) {
    if (!sb) return NULL;
    char *result = strdup(sb->buffer);
    free(sb->buffer);
    free(sb);
    return result;
}

/* ========================================
   Embedded Code Generation Implementation
   ======================================== */

// Generate embedded C++ code wrapper
char* codegen_embed_cpp(const char *cpp_code) {
    if (!cpp_code) return NULL;
    
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "#ifdef __cplusplus\n");
    sb_append(sb, "extern \"C\" {\n");
    sb_append(sb, "#endif\n\n");
    sb_append(sb, "%s\n", cpp_code);
    sb_append(sb, "\n#ifdef __cplusplus\n");
    sb_append(sb, "}\n");
    sb_append(sb, "#endif\n");
    
    return sb_to_string(sb);
}

// Generate embedded C code wrapper
char* codegen_embed_c(const char *c_code) {
    if (!c_code) return NULL;
    
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    sb_append(sb, "#ifdef __cplusplus\n");
    sb_append(sb, "extern \"C\" {\n");
    sb_append(sb, "#endif\n\n");
    sb_append(sb, "%s\n", c_code);
    sb_append(sb, "\n#ifdef __cplusplus\n");
    sb_append(sb, "}\n");
    sb_append(sb, "#endif\n");
    
    return sb_to_string(sb);
}

/* ========================================
   Platform Helper Functions
   ======================================== */

// Convert platform to string
const char* platform_to_string(Platform platform) {
    switch (platform) {
        case PLATFORM_ANDROID: return "android";
        case PLATFORM_IOS: return "ios";
        case PLATFORM_WINDOWS: return "windows";
        case PLATFORM_MACOS: return "macos";
        case PLATFORM_LINUX: return "linux";
        case PLATFORM_WEB: return "web";
        case PLATFORM_WASM: return "wasm";
        default: return "unknown";
    }
}

// Check if platform supports C++
bool platform_supports_cpp(Platform platform) {
    switch (platform) {
        case PLATFORM_WINDOWS:
        case PLATFORM_MACOS:
        case PLATFORM_LINUX:
            return true;
        case PLATFORM_ANDROID:
        case PLATFORM_IOS:
        case PLATFORM_WEB:
        case PLATFORM_WASM:
        default:
            return false;
    }
}

// Get compiler for platform
const char* platform_get_compiler(Platform platform, bool use_cpp) {
    switch (platform) {
        case PLATFORM_ANDROID:
            return use_cpp ? "clang++" : "clang";
        case PLATFORM_IOS:
            return use_cpp ? "clang++" : "clang";
        case PLATFORM_WINDOWS:
            return use_cpp ? "cl" : "cl";
        case PLATFORM_MACOS:
            return use_cpp ? "clang++" : "clang";
        case PLATFORM_LINUX:
            return use_cpp ? "g++" : "gcc";
        case PLATFORM_WEB:
        case PLATFORM_WASM:
        default:
            return "gcc";
    }
}

/* ========================================
   C++ CodeGen Helper Functions
   ======================================== */

// Get default C++ codegen options
void codegen_cpp_get_default_options(CPPVersion version, CPPCodegenOptions *options) {
    if (!options) return;
    
    options->version = version;
    options->use_std_string = true;
    options->use_auto = (version >= CPP_VER_14);
    options->use_concepts = (version >= CPP_VER_20);
    options->use_modules = false;
    options->use_range_based_for = true;
    options->use_constexpr = (version >= CPP_VER_14);
}

// Convert C++ version to string
const char* codegen_cpp_version_to_string(CPPVersion version) {
    switch (version) {
        case CPP_VER_11: return "C++11";
        case CPP_VER_14: return "C++14";
        case CPP_VER_17: return "C++17";
        case CPP_VER_20: return "C++20";
        case CPP_VER_23: return "C++23";
        default: return "C++11";
    }
}

// Parse C++ version string
CPPVersion codegen_cpp_parse_version(const char *version_str) {
    if (!version_str) return CPP_VER_11;
    
    if (strcmp(version_str, "11") == 0 || strcmp(version_str, "C++11") == 0) return CPP_VER_11;
    if (strcmp(version_str, "14") == 0 || strcmp(version_str, "C++14") == 0) return CPP_VER_14;
    if (strcmp(version_str, "17") == 0 || strcmp(version_str, "C++17") == 0) return CPP_VER_17;
    if (strcmp(version_str, "20") == 0 || strcmp(version_str, "C++20") == 0) return CPP_VER_20;
    if (strcmp(version_str, "23") == 0 || strcmp(version_str, "C++23") == 0) return CPP_VER_23;
    
    return CPP_VER_11;
}
