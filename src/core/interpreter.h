/* ============================================================
   SUB Language Interpreter - Runtime Header
   Professional-grade tree-walking interpreter with full
   operator support, control flow, try/catch, arrays, objects,
   string/array methods, and comprehensive builtins.
   ============================================================ */

#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "sub_compiler.h"

/* ---------- Forward Declarations ---------- */

struct SubVal;
typedef struct SubVal SubVal;

/* ---------- Dynamic Array Value ---------- */

typedef struct SubArray {
    SubVal   *items;
    int       count;
    int       capacity;
} SubArray;

/* ---------- Dynamic Object Value ---------- */

typedef struct SubObject {
    char    **keys;
    SubVal   *values;
    int       count;
    int       capacity;
} SubObject;

/* ---------- Runtime Value ---------- */

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_NULL,
    VAL_FUNC,
    VAL_ARRAY,
    VAL_OBJECT
} ValType;

struct SubVal {
    ValType type;
    union {
        long long   iv;
        double      fv;
        char       *sv;
        int         bv;
        ASTNode    *fn;     /* function declaration AST node   */
        SubArray   *arr;
        SubObject  *obj;
    };
};

/* ---------- Environment (Variable Scope) ---------- */

typedef struct EnvEntry {
    char           *name;
    SubVal          val;
    struct EnvEntry *next;
} EnvEntry;

typedef struct Env {
    EnvEntry    *vars;
    struct Env  *parent;
    int          returning;   /* return flag  - propagates up   */
    int          breaking;    /* break   flag - consumed by loop */
    int          continuing;  /* continue flag - consumed by loop */
    SubVal       ret_val;
} Env;

/* ---------- Environment Operations ---------- */

Env   *env_new(Env *parent);
void   env_free(Env *env);
SubVal env_get(Env *env, const char *name);
void   env_set(Env *env, const char *name, SubVal val);
void   env_define(Env *env, const char *name, SubVal val);

/* ---------- Interpreter Entry Points ---------- */

SubVal eval(ASTNode *node, Env *env);
int    interpret_file(const char *path);
int    interpret_source(const char *source, Env *env);

/* ---------- Runtime Error Policy ---------- */

/* A runtime error aborts the program with exit code 70 by default. A REPL
   should enable REPL mode instead, so that an error abandons only the line
   being evaluated and the session (and everything defined in it) survives.
   Call interp_clear_abort() before evaluating each REPL line. */
void interp_set_repl_mode(int enabled);
void interp_clear_abort(void);
int  interp_aborted(void);

#endif /* INTERPRETER_H */
