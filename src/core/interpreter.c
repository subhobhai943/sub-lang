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
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define stat _stat
#define S_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#endif
/* Networking builtins (net_fetch / net_post / net_download) speak plain
   HTTP over POSIX sockets. Windows gets a stub that reports the same
   runtime error the interpreter reports for other unsupported things. */
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>   /* sockaddr_in / sockaddr_in6. glibc's
                             <sys/socket.h> pulls this in transitively, so
                             Linux builds never noticed its absence;
                             FreeBSD's headers do not, and clang failed on
                             the incomplete types. */
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#define SUB_NET_POSIX 1
#else
#define SUB_NET_POSIX 0
/* No winsock2.h / ws2tcpip.h here on purpose: winnt.h declares the
   TOKEN_INFORMATION_CLASS enumerator `TokenType`, which collides with
   SUB's own TokenType typedef (see the long note in windows_compat.h).
   The net builtins are a runtime-error stub on Windows anyway, so the
   socket headers buy nothing and break every MinGW build that includes
   this file. */
#pragma comment(lib, "ws2_32")
#endif
#ifndef _WIN32
#include <sys/resource.h>
#endif

/* ================================================================
   Global State
   ================================================================ */

static SubVal NULL_VAL = {VAL_NULL, {.iv = 0}, NULL};
static int g_provenance_enabled = 0;
static SubProvenance *g_binary_left = NULL;
static SubProvenance *g_binary_right = NULL;

#define PROVENANCE_MAX_DEPTH 64

struct SubProvenance {
    int refs;
    int line;
    int runtime;
    char *operation;
    struct SubProvenance *parents[2];
};

static void provenance_retain(SubProvenance *p) {
    if (p) p->refs++;
}

static void provenance_release(SubProvenance *p) {
    if (!p || --p->refs > 0) return;
    provenance_release(p->parents[0]);
    provenance_release(p->parents[1]);
    free(p->operation);
    free(p);
}

static SubProvenance *provenance_new(int line, const char *operation,
                                     int runtime, SubProvenance *a,
                                     SubProvenance *b) {
    if (!g_provenance_enabled) return NULL;
    SubProvenance *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->refs = 1;
    p->line = line;
    p->runtime = runtime;
    p->operation = strdup(operation ? operation : "value");
    p->parents[0] = a;
    p->parents[1] = b;
    provenance_retain(a);
    provenance_retain(b);
    return p;
}

static char *ast_expression_text(ASTNode *node) {
    if (!node) return strdup("value");
    if (node->type == AST_IDENTIFIER || node->type == AST_LITERAL)
        return strdup(node->value ? node->value : "value");
    if (node->type == AST_WHY_EXPR)
        return strdup("why");
    if (node->type == AST_BINARY_EXPR || node->type == AST_ASSIGN_STMT) {
        char *l = ast_expression_text(node->left);
        char *r = ast_expression_text(node->right);
        size_t n = strlen(l) + strlen(r) + strlen(node->value ? node->value : "=") + 5;
        char *s = malloc(n);
        if (s) snprintf(s, n, "%s %s %s", l, node->value ? node->value : "=", r);
        free(l); free(r);
        return s ? s : strdup("expression");
    }
    if (node->type == AST_CALL_EXPR) {
        size_t n = strlen(node->value ? node->value : "call") + 3;
        for (int i = 0; i < node->child_count; i++) {
            char *a = ast_expression_text(node->children[i]);
            n += strlen(a) + 2;
            free(a);
        }
        char *s = malloc(n);
        if (!s) return strdup("call()");
        snprintf(s, n, "%s(", node->value ? node->value : "call");
        for (int i = 0; i < node->child_count; i++) {
            char *a = ast_expression_text(node->children[i]);
            if (i) strcat(s, ", ");
            strcat(s, a);
            free(a);
        }
        strcat(s, ")");
        return s;
    }
    return strdup("expression");
}

static void provenance_attach(SubVal *v, ASTNode *node, const char *label,
                              SubProvenance *a, SubProvenance *b, int runtime) {
    if (!g_provenance_enabled || !v) return;
    char *text = label ? strdup(label) : ast_expression_text(node);
    SubProvenance *next = provenance_new(node ? node->line : 0, text, runtime, a, b);
    provenance_release(v->provenance);
    v->provenance = next;
    free(text);
}

static int ast_contains_why(ASTNode *node) {
    if (!node) return 0;
    if (node->type == AST_WHY_EXPR) return 1;
    if (ast_contains_why(node->left) || ast_contains_why(node->right) ||
        ast_contains_why(node->condition) || ast_contains_why(node->body) ||
        ast_contains_why(node->next)) return 1;
    for (int i = 0; i < node->child_count; i++)
        if (ast_contains_why(node->children[i])) return 1;
    return 0;
}

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
static SubVal g_exception    = {VAL_NULL, {.iv = 0}, NULL};
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
static int g_argc = 0;
static char **g_argv = NULL;

void interp_set_args(int argc, char **argv) {
    g_argc = argc;
    g_argv = argv;
}

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

/* Set when the interpreter runs on a dedicated big-stack thread (see
   interp_run_on_big_stack): the budget then reflects that thread's actual
   stack, not the main thread's RLIMIT_STACK of a few megabytes. */
static size_t g_thread_stack_size = 0;

