/* ========================================
   SUB Language - Enhanced Error Handling System
   File: error_handler.h
   Version: 2.0
   ======================================== */

#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/* Error Severity Levels */
typedef enum {
    ERROR_INFO,
    ERROR_WARNING,
    ERROR_ERROR,
    ERROR_FATAL
} ErrorSeverity;

/* Error Categories */
typedef enum {
    ERROR_CAT_SYNTAX,           // Syntax errors during parsing
    ERROR_CAT_SEMANTIC,         // Type checking, undefined variables
    ERROR_CAT_RUNTIME,          // Runtime exceptions
    ERROR_CAT_IO,               // File I/O errors
    ERROR_CAT_MEMORY,           // Memory allocation failures
    ERROR_CAT_TYPE,             // Type mismatch errors
    ERROR_CAT_REFERENCE,        // Undefined reference errors
    ERROR_CAT_DIVISION,         // Division by zero
    ERROR_CAT_INDEX,            // Array index out of bounds
    ERROR_CAT_NULL,             // Null pointer access
    ERROR_CAT_OVERFLOW,         // Arithmetic overflow
    ERROR_CAT_CUSTOM            // User-defined exceptions
} ErrorCategory;

/* Stack Frame Information */
typedef struct StackFrame {
    char *function_name;
    char *file_name;
    int line_number;
    int column_number;
    struct StackFrame *next;
} StackFrame;

/* Error Structure */
typedef struct {
    ErrorCategory category;
    ErrorSeverity severity;
    char *message;
    char *file_name;
    int line_number;
    int column_number;
    char *code_snippet;
    StackFrame *stack_trace;
    time_t timestamp;
    char *custom_type;          // For user-defined exception types
} SubError;

/* Exception Handler Structure */
typedef struct ExceptionHandler {
    ErrorCategory catch_category;
    char *catch_type;           // Specific exception type to catch
    void (*handler_func)(SubError *error);
    struct ExceptionHandler *next;
} ExceptionHandler;

/* Error Context - Global error state */
typedef struct {
    SubError *current_error;
    ExceptionHandler *handler_stack;
    int error_count;
    int warning_count;
    FILE *log_file;
    int verbose_mode;
    int suppress_warnings;
} ErrorContext;

/* Global Error Context */
extern ErrorContext *global_error_context;

/* ========================================
   Core Error Handling Functions
   ======================================== */

// Initialize/cleanup error system
ErrorContext* error_init(void);
void error_cleanup(ErrorContext *ctx);

// Error creation and reporting
SubError* error_create(ErrorCategory category, ErrorSeverity severity,
                      const char *message, const char *file, int line, int col);
void error_report(SubError *error);
void error_free(SubError *error);

// Formatted error reporting
void error_throw(ErrorCategory category, const char *file, int line, int col,
                const char *fmt, ...);
void error_warning(const char *file, int line, int col, const char *fmt, ...);
void error_fatal(const char *file, int line, int col, const char *fmt, ...);

/* ========================================
   Stack Trace Functions
   ======================================== */

StackFrame* stack_frame_create(const char *func, const char *file, int line, int col);
void stack_frame_free(StackFrame *frame);
void stack_trace_push(SubError *error, const char *func, const char *file, int line, int col);
void stack_trace_print(StackFrame *trace);
char* stack_trace_to_string(StackFrame *trace);

/* ========================================
   Exception Handler Functions
   ======================================== */

// Register exception handlers
void exception_handler_register(ErrorCategory category, const char *type,
                               void (*handler)(SubError *));
void exception_handler_unregister(void);

// Try-catch-finally mechanism
#define TRY_BEGIN(ctx) \
    do { \
        jmp_buf __exception_buffer; \
        if (setjmp(__exception_buffer) == 0) {

#define CATCH(category, type) \
        } else if (global_error_context->current_error && \
                   global_error_context->current_error->category == category) { \
            SubError *caught_error = global_error_context->current_error;

#define CATCH_ALL \
        } else { \
            SubError *caught_error = global_error_context->current_error;

#define FINALLY \
        } \
        {

#define TRY_END \
        } \
    } while(0)

/* ========================================
   Error Utilities
   ======================================== */

// Get code snippet around error
char* error_get_code_snippet(const char *source, int line, int context_lines);

// Format error message with colors (for terminal output)
char* error_format_message(SubError *error, int use_colors);

// Error statistics
void error_print_summary(ErrorContext *ctx);
int error_has_errors(ErrorContext *ctx);
int error_has_fatals(ErrorContext *ctx);

// Logging
void error_set_log_file(ErrorContext *ctx, const char *filename);
void error_log(ErrorContext *ctx, SubError *error);

// Verbose mode control
void error_set_verbose(ErrorContext *ctx, int verbose);
void error_set_suppress_warnings(ErrorContext *ctx, int suppress);

/* ========================================
   Custom Exception Types
   ======================================== */

typedef struct CustomExceptionType {
    char *name;
    char *description;
    ErrorCategory base_category;
    struct CustomExceptionType *next;
} CustomExceptionType;

// Register custom exception types
void exception_type_register(const char *name, const char *description, ErrorCategory base);
CustomExceptionType* exception_type_find(const char *name);
void exception_type_free_all(void);

// Throw custom exceptions
void error_throw_custom(const char *type, const char *file, int line, int col,
                       const char *fmt, ...);

/* ========================================
   Error Recovery Functions
   ======================================== */

// Attempt to recover from errors during compilation
int error_can_recover(SubError *error);
void error_suggest_fix(SubError *error);
char* error_get_suggestion(SubError *error);

/* ========================================
   Macros for Convenient Error Handling
   ======================================== */

#define ERROR_CHECK(condition, category, msg) \
    if (!(condition)) { \
        error_throw(category, __FILE__, __LINE__, 0, msg); \
    }

#define ERROR_CHECK_NULL(ptr, msg) \
    ERROR_CHECK((ptr) != NULL, ERROR_CAT_NULL, msg)

#define ERROR_CHECK_BOUNDS(index, size) \
    ERROR_CHECK((index) >= 0 && (index) < (size), ERROR_CAT_INDEX, \
                "Index %d out of bounds (size: %d)", (index), (size))

#define THROW_SYNTAX_ERROR(msg, ...) \
    error_throw(ERROR_CAT_SYNTAX, __FILE__, __LINE__, 0, msg, ##__VA_ARGS__)

#define THROW_SEMANTIC_ERROR(msg, ...) \
    error_throw(ERROR_CAT_SEMANTIC, __FILE__, __LINE__, 0, msg, ##__VA_ARGS__)

#define THROW_RUNTIME_ERROR(msg, ...) \
    error_throw(ERROR_CAT_RUNTIME, __FILE__, __LINE__, 0, msg, ##__VA_ARGS__)

#define THROW_TYPE_ERROR(msg, ...) \
    error_throw(ERROR_CAT_TYPE, __FILE__, __LINE__, 0, msg, ##__VA_ARGS__)

/* ========================================
   Error Messages Constants
   ======================================== */

extern const char *ERROR_MSG_UNEXPECTED_TOKEN;
extern const char *ERROR_MSG_UNDEFINED_VARIABLE;
extern const char *ERROR_MSG_TYPE_MISMATCH;
extern const char *ERROR_MSG_DIVISION_BY_ZERO;
extern const char *ERROR_MSG_NULL_REFERENCE;
extern const char *ERROR_MSG_MEMORY_ALLOCATION;
extern const char *ERROR_MSG_FILE_NOT_FOUND;
extern const char *ERROR_MSG_INDEX_OUT_OF_BOUNDS;

#endif /* ERROR_HANDLER_H */
