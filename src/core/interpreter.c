/* ============================================================
   SUB Language Interpreter - Runtime Implementation
   Professional-grade tree-walking interpreter.

   Supported features:
     - Full operator suite (+, -, *, /, %, **, &, |, ^, ~, <<, >>)
     - Compound assignment (+=, -=, *=, /=, %=)
     - Pre/post increment and decrement (++, --)
     - Unary plus (+) and bitwise NOT (~)
     - Break and continue with proper loop propagation
     - For-in iteration over strings and arrays
     - Do-while loops
     - Try / catch / throw exception handling
     - Array and object literals
     - Member access (obj.prop, obj.method())
     - Ternary expressions (cond ? a : b)
     - Float modulo via fmod()
     - 22+ builtin functions
     - String methods: .length, .upper(), .lower(), .substring(),
       .split(), .contains(), .replace(), .trim(), .char_at()
     - Array methods: .length, .push(), .pop(), .join()
   ============================================================ */

#define _GNU_SOURCE
#include "interpreter.h"
#include "module.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#ifndef _WIN32
#include <sys/resource.h>
#endif

/* ================================================================
   Global State
   ================================================================ */

static SubVal NULL_VAL = {VAL_NULL, {.iv = 0}};

/* Read an integer out of a value whatever numeric form it arrived in.

   iv and fv share a union, so reading iv directly from a float value
   reinterprets the double's bit pattern as an integer. That is what made
   range(3.0) iterate ten times instead of three, and it is the same fault
   that made int(3.9) return 4615964438073389875. Every place that needs an
   integer out of a user-supplied value goes through this. */
static long long val_as_int(SubVal v) {
    switch (v.type) {
        case VAL_INT:   return v.iv;
        case VAL_FLOAT: return (long long)v.fv;
        case VAL_BOOL:  return v.bv ? 1 : 0;
        default:        return 0;
    }
}


/* Exception handling state (checked at every eval entry) */
static SubVal g_exception    = {VAL_NULL, {.iv = 0}};
static int    g_exception_thrown = 0;

/* ---- Runtime error policy ----
   Continuing after a runtime error with a null placeholder is the worst of
   both worlds: the user sees a warning scroll past and still gets wrong
   results, from a process that exits 0. So an error is fatal (exit 70).

   A REPL is the one place where that is too harsh — killing the process over
   a typo would discard everything defined in the session — so it aborts just
   the line under evaluation. g_runtime_aborted unwinds the evaluator the same
   way the return/break flags do; it is deliberately separate from the
   try/catch exception state so that runtime errors stay uncatchable in both
   modes rather than behaving differently depending on how you ran the code. */
static int g_repl_mode       = 0;
static int g_runtime_aborted = 0;

void interp_set_repl_mode(int enabled) { g_repl_mode = enabled ? 1 : 0; }
void interp_clear_abort(void)          { g_runtime_aborted = 0; }
int  interp_aborted(void)              { return g_runtime_aborted; }

static void runtime_error(const char *fmt, ...) {
    /* Once an abort is pending every enclosing eval() is bailing out, so any
       follow-on complaint is noise about a value the error itself created. */
    if (g_runtime_aborted) return;

    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "RuntimeError: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);

    if (!g_repl_mode) exit(70);
    g_runtime_aborted = 1;
}

/* ---- Recursion guard ----
   A SUB-level call nests many C-level eval() frames, so runaway recursion
   overflows the C stack and the OS kills the process with no diagnostic.
   We measure how much stack the evaluator has actually consumed rather than
   counting calls: a fixed call limit can't be right on both an 8 MiB Linux
   stack and a 1 MiB Windows one, and it silently stops guarding whenever a
   frame grows. */
/* The base is kept as an integer, not a pointer. It is only ever used to
   subtract one stack address from another and is never dereferenced, and
   storing it as a char* made newer gcc report -Wdangling-pointer for
   holding the address of a local past its lifetime - which would fail the
   -Werror build the moment a runner picked up that compiler. */
static uintptr_t g_stack_base   = 0;
static size_t    g_stack_budget = 0;

static void interp_stack_guard_init(void) {
    char probe;
    g_stack_base = (uintptr_t)&probe;

    size_t limit = (size_t)1 << 20;  /* Windows default thread stack: 1 MiB */
#ifndef _WIN32
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 &&
        rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 0)
        limit = (size_t)rl.rlim_cur;
    else
        limit = (size_t)8 << 20;
#endif
    /* Spend at most half the stack so the error is reported well before the
       real overflow, leaving room for the frames below us. */
    g_stack_budget = limit / 2;
}

static void interp_check_stack(void) {
    char probe;
    uintptr_t here = (uintptr_t)&probe;
    if (!g_stack_base) interp_stack_guard_init();
    size_t used = (size_t)(g_stack_base > here
                           ? g_stack_base - here
                           : here - g_stack_base);
    if (used > g_stack_budget)
        runtime_error("max recursion depth exceeded");
}

/* ================================================================
   Forward Declarations
   ================================================================ */

static SubVal eval_block(ASTNode *node, Env *env);
static void   val_free(SubVal v);
static void   print_val(SubVal v);
static SubVal val_to_str(SubVal v);
static const char *type_name(ValType t);

/* ================================================================
   Value Constructors
   ================================================================ */

static SubVal make_int(long long v)    { SubVal r = {VAL_INT, .iv = v};   return r; }
static SubVal make_float(double v)    { SubVal r = {VAL_FLOAT, .fv = v}; return r; }
static SubVal make_bool(int v)        { SubVal r = {VAL_BOOL, .bv = v ? 1 : 0}; return r; }

static SubVal make_str(const char *s) {
    SubVal r = {VAL_STRING, {.sv = NULL}};
    r.sv = strdup(s ? s : "");
    return r;
}

static SubVal make_array_val(void) {
    SubVal r = {VAL_ARRAY, {.arr = NULL}};
    r.arr = calloc(1, sizeof(SubArray));
    return r;
}

static SubVal make_object_val(void) {
    SubVal r = {VAL_OBJECT, {.obj = NULL}};
    r.obj = calloc(1, sizeof(SubObject));
    return r;
}

/* ================================================================
   Value Lifecycle
   ================================================================ */

static void val_free(SubVal v) {
    switch (v.type) {
    case VAL_STRING:
        free(v.sv);
        break;
    case VAL_ARRAY:
        if (v.arr) {
            for (int i = 0; i < v.arr->count; i++)
                val_free(v.arr->items[i]);
            free(v.arr->items);
            free(v.arr);
        }
        break;
    case VAL_OBJECT:
        if (v.obj) {
            for (int i = 0; i < v.obj->count; i++) {
                free(v.obj->keys[i]);
                val_free(v.obj->values[i]);
            }
            free(v.obj->keys);
            free(v.obj->values);
            free(v.obj);
        }
        break;
    default:
        break;
    }
}

/* Deep-copy a SubVal so that strings/arrays/objects are independently owned. */
/* Copy a value for storage in an environment slot.
   Arrays and objects used to be copied by sharing the pointer, on the theory
   that "the original owner is responsible for freeing". Nothing tracked who
   the original owner was, so two bindings to one array meant two calls to
   val_free on the same block:

       let a = [1, 2]
       let b = a          # b shares a's SubArray
       push(b, 3)
       println(a)         # segfault at scope exit: double free

   Copying properly makes a SUB array a value: assigning one copies it, and
   push/pop mutate the binding they are given rather than everything that
   ever shared it. env_get still hands out the live value, so push(a, x)
   goes on modifying a in place. */
static SubVal val_copy(SubVal v) {
    SubVal r = v;
    if (v.type == VAL_STRING && v.sv) {
        r.sv = strdup(v.sv);
        return r;
    }
    if (v.type == VAL_ARRAY && v.arr) {
        SubArray *c = calloc(1, sizeof(SubArray));
        if (!c) { r.arr = NULL; return r; }
        c->count = c->capacity = v.arr->count;
        if (c->capacity > 0) {
            c->items = malloc((size_t)c->capacity * sizeof(SubVal));
            if (!c->items) { c->count = c->capacity = 0; }
            else for (int i = 0; i < v.arr->count; i++)
                c->items[i] = val_copy(v.arr->items[i]);
        }
        r.arr = c;
        return r;
    }
    if (v.type == VAL_OBJECT && v.obj) {
        SubObject *c = calloc(1, sizeof(SubObject));
        if (!c) { r.obj = NULL; return r; }
        c->count = c->capacity = v.obj->count;
        if (c->capacity > 0) {
            c->keys   = calloc((size_t)c->capacity, sizeof(char *));
            c->values = malloc((size_t)c->capacity * sizeof(SubVal));
            if (!c->keys || !c->values) { c->count = c->capacity = 0; }
            else for (int i = 0; i < v.obj->count; i++) {
                c->keys[i]   = v.obj->keys[i] ? strdup(v.obj->keys[i]) : NULL;
                c->values[i] = val_copy(v.obj->values[i]);
            }
        }
        r.obj = c;
        return r;
    }
    return r;
}