static void interp_stack_guard_init(void) {
    char probe;
    g_stack_base = (uintptr_t)&probe;

    size_t limit = g_thread_stack_size;
    if (limit == 0) limit = (size_t)1 << 20;  /* Windows default: 1 MiB */
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
    if (getenv("SUB_DEBUG_STACK")) fprintf(stderr, "[guard] budget=%zu MB\n", g_stack_budget >> 20);
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
static void array_push(SubArray *arr, SubVal item);

/* Networking builtins (defined near the bottom, after the helpers they
   use; eval()'s builtin dispatch appears earlier in the file). */
static char *net_http_request(const char *method, const char *url,
                              const char *body, int *status_out,
                              size_t *body_len_out);

/* ================================================================
   Value Constructors
   ================================================================ */

static SubVal make_int(long long v)    { SubVal r = {VAL_INT, .iv = v};   return r; }
static SubVal make_float(double v)    { SubVal r = {VAL_FLOAT, .fv = v}; return r; }
static SubVal make_bool(int v)        { SubVal r = {VAL_BOOL, .bv = v ? 1 : 0}; return r; }

static SubVal make_str(const char *s) {
    SubVal r = {VAL_STRING, {.sv = NULL}, NULL};
    r.sv = strdup(s ? s : "");
    return r;
}

static SubVal make_array_val(void) {
    SubVal r = {VAL_ARRAY, {.arr = NULL}, NULL};
    r.arr = calloc(1, sizeof(SubArray));
    return r;
}

static SubVal make_object_val(void) {
    SubVal r = {VAL_OBJECT, {.obj = NULL}, NULL};
    r.obj = calloc(1, sizeof(SubObject));
    return r;
}

/* ================================================================
   Value Lifecycle
   ================================================================ */

static void val_free(SubVal v) {
    provenance_release(v.provenance);
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
    provenance_retain(r.provenance);
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

static char *read_file_bytes(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        runtime_error("read_file: cannot read '%s': %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        runtime_error("read_file: cannot seek '%s'", path);
        return NULL;
    }
    long end = ftell(f);
    if (end < 0 || (unsigned long long)end > (unsigned long long)SIZE_MAX - 1) {
        fclose(f);
        runtime_error("read_file: size overflow for '%s'", path);
        return NULL;
    }
    rewind(f);
    size_t len = (size_t)end;
    char *data = malloc(len + 1);
    if (!data) {
        fclose(f);
        runtime_error("read_file: out of memory for '%s'", path);
        return NULL;
    }
    if (len && fread(data, 1, len, f) != len) {
        free(data);
        fclose(f);
        runtime_error("read_file: cannot read '%s'", path);
        return NULL;
    }
    fclose(f);
    data[len] = '\0';
    if (out_len) *out_len = len;
    return data;
}

static uint32_t sha256_rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

static char *sha256_string(const char *input) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
        0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
        0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
        0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
        0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
        0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t len = strlen(input ? input : "");
    if (len > (SIZE_MAX - 72) / 8) {
        runtime_error("sha256: input is too large");
        return NULL;
    }
    size_t total = ((len + 9 + 63) / 64) * 64;
    unsigned char *msg = calloc(total, 1);
    if (!msg) {
        runtime_error("sha256: out of memory");
        return NULL;
    }
    memcpy(msg, input ? input : "", len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) msg[total - 1 - i] = (unsigned char)(bits >> (i * 8));
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[off+i*4] << 24) | ((uint32_t)msg[off+i*4+1] << 16) |
                   ((uint32_t)msg[off+i*4+2] << 8) | msg[off+i*4+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = sha256_rotr(w[i-15],7) ^ sha256_rotr(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = sha256_rotr(w[i-2],17) ^ sha256_rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], q=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t s1 = sha256_rotr(e,6) ^ sha256_rotr(e,11) ^ sha256_rotr(e,25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = q + s1 + ch + k[i] + w[i];
            uint32_t s0 = sha256_rotr(a,2) ^ sha256_rotr(a,13) ^ sha256_rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            q=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=q;
    }
    free(msg);
    char *out = malloc(65);
    if (!out) {
        runtime_error("sha256: out of memory");
        return NULL;
    }
    for (int i = 0; i < 8; i++)
        snprintf(out + i * 8, 9, "%08x", h[i]);
    return out;
}

static int mkdir_parents(const char *path) {
    char *tmp = strdup(path ? path : "");
    if (!tmp) return -1;
    size_t n = strlen(tmp);
    while (n > 1 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\')) tmp[--n] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char save = *p;
        *p = '\0';
        if (tmp[0] && mkdir(tmp, 0777) != 0 && errno != EEXIST) {
            *p = save; free(tmp); return -1;
        }
        *p = save;
    }
    int rc = mkdir(tmp, 0777);
    if (rc != 0 && errno != EEXIST) { free(tmp); return -1; }
    free(tmp);
    return 0;
}

static SubVal builtin_args(void) {
    SubVal result = make_array_val();
    int start = g_argc > 1 ? 1 : g_argc;
    for (int i = start; i < g_argc; i++)
        array_push(result.arr, make_str(g_argv[i]));
    return result;
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

static int provenance_depth(SubProvenance *p, SubProvenance **seen, int count) {
    if (!p || count >= PROVENANCE_MAX_DEPTH) return 0;
    for (int i = 0; i < count; i++) if (seen[i] == p) return 0;
    seen[count++] = p;
    int a = provenance_depth(p->parents[0], seen, count);
    int b = provenance_depth(p->parents[1], seen, count);
    return 1 + (a > b ? a : b);
}

static void provenance_sources(SubProvenance *p, SubArray *out,
                               SubProvenance **seen, int *count,
                               int *remaining) {
    if (!p || !out || !remaining || *remaining <= 0 ||
        *count >= PROVENANCE_MAX_DEPTH) return;
    for (int i = 0; i < *count; i++) if (seen[i] == p) return;
    seen[(*count)++] = p;
    char line[512];
    if (p->runtime)
        snprintf(line, sizeof(line), "%s (line %d, runtime)", p->operation, p->line);
    else
        snprintf(line, sizeof(line), "%s (line %d)", p->operation, p->line);
    array_push(out, make_str(line));
    (*remaining)--;
    provenance_sources(p->parents[0], out, seen, count, remaining);
    provenance_sources(p->parents[1], out, seen, count, remaining);
}

static SubVal make_why_value(SubVal value, ASTNode *operand) {
    SubVal result = make_object_val();
    SubVal depth = make_int(0);
    SubVal sources = make_array_val();
    SubVal rendered = val_to_str(value);
    SubVal text = make_str("");
    if (value.provenance) {
        SubProvenance *seen[PROVENANCE_MAX_DEPTH] = {0};
        depth = make_int(provenance_depth(value.provenance, seen, 0));
        int count = 0, remaining = PROVENANCE_MAX_DEPTH;
        memset(seen, 0, sizeof(seen));
        provenance_sources(value.provenance, sources.arr, seen, &count, &remaining);
        if (remaining == 0) array_push(sources.arr, make_str("... (more)"));
    } else {
        array_push(sources.arr, make_str("(no history)"));
    }
    if (operand && operand->type == AST_IDENTIFIER && operand->value) {
        size_t n = strlen(operand->value) + strlen(rendered.sv ? rendered.sv : "") + 4;
        char *named = malloc(n);
        if (named) {
            snprintf(named, n, "%s = %s", operand->value, rendered.sv ? rendered.sv : "null");
            val_free(text);
            text = make_str(named);
            free(named);
        }
    }
    object_set(result.obj, "depth", depth);
    object_set(result.obj, "sources", sources);
    object_set(result.obj, "value", rendered);
    object_set(result.obj, "text", text);
    return result;
}

/* ================================================================
   Environment Management
   ================================================================ */

Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->parent = parent;
    e->slots = NULL;
    e->slot_count = 0;
    e->slot_capacity = 0;
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
    free(env->slots);
    free(env);
}

SubVal env_get_node(ASTNode *node, Env *env, const char *name) {
    /* Fast path: direct slot access when the identifier was resolved to the
       current env (depth 0). Depth > 0 is context-dependent so not cached. */
    if (node && node->resolved_slot >= 0 && node->resolved_depth == 0) {
        if (node->resolved_slot < env->slot_count)
            return env->slots[node->resolved_slot]->val;
    }
    /* Slow path: linear name search */
    for (Env *s = env; s; s = s->parent)
        for (EnvEntry *e = s->vars; e; e = e->next)
            if (strcmp(e->name, name) == 0) {
                /* Cache only depth-0 resolutions (variable in current env) */
                if (node && s == env) {
                    node->resolved_depth = 0;
                    node->resolved_slot = e->slot;
                }
                return e->val;
            }
    runtime_error("undefined variable '%s'", name);
    return NULL_VAL;
}

SubVal env_get(Env *env, const char *name) {
    return env_get_node(NULL, env, name);
}

static void env_ensure_slot(Env *env, int slot) {
    if (slot < 0) return;
    if (slot >= env->slot_capacity) {
        int new_cap = env->slot_capacity ? env->slot_capacity * 2 : 4;
        while (new_cap <= slot) new_cap *= 2;
        EnvEntry **ns = realloc(env->slots, (size_t)new_cap * sizeof(EnvEntry *));
        if (!ns) return;
        for (int i = env->slot_capacity; i < new_cap; i++)
            ns[i] = NULL;
        env->slots = ns;
        env->slot_capacity = new_cap;
    }
}

void env_define(Env *env, const char *name, SubVal val) {
    if (!env) return;
    EnvEntry *e = calloc(1, sizeof(EnvEntry));
    e->name = strdup(name);
    e->val  = val_copy(val);      /* see val_copy: a value, not a share */
    e->slot = env->slot_count;
    env_ensure_slot(env, e->slot);
    env->slots[e->slot] = e;
    env->slot_count++;
    e->next = env->vars;
    env->vars = e;
}

void env_set_node(ASTNode *node, Env *env, const char *name, SubVal val) {
    /* Fast path: direct slot access when the identifier was resolved to the
       current env (depth 0). Depth > 0 is context-dependent so not cached. */
    if (node && node->resolved_slot >= 0 && node->resolved_depth == 0) {
        if (node->resolved_slot < env->slot_count) {
            val_free(env->slots[node->resolved_slot]->val);
            env->slots[node->resolved_slot]->val = val_copy(val);
            return;
        }
    }
    /* Slow path: linear name search */
    for (Env *s = env; s; s = s->parent)
        for (EnvEntry *e = s->vars; e; e = e->next)
            if (strcmp(e->name, name) == 0) {
                if (node && s == env) {
                    node->resolved_depth = 0;
                    node->resolved_slot = e->slot;
                }
                val_free(e->val);
                e->val = val_copy(val);
                return;
            }
    /* Variable not found - create in current scope */
    env_define(env, name, val);
}

void env_set(Env *env, const char *name, SubVal val) {
    env_set_node(NULL, env, name, val);
}

/* ================================================================
   Binary Expression Evaluation
   ================================================================ */

static SubVal eval_binary(ASTNode *node, Env *env) {
    const char *op = node->value;

    /* --- Logical short-circuit (eval right lazily) --- */
    if (strcmp(op, "&&") == 0) {
        SubVal L = eval(node->left, env);
        if (!is_truthy(L)) { val_free(L); return make_bool(0); }
        SubVal R = eval(node->right, env);
        SubVal result = make_bool(is_truthy(R));
        val_free(L);
        val_free(R);
        return result;
    }
    if (strcmp(op, "||") == 0) {
        SubVal L = eval(node->left, env);
        if (is_truthy(L)) { val_free(L); return make_bool(1); }
        SubVal R = eval(node->right, env);
        SubVal result = make_bool(is_truthy(R));
        val_free(L);
        val_free(R);
        return result;
    }

    SubVal L = eval(node->left, env);
    SubVal R = eval(node->right, env);
    g_binary_left = L.provenance;
    g_binary_right = R.provenance;
    /* The operands may be released below before the caller adds the
       expression node to the provenance chain. Keep these parent references
       alive until that attachment is complete. */
    provenance_retain(g_binary_left);
    provenance_retain(g_binary_right);

    /* Extract all data into locals. L and R are freed after the string
       comparison block, since lsv/rsv point into their heap memory. */
    ValType ltype = L.type, rtype = R.type;
    const char *lsv = L.sv ? L.sv : "";
    const char *rsv = R.sv ? R.sv : "";
    long long liv = L.iv, riv = R.iv;
    double lfv = L.fv, rfv = R.fv;
    int lbv = L.bv, rbv = R.bv;
    ASTNode *lfn = L.fn, *rfn = R.fn;
    SubArray *larr = L.arr, *rarr = R.arr;
    SubObject *lobj = L.obj, *robj = R.obj;

    /* --- String concatenation (+) --- */
    if (strcmp(op, "+") == 0 && (ltype == VAL_STRING || rtype == VAL_STRING)) {
        size_t l_len = strlen(lsv);
        size_t r_len = strlen(rsv);
        size_t total = l_len + r_len + 1;
        char *buf = NULL;

        if (ltype == VAL_STRING) {
            /* Capacity-doubling append: try to grow L's buffer.
               Makes s = s + x amortized O(1) instead of O(n). */
            /* Grow with room for a future append.  The allocator's usable
               size is not portable (and is unavailable on Windows/macOS). */
            size_t new_cap = total * 2;
            char *new_sv = (char *)realloc((char *)lsv, new_cap);
            if (new_sv) {
                L.sv = NULL;
                memcpy(new_sv + l_len, rsv, r_len + 1);
                SubVal r = {VAL_STRING, .sv = new_sv};
                val_free(L);
                val_free(R);
                return r;
            }
        }

        /* Fallback: allocate a fresh buffer */
        buf = (char *)malloc(total);
        if (buf) {
            memcpy(buf, lsv, l_len);
            memcpy(buf + l_len, rsv, r_len + 1);
        } else {
            buf = strdup("");
        }
        val_free(L);
        val_free(R);
        SubVal r = {VAL_STRING, .sv = buf};
        return r;
    }

    /* --- String comparison --- */
    if (ltype == VAL_STRING || rtype == VAL_STRING) {
        if (ltype != VAL_STRING || rtype != VAL_STRING) {
            if (strcmp(op, "==") == 0) return make_bool(0);
            if (strcmp(op, "!=") == 0) return make_bool(1);
            runtime_error("cannot compare string with %s using '%s'",
                    type_name(ltype == VAL_STRING ? rtype : ltype), op);
            return NULL_VAL;
        }
        int cmp = strcmp(lsv, rsv);
        if (strcmp(op, "==") == 0) return make_bool(cmp == 0);
        if (strcmp(op, "!=") == 0) return make_bool(cmp != 0);
        if (strcmp(op, "<")  == 0) return make_bool(cmp <  0);
        if (strcmp(op, "<=") == 0) return make_bool(cmp <= 0);
        if (strcmp(op, ">")  == 0) return make_bool(cmp >  0);
        if (strcmp(op, ">=") == 0) return make_bool(cmp >= 0);
        runtime_error("operator '%s' not supported for strings", op);
        return NULL_VAL;
    }
    val_free(L);
    val_free(R);

    /* --- Same-type non-numeric comparison --- */
    if (ltype == rtype) {
        switch (ltype) {
        case VAL_NULL:
            if (strcmp(op, "==") == 0) return make_bool(1);
            if (strcmp(op, "!=") == 0) return make_bool(0);
            runtime_error("cannot use '%s' on null", op);
            return NULL_VAL;
        case VAL_BOOL:
            if (strcmp(op, "==") == 0) return make_bool(lbv == rbv);
            if (strcmp(op, "!=") == 0) return make_bool(lbv != rbv);
            break; /* fall through to numeric */
        case VAL_FUNC:
            if (strcmp(op, "==") == 0) return make_bool(lfn == rfn);
            if (strcmp(op, "!=") == 0) return make_bool(lfn != rfn);
            runtime_error("cannot use '%s' on functions", op);
            return NULL_VAL;
        case VAL_ARRAY:
            if (strcmp(op, "==") == 0) return make_bool(larr == rarr);
            if (strcmp(op, "!=") == 0) return make_bool(larr != rarr);
            runtime_error("cannot use '%s' on arrays", op);
            return NULL_VAL;
        case VAL_OBJECT:
            if (strcmp(op, "==") == 0) return make_bool(lobj == robj);
            if (strcmp(op, "!=") == 0) return make_bool(lobj != robj);
            runtime_error("cannot use '%s' on objects", op);
            return NULL_VAL;
        default:
            break;
        }
    }

    /* --- Mixed type check --- */
    int both_numeric = (ltype == VAL_INT || ltype == VAL_FLOAT) &&
                       (rtype == VAL_INT || rtype == VAL_FLOAT);
    if (!both_numeric) {
        if (strcmp(op, "==") == 0) return make_bool(0);
        if (strcmp(op, "!=") == 0) return make_bool(1);
        runtime_error("cannot use '%s' on %s and %s",
                op, type_name(ltype), type_name(rtype));
        return NULL_VAL;
    }

    /* --- Numeric operations --- */
    /* Integers stay integers: routing int+int through a double silently
       loses the low bits above 2^53, so 4611686018427387904 + 1 answered
       ...904 through the interpreter while every other backend said ...905.
       Floats still go through double, and a comparison may mix the two. */
    int use_float = (ltype == VAL_FLOAT || rtype == VAL_FLOAT);
    long long ia = liv, ib = riv;
    double a = (ltype == VAL_FLOAT) ? lfv : (double)liv;
    double b = (rtype == VAL_FLOAT) ? rfv : (double)riv;

    /* Arithmetic */
    if (strcmp(op, "+") == 0)  return use_float ? make_float(a + b)    : make_int(ia + ib);
    if (strcmp(op, "-") == 0)  return use_float ? make_float(a - b)    : make_int(ia - ib);
    if (strcmp(op, "*") == 0)  return use_float ? make_float(a * b)    : make_int(ia * ib);

    if (strcmp(op, "/") == 0) {
        if (use_float ? b == 0.0 : ib == 0) { runtime_error("division by zero"); return NULL_VAL; }
        return use_float ? make_float(a / b) : make_int(ia / ib);
    }

    if (strcmp(op, "%") == 0) {
        if (use_float ? b == 0.0 : ib == 0) { runtime_error("modulo by zero"); return NULL_VAL; }
        return use_float ? make_float(fmod(a, b)) : make_int(ia % ib);
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
        /* Walk children[] array (primary storage).
           Inline AST_IDENTIFIER to avoid the full eval() dispatch
           overhead for the common case of variable lookups. */
        for (int i = 0; i < node->child_count && !should_stop(env); i++) {
            ASTNode *child = node->children[i];
            if (child->type == AST_IDENTIFIER)
                last = val_copy(env_get_node(child, env, child->value));
            else if (child->type == AST_LITERAL) {
                if (child->data_type == TYPE_NULL) last = NULL_VAL;
                else if (child->data_type == TYPE_STRING) last = make_str(child->value);
                else if (child->data_type == TYPE_BOOL)
                    last = make_bool(child->value && strcmp(child->value, "true") == 0);
                else if (child->data_type == TYPE_FLOAT)
                    last = make_float(atof(child->value ? child->value : "0"));
                else last = make_int(atoll(child->value ? child->value : "0"));
            } else
                last = eval(child, env);
        }
    } else {
        /* Walk body->next linked-list chain (fallback for parser linked-list storage) */
        for (ASTNode *n = node->body; n && !should_stop(env); n = n->next) {
            last = eval(n, env);
        }
    }

    return last;
}

/* First statement of a statement list, honouring the same dual storage
   (children[] array or body->next chain) that eval_block walks. */
static ASTNode *stmt_first(ASTNode *node) {
    if (!node) return NULL;
    if (node->child_count > 0) return node->children[0];
    return node->body;
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
        if (call_node && call_node->child_count > 0) {
            SubVal item = eval(call_node->children[0], env);
            if (expr_borrows_binding(call_node->children[0]))
                item = val_copy(item);
            array_push(arr, item);
        }
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
        if (node->type == AST_PROGRAM)
            g_provenance_enabled = ast_contains_why(node);
        return eval_block(node, env);

    /* ============================================================
       Literals
       ============================================================ */
    case AST_LITERAL: {
        SubVal value;
        if (node->data_type == TYPE_NULL) value = NULL_VAL;
        else if (node->data_type == TYPE_STRING) value = make_str(node->value);
        else if (node->data_type == TYPE_BOOL)
            value = make_bool(node->value && strcmp(node->value, "true") == 0);
        else if (node->data_type == TYPE_FLOAT)
            value = make_float(atof(node->value ? node->value : "0"));
        else value = make_int(atoll(node->value ? node->value : "0"));
        provenance_attach(&value, node, NULL, NULL, NULL, 0);
        return value;
    }

    /* ============================================================
       Identifier
       ============================================================ */
    case AST_IDENTIFIER:
        return val_copy(env_get_node(node, env, node->value));

    /* ============================================================
       Variable / Constant Declaration
       ============================================================ */
    case AST_VAR_DECL:
    case AST_CONST_DECL: {
        SubVal val = node->right ? eval(node->right, env) : NULL_VAL;
        if (node->right && g_provenance_enabled) {
            char *text = ast_expression_text(node->right);
            char *label = malloc(strlen(node->value ? node->value : "value") +
                                 strlen(text) + 4);
            if (label) {
                sprintf(label, "%s = %s", node->value ? node->value : "value", text);
                provenance_attach(&val, node, label, val.provenance, NULL, 0);
                free(label);
            }
            free(text);
        }
        env_define(env, node->value, val);
        return val;
    }

    case AST_WHY_EXPR: {
        SubVal value = eval(node->right, env);
        return make_why_value(value, node->right);
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
                if (g_provenance_enabled) {
                    char *text = ast_expression_text(node->right);
                    size_t n = strlen(name ? name : "value") + strlen(text) + 4;
                    char *label = malloc(n);
                    if (label) {
                        snprintf(label, n, "%s = %s", name ? name : "value", text);
                        provenance_attach(&rhs, node, label, rhs.provenance, NULL, 0);
                        free(label);
                    }
                    free(text);
                }
                env_set_node(node->left, env, name, rhs);
                return rhs;
            }

            /* Compound: get current, compute, set */
            SubVal cur = env_get_node(node->left, env, name);

            /* Convert to numbers. Integer paths stay integer: going through
               double here dropped the low bits of large values (see the note
               in eval_binary). */
            long long cia = cur.type == VAL_FLOAT ? (long long)cur.fv : cur.iv;
            long long cib = rhs.type == VAL_FLOAT ? (long long)rhs.fv : rhs.iv;
            int use_float = (cur.type == VAL_FLOAT || rhs.type == VAL_FLOAT);
            double cda = (cur.type == VAL_FLOAT) ? cur.fv : (double)cur.iv;
            double cdb = (rhs.type == VAL_FLOAT) ? rhs.fv : (double)rhs.iv;

            SubVal result;
            if      (strcmp(op, "+=") == 0) result = use_float ? make_float(cda + cdb) : make_int(cia + cib);
            else if (strcmp(op, "-=") == 0) result = use_float ? make_float(cda - cdb) : make_int(cia - cib);
            else if (strcmp(op, "*=") == 0) result = use_float ? make_float(cda * cdb) : make_int(cia * cib);
            else if (strcmp(op, "/=") == 0) {
                if (use_float ? cdb == 0.0 : cib == 0) { runtime_error("division by zero"); return NULL_VAL; }
                result = use_float ? make_float(cda / cdb) : make_int(cia / cib);
            }
            else if (strcmp(op, "%=") == 0) {
                if (use_float ? cdb == 0.0 : cib == 0) { runtime_error("modulo by zero"); return NULL_VAL; }
                result = use_float ? make_float(fmod(cda, cdb)) : make_int(cia % cib);
            }
            else if (strcmp(op, "**=") == 0) {
                result = use_float ? make_float(pow(cda, cdb)) : make_int((long long)pow(cda, cdb));
            }
            else if (strcmp(op, "&=") == 0)  result = make_int(cia & cib);
            else if (strcmp(op, "|=") == 0)  result = make_int(cia | cib);
            else if (strcmp(op, "^=") == 0)  result = make_int(cia ^ cib);
            else if (strcmp(op, "<<=") == 0) result = make_int(cia << cib);
            else if (strcmp(op, ">>=") == 0) result = make_int(cia >> cib);
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

            env_set_node(node->left, env, name, result);
            return result;
        }

        /* Assignment through an index: arr[i] = v, and obj["key"] = v for
           objects. Compound forms go through the array path above. */
        if (node->left && node->left->type == AST_ARRAY_ACCESS &&
            op && strcmp(op, "=") == 0) {
            /* Use env_get_node directly for identifiers so we get a borrowed
               reference and can modify the original array/object in place.
               For non-identifier containers, eval() returns an owned copy. */
            SubVal target;
            if (node->left->left->type == AST_IDENTIFIER) {
                target = env_get_node(node->left->left, env,
                                      node->left->left->value);
            } else {
                target = eval(node->left->left, env);
            }
            if (target.type == VAL_OBJECT && target.obj) {
                SubVal key_val = val_to_str(eval(node->left->right, env));
                object_set(target.obj, key_val.sv, val_copy(rhs));
                val_free(key_val);
                if (node->left->left->type != AST_IDENTIFIER)
                    val_free(target);
                return rhs;
            }
            if (target.type == VAL_ARRAY && target.arr) {
                SubVal idx_val = eval(node->left->right, env);
                long long idx = val_as_int(idx_val);
                val_free(idx_val);
                if (idx < 0) idx += (long long)target.arr->count;
                if (idx < 0 || idx >= target.arr->count) {
                    int count = target.arr->count;   /* read before the free below */
                    if (node->left->left->type != AST_IDENTIFIER)
                        val_free(target);
                    runtime_error("array index %lld out of bounds [0, %d)",
                                  idx, count);
                    return NULL_VAL;
                }
                if (expr_borrows_binding(node->right)) { /* already copied */ }
                val_free(target.arr->items[idx]);
                target.arr->items[idx] = rhs;
                if (node->left->left->type != AST_IDENTIFIER)
                    val_free(target);
                return rhs;
            }
            if (node->left->left->type != AST_IDENTIFIER)
                val_free(target);
            runtime_error("cannot index-assign into %s", type_name(target.type));
            return NULL_VAL;
        }

        /* Compound assignment on array access: arr[i] += val */
        if (node->left && node->left->type == AST_ARRAY_ACCESS) {
            /* Use env_get_node directly for identifiers so we get a borrowed
               reference and can modify the original array in place. */
            SubVal arr_val;
            if (node->left->left->type == AST_IDENTIFIER) {
                arr_val = env_get_node(node->left->left, env,
                                       node->left->left->value);
            } else {
                arr_val = eval(node->left->left, env);
            }
            if (arr_val.type != VAL_ARRAY || !arr_val.arr) {
                if (node->left->left->type != AST_IDENTIFIER)
                    val_free(arr_val);
                runtime_error("compound assignment on non-array");
                return NULL_VAL;
            }
            SubVal idx_val = eval(node->left->right, env);
            long long idx = val_as_int(idx_val);
            val_free(idx_val);
            if (idx < 0) idx += (long long)arr_val.arr->count;
            if (idx < 0 || idx >= arr_val.arr->count) {
                int count = arr_val.arr->count;   /* read before the free below */
                if (node->left->left->type != AST_IDENTIFIER)
                    val_free(arr_val);
                runtime_error("array index %lld out of bounds [0, %d)",
                              idx, count);
                return NULL_VAL;
            }
            SubVal *item = &arr_val.arr->items[idx];
            long long iia = item->type == VAL_FLOAT ? (long long)item->fv : item->iv;
            long long iib = rhs.type == VAL_FLOAT ? (long long)rhs.fv : rhs.iv;
            double ida = (item->type == VAL_FLOAT) ? item->fv : (double)item->iv;
            double idb = (rhs.type == VAL_FLOAT) ? rhs.fv : (double)rhs.iv;
            int uf = (item->type == VAL_FLOAT || rhs.type == VAL_FLOAT);
            SubVal result;
            if      (strcmp(op, "+=") == 0) result = uf ? make_float(ida + idb) : make_int(iia + iib);
            else if (strcmp(op, "-=") == 0) result = uf ? make_float(ida - idb) : make_int(iia - iib);
            else if (strcmp(op, "*=") == 0) result = uf ? make_float(ida * idb) : make_int(iia * iib);
            else if (strcmp(op, "/=") == 0) {
                if (uf ? idb == 0.0 : iib == 0) { runtime_error("division by zero"); if (node->left->left->type != AST_IDENTIFIER) val_free(arr_val); return NULL_VAL; }
                result = uf ? make_float(ida / idb) : make_int(iia / iib);
            }
            else if (strcmp(op, "%=") == 0) {
                if (uf ? idb == 0.0 : iib == 0) { runtime_error("modulo by zero"); if (node->left->left->type != AST_IDENTIFIER) val_free(arr_val); return NULL_VAL; }
                result = uf ? make_float(fmod(ida, idb)) : make_int(iia % iib);
            }
            else result = rhs;
            val_free(*item);
            *item = result;
            if (node->left->left->type != AST_IDENTIFIER)
                val_free(arr_val);
            return result;
        }

        /* Member assignment: obj.field = value (also writes object-literal
           and class instances; `length` stays read-only). */
        if (node->left && node->left->type == AST_MEMBER_ACCESS) {
            const char *member = node->left->value;
            if (op && strcmp(op, "=") != 0) {
                runtime_error("compound assignment '%s' on property '%s' "
                              "is not supported", op, member ? member : "?");
                return NULL_VAL;
            }
            if (member && strcmp(member, "length") == 0) {
                runtime_error("cannot assign to read-only property 'length'");
                return NULL_VAL;
            }
            /* Use env_get_node directly for identifiers so we get a borrowed
               reference and can modify the original object in place. */
            SubVal target;
            if (node->left->left->type == AST_IDENTIFIER) {
                target = env_get_node(node->left->left, env,
                                      node->left->left->value);
            } else {
                target = eval(node->left->left, env);
            }
            if (target.type != VAL_OBJECT || !target.obj) {
                if (node->left->left->type != AST_IDENTIFIER)
                    val_free(target);
                runtime_error("cannot set property '%s' on %s",
                        member ? member : "?", type_name(target.type));
                return NULL_VAL;
            }
            object_set(target.obj, member, val_copy(rhs));
            if (node->left->left->type != AST_IDENTIFIER)
                val_free(target);
            return rhs;
        }

        /* Simple assignment fallback */
        env_set_node(node->left, env, node->left ? node->left->value : "", rhs);
        return rhs;
    }

    /* ============================================================
       Binary Expression
       ============================================================ */
    case AST_BINARY_EXPR: {
        SubVal result = eval_binary(node, env);
        if (g_provenance_enabled) {
            provenance_attach(&result, node, NULL, g_binary_left, g_binary_right, 0);
            provenance_release(g_binary_left);
            provenance_release(g_binary_right);
        }
        return result;
    }

    /* ============================================================
       Unary Expression (-, !, +, ~, ++, --)
       ============================================================ */
    case AST_UNARY_EXPR: {
        const char *op = node->value;

        /* Pre-increment: ++var */
        if (op && strcmp(op, "++") == 0 && node->right && node->right->type == AST_IDENTIFIER) {
            const char *name = node->right->value;
            SubVal cur = env_get_node(node->right, env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv + 1.0) : make_int(cur.iv + 1);
            env_set_node(node->right, env, name, nxt);
            return nxt;
        }

        /* Pre-decrement: --var */
        if (op && strcmp(op, "--") == 0 && node->right && node->right->type == AST_IDENTIFIER) {
            const char *name = node->right->value;
            SubVal cur = env_get_node(node->right, env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv - 1.0) : make_int(cur.iv - 1);
            env_set_node(node->right, env, name, nxt);
            return nxt;
        }

        /* Post-increment: var++ (operand in left) */
        if (op && strcmp(op, "++") == 0 && node->left && node->left->type == AST_IDENTIFIER) {
            const char *name = node->left->value;
            SubVal cur = env_get_node(node->left, env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv + 1.0) : make_int(cur.iv + 1);
            env_set_node(node->left, env, name, nxt);
            return cur; /* return old value */
        }

        /* Post-decrement: var-- (operand in left) */
        if (op && strcmp(op, "--") == 0 && node->left && node->left->type == AST_IDENTIFIER) {
            const char *name = node->left->value;
            SubVal cur = env_get_node(node->left, env, name);
            SubVal nxt = (cur.type == VAL_FLOAT) ? make_float(cur.fv - 1.0) : make_int(cur.iv - 1);
            env_set_node(node->left, env, name, nxt);
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
       Class Declaration
       ------------------------------------------------------------
       `class Point { let x = 0 }` evaluates to a constructor: the class
       name is bound to the decl's AST node, and `Point.new` builds an
       object by evaluating each field declaration in a fresh scope. A
       class body holding only field declarations is what the parser
       produces today; a method (fn) inside the body is refused with a
       clear error rather than being silently dropped.
       ============================================================ */
    case AST_CLASS_DECL: {
        if (!node->value) return NULL_VAL;
        for (ASTNode *stmt = stmt_first(node->body ? node->body : node); stmt; stmt = stmt->next) {
            if (stmt->type != AST_VAR_DECL && stmt->type != AST_CONST_DECL) {
                runtime_error("class %s: only field declarations are supported "
                              "in the interpreter so far (line %d)",
                              node->value, stmt->line);
                return NULL_VAL;
            }
        }
        SubVal ctor = {VAL_FUNC, {.fn = node}, NULL};
        env_define(env, node->value, ctor);
        /* env_define stores a copy of the value, and a VAL_FUNC "copy" is
           the same node pointer, so the definition outlives this eval. */
        return NULL_VAL;
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
            if (strcmp(member, "length") == 0) {
                SubVal result = make_int((long long)strlen(obj.sv ? obj.sv : ""));
                val_free(obj);
                return result;
            }
        }

        /* Array property */
        if (obj.type == VAL_ARRAY && obj.arr) {
            if (strcmp(member, "length") == 0) {
                SubVal result = make_int((long long)obj.arr->count);
                val_free(obj);
                return result;
            }
        }

        /* Object property: .length is the property count, as len() is */
        if (obj.type == VAL_OBJECT && obj.obj) {
            if (strcmp(member, "length") == 0) {
                SubVal result = make_int((long long)obj.obj->count);
                val_free(obj);
                return result;
            }
            SubVal result = val_copy(object_get(obj.obj, member));
            val_free(obj);
            return result;
        }

        /* Class constructor: `Point.new` evaluates the class's field
           declarations into a fresh object. Anything else named `.new` on
           a non-object is a runtime error below. */
        if (obj.type == VAL_FUNC && obj.fn &&
            obj.fn->type == AST_CLASS_DECL && strcmp(member, "new") == 0) {
            SubVal inst = make_object_val();
            Env *ctor_env = env_new(env);
            for (ASTNode *stmt = stmt_first(obj.fn->body ? obj.fn->body : obj.fn); stmt; stmt = stmt->next) {
                if (!stmt->value) continue;
                SubVal field = stmt->right ? eval(stmt->right, ctor_env) : NULL_VAL;
                /* object_set stores the value without copying, so hand it
                   a copy; the evaluated field value is freed below. */
                object_set(inst.obj, stmt->value, val_copy(field));
                /* object_set copies, so the evaluated value is ours. */
                val_free(field);
            }
            env_free(ctor_env);
            val_free(obj);
            return inst;
        }

        val_free(obj);
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
                /* Capture the count before freeing: the error message reads
                   it, and val_free(container) releases the array it lives in. */
                int count = container.arr->count;
                val_free(container);
                val_free(idx);
                runtime_error("array index %lld out of bounds [0, %d)",
                        i, count);
                return NULL_VAL;
            }
            SubVal result = val_copy(container.arr->items[i]);
            val_free(container);
            val_free(idx);
            return result;
        }

        if (container.type == VAL_STRING) {
            const char *str = container.sv ? container.sv : "";
            long long slen = (long long)strlen(str);
            if (i < 0) i += slen;
            if (i < 0 || i >= slen) {
                val_free(container);
                val_free(idx);
                runtime_error("string index %lld out of bounds", i);
                return NULL_VAL;
            }
            char ch[2] = {str[i], '\0'};
            SubVal result = make_str(ch);
            val_free(container);
            val_free(idx);
            return result;
        }

        /* Object property access: obj["key"] */
        if (container.type == VAL_OBJECT && container.obj) {
            SubVal key_val = val_to_str(idx);
            SubVal result = val_copy(object_get(container.obj, key_val.sv));
            val_free(key_val);
            val_free(container);
            val_free(idx);
            return result;
        }

        val_free(container);
        val_free(idx);
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
                if (g_runtime_aborted) { val_free(arg); return NULL_VAL; }
                print_val(arg);
                val_free(arg);
                printf("\n");
            }
            return NULL_VAL;
        }

        /* println: all args on one line, space-separated, then newline */
        if (fn && strcmp(fn, "println") == 0) {
            for (int i = 0; i < node->child_count; i++) {
                SubVal arg = eval(node->children[i], env);
                if (g_runtime_aborted) { val_free(arg); return NULL_VAL; }
                if (i > 0) printf(" ");
                print_val(arg);
                val_free(arg);
            }
            printf("\n");
            return NULL_VAL;
        }

        /* str / to_string: convert to string */
        if (fn && (strcmp(fn, "str") == 0 || strcmp(fn, "to_string") == 0)) {
            if (node->child_count > 0) {
                SubVal v = eval(node->children[0], env);
                SubVal r = val_to_str(v);
                val_free(v);
                return r;
            }
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
                long long result = 0;
                if (v.type == VAL_STRING) result = (long long)strlen(v.sv ? v.sv : "");
                else if (v.type == VAL_ARRAY  && v.arr) result = (long long)v.arr->count;
                else if (v.type == VAL_OBJECT && v.obj) result = (long long)v.obj->count;
                val_free(v);
                return make_int(result);
            }
            return make_int(0);
        }

        /* type: return type name as string */
        if (fn && strcmp(fn, "type") == 0) {
            if (node->child_count > 0) {
                SubVal v = eval(node->children[0], env);
                SubVal r = make_str(type_name(v.type));
                val_free(v);
                return r;
            }
            return make_str("null");
        }

        /* abs: absolute value */
        if (fn && strcmp(fn, "abs") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            SubVal result;
            if (v.type == VAL_FLOAT) result = make_float(fabs(v.fv));
            else if (v.type == VAL_INT) result = make_int(llabs(v.iv));
            else result = v;
            val_free(v);
            return result;
        }

        /* min: minimum of two values */
        if (fn && strcmp(fn, "min") == 0 && node->child_count >= 2) {
            SubVal a = eval(node->children[0], env);
            SubVal b = eval(node->children[1], env);
            double da = (a.type == VAL_FLOAT) ? a.fv : (double)a.iv;
            double db = (b.type == VAL_FLOAT) ? b.fv : (double)b.iv;
            int uf = (a.type == VAL_FLOAT || b.type == VAL_FLOAT);
            SubVal result = uf ? make_float(da < db ? da : db) : make_int(da < db ? (long long)da : (long long)db);
            val_free(a);
            val_free(b);
            return result;
        }

        /* max: maximum of two values */
        if (fn && strcmp(fn, "max") == 0 && node->child_count >= 2) {
            SubVal a = eval(node->children[0], env);
            SubVal b = eval(node->children[1], env);
            double da = (a.type == VAL_FLOAT) ? a.fv : (double)a.iv;
            double db = (b.type == VAL_FLOAT) ? b.fv : (double)b.iv;
            int uf = (a.type == VAL_FLOAT || b.type == VAL_FLOAT);
            SubVal result = uf ? make_float(da > db ? da : db) : make_int(da > db ? (long long)da : (long long)db);
            val_free(a);
            val_free(b);
            return result;
        }

        /* sqrt: square root */
        if (fn && strcmp(fn, "sqrt") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            val_free(v);
            return make_float(sqrt(d));
        }

        /* floor: floor */
        if (fn && strcmp(fn, "floor") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            val_free(v);
            return make_int((long long)floor(d));
        }

        /* ceil: ceiling */
        if (fn && strcmp(fn, "ceil") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            val_free(v);
            return make_int((long long)ceil(d));
        }

        /* round: round to nearest integer */
        if (fn && strcmp(fn, "round") == 0 && node->child_count > 0) {
            SubVal v = to_number(eval(node->children[0], env));
            double d = (v.type == VAL_FLOAT) ? v.fv : (double)v.iv;
            val_free(v);
            return make_int((long long)round(d));
        }

        /* upper: string to uppercase */
        if (fn && strcmp(fn, "upper") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            SubVal result;
            if (v.type == VAL_STRING && v.sv) {
                char *r = strdup(v.sv);
                for (int i = 0; r[i]; i++)
                    r[i] = (char)toupper((unsigned char)r[i]);
                result = (SubVal){VAL_STRING, .sv = r};
            } else {
                result = v;
            }
            val_free(v);
            return result;
        }

        /* lower: string to lowercase */
        if (fn && strcmp(fn, "lower") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            SubVal result;
            if (v.type == VAL_STRING && v.sv) {
                char *r = strdup(v.sv);
                for (int i = 0; r[i]; i++)
                    r[i] = (char)tolower((unsigned char)r[i]);
                result = (SubVal){VAL_STRING, .sv = r};
            } else {
                result = v;
            }
            val_free(v);
            return result;
        }

        /* split(s[, sep]) */
        if (fn && strcmp(fn, "split") == 0 && node->child_count > 0) {
            SubVal sv = eval(node->children[0], env);
            SubVal result;
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = node->child_count > 1 ? &node->children[1] : NULL;
                result = eval_string_method(sv.sv, "split", &temp, env);
            } else {
                result = make_array_val();
            }
            val_free(sv);
            return result;
        }

        /* join: join array elements with separator */
        if (fn && strcmp(fn, "join") == 0 && node->child_count >= 1) {
            SubVal arr = eval(node->children[0], env);
            SubVal result;
            if (arr.type == VAL_ARRAY && arr.arr) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = node->child_count > 1 ? &node->children[1] : NULL;
                result = eval_array_method(arr.arr, "join", &temp, env);
            } else {
                result = make_str("");
            }
            val_free(arr);
            return result;
        }

        /* substring(s, start[, end]) */
        if (fn && strcmp(fn, "substring") == 0 && node->child_count >= 2) {
            SubVal sv = eval(node->children[0], env);
            SubVal result;
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                result = eval_string_method(sv.sv, "substring", &temp, env);
            } else {
                result = NULL_VAL;
            }
            val_free(sv);
            return result;
        }

        /* contains(s, sub) */
        if (fn && strcmp(fn, "contains") == 0 && node->child_count >= 2) {
            SubVal sv = eval(node->children[0], env);
            SubVal result;
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                result = eval_string_method(sv.sv, "contains", &temp, env);
            } else {
                result = make_bool(0);
            }
            val_free(sv);
            return result;
        }

        /* replace(s, old, new) */
        if (fn && strcmp(fn, "replace") == 0 && node->child_count >= 3) {
            SubVal sv = eval(node->children[0], env);
            SubVal result;
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                result = eval_string_method(sv.sv, "replace", &temp, env);
            } else {
                result = NULL_VAL;
            }
            val_free(sv);
            return result;
        }

        /* trim(s) */
        if (fn && strcmp(fn, "trim") == 0 && node->child_count > 0) {
            SubVal sv = eval(node->children[0], env);
            SubVal result;
            if (sv.type == VAL_STRING) {
                result = eval_string_method(sv.sv, "trim", NULL, env);
            } else {
                result = sv;
            }
            val_free(sv);
            return result;
        }

        /* char_at(s, i) */
        if (fn && strcmp(fn, "char_at") == 0 && node->child_count >= 2) {
            SubVal sv = eval(node->children[0], env);
            SubVal result;
            if (sv.type == VAL_STRING) {
                ASTNode temp = *node;
                temp.child_count = node->child_count - 1;
                temp.children    = &node->children[1];
                result = eval_string_method(sv.sv, "char_at", &temp, env);
            } else {
                result = NULL_VAL;
            }
            val_free(sv);
            return result;
        }

        /* push(arr, item) */
        if (fn && strcmp(fn, "push") == 0 && node->child_count >= 2) {
            int borrowed_arr = node->children[0]->type == AST_IDENTIFIER;
            SubVal arr = borrowed_arr
                ? env_get_node(node->children[0], env, node->children[0]->value)
                : eval(node->children[0], env);
            if (arr.type == VAL_ARRAY && arr.arr) {
                SubVal item = eval(node->children[1], env);
                /* The array takes ownership, so a value that is still a
                   binding's has to be copied first -- otherwise the array and
                   the scope both free it. */
                if (expr_borrows_binding(node->children[1]))
                    item = val_copy(item);
                array_push(arr.arr, item);
            }
            if (!borrowed_arr)
                val_free(arr);
            return NULL_VAL;
        }

        /* append(arr, item) - alias for push */
        if (fn && strcmp(fn, "append") == 0 && node->child_count >= 2) {
            int borrowed_arr = node->children[0]->type == AST_IDENTIFIER;
            SubVal arr = borrowed_arr
                ? env_get_node(node->children[0], env, node->children[0]->value)
                : eval(node->children[0], env);
            if (arr.type == VAL_ARRAY && arr.arr) {
                SubVal item = eval(node->children[1], env);
                /* The array takes ownership, so a value that is still a
                   binding's has to be copied first -- otherwise the array and
                   the scope both free it. */
                if (expr_borrows_binding(node->children[1]))
                    item = val_copy(item);
                array_push(arr.arr, item);
            }
            if (!borrowed_arr)
                val_free(arr);
            return NULL_VAL;
        }

        /* pop(arr) */
        if (fn && strcmp(fn, "pop") == 0 && node->child_count > 0) {
            int borrowed_arr = node->children[0]->type == AST_IDENTIFIER;
            SubVal arr = borrowed_arr
                ? env_get_node(node->children[0], env, node->children[0]->value)
                : eval(node->children[0], env);
            SubVal result;
            if (arr.type == VAL_ARRAY && arr.arr)
                result = array_pop(arr.arr);
            else
                result = NULL_VAL;
            if (!borrowed_arr)
                val_free(arr);
            return result;
        }

        /* input([prompt]) */
        if (fn && strcmp(fn, "input") == 0) {
            SubVal prompt = NULL_VAL;
            if (node->child_count > 0) {
                prompt = eval(node->children[0], env);
                print_val(prompt);
                val_free(prompt);
            }
            char buf[1024];
            if (!fgets(buf, sizeof(buf), stdin)) {
                SubVal empty = make_str("");
                provenance_attach(&empty, node, "input() = \"\"", NULL, NULL, 1);
                return empty;
            }
            size_t l = strlen(buf);
            if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
            SubVal input = make_str(buf);
            char label[1200];
            snprintf(label, sizeof(label), "input() = %s", buf);
            provenance_attach(&input, node, label, NULL, NULL, 1);
            return input;
        }

        if (fn && strcmp(fn, "read_file") == 0) {
            if (node->child_count < 1) runtime_error("read_file needs a path");
            SubVal pv = eval(node->children[0], env);
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            size_t len = 0;
            char *data = read_file_bytes(path, &len);
            val_free(pv);
            if (!data) return NULL_VAL;
            (void)len;
            return (SubVal){VAL_STRING, {.sv = data}, NULL};
        }

        if (fn && strcmp(fn, "sha256") == 0) {
            if (node->child_count < 1) runtime_error("sha256 needs a string");
            SubVal input = eval(node->children[0], env);
            const char *text = input.type == VAL_STRING && input.sv ? input.sv : "";
            char *digest = sha256_string(text);
            val_free(input);
            if (!digest) return NULL_VAL;
            return (SubVal){VAL_STRING, {.sv = digest}, NULL};
        }

        if (fn && (strcmp(fn, "write_file") == 0 ||
                   strcmp(fn, "append_file") == 0)) {
            if (node->child_count < 2)
                runtime_error("%s needs a path and content", fn);
            SubVal pv = eval(node->children[0], env);
            SubVal cv = eval(node->children[1], env);
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            const char *content = cv.type == VAL_STRING && cv.sv ? cv.sv : "";
            FILE *f = fopen(path, strcmp(fn, "write_file") == 0 ? "wb" : "ab");
            if (!f) {
                val_free(pv);
                val_free(cv);
                runtime_error("%s: cannot open '%s': %s", fn, path, strerror(errno));
                return NULL_VAL;
            }
            size_t len = strlen(content);
            size_t wrote = len ? fwrite(content, 1, len, f) : 0;
            int failed = ferror(f);
            fclose(f);
            val_free(pv);
            val_free(cv);
            if (failed || wrote != len) {
                runtime_error("%s: cannot write '%s'", fn, path);
                return NULL_VAL;
            }
            return make_int((long long)wrote);
        }

        if (fn && (strcmp(fn, "file_exists") == 0 || strcmp(fn, "is_dir") == 0)) {
            if (node->child_count < 1)
                runtime_error("%s needs a path", fn);
            SubVal pv = eval(node->children[0], env);
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            struct stat st;
            int exists = stat(path, &st) == 0;
            val_free(pv);
            if (strcmp(fn, "is_dir") == 0) exists = exists && S_ISDIR(st.st_mode);
            return make_bool(exists);
        }

        if (fn && strcmp(fn, "mkdir") == 0) {
            if (node->child_count < 1) runtime_error("mkdir needs a path");
            SubVal pv = eval(node->children[0], env);
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            if (mkdir_parents(path) != 0) {
                val_free(pv);
                runtime_error("mkdir: cannot create '%s': %s", path, strerror(errno));
                return NULL_VAL;
            }
            val_free(pv);
            return make_bool(1);
        }

        if (fn && strcmp(fn, "list_dir") == 0) {
            if (node->child_count < 1) runtime_error("list_dir needs a path");
            SubVal pv = eval(node->children[0], env);
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            SubVal result = make_array_val();
#ifdef _WIN32
            char pattern[4096];
            if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >= (int)sizeof(pattern))
                runtime_error("list_dir: path too long '%s'", path);
            struct _finddata_t item;
            intptr_t h = _findfirst(pattern, &item);
            if (h == -1) {
                val_free(pv);
                runtime_error("list_dir: cannot read '%s': %s", path, strerror(errno));
                return NULL_VAL;
            }
            do {
                if (strcmp(item.name, ".") && strcmp(item.name, ".."))
                    array_push(result.arr, make_str(item.name));
            } while (_findnext(h, &item) == 0);
            _findclose(h);
