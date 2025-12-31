/* ========================================
   SUB Language Utility Functions (Compiler)
   File: error_handler.c
   ======================================== */

#include "sub_compiler.h"

// Utility: Print compilation error
void compile_error(const char *message, int line) {
    fprintf(stderr, "Compilation error at line %d: %s\n", line, message);
}

void compile_error_with_col(const char *message, int line, int column) {
    fprintf(stderr, "Compilation error at line %d, column %d: %s\n", line, column, message);
}