/* Whether evaluating this expression yields a value some binding still owns,
   rather than a freshly built one. Only these three reach into a binding:
   a name, an element of one, and a field of one. Everything else -- a
   literal, an operator, a call -- constructs its result. */
static int expr_borrows_binding(ASTNode *n) {
    if (!n) return 0;
    switch (n->type) {
    case AST_IDENTIFIER:
    case AST_ARRAY_ACCESS:
    case AST_MEMBER_ACCESS:
        return 1;
    default:
        return 0;
    }
}

/* ================================================================
   Array Helpers
   ================================================================ */

static void array_push(SubArray *arr, SubVal item) {
    if (!arr) return;
    if (arr->count >= arr->capacity) {
        int new_cap = arr->capacity ? arr->capacity * 2 : 8;
        arr->items = realloc(arr->items, (size_t)new_cap * sizeof(SubVal));
        arr->capacity = new_cap;
    }
    arr->items[arr->count++] = item;
}

static SubVal array_pop(SubArray *arr) {
    if (!arr || arr->count <= 0) {
        runtime_error("pop from empty array");
        return NULL_VAL;
    }
    return arr->items[--arr->count];
}

/* ================================================================
   Object Helpers
   ================================================================ */

static void object_set(SubObject *obj, const char *key, SubVal val) {
    if (!obj) return;
    for (int i = 0; i < obj->count; i++) {
        if (strcmp(obj->keys[i], key) == 0) {
            val_free(obj->values[i]);
            obj->values[i] = val;
            return;
        }
    }
    if (obj->count >= obj->capacity) {
        int new_cap = obj->capacity ? obj->capacity * 2 : 8;
        obj->keys   = realloc(obj->keys,   (size_t)new_cap * sizeof(char *));
        obj->values = realloc(obj->values,  (size_t)new_cap * sizeof(SubVal));
        obj->capacity = new_cap;
    }
    obj->keys[obj->count]   = strdup(key);
    obj->values[obj->count] = val;
    obj->count++;
}

static SubVal object_get(SubObject *obj, const char *key) {
    if (!obj) {
        runtime_error("cannot access property '%s' of null", key);
        return NULL_VAL;
    }
    for (int i = 0; i < obj->count; i++) {
        if (strcmp(obj->keys[i], key) == 0)
            return obj->values[i];
    }
    runtime_error("undefined object property '%s'", key);
    return NULL_VAL;
}

/* ================================================================
   Type Utilities
   ================================================================ */

static const char *type_name(ValType t) {
    switch (t) {
    case VAL_INT:    return "int";
    case VAL_FLOAT:  return "float";
    case VAL_STRING: return "string";
    case VAL_BOOL:   return "bool";
    case VAL_NULL:   return "null";
    case VAL_FUNC:   return "function";
    case VAL_ARRAY:  return "array";
    case VAL_OBJECT: return "object";
    default:         return "unknown";
    }
}

static int values_equal(SubVal a, SubVal b) {
    if (a.type != b.type) {
        /* Allow int/float comparison */
        if ((a.type == VAL_INT && b.type == VAL_FLOAT) ||
            (a.type == VAL_FLOAT && b.type == VAL_INT))
            return (a.type == VAL_INT ? (double)a.iv : a.fv) ==
                   (b.type == VAL_INT ? (double)b.iv : b.fv);
        return 0;
    }
    switch (a.type) {
    case VAL_INT:    return a.iv == b.iv;
    case VAL_FLOAT:  return a.fv == b.fv;
    case VAL_BOOL:   return a.bv == b.bv;
    case VAL_STRING: return a.sv && b.sv && strcmp(a.sv, b.sv) == 0;
    case VAL_NULL:   return 1;
    default:         return 0;
    }
}

static int is_truthy(SubVal v) {
    switch (v.type) {
    case VAL_BOOL:   return v.bv;
    case VAL_INT:    return v.iv != 0;
    case VAL_FLOAT:  return v.fv != 0.0;
    case VAL_STRING: return v.sv && v.sv[0];
    case VAL_ARRAY:  return v.arr != NULL && v.arr->count > 0;
    case VAL_OBJECT: return v.obj != NULL && v.obj->count > 0;
    default:         return 0;
    }
}

static SubVal to_number(SubVal v) {
    switch (v.type) {
    case VAL_INT:   return v;
    case VAL_FLOAT: return v;
    case VAL_BOOL:  return make_int((long long)v.bv);
    case VAL_STRING: {
        if (!v.sv || !v.sv[0]) return make_int(0);
        char *end;
        long long i = strtoll(v.sv, &end, 10);
        if (*end == '\0') return make_int(i);
        double f = strtod(v.sv, &end);
        if (*end == '\0') return make_float(f);
        return make_int(0);
    }
    default: return make_int(0);
    }
}

/* ================================================================
   Value to String Conversion
   ================================================================ */

static SubVal val_to_str(SubVal v) {
    char buf[512];
    switch (v.type) {
    case VAL_INT:
        snprintf(buf, sizeof(buf), "%lld", v.iv);
        return make_str(buf);
    case VAL_FLOAT:
        snprintf(buf, sizeof(buf), "%g", v.fv);
        return make_str(buf);
    case VAL_BOOL:
        return make_str(v.bv ? "true" : "false");
    case VAL_STRING:
        return make_str(v.sv ? v.sv : "");
    case VAL_NULL:
        return make_str("null");
    case VAL_FUNC:
        return make_str("<function>");
    case VAL_ARRAY: {
        size_t cap = 256;
        char  *str = malloc(cap);
        size_t len = 0;
        str[len++] = '[';
        for (int i = 0; i < (v.arr ? v.arr->count : 0); i++) {
            SubVal s = val_to_str(v.arr->items[i]);
            const char *item = s.sv ? s.sv : "null";
            size_t ilen = strlen(item);
            while (len + ilen + 4 > cap) { cap *= 2; str = realloc(str, cap); }
            if (i > 0) { str[len++] = ','; str[len++] = ' '; }
            memcpy(str + len, item, ilen);
            len += ilen;
            val_free(s);
        }
        str[len++] = ']';
        str[len]   = '\0';
        SubVal r = {VAL_STRING, .sv = str};
        return r;
    }
    case VAL_OBJECT: {
        size_t cap = 256;
        char  *str = malloc(cap);
        size_t len = 0;
        str[len++] = '{';
        for (int i = 0; i < (v.obj ? v.obj->count : 0); i++) {
            const char *key = v.obj->keys[i];
            SubVal s = val_to_str(v.obj->values[i]);
            const char *vs = s.sv ? s.sv : "null";
            size_t needed = strlen(key) + strlen(vs) + 16;
            while (len + needed > cap) { cap *= 2; str = realloc(str, cap); }
            if (i > 0) { str[len++] = ','; str[len++] = ' '; }
            len += (size_t)snprintf(str + len, cap - len, "%s: %s", key, vs);
            val_free(s);
        }
        str[len++] = '}';
        str[len]   = '\0';
        SubVal r = {VAL_STRING, .sv = str};
        return r;
    }
    default:
        return make_str("null");
    }
}

/* ================================================================
   Value Printing
   ================================================================ */

static void print_val(SubVal v) {
    switch (v.type) {
    case VAL_INT:    printf("%lld", v.iv); break;
    case VAL_FLOAT:  printf("%g",   v.fv); break;
    case VAL_BOOL:   printf("%s",   v.bv ? "true" : "false"); break;
    case VAL_STRING: printf("%s",   v.sv ? v.sv : ""); break;
    case VAL_NULL:   printf("null"); break;
    case VAL_FUNC:   printf("<function>"); break;
    case VAL_ARRAY: {
        printf("[");
        for (int i = 0; i < (v.arr ? v.arr->count : 0); i++) {
            if (i > 0) printf(", ");
            print_val(v.arr->items[i]);
        }
        printf("]");
        break;
    }
    case VAL_OBJECT: {
        printf("{");
        for (int i = 0; i < (v.obj ? v.obj->count : 0); i++) {
            if (i > 0) printf(", ");
            printf("%s: ", v.obj->keys[i]);
            print_val(v.obj->values[i]);
        }
        printf("}");
        break;
    }
    }
}

/* ================================================================
   Environment Management
   ================================================================ */

Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->parent = parent;
    return e;
}

void env_free(Env *env) {
    if (!env) return;
    EnvEntry *e = env->vars;
    while (e) {
        EnvEntry *n = e->next;
        free(e->name);
        val_free(e->val);
        free(e);
        e = n;
    }
    free(env);
}

