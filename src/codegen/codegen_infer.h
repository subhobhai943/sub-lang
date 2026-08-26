/* ========================================
   SUB Language - Shared Signature Inference
   ----------------------------------------
   SUB source is dynamically typed, but half the backends (C, C++, Rust,
   Java, Go, Swift, Kotlin) must emit a concrete return type and parameter
   type for every function. Each backend used to guess on its own, which is
   why the statically typed targets emitted code that did not compile:
   functions came out `void` while their bodies returned a value, and
   parameters came out `Object`/`interface{}`/`Any`/`long long` regardless of
   what callers actually passed.

   infer_function_signatures() runs once over the program AST and writes the
   inferred types back onto the nodes:

     - AST_FUNCTION_DECL->data_type          becomes the return type
     - AST_FUNCTION_DECL->children[i]->data_type  becomes parameter i's type

   Every backend then just reads data_type, so a fix here fixes all of them.
   Explicit annotations already present in the source are never overwritten.
   ======================================== */

#ifndef CODEGEN_INFER_H
#define CODEGEN_INFER_H

#include "sub_compiler.h"
#include <string.h>

/* Annotate every function declaration reachable from `program` with an
   inferred return type and parameter types. Safe to call more than once and
   safe to call with NULL. */
void infer_function_signatures(ASTNode *program);

/* Best-effort static type of an arbitrary expression node.
   Returns TYPE_UNKNOWN when nothing can be determined. */
DataType infer_expr_type(ASTNode *expr);

/* Return type a function body implies, ignoring any explicit annotation.
   TYPE_VOID when the body never returns a value. */
DataType infer_return_type(ASTNode *body);

/* True when an exponent is written as a negative value, e.g. `2 ** -1`.
   Such a power yields a fraction, so backends must use the floating-point
   form - the interpreter returns 0.5, and an integer power would give 0. */
int exponent_is_negative(ASTNode *expr);

/* True when `expr` is the literal null (or its deprecated spelling nil). */
int expr_is_null_literal(ASTNode *expr);

/* Inferred type of parameter `index` of user function `fn_name`, or
   TYPE_UNKNOWN when the function or parameter is not known. Backends use it
   to emit an argument in the form the callee's signature expects. */
DataType param_type_of(const char *fn_name, int index);

/* True when `fn_name` names a user function that can return null as well as
   a real value. Backends for statically typed targets use this to pick a
   representation that has room for "no value". */
int function_is_nullable(const char *fn_name);

/* `print`, `println` and `show` all mean "write a line to stdout" in SUB.
   Backends used to test only for print/show, so `println(...)` fell through
   and was emitted as a call to a function that does not exist in the target
   language. Every backend shares this predicate now. */
static inline int is_print_builtin(const char *name) {
    if (!name) return 0;
    return strcmp(name, "print")   == 0 ||
           strcmp(name, "println") == 0 ||
           strcmp(name, "show")    == 0;
}

#endif /* CODEGEN_INFER_H */
