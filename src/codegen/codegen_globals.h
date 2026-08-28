/* Which top-level variables a function body can reach, so that ten backends
   agree on what a global is instead of each deciding for itself.

   In the interpreter a function closes over the global environment, so a
   top-level `let` is readable and assignable from inside any function:

       let calls = 0
       fn bump() { calls = calls + 1 }

   Every compiled backend puts top-level code inside a main(), which leaves
   that `let` as a local of main() that `bump` cannot name. The fix is the
   same everywhere: emit the *declaration* at whatever file or module scope
   the target has, and leave the *initializer* where it was written, as an
   assignment in main.

   Splitting it that way matters. A top-level `let` may be initialized by
   anything -- a call, an array literal, arithmetic over an earlier global --
   and C, Rust and Java all require a file-scope initializer to be a constant
   expression. Keeping the initializer in main also keeps the side effects of
   one in their original order relative to the rest of the program. */
#ifndef SUB_CODEGEN_GLOBALS_H
#define SUB_CODEGEN_GLOBALS_H

#include "../include/sub_compiler.h"

#define GLOBALS_MAX 256

typedef struct {
    ASTNode *decls[GLOBALS_MAX];   /* the AST_VAR_DECL / AST_CONST_DECL nodes */
    int count;
} Globals;

/* Collect the top-level variable declarations of a program, in source order.
   A declaration nested inside a function, a loop or a block is a local and
   is not collected. */
void globals_collect(Globals *g, ASTNode *program);

/* Whether `name` is one of them. Backends ask this at an assignment or a
   read to decide whether it refers to a global. */
int globals_has(const Globals *g, const char *name);

/* Whether this is one of the collected declaration nodes itself, rather than
   some other declaration that happens to share a name. A backend emitting a
   top-level statement asks this to tell "the global's initializer, which is
   now an assignment" apart from "a local of a top-level loop that shadows
   the global, which is still a declaration". */
int globals_is_decl(const Globals *g, ASTNode *node);

/* Whether decls[i] is the first declaration of its name. A program may
   declare a top-level name twice; the interpreter rebinds it in the same
   global environment, so both are the same variable. The pass that writes
   one declaration per global emits only the first, and every one of them is
   a globals_is_decl() -- so the second becomes a plain assignment. */
int globals_is_first(const Globals *g, int i);

/* The globals this function assigns to, written into `out` in the order
   found and deduplicated; returns how many. Python needs the list, to emit a
   `global` statement, because an assignment in a function otherwise makes a
   local. `fn` is the AST_FUNCTION_DECL, not its body: a parameter of the
   same name shadows the global and must not appear here.

   A name the function also declares with its own `let` is excluded too. SUB
   gives no way to mean the global before such a declaration and the local
   after it, since the interpreter creates the local when the call's scope is
   entered -- so the whole body refers to one or the other, never both. */
int fn_assigned_globals(const Globals *g, ASTNode *fn,
                        const char *out[], int max);

/* The globals this function shadows with something of its own -- a
   parameter, or a `let` in its body. A backend that renames globals in place
   (Ruby's `$name`) needs this so that a local of the same name is left
   alone. Returns how many were written to `out`. */
int fn_shadowed_globals(const Globals *g, ASTNode *fn,
                        const char *out[], int max);

/* Whether this function reads or assigns any global. Rust needs to know
   before emitting a body, because a global there is a `static mut` and every
   mention of one has to sit inside `unsafe`. */
int fn_uses_global(const Globals *g, ASTNode *fn);

#endif