#else
            DIR *dir = opendir(path);
            if (!dir) {
                val_free(pv);
                runtime_error("list_dir: cannot read '%s': %s", path, strerror(errno));
                return NULL_VAL;
            }
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL)
                if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
                    array_push(result.arr, make_str(entry->d_name));
            closedir(dir);
#endif
            val_free(pv);
            for (int i = 0; i < result.arr->count; i++)
                for (int j = i + 1; j < result.arr->count; j++)
                    if (strcmp(result.arr->items[i].sv, result.arr->items[j].sv) > 0) {
                        SubVal t = result.arr->items[i];
                        result.arr->items[i] = result.arr->items[j];
                        result.arr->items[j] = t;
                    }
            return result;
        }

        if (fn && strcmp(fn, "remove") == 0) {
            if (node->child_count < 1) runtime_error("remove needs a path");
            SubVal pv = eval(node->children[0], env);
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            int rc = rmdir(path);
            if (rc != 0 && (errno == ENOTDIR || errno == EINVAL))
                rc = remove(path);
            if (rc != 0 && errno == ENOENT) {
                val_free(pv);
                return make_bool(0);
            }
            if (rc != 0) {
                runtime_error("remove: cannot remove '%s': %s", path, strerror(errno));
                val_free(pv);
                return NULL_VAL;
            }
            val_free(pv);
            return make_bool(1);
        }

        if (fn && strcmp(fn, "rename") == 0) {
            if (node->child_count < 2) runtime_error("rename needs source and destination");
            SubVal from = eval(node->children[0], env);
            SubVal to = eval(node->children[1], env);
            const char *src = from.type == VAL_STRING && from.sv ? from.sv : "";
            const char *dst = to.type == VAL_STRING && to.sv ? to.sv : "";
            if (rename(src, dst) != 0) {
                val_free(from);
                val_free(to);
                runtime_error("rename: cannot rename '%s' to '%s': %s",
                              src, dst, strerror(errno));
                return NULL_VAL;
            }
            val_free(from);
            val_free(to);
            return make_bool(1);
        }

        if (fn && strcmp(fn, "env") == 0) {
            if (node->child_count < 1) runtime_error("env needs a name");
            SubVal nv = eval(node->children[0], env);
            const char *name = nv.type == VAL_STRING && nv.sv ? nv.sv : "";
            const char *value = getenv(name);
            val_free(nv);
            return make_str(value ? value : "");
        }

        if (fn && strcmp(fn, "args") == 0)
            return builtin_args();

        if (fn && strcmp(fn, "object_keys") == 0) {
            if (node->child_count < 1) runtime_error("object_keys needs an object");
            SubVal ov = eval(node->children[0], env);
            SubVal result;
            if (ov.type != VAL_OBJECT || !ov.obj) {
                runtime_error("object_keys needs an object");
                val_free(ov);
                return NULL_VAL;
            }
            result = make_array_val();
            for (int i = 0; i < ov.obj->count; i++)
                array_push(result.arr, make_str(ov.obj->keys[i]));
            val_free(ov);
            return result;
        }

        if (fn && strcmp(fn, "exit") == 0) {
            long long code = 0;
            if (node->child_count > 0) {
                SubVal cv = eval(node->children[0], env);
                code = val_as_int(cv);
                val_free(cv);
            }
            exit((int)code);
        }

        if (fn && strcmp(fn, "exec") == 0) {
            if (node->child_count < 1) runtime_error("exec needs a command");
            SubVal cv = eval(node->children[0], env);
            const char *command = cv.type == VAL_STRING && cv.sv ? cv.sv : "";
#ifdef _WIN32
            FILE *pipe = _popen(command, "r");
#else
            FILE *pipe = popen(command, "r");
#endif
            if (!pipe) {
                runtime_error("exec: cannot run command '%s': %s", command, strerror(errno));
                val_free(cv);
                return NULL_VAL;
            }
            val_free(cv);
            size_t cap = 4096, len = 0;
            char *out = malloc(cap);
            if (!out) runtime_error("exec: out of memory for command '%s'", command);
            char buf[1024];
            while (out && fgets(buf, sizeof(buf), pipe)) {
                size_t n = strlen(buf);
                if (n > SIZE_MAX - len - 1) {
                    free(out); out = NULL;
                    runtime_error("exec: output too large for command '%s'", command);
                    break;
                }
                while (len + n + 1 > cap) {
                    if (cap > SIZE_MAX / 2) { free(out); out = NULL; break; }
                    cap *= 2;
                    char *grown = realloc(out, cap);
                    if (!grown) { free(out); out = NULL; break; }
                    out = grown;
                }
                if (!out) break;
                memcpy(out + len, buf, n);
                len += n;
            }
#ifdef _WIN32
            int status = _pclose(pipe);
#else
            int status = pclose(pipe);
#endif
            if (!out) return NULL_VAL;
            out[len] = '\0';
            SubVal result = make_object_val();
            object_set(result.obj, "code", make_int(
#ifdef _WIN32
                status
#else
                WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)
#endif
            ));
            object_set(result.obj, "out", (SubVal){VAL_STRING, {.sv = out}, NULL});
            return result;
        }

        /* net_fetch(url) -> response body (HTTP GET) */
        if (fn && strcmp(fn, "net_fetch") == 0) {
            if (node->child_count < 1)
                runtime_error("net_fetch needs a URL");
            SubVal uv = eval(node->children[0], env);
            const char *url = uv.type == VAL_STRING && uv.sv ? uv.sv : "";
            size_t blen = 0;
            char *body = net_http_request("GET", url, NULL, NULL, &blen);
            val_free(uv);
            if (!body) return NULL_VAL;
            SubVal r = {VAL_STRING, {.sv = body}, NULL};
            return r;
        }

        /* net_post(url, body) -> response body (HTTP POST) */
        if (fn && strcmp(fn, "net_post") == 0) {
            if (node->child_count < 2)
                runtime_error("net_post needs a URL and a body");
            SubVal uv = eval(node->children[0], env);
            SubVal bv = eval(node->children[1], env);
            const char *url = uv.type == VAL_STRING && uv.sv ? uv.sv : "";
            char *bstr = bv.type == VAL_STRING && bv.sv ? bv.sv : "";
            size_t blen = 0;
            char *resp = net_http_request("POST", url, bstr, NULL, &blen);
            val_free(uv);
            val_free(bv);
            if (!resp) return NULL_VAL;
            SubVal r = {VAL_STRING, {.sv = resp}, NULL};
            return r;
        }

        /* net_download(url, path) -> bytes written */
        if (fn && strcmp(fn, "net_download") == 0) {
            if (node->child_count < 2)
                runtime_error("net_download needs a URL and a file path");
            SubVal uv = eval(node->children[0], env);
            SubVal pv = eval(node->children[1], env);
            const char *url = uv.type == VAL_STRING && uv.sv ? uv.sv : "";
            const char *path = pv.type == VAL_STRING && pv.sv ? pv.sv : "";
            size_t blen = 0;
            char *body = net_http_request("GET", url, NULL, NULL, &blen);
            val_free(uv);
            if (!body) return NULL_VAL;
            FILE *f = fopen(path, "wb");
            if (!f) {
                free(body);
                runtime_error("net_download: cannot write '%s'", path);
                val_free(pv);
                return NULL_VAL;
            }
            size_t wrote = fwrite(body, 1, blen, f);
            fclose(f);
            free(body);
            val_free(pv);
            return make_int((long long)wrote);
        }

        /* ==== User-defined function call ==== */
        if (fn) {
            SubVal fv = env_get_node(node, env, fn);
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
                SubVal ret = fn_env->returning ? val_copy(fn_env->ret_val) : NULL_VAL;
                if (g_provenance_enabled) {
                    char *call_text = ast_expression_text(node);
                    provenance_attach(&ret, node, call_text, ret.provenance, NULL, 0);
                    free(call_text);
                }
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
   Networking: net_fetch / net_post / net_download
   --------------------------------------------------------------
   Minimal HTTP/1.1 over POSIX sockets: no dependency on curl or any
   other tool, matching the zero-dependency story of the rest of the
   toolchain. http://host[:port]/path only; https needs TLS and reports
   a clear error instead of silently returning nothing.
   ================================================================ */

#if SUB_NET_POSIX

typedef struct {
    char host[256];
    int  port;
    const char *path;      /* points into the original url */
} NetUrl;

/* Parse "http://host[:port]/path". Returns 0 on success. */
static int net_parse_url(const char *url, NetUrl *out) {
    if (!url || strncmp(url, "http://", 7) != 0) {
        if (url && strncmp(url, "https://", 8) == 0)
            runtime_error("https is not supported by net_fetch "
                          "(no TLS in the builtin HTTP client)");
        else
            runtime_error("net_fetch needs an http:// URL");
        return -1;
    }
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    size_t hostlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hostlen == 0 || hostlen >= sizeof(out->host)) {
        runtime_error("net_fetch: invalid host in URL");
        return -1;
    }
    /* IPv6 literals are out of scope for this client; brackets keep the
       parse from misreading a colon inside an address as the port. */
    if (memchr(p, ':', hostlen)) {
        char *colon = NULL;
        /* [::1]:8080 style */
        if (p[0] == '[') {
            const char *close = memchr(p, ']', hostlen);
            if (!close) { runtime_error("net_fetch: malformed IPv6 host"); return -1; }
            size_t inner = (size_t)(close - p - 1);
            if (inner >= sizeof(out->host)) { runtime_error("net_fetch: host too long"); return -1; }
            memcpy(out->host, p + 1, inner);
            out->host[inner] = 0;
            if (close + 1 < p + hostlen && *(close + 1) == ':') {
                out->port = atoi(close + 2);
                if (out->port <= 0) out->port = 80;
            } else out->port = 80;
        } else {
            char hostbuf[256];
            if (hostlen >= sizeof(hostbuf)) { runtime_error("net_fetch: host too long"); return -1; }
            memcpy(hostbuf, p, hostlen);
            hostbuf[hostlen] = 0;
            colon = strchr(hostbuf, ':');
            *colon = 0;
            snprintf(out->host, sizeof(out->host), "%s", hostbuf);
            out->port = atoi(colon + 1);
            if (out->port <= 0) out->port = 80;
        }
    } else {
        memcpy(out->host, p, hostlen);
        out->host[hostlen] = 0;
        out->port = 80;
    }
    out->path = slash ? slash : "/";
    return 0;
}