SubVal env_get(Env *env, const char *name) {
    for (Env *s = env; s; s = s->parent)
        for (EnvEntry *e = s->vars; e; e = e->next)
            if (strcmp(e->name, name) == 0)
                return e->val;
    runtime_error("undefined variable '%s'", name);
    return NULL_VAL;
}

void env_define(Env *env, const char *name, SubVal val) {
    if (!env) return;
    EnvEntry *e = calloc(1, sizeof(EnvEntry));
    e->name = strdup(name);
    e->val  = val_copy(val);      /* see val_copy: a value, not a share */
    e->next = env->vars;
    env->vars = e;
}

void env_set(Env *env, const char *name, SubVal val) {
    for (Env *s = env; s; s = s->parent)
        for (EnvEntry *e = s->vars; e; e = e->next)
            if (strcmp(e->name, name) == 0) {
                val_free(e->val);
                e->val = val_copy(val);  /* see val_copy */
                return;
            }
    /* Variable not found - create in current scope */
    env_define(env, name, val);
}

/* ================================================================
   Binary Expression Evaluation
   ================================================================ */

static SubVal eval_binary(ASTNode *node, Env *env) {
    const char *op = node->value;

    /* --- Logical short-circuit (eval right lazily) --- */
    if (strcmp(op, "&&") == 0) {
        SubVal L = eval(node->left, env);
        if (!is_truthy(L)) return make_bool(0);
        return make_bool(is_truthy(eval(node->right, env)));
    }
    if (strcmp(op, "||") == 0) {
        SubVal L = eval(node->left, env);
        if (is_truthy(L)) return make_bool(1);
        return make_bool(is_truthy(eval(node->right, env)));
    }

    SubVal L = eval(node->left, env);
    SubVal R = eval(node->right, env);

    /* --- String concatenation (+) --- */
    if (strcmp(op, "+") == 0 && (L.type == VAL_STRING || R.type == VAL_STRING)) {
        SubVal ls = val_to_str(L);
        SubVal rs = val_to_str(R);
        size_t n  = strlen(ls.sv) + strlen(rs.sv) + 1;
        char *buf = malloc(n);
        if (buf) snprintf(buf, n, "%s%s", ls.sv, rs.sv);
        else buf = strdup("");
        val_free(ls);
        val_free(rs);
        SubVal r = {VAL_STRING, .sv = buf};
        return r;
    }

    /* --- String comparison --- */
    if (L.type == VAL_STRING || R.type == VAL_STRING) {
        if (L.type != VAL_STRING || R.type != VAL_STRING) {
            if (strcmp(op, "==") == 0) return make_bool(0);
            if (strcmp(op, "!=") == 0) return make_bool(1);
            runtime_error("cannot compare string with %s using '%s'",
                    type_name(L.type == VAL_STRING ? R.type : L.type), op);
            return NULL_VAL;
        }
        const char *ls = L.sv ? L.sv : "";
        const char *rs = R.sv ? R.sv : "";
        int cmp = strcmp(ls, rs);
        if (strcmp(op, "==") == 0) return make_bool(cmp == 0);
        if (strcmp(op, "!=") == 0) return make_bool(cmp != 0);
        if (strcmp(op, "<")  == 0) return make_bool(cmp <  0);
        if (strcmp(op, "<=") == 0) return make_bool(cmp <= 0);
        if (strcmp(op, ">")  == 0) return make_bool(cmp >  0);
        if (strcmp(op, ">=") == 0) return make_bool(cmp >= 0);
        runtime_error("operator '%s' not supported for strings", op);
        return NULL_VAL;
    }

    /* --- Same-type non-numeric comparison --- */
    if (L.type == R.type) {
        switch (L.type) {
        case VAL_NULL:
            if (strcmp(op, "==") == 0) return make_bool(1);
            if (strcmp(op, "!=") == 0) return make_bool(0);
            runtime_error("cannot use '%s' on null", op);
            return NULL_VAL;
        case VAL_BOOL:
            if (strcmp(op, "==") == 0) return make_bool(L.bv == R.bv);
            if (strcmp(op, "!=") == 0) return make_bool(L.bv != R.bv);
            break; /* fall through to numeric */
        case VAL_FUNC:
            if (strcmp(op, "==") == 0) return make_bool(L.fn == R.fn);
            if (strcmp(op, "!=") == 0) return make_bool(L.fn != R.fn);
            runtime_error("cannot use '%s' on functions", op);
            return NULL_VAL;
        case VAL_ARRAY:
            if (strcmp(op, "==") == 0) return make_bool(L.arr == R.arr);
            if (strcmp(op, "!=") == 0) return make_bool(L.arr != R.arr);
            runtime_error("cannot use '%s' on arrays", op);
            return NULL_VAL;
        case VAL_OBJECT:
            if (strcmp(op, "==") == 0) return make_bool(L.obj == R.obj);
            if (strcmp(op, "!=") == 0) return make_bool(L.obj != R.obj);
            runtime_error("cannot use '%s' on objects", op);
            return NULL_VAL;
        default:
            break;
        }
    }

    /* --- Mixed type check --- */
    int both_numeric = (L.type == VAL_INT || L.type == VAL_FLOAT) &&
                       (R.type == VAL_INT || R.type == VAL_FLOAT);
    if (!both_numeric) {
        if (strcmp(op, "==") == 0) return make_bool(0);
        if (strcmp(op, "!=") == 0) return make_bool(1);
        runtime_error("cannot use '%s' on %s and %s",
                op, type_name(L.type), type_name(R.type));
        return NULL_VAL;
    }

    /* --- Numeric operations --- */
    double a = (L.type == VAL_FLOAT) ? L.fv : (double)L.iv;
    double b = (R.type == VAL_FLOAT) ? R.fv : (double)R.iv;
    int use_float = (L.type == VAL_FLOAT || R.type == VAL_FLOAT);

    /* Arithmetic */
    if (strcmp(op, "+") == 0)  return use_float ? make_float(a + b)    : make_int((long long)(a + b));
    if (strcmp(op, "-") == 0)  return use_float ? make_float(a - b)    : make_int((long long)(a - b));
    if (strcmp(op, "*") == 0)  return use_float ? make_float(a * b)    : make_int((long long)(a * b));

    if (strcmp(op, "/") == 0) {
        if (b == 0.0) { runtime_error("division by zero"); return NULL_VAL; }
        return use_float ? make_float(a / b) : make_int((long long)(a / b));
    }

    if (strcmp(op, "%") == 0) {
        if (b == 0.0) { runtime_error("modulo by zero"); return NULL_VAL; }
        return use_float ? make_float(fmod(a, b)) : make_int((long long)a % (long long)b);
    }

    if (strcmp(op, "**") == 0) {
        double r = pow(a, b);
        /* A negative exponent produces a fraction, so truncating it to an
           integer turns 2 ** -1 into 0 rather than 0.5. Two integers still
           give an integer for non-negative exponents (2 ** 10 is 1024). */
        if (!use_float && b < 0) return make_float(r);
        return use_float ? make_float(r) : make_int((long long)r);
    }

    /* Comparison */
    if (strcmp(op, "==") == 0) return make_bool(a == b);
    if (strcmp(op, "!=") == 0) return make_bool(a != b);
    if (strcmp(op, "<")  == 0) return make_bool(a <  b);
    if (strcmp(op, "<=") == 0) return make_bool(a <= b);
    if (strcmp(op, ">")  == 0) return make_bool(a >  b);
    if (strcmp(op, ">=") == 0) return make_bool(a >= b);

    /* Bitwise (integers only) */
    long long ia = (long long)a, ib = (long long)b;
    if (strcmp(op, "&")  == 0) return make_int(ia & ib);
    if (strcmp(op, "|")  == 0) return make_int(ia | ib);
    if (strcmp(op, "^")  == 0) return make_int(ia ^ ib);
    if (strcmp(op, "<<") == 0) {
        if (ib < 0 || ib >= (long long)(sizeof(long long) * 8)) {
            runtime_error("left shift by %lld out of range", ib);
            return NULL_VAL;
        }
        return make_int(ia << ib);
    }
    if (strcmp(op, ">>") == 0) {
        if (ib < 0 || ib >= (long long)(sizeof(long long) * 8)) {
            runtime_error("right shift by %lld out of range", ib);
            return NULL_VAL;
        }
        return make_int(ia >> ib);
    }

    runtime_error("unknown binary operator '%s'", op);
    return NULL_VAL;
}

/* ================================================================
   Helper: check if flow-control flags should stop evaluation
   ================================================================ */

static int should_stop(Env *env) {
    return env->returning || env->breaking || env->continuing ||
           g_exception_thrown || g_runtime_aborted;
}

/* ================================================================
   Block Evaluation (walks children[] AND body->next chain)
   ================================================================ */