/* One send() loop over a whole buffer. */
static int net_send_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, buf, len, 0);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Read until EOF or the connection closes. Returns a malloc'd body. */
static char *net_read_all(int fd, size_t *out_len) {
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (len == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, cap - len, 0);
        if (n == 0) break;                    /* orderly close */
        if (n < 0) { free(buf); return NULL; } /* error */
        len += (size_t)n;
    }
    *out_len = len;
    return buf;
}

/* Perform one HTTP request. method is "GET" or "POST"; body may be NULL.
   Returns the response body, malloc'd and NUL-terminated, with headers
   stripped; a Content-Length is honoured, and a chunked body is
   de-chunked. status_out (may be NULL) receives the status code.
   body_len_out (may be NULL) receives the number of body bytes. */
static char *net_http_request(const char *method, const char *url,
                              const char *body, int *status_out,
                              size_t *body_len_out) {
    NetUrl u;
    if (net_parse_url(url, &u) != 0) return NULL;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(u.host, NULL, &hints, &res);
    if (rc != 0) {
        runtime_error("net_fetch: cannot resolve host '%s' (%s)",
                      u.host, gai_strerror(rc));
        return NULL;
    }
    /* port into a sockaddr */
    struct sockaddr_storage ss;
    memcpy(&ss, res->ai_addr, res->ai_addrlen);
    if (ss.ss_family == AF_INET)
        ((struct sockaddr_in*)&ss)->sin_port = htons((uint16_t)u.port);
    else
        ((struct sockaddr_in6*)&ss)->sin6_port = htons((uint16_t)u.port);

    int fd = socket(res->ai_family, SOCK_STREAM, 0);
    freeaddrinfo(res);
    if (fd < 0) { runtime_error("net_fetch: cannot create socket"); return NULL; }

    if (connect(fd, (struct sockaddr*)&ss, sizeof ss) != 0) {
        close(fd);
        runtime_error("net_fetch: cannot connect to %s:%d", u.host, u.port);
        return NULL;
    }

    char req[4096];
    int n = snprintf(req, sizeof req,
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Connection: close\r\n"
                     "User-Agent: SUB-lang/1.0\r\n",
                     method, u.path, u.host);
    if (body && strcmp(method, "POST") == 0) {
        size_t blen = strlen(body);
        n += snprintf(req + n, sizeof(req) - (size_t)n,
                      "Content-Type: text/plain\r\n"
                      "Content-Length: %zu\r\n", blen);
    }
    n += snprintf(req + n, sizeof(req) - (size_t)n, "\r\n");
    if (n <= 0 || (size_t)n >= sizeof(req)) {
        close(fd);
        runtime_error("net_fetch: request too long");
        return NULL;
    }
    if (net_send_all(fd, req, (size_t)n) != 0) {
        close(fd);
        runtime_error("net_fetch: send failed");
        return NULL;
    }
    if (body && strcmp(method, "POST") == 0 &&
        net_send_all(fd, body, strlen(body)) != 0) {
        close(fd);
        runtime_error("net_fetch: send failed");
        return NULL;
    }

    size_t rawlen = 0;
    char *raw = net_read_all(fd, &rawlen);
    close(fd);
    if (!raw) { runtime_error("net_fetch: read failed"); return NULL; }

    /* headers end at \r\n\r\n */
    char *body_start = NULL;
    char *chunked = NULL;
    long long content_len = -1;
    int status = 0;
    {
        /* NUL-terminate so strstr is safe; rawlen bytes may be binary,
           but HTTP headers are text and the body offset is what matters. */
        char *nul = realloc(raw, rawlen + 1);
        if (!nul) { free(raw); return NULL; }
        raw = nul;
        raw[rawlen] = '\0';
        char *hdr_end = strstr(raw, "\r\n\r\n");
        if (!hdr_end) {
            free(raw);
            runtime_error("net_fetch: malformed HTTP response");
            return NULL;
        }
        *hdr_end = '\0';
        body_start = hdr_end + 4;

        /* status line: HTTP/1.1 200 OK */
        if (sscanf(raw, "HTTP/%*d.%*d %d", &status) != 1) status = 0;

        /* scan headers */
        char *line = raw;
        while ((line = strstr(line, "\r\n")) != NULL) {
            line += 2;
            if (strncasecmp(line, "Content-Length:", 15) == 0)
                content_len = strtoll(line + 15, NULL, 10);
            else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 &&
                     strcasestr(line, "chunked"))
                chunked = body_start;
        }
        (void)chunked;
    }

    /* Body length: Content-Length when given, else up to the NUL we
       appended (the body may itself contain NULs, but for a text
       protocol the length header is the norm; without it we take
       everything to the end of the stream, minus our sentinel). */
    size_t body_len;
    if (content_len >= 0 && (size_t)content_len <= rawlen - (size_t)(body_start - raw))
        body_len = (size_t)content_len;
    else
        body_len = rawlen - (size_t)(body_start - raw);

    /* De-chunk when the server said so. */
    char *out_body = NULL;
    size_t out_len = 0;
    if (chunked) {
        size_t cap = body_len ? body_len : 1;
        out_body = malloc(cap + 1);
        if (!out_body) { free(raw); return NULL; }
        char *rp = body_start;
        while (rp < raw + rawlen) {
            long long chunk = strtoll(rp, NULL, 16);
            if (chunk <= 0) break;
            rp = strstr(rp, "\r\n");
            if (!rp) break;
            rp += 2;
            if (rp + (size_t)chunk > raw + rawlen) break;
            if (out_len + (size_t)chunk > cap) {
                cap = (out_len + (size_t)chunk) * 2;
                char *nb = realloc(out_body, cap + 1);
                if (!nb) { free(out_body); free(raw); return NULL; }
                out_body = nb;
            }
            memcpy(out_body + out_len, rp, (size_t)chunk);
            out_len += (size_t)chunk;
            rp += chunk + 2;                  /* data + CRLF */
        }
        out_body[out_len] = '\0';
    } else {
        out_body = malloc(body_len + 1);
        if (!out_body) { free(raw); return NULL; }
        memcpy(out_body, body_start, body_len);
        out_body[body_len] = '\0';
        out_len = body_len;
    }
    free(raw);
    if (status_out) *status_out = status;
    if (body_len_out) *body_len_out = out_len;
    return out_body;
}