static SubVal eval_block(ASTNode *node, Env *env) {
    if (!node) return NULL_VAL;
    SubVal last = NULL_VAL;

    if (node->child_count > 0) {
        /* Walk children[] array (primary storage) */
        for (int i = 0; i < node->child_count && !should_stop(env); i++)
            last = eval(node->children[i], env);
    } else {
        /* Walk body->next linked-list chain (fallback for parser linked-list storage) */
        for (ASTNode *n = node->body; n && !should_stop(env); n = n->next)
            last = eval(n, env);
    }

    return last;
}

/* ================================================================
   Helper: propagate loop control from child env to parent
   Returns 1 if the caller should break out of the C loop.
   ================================================================ */

static int propagate_loop_control(Env *loop_env, Env *parent_env) {
    if (loop_env->returning) {
        parent_env->returning = 1;
        parent_env->ret_val   = loop_env->ret_val;
        return 1; /* break the C loop */
    }
    if (loop_env->breaking) {
        return 1; /* break the C loop */
    }
    /* continuing: just let the C loop continue to next iteration */
    return 0;
}

/* ================================================================
   String Method Helper (called from both CALL_EXPR and MEMBER_ACCESS)
   ================================================================ */

static SubVal eval_string_method(const char *str, const char *method,
                                 ASTNode *call_node, Env *env) {
    if (!str) str = "";

    /* --- Properties --- */
    if (strcmp(method, "length") == 0)
        return make_int((long long)strlen(str));

    /* --- Methods --- */
    if (strcmp(method, "upper") == 0) {
        char *r = strdup(str);
        for (int i = 0; r[i]; i++)
            r[i] = (char)toupper((unsigned char)r[i]);
        SubVal v = {VAL_STRING, .sv = r};
        return v;
    }

    if (strcmp(method, "lower") == 0) {
        char *r = strdup(str);
        for (int i = 0; r[i]; i++)
            r[i] = (char)tolower((unsigned char)r[i]);
        SubVal v = {VAL_STRING, .sv = r};
        return v;
    }

    if (strcmp(method, "substring") == 0) {
        long long start = 0;
        long long end   = (long long)strlen(str);
        int nargs = call_node ? call_node->child_count : 0;
        if (nargs > 0) start = val_as_int(eval(call_node->children[0], env));
        if (nargs > 1) end   = val_as_int(eval(call_node->children[1], env));
        long long slen = (long long)strlen(str);
        if (start < 0) start = 0;
        if (end   < 0) end   = 0;
        if (start > slen) start = slen;
        if (end   > slen) end   = slen;
        if (start > end)   start = end;
        long long len = end - start;
        char *r = malloc((size_t)len + 1);
        memcpy(r, str + start, (size_t)len);
        r[len] = '\0';
        SubVal v = {VAL_STRING, .sv = r};
        return v;
    }

    if (strcmp(method, "split") == 0) {
        const char *sep = " ";
        if (call_node && call_node->child_count > 0) {
            SubVal sv = eval(call_node->children[0], env);
            if (sv.type == VAL_STRING && sv.sv) sep = sv.sv;
        }
        SubVal arr = make_array_val();
        size_t sep_len = strlen(sep);
        if (sep_len == 0) {
            /* Split into individual characters */
            for (int i = 0; str[i]; i++) {
                char ch[2] = {str[i], '\0'};
                array_push(arr.arr, make_str(ch));
            }
        } else {
            const char *p = str;
            while (1) {
                const char *next = strstr(p, sep);
                size_t part_len = next ? (size_t)(next - p) : strlen(p);
                char *part = malloc(part_len + 1);
                memcpy(part, p, part_len);
                part[part_len] = '\0';
                array_push(arr.arr, (SubVal){VAL_STRING, .sv = part});
                if (next) p = next + sep_len; else break;
            }
        }
        return arr;
    }

    if (strcmp(method, "contains") == 0) {
        if (call_node && call_node->child_count > 0) {
            SubVal sv = eval(call_node->children[0], env);
            if (sv.type == VAL_STRING && sv.sv)
                return make_bool(strstr(str, sv.sv) != NULL);
        }
        return make_bool(0);
    }

    if (strcmp(method, "replace") == 0) {
        const char *old_s = "", *new_s = "";
        if (call_node) {
            if (call_node->child_count > 0) {
                SubVal v = eval(call_node->children[0], env);
                if (v.type == VAL_STRING) old_s = v.sv ? v.sv : "";
            }
            if (call_node->child_count > 1) {
                SubVal v = eval(call_node->children[1], env);
                if (v.type == VAL_STRING) new_s = v.sv ? v.sv : "";
            }
        }
        size_t old_len = strlen(old_s);
        if (old_len == 0) return make_str(str); /* empty pattern: no change */

        /* Count occurrences */
        int count = 0;
        const char *p = str;
        while ((p = strstr(p, old_s))) { count++; p += old_len; }

        size_t new_len  = strlen(new_s);
        size_t result_sz = strlen(str) + (size_t)count * (new_len > old_len ? new_len - old_len : 0) + 1;
        char *result = malloc(result_sz);
        char *r = result;
        p = str;
        while (*p) {
            if (strncmp(p, old_s, old_len) == 0) {
                memcpy(r, new_s, new_len);
                r += new_len;
                p += old_len;
            } else {
                *r++ = *p++;
            }
        }
        *r = '\0';
        SubVal v = {VAL_STRING, .sv = result};
        return v;
    }

    if (strcmp(method, "trim") == 0) {
        const char *start = str;
        while (*start && isspace((unsigned char)*start)) start++;
        const char *end = str + strlen(str);
        while (end > start && isspace((unsigned char)*(end - 1))) end--;
        size_t len = (size_t)(end - start);
        char *r = malloc(len + 1);
        memcpy(r, start, len);
        r[len] = '\0';
        SubVal v = {VAL_STRING, .sv = r};
        return v;
    }

    if (strcmp(method, "char_at") == 0) {
        long long idx = 0;
        if (call_node && call_node->child_count > 0)
            idx = val_as_int(eval(call_node->children[0], env));
        long long slen = (long long)strlen(str);
        if (idx < 0) idx += slen; /* support negative indices */
        if (idx < 0 || idx >= slen) {
            runtime_error("char_at(%lld) out of range [0, %lld)", idx, slen);
            return NULL_VAL;
        }
        char ch[2] = {str[idx], '\0'};
        return make_str(ch);
    }

    runtime_error("string has no method '%s'", method);
    return NULL_VAL;
}

/* ================================================================
   Array Method Helper
   ================================================================ */

static SubVal eval_array_method(SubArray *arr, const char *method,
                                ASTNode *call_node, Env *env) {
    if (!arr) {
        runtime_error("cannot call method on null array");
        return NULL_VAL;
    }

    /* --- Properties --- */
    if (strcmp(method, "length") == 0)
        return make_int((long long)arr->count);

    /* --- Methods --- */
    if (strcmp(method, "push") == 0) {
        if (call_node && call_node->child_count > 0)
            array_push(arr, eval(call_node->children[0], env));
        return NULL_VAL;
    }

    if (strcmp(method, "pop") == 0)
        return array_pop(arr);

    if (strcmp(method, "join") == 0) {
        const char *sep = "";
        if (call_node && call_node->child_count > 0) {
            SubVal v = eval(call_node->children[0], env);
            if (v.type == VAL_STRING) sep = v.sv ? v.sv : "";
        }
        /* Calculate total length */
        size_t total = 1; /* at least NUL terminator */
        for (int i = 0; i < arr->count; i++) {
            SubVal s = val_to_str(arr->items[i]);
            total += strlen(s.sv);
            if (i < arr->count - 1) total += strlen(sep);
            val_free(s);
        }
        char *result = malloc(total);
        char *r = result;
        for (int i = 0; i < arr->count; i++) {
            SubVal s = val_to_str(arr->items[i]);
            size_t slen = strlen(s.sv);
            memcpy(r, s.sv, slen);
            r += slen;
            val_free(s);
            if (i < arr->count - 1) {
                size_t seplen = strlen(sep);
                memcpy(r, sep, seplen);
                r += seplen;
            }
        }
        *r = '\0';
        SubVal v = {VAL_STRING, .sv = result};
        return v;
    }

    runtime_error("array has no method '%s'", method);
    return NULL_VAL;
}

/* ================================================================
   Main Evaluation Function
   ================================================================ */

SubVal eval(ASTNode *node, Env *env) {
    /* Early exit on any flow-control signal */
    if (!node || env->returning || env->breaking || env->continuing ||
        g_exception_thrown || g_runtime_aborted)
        return NULL_VAL;

    switch (node->type) {

    /* ============================================================
       Program / Block
       ============================================================ */
    case AST_PROGRAM:
    case AST_BLOCK:
        return eval_block(node, env);

    /* ============================================================
       Literals
       ============================================================ */
    case AST_LITERAL: {
        if (node->data_type == TYPE_NULL)   return NULL_VAL;
        if (node->data_type == TYPE_STRING) return make_str(node->value);
        if (node->data_type == TYPE_BOOL)
            return make_bool(node->value && strcmp(node->value, "true") == 0);
        if (node->data_type == TYPE_FLOAT)
            return make_float(atof(node->value ? node->value : "0"));
        return make_int(atoll(node->value ? node->value : "0"));
    }

    /* ============================================================
       Identifier
       ============================================================ */
    case AST_IDENTIFIER:
        return env_get(env, node->value);

    /* ============================================================
       Variable / Constant Declaration
       ============================================================ */
    case AST_VAR_DECL:
    case AST_CONST_DECL: {
        SubVal val = node->right ? eval(node->right, env) : NULL_VAL;
        env_define(env, node->value, val);
        return val;
    }

    /* ============================================================
       Assignment (= , += , -= , *= , /= , %=)
       ============================================================ */
    case AST_ASSIGN_STMT: {
        const char *op = node->value;
        SubVal rhs = eval(node->right, env);

        /* Whatever this assigns to takes ownership, so a value that is still
           some binding's has to be copied first. `a[0] = a[1]` on an array of
           strings freed the element it was overwriting and then stored the
           other element's own pointer, leaving the array holding one string
           twice -- a double free at exit. */
        if (expr_borrows_binding(node->right)) rhs = val_copy(rhs);

        /* Compound assignment on identifier */
        if (node->left && node->left->type == AST_IDENTIFIER) {
            const char *name = node->left->value;

            if (op && strcmp(op, "=") == 0) {
                env_set(env, name, rhs);
                return rhs;
            }

            /* Compound: get current, compute, set */
            SubVal cur = env_get(env, name);

            /* Convert to numbers */
            double a, b;
            int use_float = (cur.type == VAL_FLOAT || rhs.type == VAL_FLOAT);
            a = (cur.type == VAL_FLOAT) ? cur.fv : (double)cur.iv;
            b = (rhs.type == VAL_FLOAT) ? rhs.fv : (double)rhs.iv;

            SubVal result;
            if      (strcmp(op, "+=") == 0) result = use_float ? make_float(a + b) : make_int((long long)(a + b));
            else if (strcmp(op, "-=") == 0) result = use_float ? make_float(a - b) : make_int((long long)(a - b));
            else if (strcmp(op, "*=") == 0) result = use_float ? make_float(a * b) : make_int((long long)(a * b));
            else if (strcmp(op, "/=") == 0) {
                if (b == 0.0) { runtime_error("division by zero"); return NULL_VAL; }
                result = use_float ? make_float(a / b) : make_int((long long)(a / b));
            }
            else if (strcmp(op, "%=") == 0) {
                if (b == 0.0) { runtime_error("modulo by zero"); return NULL_VAL; }
                result = use_float ? make_float(fmod(a, b)) : make_int((long long)a % (long long)b);
            }
            else if (strcmp(op, "**=") == 0) {
                result = use_float ? make_float(pow(a, b)) : make_int((long long)pow(a, b));
            }
            else if (strcmp(op, "&=") == 0)  result = make_int((long long)a & (long long)b);
            else if (strcmp(op, "|=") == 0)  result = make_int((long long)a | (long long)b);
            else if (strcmp(op, "^=") == 0)  result = make_int((long long)a ^ (long long)b);
            else if (strcmp(op, "<<=") == 0) result = make_int((long long)a << (long long)b);
            else if (strcmp(op, ">>=") == 0) result = make_int((long long)a >> (long long)b);
            else {
                runtime_error("unknown assignment operator '%s'", op);
                return NULL_VAL;
            }

            /* String concatenation for += */
            if (strcmp(op, "+=") == 0 && (cur.type == VAL_STRING || rhs.type == VAL_STRING)) {
                result = eval_binary(
                    &(ASTNode){.type = AST_BINARY_EXPR, .value = "+", .left = node->left, .right = node->right},
                    env
                );
            }

            env_set(env, name, result);
            return result;
        }

        /* Compound assignment on array access: arr[i] += val */
        if (node->left && node->left->type == AST_ARRAY_ACCESS) {
            SubVal arr_val = eval(node->left->left, env);
            if (arr_val.type != VAL_ARRAY || !arr_val.arr) {
                runtime_error("compound assignment on non-array");
                return NULL_VAL;
            }
            SubVal idx_val = eval(node->left->right, env);
            long long idx = val_as_int(idx_val);
            if (idx < 0) idx += (long long)arr_val.arr->count;
            if (idx < 0 || idx >= arr_val.arr->count) {
                runtime_error("array index %lld out of bounds [0, %d)",
                              idx, arr_val.arr->count);
                return NULL_VAL;
            }
            SubVal *item = &arr_val.arr->items[idx];
            double a = (item->type == VAL_FLOAT) ? item->fv : (double)item->iv;
            double b = (rhs.type == VAL_FLOAT) ? rhs.fv : (double)rhs.iv;
            int uf = (item->type == VAL_FLOAT || rhs.type == VAL_FLOAT);
            SubVal result;
            if      (strcmp(op, "+=") == 0) result = uf ? make_float(a + b) : make_int((long long)(a + b));
            else if (strcmp(op, "-=") == 0) result = uf ? make_float(a - b) : make_int((long long)(a - b));
            else if (strcmp(op, "*=") == 0) result = uf ? make_float(a * b) : make_int((long long)(a * b));
            else if (strcmp(op, "/=") == 0) {
                if (b == 0.0) { runtime_error("division by zero"); return NULL_VAL; }
                result = uf ? make_float(a / b) : make_int((long long)(a / b));
            }
            else if (strcmp(op, "%=") == 0) {
                if (b == 0.0) { runtime_error("modulo by zero"); return NULL_VAL; }
                result = uf ? make_float(fmod(a, b)) : make_int((long long)a % (long long)b);
            }
            else result = rhs;
            val_free(*item);
            *item = result;
            return result;
        }

        /* Simple assignment fallback */
        env_set(env, node->left ? node->left->value : "", rhs);
        return rhs;
    }

    /* ============================================================
       Binary Expression
       ============================================================ */
    case AST_BINARY_EXPR:
        return eval_binary(node, env);

    /* ============================================================
       Unary Expression (-, !, +, ~, ++, --)
       ============================================================ */
    case AST_UNARY_EXPR: {
        const char *op = node->value;

        /* Pre-increment: ++var */
        if (op && strcmp(op, "++") == 0 && node->right && node->right->type == AST_IDENTIFIER) {
            const char *name = node->right->value;
            SubVal cur = env_get(env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv + 1.0) : make_int(cur.iv + 1);
            env_set(env, name, nxt);
            return nxt;
        }

        /* Pre-decrement: --var */
        if (op && strcmp(op, "--") == 0 && node->right && node->right->type == AST_IDENTIFIER) {
            const char *name = node->right->value;
            SubVal cur = env_get(env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv - 1.0) : make_int(cur.iv - 1);
            env_set(env, name, nxt);
            return nxt;
        }

        /* Post-increment: var++ (operand in left) */
        if (op && strcmp(op, "++") == 0 && node->left && node->left->type == AST_IDENTIFIER) {
            const char *name = node->left->value;
            SubVal cur = env_get(env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv + 1.0) : make_int(cur.iv + 1);
            env_set(env, name, nxt);
            return cur; /* return old value */
        }

        /* Post-decrement: var-- (operand in left) */
        if (op && strcmp(op, "--") == 0 && node->left && node->left->type == AST_IDENTIFIER) {
            const char *name = node->left->value;
            SubVal cur = env_get(env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv - 1.0) : make_int(cur.iv - 1);
            env_set(env, name, nxt);
            return cur; /* return old value */
        }

        SubVal v = eval(node->right, env);

        /* Unary minus */
        if (op && strcmp(op, "-") == 0)
            return (v.type == VAL_FLOAT) ? make_float(-v.fv) : make_int(-v.iv);

        /* Unary plus */
        if (op && strcmp(op, "+") == 0)
            return to_number(v);

        /* Logical NOT */
        if (op && strcmp(op, "!") == 0)
            return make_bool(!is_truthy(v));

        /* Bitwise NOT */
        if (op && strcmp(op, "~") == 0)
            return make_int(~(v.type == VAL_INT ? v.iv : (long long)v.fv));

        return v;
    }

    /* ============================================================
       If / Elif / Else
       ============================================================ */
    case AST_IF_STMT: {
        SubVal cond = eval(node->condition, env);
        if (is_truthy(cond))
            return eval(node->body, env);
        /* right is either another if (elif) or a block (else) */
        if (node->right)
            return eval(node->right, env);
        return NULL_VAL;
    }

    /* ============================================================
       While Loop
       ============================================================ */
    case AST_WHILE_STMT: {
        SubVal r = NULL_VAL;
        while (!env->returning && !g_exception_thrown && !g_runtime_aborted) {
            SubVal c = eval(node->condition, env);
            if (!is_truthy(c)) break;

            Env *loop = env_new(env);
            r = eval(node->body, loop);
            if (propagate_loop_control(loop, env)) {
                env_free(loop);
                break;
            }
            env_free(loop);
        }
        return r;
    }

    /* ============================================================
       For Loop (range, string, array iteration)
       ============================================================ */
    case AST_FOR_STMT: {
        if (!node->value) return NULL_VAL;

        SubVal r = NULL_VAL;
        ASTNode *range_node = (node->children && node->child_count > 0) ? node->children[0] : NULL;

        if (range_node && range_node->type == AST_RANGE_EXPR) {
            /* ---- Range iteration ---- */
            long long start = 0, end_v = 10;
            if (range_node->left && range_node->right) {
                start = val_as_int(eval(range_node->left, env));
                end_v = val_as_int(eval(range_node->right, env));
            } else if (range_node->left) {
                end_v = val_as_int(eval(range_node->left, env));
            }
            for (long long i = start; i < end_v && !env->returning && !g_exception_thrown && !g_runtime_aborted; i++) {
                Env *loop = env_new(env);
                env_define(loop, node->value, make_int(i));
                r = eval(node->body, loop);
                if (propagate_loop_control(loop, env)) {
                    env_free(loop);
                    break;
                }
                env_free(loop);
            }
        } else if (node->condition) {
            /* ---- Collection iteration ---- */
            SubVal collection = eval(node->condition, env);

            if (collection.type == VAL_STRING) {
                /* String iteration: each character */
                const char *str = collection.sv ? collection.sv : "";
                int slen = (int)strlen(str);
                for (int i = 0; i < slen && !env->returning && !g_exception_thrown && !g_runtime_aborted; i++) {
                    Env *loop = env_new(env);
                    char ch[2] = {str[i], '\0'};
                    env_define(loop, node->value, make_str(ch));
                    r = eval(node->body, loop);
                    if (propagate_loop_control(loop, env)) {
                        env_free(loop);
                        break;
                    }
                    env_free(loop);
                }
            } else if (collection.type == VAL_ARRAY && collection.arr) {
                /* Array iteration */
                for (int i = 0; i < collection.arr->count && !env->returning && !g_exception_thrown && !g_runtime_aborted; i++) {
                    Env *loop = env_new(env);
                    /* A copy: the loop scope owns what it is handed and frees
                       it at the end of the iteration, so binding the array's
                       own element freed the array out from under itself. An
                       array of numbers survived that because freeing a number
                       does nothing; an array of strings did not. */
                    env_define(loop, node->value, val_copy(collection.arr->items[i]));
                    r = eval(node->body, loop);
                    if (propagate_loop_control(loop, env)) {
                        env_free(loop);
                        break;
                    }
                    env_free(loop);
                }
            } else {
                runtime_error("cannot iterate over %s", type_name(collection.type));
            }
        }
        return r;
    }

    /* ============================================================
       Do-While Loop
       ============================================================ */
    case AST_DO_WHILE_STMT: {
        SubVal r = NULL_VAL;
        do {
            Env *loop = env_new(env);
            r = eval(node->body, loop);
            if (propagate_loop_control(loop, env)) {
                env_free(loop);
                break;
            }
            env_free(loop);
        } while (!env->returning && !g_exception_thrown && !g_runtime_aborted &&
                 is_truthy(eval(node->condition, env)));
        return r;
    }

    /* ============================================================
       Function Declaration
       ============================================================ */
    case AST_FUNCTION_DECL:
    case AST_ARROW_FUNCTION: {
        SubVal fv = {VAL_FUNC, .fn = node};
        if (node->value)
            env_define(env, node->value, fv);
        return fv;
    }

    /* ============================================================
       Return Statement
       ============================================================ */
    case AST_RETURN_STMT: {
        SubVal rv = node->right ? eval(node->right, env) : NULL_VAL;
        /* The caller frees this scope the moment the call returns, and
           env_get hands out the environment's own value rather than a copy.
           So a value still reachable from a binding here has to be copied out
           before that happens, or the caller is handed freed memory:

               fn f() { let o = []; push(o, 7); return o }
               println(f())        # [] on a good day, a segfault otherwise

           A value the expression built fresh -- an array literal, the result
           of an operator or of another call -- is owned by nobody and is
           returned as it is, so this does not copy on every return. */
        if (node->right && expr_borrows_binding(node->right))
            rv = val_copy(rv);
        env->returning = 1;
        env->ret_val   = rv;
        return rv;
    }

    /* ============================================================
       Break Statement
       ============================================================ */
    case AST_BREAK_STMT:
        env->breaking = 1;
        return NULL_VAL;

    /* ============================================================
       Continue Statement
       ============================================================ */
    case AST_CONTINUE_STMT:
        env->continuing = 1;
        return NULL_VAL;

    /* ============================================================
       Try / Catch / Throw
       ============================================================ */
    case AST_TRY_STMT: {
        /* Save and reset exception state */
        int prev_thrown = g_exception_thrown;
        g_exception_thrown = 0;
        SubVal prev_exception = g_exception;
        g_exception = NULL_VAL;

        /* Execute try body */
        eval(node->body, env);

        if (g_exception_thrown) {
            /* Exception was thrown - handle catch */
            g_exception_thrown = 0;
            SubVal thrown_val = g_exception;
            g_exception = prev_exception;

            if (node->right && node->right->type == AST_CATCH_CLAUSE) {
                ASTNode *catch_clause = node->right;
                Env *catch_env = env_new(env);
                /* Bind exception to the variable named in catch clause */
                const char *ex_var = catch_clause->value ? catch_clause->value : "e";
                env_define(catch_env, ex_var, thrown_val);
                /* Execute catch handler body */
                eval(catch_clause->body, catch_env);
                env_free(catch_env);
            }
        } else {
            g_exception = prev_exception;
        }

        /* Execute finally clause (stored in next chain) */
        for (ASTNode *n = node->next; n; n = n->next) {
            if (n->type == AST_FINALLY_CLAUSE && n->body) {
                eval(n->body, env);
                break;
            }
        }

        /* Restore exception state */
        g_exception_thrown = prev_thrown;
        return NULL_VAL;
    }

    case AST_THROW_STMT: {
        SubVal thrown = node->right ? eval(node->right, env) : NULL_VAL;
        g_exception = thrown;
        g_exception_thrown = 1;
        return NULL_VAL;
    }

    /* ============================================================
       Array Literal [1, 2, 3]
       ============================================================ */
    case AST_ARRAY_LITERAL: {
        SubVal arr = make_array_val();
        for (int i = 0; i < node->child_count; i++)
            array_push(arr.arr, eval(node->children[i], env));
        return arr;
    }

    /* ============================================================
       Object Literal { key: value, ... }
       ============================================================ */
    case AST_OBJECT_LITERAL: {
        SubVal obj = make_object_val();
        for (int i = 0; i < node->child_count; i++) {
            ASTNode *child = node->children[i];
            if (!child) continue;

            /* Parser stores pairs as AST_VAR_DECL: value=key, right=expr */
            if (child->type == AST_VAR_DECL && child->value) {
                SubVal val = child->right ? eval(child->right, env) : NULL_VAL;
                object_set(obj.obj, child->value, val);
            }
            /* Also support AST_BINARY_EXPR with ":" operator */
            else if (child->type == AST_BINARY_EXPR &&
                     child->value && strcmp(child->value, ":") == 0 &&
                     child->left && child->left->type == AST_IDENTIFIER) {
                SubVal val = eval(child->right, env);
                object_set(obj.obj, child->left->value, val);
            }
            /* Fallback: evaluate key expression as string */
            else if (child->left) {
                SubVal key = val_to_str(eval(child->left, env));
                SubVal val = eval(child->right, env);
                object_set(obj.obj, key.sv, val);
                val_free(key);
            }
        }
        return obj;
    }

    /* ============================================================
       Member Access (obj.prop, obj.method())
       Handles property reads for string/array .length
       ============================================================ */
    case AST_MEMBER_ACCESS: {
        const char *member = node->value;
        if (!member) return NULL_VAL;
        SubVal obj = eval(node->left, env);

        /* String property */
        if (obj.type == VAL_STRING) {
            if (strcmp(member, "length") == 0)
                return make_int((long long)strlen(obj.sv ? obj.sv : ""));
        }

        /* Array property */
        if (obj.type == VAL_ARRAY && obj.arr) {
            if (strcmp(member, "length") == 0)
                return make_int((long long)obj.arr->count);
        }

        /* Object property */
        if (obj.type == VAL_OBJECT && obj.obj) {
            return object_get(obj.obj, member);
        }

        runtime_error("cannot access property '%s' on %s",
                member, type_name(obj.type));
        return NULL_VAL;
    }

    /* ============================================================
       Array Index Access (arr[i], str[i])
       ============================================================ */
    case AST_ARRAY_ACCESS: {
        SubVal container = eval(node->left, env);
        SubVal idx       = eval(node->right, env);
        long long i = val_as_int(idx);

        if (container.type == VAL_ARRAY && container.arr) {
            if (i < 0) i += (long long)container.arr->count;
            if (i < 0 || i >= container.arr->count) {
                runtime_error("array index %lld out of bounds [0, %d)",
                        i, container.arr->count);
                return NULL_VAL;
            }
            return container.arr->items[i];
        }

        if (container.type == VAL_STRING) {
            const char *str = container.sv ? container.sv : "";
            long long slen = (long long)strlen(str);
            if (i < 0) i += slen;
            if (i < 0 || i >= slen) {
                runtime_error("string index %lld out of bounds", i);
                return NULL_VAL;
            }
            char ch[2] = {str[i], '\0'};
            return make_str(ch);
        }

        /* Object property access: obj["key"] */
        if (container.type == VAL_OBJECT && container.obj) {
            SubVal key_val = val_to_str(idx);
            SubVal result = object_get(container.obj, key_val.sv);
            val_free(key_val);
            return result;
        }

        runtime_error("cannot index into %s", type_name(container.type));
        return NULL_VAL;
    }

    /* ============================================================
       Ternary Expression (cond ? then : else)
       ============================================================ */
    case AST_TERNARY_EXPR: {
        SubVal cond = eval(node->condition, env);
        if (is_truthy(cond))
            return node->left  ? eval(node->left, env) : NULL_VAL;
        return node->right ? eval(node->right, env) : NULL_VAL;
    }

    /* ============================================================
       Function Call (builtins + user functions + member methods)
       ============================================================ */
    case AST_CALL_EXPR: {

        /* --- Member method call: obj.method(args) --- */
        if (node->left && node->left->type == AST_MEMBER_ACCESS) {
            ASTNode *member_node = node->left;
            const char *method   = member_node->value;
            SubVal obj           = eval(member_node->left, env);

            if (obj.type == VAL_STRING)
                return eval_string_method(obj.sv, method, node, env);

            if (obj.type == VAL_ARRAY && obj.arr)
                return eval_array_method(obj.arr, method, node, env);

            runtime_error("cannot call method '%s' on %s",
                    method ? method : "(null)", type_name(obj.type));
            return NULL_VAL;
        }

        /* --- Named builtin / user function call --- */
        const char *fn = node->value;

        /* ==== Builtins ==== */

        /* print / show: each arg on its own line.
           An argument that failed to evaluate has no value worth showing —
           printing it would put a bare "null" under the error message. */
        if (fn && (strcmp(fn, "print") == 0 || strcmp(fn, "show") == 0)) {
            for (int i = 0; i < node->child_count; i++) {
                SubVal arg = eval(node->children[i], env);
                if (g_runtime_aborted) return NULL_VAL;
                print_val(arg);
                printf("\n");
            }
            return NULL_VAL;
        }

        /* println: all args on one line, space-separated, then newline */
        if (fn && strcmp(fn, "println") == 0) {
            for (int i = 0; i < node->child_count; i++) {
                SubVal arg = eval(node->children[i], env);
                if (g_runtime_aborted) return NULL_VAL;
                if (i > 0) printf(" ");
                print_val(arg);
            }
            printf("\n");
            return NULL_VAL;
        }

        /* str / to_string: convert to string */
        if (fn && (strcmp(fn, "str") == 0 || strcmp(fn, "to_string") == 0)) {
            if (node->child_count > 0) return val_to_str(eval(node->children[0], env));
            return make_str("");
        }

        /* int: convert to integer */
        if (fn && strcmp(fn, "int") == 0) {
            if (node->child_count > 0) {
                SubVal v = to_number(eval(node->children[0], env));
                /* iv and fv share a union: reading iv from a float value
                   reinterprets the double's bit pattern, which is why
                   int(3.9) used to return 4615964438073389875. Truncate
                   toward zero instead, matching every backend. */
                if (v.type == VAL_FLOAT) return make_int((long long)v.fv);
                return make_int(v.iv);
            }
            return make_int(0);
        }

        /* float: convert to float */
        if (fn && strcmp(fn, "float") == 0) {
            if (node->child_count > 0) {
                SubVal v = to_number(eval(node->children[0], env));
                return make_float(v.type == VAL_FLOAT ? v.fv : (double)v.iv);
            }
            return make_float(0.0);
        }

        /* len: length of string or array */
        if (fn && strcmp(fn, "len") == 0) {
            if (node->child_count > 0) {
                SubVal v = eval(node->children[0], env);
                if (v.type == VAL_STRING) return make_int((long long)strlen(v.sv ? v.sv : ""));
                if (v.type == VAL_ARRAY  && v.arr) return make_int((long long)v.arr->count);
                if (v.type == VAL_OBJECT && v.obj) return make_int((long long)v.obj->count);
            }
            return make_int(0);
        }

        /* type: return type name as string */
        if (fn && strcmp(fn, "type") == 0) {
            if (node->child_count > 0)
                return make_str(type_name(eval(node->children[0], env).type));
            return make_str("null");
        }

        /* abs: absolute value */
        if (fn && strcmp(fn, "abs") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            if (v.type == VAL_FLOAT) return make_float(fabs(v.fv));
            if (v.type == VAL_INT)   return make_int(llabs(v.iv));
            return v;
        }

        /* min: minimum of two values */
        if (fn && strcmp(fn, "min") == 0 && node->child_count >= 2) {
            SubVal a = eval(node->children[0], env);
            SubVal b = eval(node->children[1], env);
            double da = (a.type == VAL_FLOAT) ? a.fv : (double)a.iv;
            double db = (b.type == VAL_FLOAT) ? b.fv : (double)b.iv;
            int uf = (a.type == VAL_FLOAT || b.type == VAL_FLOAT);
            return uf ? make_float(da < db ? da : db) : make_int(da < db ? (long long)da : (long long)db);
        }

        /* max: maximum of two values */
        if (fn && strcmp(fn, "max") == 0 && node->child_count >= 2) {
            SubVal a = eval(node->children[0], env);
            SubVal b = eval(node->children[1], env);
            double da = (a.type == VAL_FLOAT) ? a.fv : (double)a.iv;
            double db = (b.type == VAL_FLOAT) ? b.fv : (double)b.iv;
            int uf = (a.type == VAL_FLOAT || b.type == VAL_FLOAT);
            return uf ? make_float(da > db ? da : db) : make_int(da > db ? (long long)da : (long long)db);
        }

        /* sqrt: square root */
        if (fn && strcmp(fn, "sqrt") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            return make_float(sqrt(d));
        }

        /* floor: floor */
        if (fn && strcmp(fn, "floor") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            return make_int((long long)floor(d));
        }

        /* ceil: ceiling */
        if (fn && strcmp(fn, "ceil") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            return make_int((long long)ceil(d));
        }

        /* round: round to nearest integer */
        if (fn && strcmp(fn, "round") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            return make_int((long long)round(d));
        }

        /* upper: string to uppercase */
        if (fn && strcmp(fn, "upper") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            if (v.type == VAL_STRING && v.sv) {
                char *r = strdup(v.sv);
                for (int i = 0; r[i]; i++)
                    r[i] = (char)toupper((unsigned char)r[i]);
                SubVal res = {VAL_STRING, .sv = r};
                return res;
            }
            return v;
        }

        /* lower: string to lowercase */
        if (fn && strcmp(fn, "lower") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            if (v.type == VAL_STRING && v.sv) {
                char *r = strdup(v.sv);
                for (int i = 0; r[i]; i++)
                    r[i] = (char)tolower((unsigned char)r[i]);
                SubVal res = {VAL_STRING, .sv = r};
                return res;
            }
            return v;
        }

        /* split(s[, sep]) */
        if (fn && strcmp(fn, "split") == 0 && node->child_count > 0) {
            SubVal sv = eval(node->children[0], env);
            if (sv.type == VAL_STRING) {
                /* Every other function-form builtin here shifts the argument
                   list past the subject before handing it to the method
                   helper. This one passed `node` through unshifted, so the
                   helper read the string itself as the separator and
                   split("a,,b", ",") answered ["a", ""] -- the string cut on
                   itself -- rather than ["a", "", "b"]. */
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = node->child_count > 1 ? &node->children[1] : NULL;
                return eval_string_method(sv.sv, "split", &temp, env);
            }
            return make_array_val();
        }

        /* join: join array elements with separator */
        if (fn && strcmp(fn, "join") == 0 && node->child_count >= 1) {
            SubVal arr = eval(node->children[0], env);
            if (arr.type == VAL_ARRAY && arr.arr) {
                /* Temporarily set node->children so method helper can find separator */
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = node->child_count > 1 ? &node->children[1] : NULL;
                return eval_array_method(arr.arr, "join", &temp, env);
            }
            return make_str("");
        }

        /* substring(s, start[, end]) */
        if (fn && strcmp(fn, "substring") == 0 && node->child_count >= 2) {
            SubVal sv = eval(node->children[0], env);
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                return eval_string_method(sv.sv, "substring", &temp, env);
            }
            return NULL_VAL;
        }

        /* contains(s, sub) */
        if (fn && strcmp(fn, "contains") == 0 && node->child_count >= 2) {
            SubVal sv = eval(node->children[0], env);
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                return eval_string_method(sv.sv, "contains", &temp, env);
            }
            return make_bool(0);
        }

        /* replace(s, old, new) */
        if (fn && strcmp(fn, "replace") == 0 && node->child_count >= 3) {
            SubVal sv = eval(node->children[0], env);
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                return eval_string_method(sv.sv, "replace", &temp, env);
            }
            return NULL_VAL;
        }

        /* trim(s) */
        if (fn && strcmp(fn, "trim") == 0 && node->child_count > 0) {
            SubVal sv = eval(node->children[0], env);
            if (sv.type == VAL_STRING)
                return eval_string_method(sv.sv, "trim", NULL, env);
            return sv;
        }

        /* char_at(s, i) */
        if (fn && strcmp(fn, "char_at") == 0 && node->child_count >= 2) {
            SubVal sv = eval(node->children[0], env);
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                return eval_string_method(sv.sv, "char_at", &temp, env);
            }
            return NULL_VAL;
        }

        /* push(arr, item) */
        if (fn && strcmp(fn, "push") == 0 && node->child_count >= 2) {
            SubVal arr = eval(node->children[0], env);
            if (arr.type == VAL_ARRAY && arr.arr) {
                SubVal item = eval(node->children[1], env);
                /* The array takes ownership, so a value that is still a
                   binding's has to be copied first -- otherwise the array and
                   the scope both free it. */
                if (expr_borrows_binding(node->children[1]))
                    item = val_copy(item);
                array_push(arr.arr, item);
            }
            return NULL_VAL;
        }

        /* append(arr, item) - alias for push */
        if (fn && strcmp(fn, "append") == 0 && node->child_count >= 2) {
            SubVal arr = eval(node->children[0], env);
            if (arr.type == VAL_ARRAY && arr.arr) {
                SubVal item = eval(node->children[1], env);
                /* The array takes ownership, so a value that is still a
                   binding's has to be copied first -- otherwise the array and
                   the scope both free it. */
                if (expr_borrows_binding(node->children[1]))
                    item = val_copy(item);
                array_push(arr.arr, item);
            }
            return NULL_VAL;
        }

        /* pop(arr) */
        if (fn && strcmp(fn, "pop") == 0 && node->child_count > 0) {
            SubVal arr = eval(node->children[0], env);
            if (arr.type == VAL_ARRAY && arr.arr)
                return array_pop(arr.arr);
            return NULL_VAL;
        }

        /* input([prompt]) */
        if (fn && strcmp(fn, "input") == 0) {
            if (node->child_count > 0) print_val(eval(node->children[0], env));
            char buf[1024];
            if (!fgets(buf, sizeof(buf), stdin)) return make_str("");
            size_t l = strlen(buf);
            if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
            return make_str(buf);
        }

        /* ==== User-defined function call ==== */
        if (fn) {
            SubVal fv = env_get(env, fn);
            if (fv.type == VAL_FUNC && fv.fn) {
                ASTNode *fn_decl = fv.fn;

                interp_check_stack();

                Env *fn_env = env_new(env);
                /* Bind parameters from function's children to call's children */
                if (fn_decl->children) {
                    for (int i = 0; i < fn_decl->child_count && i < node->child_count; i++) {
                        SubVal arg = eval(node->children[i], env);
                        env_define(fn_env, fn_decl->children[i]->value, arg);
                    }
                }
                eval(fn_decl->body, fn_env);
                SubVal ret = fn_env->returning ? fn_env->ret_val : NULL_VAL;
                env_free(fn_env);
                return ret;
            }
            runtime_error("'%s' is not a function (type: %s)",
                    fn, type_name(fv.type));
        } else {
            runtime_error("call to unnamed expression");
        }
        return NULL_VAL;
    }

    /* ============================================================
       Catch / Finally clauses (handled by AST_TRY_STMT)
       ============================================================ */
    case AST_CATCH_CLAUSE:
    case AST_FINALLY_CLAUSE:
        /* These are handled internally by AST_TRY_STMT; if reached
           directly, just evaluate the body as a block. */
        return node->body ? eval(node->body, env) : NULL_VAL;

    /* ============================================================
       Range Expression (only valid inside for-loop; evaluate as 0)
       ============================================================ */
    case AST_RANGE_EXPR:
        return NULL_VAL;

    /* ============================================================
       Switch / Match Statement
       ============================================================ */
    case AST_SWITCH_STMT: {
        if (!node->children) return NULL_VAL;
        SubVal scrutinee = node->condition ? eval(node->condition, env) : NULL_VAL;
        ASTNode *default_clause = NULL;
        ASTNode *chosen = NULL;

        /* Find the clause to run before running anything: a case body is a
           statement list and may have side effects, so it must not be
           evaluated while still deciding which case matched. */
        for (int i = 0; i < node->child_count && !chosen; i++) {
            ASTNode *clause = node->children[i];
            if (!clause) continue;
            if (clause->type == AST_DEFAULT_CLAUSE) {
                default_clause = clause;
                continue;
            }
            if (clause->type != AST_CASE_CLAUSE) continue;
            for (int j = 0; j < clause->child_count; j++) {
                /* Not freed: env_get hands out the live value rather than a
                   copy, so a scrutinee or case value that is just a variable
                   is the environment's own, and releasing it here left the
                   environment holding a dangling pointer. */
                SubVal cv = eval(clause->children[j], env);
                if (values_equal(scrutinee, cv)) { chosen = clause; break; }
            }
        }
        if (!chosen) chosen = default_clause;

        if (chosen && chosen->body) {
            /* Cases do not fall through, so `break` here means "leave the
               switch" and must not reach an enclosing loop. `continue` and
               `return` still belong to whatever encloses the switch. */
            Env *scope = env_new(env);
            eval(chosen->body, scope);
            if (scope->returning) {
                env->returning = 1;
                env->ret_val   = scope->ret_val;
            } else if (scope->continuing) {
                env->continuing = 1;
            }
            env_free(scope);
        }
        return NULL_VAL;
    }

    /* ============================================================
       Unhandled / Pass-through
       ============================================================ */
    default:
        return NULL_VAL;
    }
}