#else  /* !_WIN32: stubs with the same errors */

static char *net_http_request(const char *method, const char *url,
                              const char *body, int *status_out,
                              size_t *body_len_out) {
    (void)method; (void)url; (void)body;
    if (status_out) *status_out = 0;
    if (body_len_out) *body_len_out = 0;
    runtime_error("net_fetch is not supported on this platform");
    return NULL;
}

#endif

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

    g_provenance_enabled = ast_contains_why(ast);
    SubVal top = eval(ast, env);
    g_provenance_enabled = 0;
    val_free(top);
    int aborted = g_runtime_aborted;

    parser_free_ast(ast);
    lexer_free_tokens(toks, ntok);
    return aborted ? 1 : 0;
}

#if SUB_NET_POSIX
#include <pthread.h>
#endif

/* Run f(arg) on a thread with a large stack. A tree-walking interpreter
   burns kilobytes of C stack per SUB-level recursion, and the main thread
   inherits the process's 8 MiB default - so `fib(30)`-style recursion that
   the compiled backends handle with room to spare died at ~1200 frames.
   A dedicated 256 MiB stack puts the interpreter comfortably past the
   compiled backends' practical limits while the stack guard still reports
   a clean error if something truly unbounded runs. */
int interp_run_on_big_stack(void *(*fn)(void *), void *arg) {
#if SUB_NET_POSIX
    pthread_attr_t attr;
    pthread_t th;
    g_thread_stack_size = (size_t)256 << 20;  /* before create: the worker
                                                 may guard_init immediately */
    if (pthread_attr_init(&attr) == 0 &&
        pthread_attr_setstacksize(&attr, (size_t)256 << 20) == 0 &&
        pthread_create(&th, &attr, fn, arg) == 0) {
        void *ret = NULL;
        pthread_join(th, &ret);
        return ret ? *(int *)ret : 0;
    } else {
        g_thread_stack_size = 0;  /* fell back: budget from RLIMIT_STACK */
    }
    /* fall back to the calling thread if the thread cannot be made */
#endif
    {
        void *ret = fn(arg);
        return ret ? *(int *)ret : 0;
    }
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
    SubVal top = eval(ast, global);
    val_free(top);
    env_free(global);

    parser_free_ast(ast);
    lexer_free_tokens(toks, ntok);
    free(src);
    return 0;
}