/* ================================================================
   Interpret a Source File
   ================================================================ */

int interpret_source(const char *source, Env *env) {
    if (!source || !env) return 1;

    interp_stack_guard_init();
    interp_clear_abort();

    int ntok;
    Token *toks = lexer_tokenize(source, &ntok);
    if (!toks) return 1;

    ASTNode *ast = parser_parse(toks, ntok);
    if (!ast) {
        lexer_free_tokens(toks, ntok);
        return 1;
    }

    /* Soft semantic check — warnings only for the interpreter */
    if (ast && !semantic_analyze(ast)) {
        fprintf(stderr, "Warning: semantic analysis reported issues (continuing)\n");
    }

    eval(ast, env);
    int aborted = g_runtime_aborted;

    parser_free_ast(ast);
    lexer_free_tokens(toks, ntok);
    return aborted ? 1 : 0;
}

int interpret_file(const char *path) {
    interp_stack_guard_init();

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'\n", path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return 1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 1; }

    char *src = malloc((size_t)sz + 1);
    if (!src) { fclose(f); return 1; }
    size_t read_sz = fread(src, 1, (size_t)sz, f);
    fclose(f);
    if ((long)read_sz != sz) { free(src); return 1; }
    src[read_sz] = '\0';

    int ntok;
    Token *toks = lexer_tokenize(src, &ntok);

    /* Imports in this file resolve relative to it. */
    module_reset();
    module_set_source(path);
    ASTNode *ast = parser_parse(toks, ntok);
    module_set_source(NULL);
    if (!ast) {
        fprintf(stderr, "Error: parsing failed\n");
        free(src); lexer_free_tokens(toks, ntok);
        return 1;
    }

    /* Run semantic analysis as a soft check (warnings only).
       The interpreter performs its own runtime checks, so semantic
       errors (e.g. unknown builtins not registered in the static
       symbol table) should not prevent execution. */
    if (ast && !semantic_analyze(ast)) {
        fprintf(stderr, "Warning: semantic analysis reported issues (continuing)\n");
    }

    Env *global = env_new(NULL);
    eval(ast, global);
    env_free(global);

    parser_free_ast(ast);
    lexer_free_tokens(toks, ntok);
    free(src);
    return 0;
}
