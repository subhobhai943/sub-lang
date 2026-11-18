/* ========================================
   SUB Language - Shared Signature Inference
   See codegen_infer.h for why this exists.
   ======================================== */

#include "codegen_infer.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------------------------------------------
   Interned names

   Identifier spellings point into the token storage and stay valid for
   the whole compilation. Interning them once lets the registries below
   compare names by pointer instead of re-scanning strings on every
   lookup.
   ---------------------------------------------------------------- */

#define INTERN_INIT 64

static const char **g_intern_tab;
static int g_intern_len = 0, g_intern_cap = 0;

static unsigned str_hash(const char *s) {
    unsigned h = 2166136261U;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619U;
    return h;
}

static void intern_grow(void) {
    int ncap = g_intern_cap ? g_intern_cap * 2 : INTERN_INIT;
    const char **ntab = calloc((size_t)ncap, sizeof(const char *));
    if (!ntab) abort();
    for (int i = 0; i < g_intern_cap; i++) {
        if (!g_intern_tab[i]) continue;
        unsigned h = str_hash(g_intern_tab[i]) & (unsigned)(ncap - 1);
        while (ntab[h]) h = (h + 1) & (unsigned)(ncap - 1);
        ntab[h] = g_intern_tab[i];
    }
    free(g_intern_tab);
    g_intern_tab = ntab;
    g_intern_cap = ncap;
}

/* Returns a canonical pointer for `s`: intern it on first sight, so the
   registries can compare names with `==` in the common case. */
static const char *intern_name(const char *s) {
    if (!s) return s;
    if (g_intern_cap == 0 || g_intern_len + 1 > g_intern_cap * 3 / 4)
        intern_grow();
    unsigned h = str_hash(s) & (unsigned)(g_intern_cap - 1);
    while (g_intern_tab[h]) {
        if (g_intern_tab[h] == s) return s;
        if (strcmp(g_intern_tab[h], s) == 0) return g_intern_tab[h];
        h = (h + 1) & (unsigned)(g_intern_cap - 1);
    }
    g_intern_tab[h] = s;
    g_intern_len++;
    return s;
}

#define MAX_TRACKED_FUNCTIONS 512

/* ----------------------------------------------------------------
   Function registry

   Growable: a program of any size has to track every function. A fixed
   cap of 512 silently dropped every function past that from inference -
   their parameters defaulted to int and their return types were lost.
   The name table is a hash table so lookup stays O(1) for programs with
   thousands of functions.
   ---------------------------------------------------------------- */

typedef struct {
    ASTNode *decl;        /* the AST_FUNCTION_DECL node */
    const char *name;
} FnEntry;

typedef struct {
    FnEntry *items;
    int count, cap;
} FnTable;

static FnTable g_fns;

/* name -> index into g_fns.items, open addressing */
typedef struct {
    const char *name;
    int         index;
} FnSlot;

static FnSlot *g_fn_tab;
static int g_fn_len = 0, g_fn_cap = 0;

static unsigned fn_slot_hash(const char *name) {
    return str_hash(name);
}

static void fn_tab_grow(void) {
    int ncap = g_fn_cap ? g_fn_cap * 2 : 64;
    FnSlot *nt = calloc((size_t)ncap, sizeof(FnSlot));
    if (!nt) abort();
    for (int i = 0; i < g_fn_cap; i++) {
        if (!g_fn_tab[i].name) continue;
        unsigned h = fn_slot_hash(g_fn_tab[i].name) & (unsigned)(ncap - 1);
        while (nt[h].name) h = (h + 1) & (unsigned)(ncap - 1);
        nt[h] = g_fn_tab[i];
    }
    free(g_fn_tab);
    g_fn_tab = nt;
    g_fn_cap = ncap;
}

static int fn_lookup_index(const char *name) {
    if (!name || g_fn_cap == 0) return -1;
    unsigned h = fn_slot_hash(name) & (unsigned)(g_fn_cap - 1);
    while (g_fn_tab[h].name) {
        if (g_fn_tab[h].name == name) return g_fn_tab[h].index;
        if (strcmp(g_fn_tab[h].name, name) == 0) return g_fn_tab[h].index;
        h = (h + 1) & (unsigned)(g_fn_cap - 1);
    }
    return -1;
}

static ASTNode *fn_lookup(const char *name) {
    int i = fn_lookup_index(name);
    return i < 0 ? NULL : g_fns.items[i].decl;
}

static void fn_grow(void) {
    int ncap = g_fns.cap ? g_fns.cap * 2 : 64;
    FnEntry *ni = realloc(g_fns.items, (size_t)ncap * sizeof(FnEntry));
    if (!ni) abort();
    g_fns.items = ni;
    g_fns.cap = ncap;
}

/* Register `node`, keeping g_fns and the name hash table in step. */
static void fn_register_infer(ASTNode *node) {
    if (g_fns.count == g_fns.cap) fn_grow();
    g_fns.items[g_fns.count].decl = node;
    g_fns.items[g_fns.count].name = node->value;
    int idx = g_fns.count++;
    if (g_fn_cap == 0 || g_fn_len + 1 > g_fn_cap * 3 / 4) fn_tab_grow();
    unsigned h = fn_slot_hash(node->value) & (unsigned)(g_fn_cap - 1);
    while (g_fn_tab[h].name) h = (h + 1) & (unsigned)(g_fn_cap - 1);
    g_fn_tab[h].name = node->value;
    g_fn_tab[h].index = idx;
    g_fn_len++;
}

/* ----------------------------------------------------------------
   Type lattice
   ---------------------------------------------------------------- */

/* Combine two observations about the same slot.
   Widening int -> float is safe; genuinely conflicting observations
   (e.g. a parameter called with both a string and a number) collapse to
   TYPE_GENERIC so backends can fall back to their "any" type. */
static DataType type_merge(DataType a, DataType b) {
    if (a == TYPE_UNKNOWN) return b;
    if (b == TYPE_UNKNOWN) return a;
    if (a == b) return a;

    /* null tells us nothing about the value's shape */
    if (a == TYPE_NULL) return b;
    if (b == TYPE_NULL) return a;

    /* auto is a placeholder, not a real observation */
    if (a == TYPE_AUTO) return b;
    if (b == TYPE_AUTO) return a;

    if ((a == TYPE_INT && b == TYPE_FLOAT) || (a == TYPE_FLOAT && b == TYPE_INT))
        return TYPE_FLOAT;

    return TYPE_GENERIC;
}

/* ----------------------------------------------------------------
   Builtin return types
   ---------------------------------------------------------------- */

static int builtin_return_type(const char *name, DataType *out) {
    static const struct { const char *name; DataType type; } builtins[] = {
        {"str",       TYPE_STRING}, {"to_string", TYPE_STRING},
        {"string",    TYPE_STRING}, {"input",     TYPE_STRING},
        {"upper",     TYPE_STRING}, {"lower",     TYPE_STRING},
        {"trim",      TYPE_STRING}, {"replace",   TYPE_STRING},
        {"substring", TYPE_STRING}, {"char_at",   TYPE_STRING},
        {"join",      TYPE_STRING}, {"type",      TYPE_STRING},

        {"int",       TYPE_INT},    {"len",       TYPE_INT},
        {"length",    TYPE_INT},

        {"float",     TYPE_FLOAT},  {"sqrt",      TYPE_FLOAT},

        /* floor/ceil/round hand back a whole number, not a double - the
           interpreter returns an int and so must every backend, or a
           statically typed target declares the wrong variable type and
           prints 2.0 where the language says 2. abs/min/max depend on
           their arguments and are handled in infer_expr_type instead. */
        {"round",     TYPE_INT},    {"floor",     TYPE_INT},
        {"ceil",      TYPE_INT},

        {"bool",      TYPE_BOOL},   {"contains",  TYPE_BOOL},

        {"array",     TYPE_ARRAY},  {"split",     TYPE_ARRAY},
        {"range",     TYPE_ARRAY},

        {"print",     TYPE_VOID},   {"println",   TYPE_VOID},
        {"show",      TYPE_VOID},   {"push",      TYPE_VOID},
        {"append",    TYPE_VOID},
    };

    if (!name) return 0;
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(builtins[i].name, name) == 0) {
            *out = builtins[i].type;
            return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------
   Variable and array-element registries

   An identifier node carries no type of its own; the type lives on the
   declaration. Without somewhere to look it up, `println(r)` could not tell
   that r was a double, and the C backend picked the %ld format for it -
   printing the double's bit pattern instead of the number.

   Entries are keyed by (owner function, name); owner is NULL for a
   top-level variable. Two functions may each have a local called `out` of
   different types -- arrays.sb and strings.sb both do -- so a table keyed
   by name alone would answer for whichever was recorded last. That is how
   len(out) on an array came out as strlen().

   Lookup order is deliberate and unchanged: this function's own entries
   first, then the top-level scope. Open addressing makes both lookup and
   record expected O(1) instead of O(total entries).
   ---------------------------------------------------------------- */

/* The function whose body is being walked or generated, NULL at top level.
   Declared here because the type tables below are keyed by it. */
static ASTNode *g_current_fn;

typedef struct {
    ASTNode *owner;    /* NULL for a top-level variable */
    const char *name;  /* interned */
    DataType    type;
} VarSlot;

typedef struct {
    VarSlot *items;
    int len, cap;
} VarTable;

static VarTable g_vars, g_elems;

/* Object property types, keyed by variable name and property name. An
   object literal {"name": "x", "version": 2} and a class's field
   declarations both say what each property holds; the use site says only
   obj.name. The C backend needs the static type to read the 64-bit slot
   correctly, and without this it guessed and printed 2 as a string. */
typedef struct {
    const char *var_name;
    const char *prop_name;
    DataType    type;
#define MAX_TRACKED_VARS 1024
} PropEntry;
static struct {
    PropEntry items[MAX_TRACKED_VARS];
    int count;
} g_props;

static unsigned var_slot_hash(ASTNode *owner, const char *name) {
    return str_hash(name) ^ (unsigned)((uintptr_t)owner * 0x9E3779B9U);
}

static void var_tab_grow(VarTable *tab) {
    int ncap = tab->cap ? tab->cap * 2 : 64;
    VarSlot *ni = calloc((size_t)ncap, sizeof(VarSlot));
    if (!ni) abort();
    for (int i = 0; i < tab->cap; i++) {
        if (!tab->items[i].name) continue;
        unsigned h = var_slot_hash(tab->items[i].owner, tab->items[i].name)
                     & (unsigned)(ncap - 1);
        while (ni[h].name) h = (h + 1) & (unsigned)(ncap - 1);
        ni[h] = tab->items[i];
    }
    free(tab->items);
    tab->items = ni;
    tab->cap = ncap;
}

static int var_tab_find(VarTable *tab, ASTNode *owner, const char *name) {
    if (tab->cap == 0) return -1;
    unsigned h = var_slot_hash(owner, name) & (unsigned)(tab->cap - 1);
    while (tab->items[h].name) {
        if (tab->items[h].owner == owner &&
            (tab->items[h].name == name || strcmp(tab->items[h].name, name) == 0))
            return h;
        h = (h + 1) & (unsigned)(tab->cap - 1);
    }
    return -1;
}

static void var_tab_insert(VarTable *tab, ASTNode *owner, const char *name,
                           DataType t) {
    if (tab->cap == 0 || tab->len + 1 > tab->cap * 3 / 4) var_tab_grow(tab);
    unsigned h = var_slot_hash(owner, name) & (unsigned)(tab->cap - 1);
    while (tab->items[h].name) h = (h + 1) & (unsigned)(tab->cap - 1);
    tab->items[h].owner = owner;
    tab->items[h].name = name;
    tab->items[h].type = t;
    tab->len++;
}

static DataType var_tab_lookup(VarTable *tab, ASTNode *owner, const char *name) {
    int i = var_tab_find(tab, owner, name);
    return i >= 0 ? tab->items[i].type : TYPE_UNKNOWN;
}

/* ----------------------------------------------------------------
   Single-visit AST walks

   Statements live both on a `next` chain and in `children[]`, so a
   naive left/right/condition/body/children/next walk processes every
   statement of an m-statement block once via the chain and once per
   preceding sibling via children[] - O(m^2) per block, and the passes
   below dominated the profile after the lookup tables went in. Every
   walk marks visited nodes in a set passed down the recursion, so each
   node is processed exactly once. All the merges the passes perform
   (type_merge, prop_record conflict widening) are idempotent, and the
   writes store the same value twice at most, so visiting once instead
   of twice produces identical types.
   ---------------------------------------------------------------- */

typedef struct {
    ASTNode **items;
    int len, cap;
} AstSet;

static int ast_set_has(AstSet *set, ASTNode *n) {
    if (!n || set->cap == 0) return 0;
    unsigned h = (unsigned)((uintptr_t)n) * 0x9E3779B9U;
    h &= (unsigned)(set->cap - 1);
    while (set->items[h]) {
        if (set->items[h] == n) return 1;
        h = (h + 1) & (unsigned)(set->cap - 1);
    }
    return 0;
}

static void ast_set_add(AstSet *set, ASTNode *n) {
    if (!n) return;
    if (set->len + 1 > set->cap * 3 / 4) {
        int ncap = set->cap ? set->cap * 2 : 64;
        ASTNode **ni = calloc((size_t)ncap, sizeof(ASTNode *));
        ASTNode **old = set->items;
        int ocap = set->cap;
        if (!ni) abort();
        for (int i = 0; i < ocap; i++) {
            if (!old[i]) continue;
            unsigned h = (unsigned)((uintptr_t)old[i]) * 0x9E3779B9U;
            h &= (unsigned)(ncap - 1);
            while (ni[h]) h = (h + 1) & (unsigned)(ncap - 1);
            ni[h] = old[i];
        }
        if (old) free(old);
        set->items = ni;
        set->cap = ncap;
    }
    unsigned h = (unsigned)((uintptr_t)n) * 0x9E3779B9U;
    h &= (unsigned)(set->cap - 1);
    while (set->items[h]) h = (h + 1) & (unsigned)(set->cap - 1);
    set->items[h] = n;
    set->len++;
}

/* Enter node `n` in walk `seen`: returns (via the enclosing function) if
   `n` is NULL or was already visited, otherwise marks it visited. */
#define WALK_ENTER(seen, n) do {                              \
    if (!n || ast_set_has(seen, n)) return;                   \
    ast_set_add(seen, n);                                     \
} while (0)

/* Clear a walk set between passes (len = 0 alone would leave stale
   entries that make later walks skip nodes they should process). */
#define WALK_RESET(seen) do {                                 \
    memset((seen).items, 0, sizeof(void *) * (size_t)(seen).cap); \
    (seen).len = 0;                                           \
} while (0)

void prop_record(const char *var_name, const char *prop_name, DataType t) {
    if (!var_name || !prop_name || t == TYPE_UNKNOWN || t == TYPE_VOID) return;
    var_name = intern_name(var_name);
    prop_name = intern_name(prop_name);
    for (int i = 0; i < g_props.count; i++)
        if (g_props.items[i].var_name == var_name &&
            g_props.items[i].prop_name == prop_name) {
            if (g_props.items[i].type != t)
                g_props.items[i].type = TYPE_GENERIC; /* conflicting: read as string */
            return;
        }
    if (g_props.count >= MAX_TRACKED_VARS) return;
    g_props.items[g_props.count].var_name  = var_name;
    g_props.items[g_props.count].prop_name = prop_name;
    g_props.items[g_props.count].type      = t;
    g_props.count++;
}

DataType obj_prop_type(const char *var_name, const char *prop_name) {
    if (!var_name || !prop_name) return TYPE_UNKNOWN;
    var_name = intern_name(var_name);
    prop_name = intern_name(prop_name);
    for (int i = 0; i < g_props.count; i++)
        if (g_props.items[i].var_name == var_name &&
            g_props.items[i].prop_name == prop_name)
            return g_props.items[i].type;
    return TYPE_UNKNOWN;
}

void prop_reset(void) {
    g_props.count = 0;
}

/* Class declarations, so `let p = Point.new` can stamp p with Point's
   declared field types. Points into the program AST; rebuilt per pass. */
#define MAX_INFER_CLASSES 64
static ASTNode *g_iclasses[MAX_INFER_CLASSES];
static int g_iclass_count;

static void iclasses_collect(ASTNode *n) {
    if (!n || g_iclass_count >= MAX_INFER_CLASSES) return;
    if (n->type == AST_CLASS_DECL) g_iclasses[g_iclass_count++] = n;
    iclasses_collect(n->body);
    iclasses_collect(n->left);
    iclasses_collect(n->right);
    for (int i = 0; i < n->child_count; i++)
        iclasses_collect(n->children[i]);
}

static ASTNode* iclass_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_iclass_count; i++)
        if (g_iclasses[i]->value &&
            strcmp(g_iclasses[i]->value, name) == 0)
            return g_iclasses[i];
    return NULL;
}

/* Stamp a variable bound to `ClassName.new` with the class's field types,
   so obj_prop_type(var, field) answers for p.x later. */
static void record_instance_props(const char *var_name, ASTNode *ctor_expr) {
    if (!var_name || !ctor_expr) return;
    if (ctor_expr->type != AST_MEMBER_ACCESS || !ctor_expr->left ||
        ctor_expr->left->type != AST_IDENTIFIER || !ctor_expr->left->value ||
        !ctor_expr->value || strcmp(ctor_expr->value, "new") != 0)
        return;
    ASTNode *cls = iclass_lookup(ctor_expr->left->value);
    if (!cls || !cls->body) return;
    ASTNode *first = cls->body->body;
    if (!first && cls->body->child_count > 0 &&
        cls->body->children[0] &&
        (cls->body->children[0]->type == AST_VAR_DECL ||
         cls->body->children[0]->type == AST_CONST_DECL))
        first = cls->body->children[0];
    for (ASTNode *f = first; f; f = f->next) {
        if ((f->type == AST_VAR_DECL || f->type == AST_CONST_DECL) &&
            f->value && f->right)
            prop_record(var_name, f->value, infer_expr_type(f->right));
    }
}

static void elem_record(const char *name, DataType t) {
    if (!name || t == TYPE_UNKNOWN) return;
    name = intern_name(name);
    int i = var_tab_find(&g_elems, g_current_fn, name);
    if (i >= 0) { g_elems.items[i].type = type_merge(g_elems.items[i].type, t); return; }
    var_tab_insert(&g_elems, g_current_fn, name, t);
}

static DataType elem_lookup(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    /* This function's own first, then a top-level one. */
    DataType t = var_tab_lookup(&g_elems, g_current_fn, name);
    if (t != TYPE_UNKNOWN) return t;
    return var_tab_lookup(&g_elems, NULL, name);
}

static void var_record(const char *name, DataType t) {
    if (!name || t == TYPE_UNKNOWN) return;
    name = intern_name(name);
    int i = var_tab_find(&g_vars, g_current_fn, name);
    if (i >= 0) { g_vars.items[i].type = type_merge(g_vars.items[i].type, t); return; }
    var_tab_insert(&g_vars, g_current_fn, name, t);
}

static DataType var_lookup(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    /* This function's own first, then a top-level one. */
    int i = var_tab_find(&g_vars, g_current_fn, name);
    if (i >= 0) return g_vars.items[i].type;
    i = var_tab_find(&g_vars, NULL, name);
    if (i >= 0) return g_vars.items[i].type;
    return TYPE_UNKNOWN;
}

/* ----------------------------------------------------------------
   Expression typing
   ---------------------------------------------------------------- */

static int is_comparison_op(const char *op) {
    if (!op) return 0;
    return strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
           strcmp(op, "<")  == 0 || strcmp(op, ">")  == 0 ||
           strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
           strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 ||
           strcmp(op, "and") == 0 || strcmp(op, "or") == 0;
}

/* Guard against cycles in mutually recursive functions. */
static int g_expr_depth = 0;
#define MAX_EXPR_DEPTH 64

/* The function whose body is currently being typed. Identifiers inside a body
   are most often the function's own parameters, and the parameter declaration
   is where the inferred type lives - the identifier nodes referencing it are
   left untyped by the parser. Without this, `return n` looks untyped and the
   whole function degrades to the generic fallback type. (Declared above, with
   the type tables that are keyed by it.) */

/* The parser marks un-annotated declarations TYPE_AUTO and leaves other nodes
   TYPE_UNKNOWN. Both mean "nothing known yet", so every check below has to
   accept either - treating AUTO as a resolved type is what left recursive
   functions typed as `auto` and pushed backends onto their fallback type. */
static int type_is_unresolved(DataType t) {
    return t == TYPE_UNKNOWN || t == TYPE_AUTO;
}

DataType infer_elem_type(ASTNode *expr);

ASTNode *infer_enter_function(ASTNode *fn) {
    ASTNode *prev = g_current_fn;
    g_current_fn = fn;
    return prev;
}

/* Whether `name` is a parameter of `fn`. */
static int param_of(ASTNode *fn, const char *name) {
    if (!fn || !name) return 0;
    for (int i = 0; i < fn->child_count; i++) {
        ASTNode *p = fn->children[i];
        if (p && p->value && strcmp(p->value, name) == 0) return 1;
    }
    return 0;
}

/* The element type of an array parameter of the function being generated. */
static DataType param_elem_in_current_fn(const char *name) {
    if (!g_current_fn || !name) return TYPE_UNKNOWN;
    for (int i = 0; i < g_current_fn->child_count; i++) {
        ASTNode *p = g_current_fn->children[i];
        if (p && p->value && strcmp(p->value, name) == 0) return p->elem_type;
    }
    return TYPE_UNKNOWN;
}

static DataType param_type_in_current_fn(const char *name) {
    if (!g_current_fn || !name) return TYPE_UNKNOWN;
    for (int i = 0; i < g_current_fn->child_count; i++) {
        ASTNode *p = g_current_fn->children[i];
        if (p && p->value && strcmp(p->value, name) == 0)
            return p->data_type;
    }
    return TYPE_UNKNOWN;
}

DataType infer_expr_type(ASTNode *expr) {
    if (!expr) return TYPE_UNKNOWN;
    if (g_expr_depth > MAX_EXPR_DEPTH) return TYPE_UNKNOWN;

    g_expr_depth++;
    DataType result = TYPE_UNKNOWN;

    switch (expr->type) {
        case AST_LITERAL:
            result = expr->data_type;
            break;

        case AST_ARRAY_LITERAL:
            result = TYPE_ARRAY;
            break;

        case AST_OBJECT_LITERAL:
            result = TYPE_OBJECT;
            break;

        case AST_RANGE_EXPR:
            result = TYPE_ARRAY;
            break;

        case AST_BINARY_EXPR: {
            if (is_comparison_op(expr->value)) {
                result = TYPE_BOOL;
                break;
            }
            DataType l = infer_expr_type(expr->left);
            DataType r = infer_expr_type(expr->right);

            /* Concatenation: a string on either side makes the whole
               expression a string, even when the other side is a number. */
            if (expr->value && strcmp(expr->value, "+") == 0 &&
                (l == TYPE_STRING || r == TYPE_STRING)) {
                result = TYPE_STRING;
                break;
            }
            /* A negative exponent yields a fraction, so the expression is
               float even when both operands are integers. Without this the
               C backend picked the %ld format for 2 ** -1. */
            if (expr->value && strcmp(expr->value, "**") == 0) {
                if (exponent_is_negative(expr->right)) { result = TYPE_FLOAT; break; }
                result = type_merge(l, r);
                break;
            }
            /* Division is integer division when both operands are integers
               (9 / 2 is 4, as the interpreter computes it) and float
               otherwise. Typing it as always-float contradicted the code
               the backends actually emit. */
            if (expr->value && strcmp(expr->value, "/") == 0) {
                result = (l == TYPE_INT && r == TYPE_INT) ? TYPE_INT : TYPE_FLOAT;
                break;
            }
            result = type_merge(l, r);
            break;
        }

        case AST_UNARY_EXPR:
            if (expr->value && (strcmp(expr->value, "!") == 0 ||
                                strcmp(expr->value, "not") == 0))
                result = TYPE_BOOL;
            else
                result = infer_expr_type(expr->right ? expr->right : expr->left);
            break;

        case AST_TERNARY_EXPR:
            result = type_merge(infer_expr_type(expr->left),
                                infer_expr_type(expr->right));
            break;

        case AST_CALL_EXPR: {
            DataType bt;
            /* abs/min/max preserve the shape of what they are given: abs of
               an int is an int, min of two ints is an int. */
            if (expr->value && (strcmp(expr->value, "abs") == 0 ||
                                strcmp(expr->value, "min") == 0 ||
                                strcmp(expr->value, "max") == 0)) {
                result = TYPE_UNKNOWN;
                for (int i = 0; i < expr->child_count; i++)
                    result = type_merge(result, infer_expr_type(expr->children[i]));
                if (type_is_unresolved(result)) result = TYPE_INT;
                break;
            }
            if (expr->value && builtin_return_type(expr->value, &bt)) {
                result = bt;
                break;
            }
            ASTNode *callee = fn_lookup(expr->value);
            if (callee) result = callee->data_type;
            break;
        }

        case AST_ARRAY_ACCESS:
            /* a[i] has the array's element type. Without this the C backend
               picked its printf format from the node's stale annotation.
               obj["k"] on an object is that property's value; without a
               declared type per property it is typed as a string, which is
               what the C backend reads out of the slot. */
            if (infer_expr_type(expr->left) == TYPE_OBJECT) {
                /* A property read: the variable name plus the key is the

                   registry key. A key that is not a literal string, or a
                   property never recorded, is typed as a string, which is
                   how the C backend reads an untagged slot. */
                if (expr->left->type == AST_IDENTIFIER &&
                    expr->right && expr->right->type == AST_LITERAL &&
                    expr->right->data_type == TYPE_STRING) {
                    DataType pt = obj_prop_type(expr->left->value, expr->right->value);
                    if (!type_is_unresolved(pt)) { result = pt; break; }
                }
                result = TYPE_STRING;
                break;
            }
            result = infer_elem_type(expr->left);
            break;

        case AST_MEMBER_ACCESS:
            /* obj.field mirrors obj["field"]: without per-property types it
               reads as a string. (.length is handled where it is emitted.) */
            if (expr->value && strcmp(expr->value, "length") == 0)
                result = TYPE_INT;
            else if (expr->value && strcmp(expr->value, "new") == 0)
                result = TYPE_OBJECT;
            else if (expr->left && expr->left->type == AST_IDENTIFIER) {
                DataType pt = obj_prop_type(expr->left->value, expr->value);
                if (!type_is_unresolved(pt)) result = pt;
                else if (infer_expr_type(expr->left) == TYPE_OBJECT) result = TYPE_STRING;
                else result = expr->data_type;
            }
            else
                result = expr->data_type;
            break;

        case AST_IDENTIFIER:
            /* A parameter of the function being generated wins outright. The
               annotation on the use site and the var registry below are both
               keyed by name alone and shared across every function, so with a
               library in scope they answer for the wrong `a`: strings.sb
               declares `a: string`, which made len(a) in arrays.sb compile to
               strlen() over a SubArray. */
            result = param_type_in_current_fn(expr->value);
            if (type_is_unresolved(result))
                result = expr->data_type;
            if (type_is_unresolved(result))
                result = var_lookup(expr->value);
            break;

        default:
            result = expr->data_type;
            break;
    }

    g_expr_depth--;
    return result;
}

/* ----------------------------------------------------------------
   Return type inference
   ---------------------------------------------------------------- */

/* Walk a function body collecting the types of every `return <expr>`.
   Does not descend into nested function declarations, whose returns
   belong to that inner function. */
static void collect_return_types(ASTNode *node, DataType *acc, int *saw_value_return,
                                 AstSet *seen) {
    WALK_ENTER(seen, node);

    if (node->type == AST_FUNCTION_DECL || node->type == AST_ARROW_FUNCTION)
        return;

    if (node->type == AST_RETURN_STMT) {
        if (node->right) {
            *saw_value_return = 1;
            *acc = type_merge(*acc, infer_expr_type(node->right));
        }
        /* A bare `return` contributes nothing; a function mixing bare and
           value returns still needs the value type. */
    }

    collect_return_types(node->left,      acc, saw_value_return, seen);
    collect_return_types(node->right,     acc, saw_value_return, seen);
    collect_return_types(node->condition, acc, saw_value_return, seen);
    collect_return_types(node->body,      acc, saw_value_return, seen);
    for (int i = 0; i < node->child_count; i++)
        collect_return_types(node->children[i], acc, saw_value_return, seen);
    collect_return_types(node->next,      acc, saw_value_return, seen);
}

/* The element type of the arrays a function returns, for callers that need to
   know what join() or an index will hand back. Stored on the declaration's
   elem_type, the same field a parameter uses. */
static void collect_return_elem(ASTNode *node, DataType *acc, AstSet *seen) {
    WALK_ENTER(seen, node);
    if (node->type == AST_FUNCTION_DECL || node->type == AST_ARROW_FUNCTION)
        return;
    if (node->type == AST_RETURN_STMT && node->right &&
        infer_expr_type(node->right) == TYPE_ARRAY)
        *acc = type_merge(*acc, infer_elem_type(node->right));

    collect_return_elem(node->left,      acc, seen);
    collect_return_elem(node->right,     acc, seen);
    collect_return_elem(node->condition, acc, seen);
    collect_return_elem(node->body,      acc, seen);
    for (int i = 0; i < node->child_count; i++)
        collect_return_elem(node->children[i], acc, seen);
    collect_return_elem(node->next,      acc, seen);
}

DataType infer_return_type(ASTNode *body) {
    DataType acc = TYPE_UNKNOWN;
    int saw_value_return = 0;
    AstSet seen = {NULL, 0, 0};
    collect_return_types(body, &acc, &saw_value_return, &seen);

    if (!saw_value_return) {
        free(seen.items);
        return TYPE_VOID;
    }
    /* Returns a value we could not classify - let the backend pick its widest
       type rather than emitting `void`, which miscompiles on every target
       that checks return types. */
    if (type_is_unresolved(acc) || acc == TYPE_NULL) {
        free(seen.items);
        return TYPE_GENERIC;
    }
    free(seen.items);
    return acc;
}

/* ----------------------------------------------------------------
   Parameter inference from call sites
   ---------------------------------------------------------------- */

/* ----------------------------------------------------------------
   Call-site indexing

   propagate_call_sites used to re-walk the whole AST once per round.
   Call sites never move and a site's merge only depends on the callee's
   current parameter types, so the sites are collected once (tree order)
   and the same list is replayed for every round: identical merges in
   identical order, O(sites) per round instead of O(program).
   ---------------------------------------------------------------- */

typedef struct {
    ASTNode *callee;   /* resolved callee declaration */
    ASTNode *site;     /* the AST_CALL_EXPR node */
    int     arg_index; /* which argument / parameter pair */
} CallSite;

static CallSite *g_sites;
static int g_nsites = 0, g_sites_cap = 0;

static void site_push(ASTNode *callee, ASTNode *site, int arg_index) {
    if (g_nsites == g_sites_cap) {
        int ncap = g_sites_cap ? g_sites_cap * 2 : 64;
        CallSite *ni = realloc(g_sites, (size_t)ncap * sizeof(CallSite));
        if (!ni) abort();
        g_sites = ni;
        g_sites_cap = ncap;
    }
    g_sites[g_nsites].callee = callee;
    g_sites[g_nsites].site   = site;
    g_sites[g_nsites].arg_index = arg_index;
    g_nsites++;
}

/* One entry per function: indices into g_sites of the call sites inside
   that function's body subtree. A site under a nested function is listed
   for every enclosing function, exactly as re-walking each body visits it. */
typedef struct {
    int *items;
    int count, cap;
} FnSiteList;
static FnSiteList *g_fn_site_lists = NULL;
static int g_fn_site_lists_cap = 0;

static void fn_site_list_push(int fn_index, int site_index) {
    FnSiteList *l = &g_fn_site_lists[fn_index];
    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 16;
        int *ni = realloc(l->items, (size_t)ncap * sizeof(int));
        if (!ni) abort();
        l->items = ni;
        l->cap = ncap;
    }
    l->items[l->count++] = site_index;
}

/* Enclosing functions of the node currently being walked (indices into
   g_fns; -1 for arrow functions, which are not in g_fns but still group
   the call sites inside them for their named enclosing functions). */
static int *g_enclosing_stack;
static int g_enclosing_len = 0, g_enclosing_cap = 0;

static void enclosing_push(int fn_index) {
    if (g_enclosing_len == g_enclosing_cap) {
        int ncap = g_enclosing_cap ? g_enclosing_cap * 2 : 16;
        int *ni = realloc(g_enclosing_stack, (size_t)ncap * sizeof(int));
        if (!ni) abort();
        g_enclosing_stack = ni;
        g_enclosing_cap = ncap;
    }
    g_enclosing_stack[g_enclosing_len++] = fn_index;
}

static void enclosing_pop(void) {
    if (g_enclosing_len > 0) g_enclosing_len--;
}

static void collect_call_sites_walk(ASTNode *node, AstSet *seen) {
    int is_fn, fn_index;
    WALK_ENTER(seen, node);

    if (node->type == AST_CALL_EXPR && node->value) {
        ASTNode *decl = fn_lookup(node->value);
        if (decl) {
            int n = node->child_count < decl->child_count
                    ? node->child_count : decl->child_count;
            int base = g_nsites;
            for (int i = 0; i < n; i++) site_push(decl, node, i);
            for (int e = 0; e < g_enclosing_len; e++)
                if (g_enclosing_stack[e] >= 0)
                    for (int i = 0; i < n; i++)
                        fn_site_list_push(g_enclosing_stack[e], base + i);
        }
    }

    is_fn = (node->type == AST_FUNCTION_DECL || node->type == AST_ARROW_FUNCTION);
    fn_index = -1;
    if (is_fn && node->value) fn_index = fn_lookup_index(node->value);
    if (fn_index >= 0) enclosing_push(fn_index);

    collect_call_sites_walk(node->left, seen);
    collect_call_sites_walk(node->right, seen);
    collect_call_sites_walk(node->condition, seen);
    collect_call_sites_walk(node->body, seen);
    for (int i = 0; i < node->child_count; i++)
        collect_call_sites_walk(node->children[i], seen);
    collect_call_sites_walk(node->next, seen);

    if (fn_index >= 0) enclosing_pop();
}

static void collect_call_sites(ASTNode *program) {
    int i;
    AstSet seen = {NULL, 0, 0};
    g_nsites = 0;
    if (g_fns.count > 0) {
        if (!g_fn_site_lists) {
            g_fn_site_lists = calloc((size_t)g_fns.count, sizeof(FnSiteList));
            if (!g_fn_site_lists) abort();
            g_fn_site_lists_cap = g_fns.count;
        } else if (g_fns.count > g_fn_site_lists_cap) {
            FnSiteList *ni = realloc(g_fn_site_lists,
                                     (size_t)g_fns.count * sizeof(FnSiteList));
            if (!ni) abort();
            for (i = g_fn_site_lists_cap; i < g_fns.count; i++) {
                ni[i].items = NULL; ni[i].count = 0; ni[i].cap = 0;
            }
            g_fn_site_lists = ni;
            g_fn_site_lists_cap = g_fns.count;
        }
        for (i = 0; i < g_fn_site_lists_cap; i++) g_fn_site_lists[i].count = 0;
    }
    if (!program) return;
    collect_call_sites_walk(program, &seen);
    free(seen.items);
}

/* One merge, exactly as the old per-site code did it. */
static void apply_call_site(const CallSite *s) {
    ASTNode *decl = s->callee;
    ASTNode *site = s->site;
    int i = s->arg_index;
    ASTNode *param = decl->children[i];
    ASTNode *arg   = site->children[i];
    if (!param || !arg) return;
    /* Never override a type the programmer wrote down. */
    if (param->metadata || param->explicit_type) return;
    DataType at = infer_expr_type(arg);
    if (!type_is_unresolved(at))
        param->data_type = type_merge(param->data_type, at);
    /* An array parameter also needs to know what is in the array,
       or the body reads every element back as the default type. */
    if (at == TYPE_ARRAY) {
        DataType et = infer_elem_type(arg);
        if (!type_is_unresolved(et))
            param->elem_type = type_merge(param->elem_type, et);
    }
}

static void propagate_call_sites_round(void) {
    for (int i = 0; i < g_nsites; i++) apply_call_site(&g_sites[i]);
}

static void propagate_call_sites_in_fn(int fn_index) {
    FnSiteList *l = &g_fn_site_lists[fn_index];
    for (int i = 0; i < l->count; i++) apply_call_site(&g_sites[l->items[i]]);
}

/* ----------------------------------------------------------------
   Parameter inference from body usage
   ---------------------------------------------------------------- */

/* Look for uses of `pname` inside `node` that reveal its type, e.g.
   `"Hello, " + name` implies name is a string. Used only when no call
   site pinned the parameter down. */
/* Names bound to an element of `arr`: the variable of a `for x in arr` loop,
   and any `let x = arr[...]`. What those are used for is what the array
   holds. */
static void bind_element_names(ASTNode *n, const char *arr,
                               const char *out[], int max, int *count,
                               AstSet *seen) {
    WALK_ENTER(seen, n);
    if (*count >= max) return;

    if (n->type == AST_FOR_STMT && n->value && n->condition &&
        n->condition->type == AST_IDENTIFIER && n->condition->value &&
        strcmp(n->condition->value, arr) == 0)
        out[(*count)++] = n->value;

    if ((n->type == AST_VAR_DECL || n->type == AST_CONST_DECL) && n->value &&
        n->right && n->right->type == AST_ARRAY_ACCESS &&
        n->right->left && n->right->left->type == AST_IDENTIFIER &&
        n->right->left->value &&
        strcmp(n->right->left->value, arr) == 0 && *count < max)
        out[(*count)++] = n->value;

    for (int i = 0; i < n->child_count; i++)
        bind_element_names(n->children[i], arr, out, max, count, seen);
    bind_element_names(n->left, arr, out, max, count, seen);
    bind_element_names(n->right, arr, out, max, count, seen);
    bind_element_names(n->condition, arr, out, max, count, seen);
    bind_element_names(n->body, arr, out, max, count, seen);
    bind_element_names(n->next, arr, out, max, count, seen);
}

/* An array handed straight to another function holds whatever that
   function's parameter was found to hold. mean_float() gives no other clue:
   it only passes its array to sum_float() and divides by its length. */
static void elem_from_pass_through(ASTNode *n, const char *pname, DataType *acc,
                                   AstSet *seen) {
    WALK_ENTER(seen, n);
    if (n->type == AST_CALL_EXPR && n->value) {
        ASTNode *decl = fn_lookup(n->value);
        if (decl) {
            int c = n->child_count < decl->child_count
                        ? n->child_count : decl->child_count;
            for (int i = 0; i < c; i++) {
                ASTNode *arg = n->children[i];
                if (arg && arg->type == AST_IDENTIFIER && arg->value &&
                    strcmp(arg->value, pname) == 0 && decl->children[i])
                    *acc = type_merge(*acc, decl->children[i]->elem_type);
            }
        }
    }
    for (int i = 0; i < n->child_count; i++)
        elem_from_pass_through(n->children[i], pname, acc, seen);
    elem_from_pass_through(n->left, pname, acc, seen);
    elem_from_pass_through(n->right, pname, acc, seen);
    elem_from_pass_through(n->condition, pname, acc, seen);
    elem_from_pass_through(n->body, pname, acc, seen);
    elem_from_pass_through(n->next, pname, acc, seen);
}

/* Whether the body has a bare `return <name>`. With an explicit return type
   that pins the name down, which is the only clue min_float() gives about
   what its array holds. */
static int returns_name(ASTNode *n, const char *name, AstSet *seen) {
    if (!n || !name || ast_set_has(seen, n)) return 0;
    ast_set_add(seen, n);
    if (n->type == AST_RETURN_STMT && n->right &&
        n->right->type == AST_IDENTIFIER && n->right->value &&
        strcmp(n->right->value, name) == 0) return 1;
    for (int i = 0; i < n->child_count; i++)
        if (returns_name(n->children[i], name, seen)) return 1;
    return returns_name(n->left, name, seen) || returns_name(n->right, name, seen) ||
           returns_name(n->condition, name, seen) || returns_name(n->body, name, seen) ||
           returns_name(n->next, name, seen);
}

static void scan_param_usage(ASTNode *node, const char *pname, DataType *acc,
                             AstSet *seen) {
    WALK_ENTER(seen, node);
    if (!pname) return;

    if (node->type == AST_BINARY_EXPR && node->value) {
        ASTNode *l = node->left, *r = node->right;
        int l_is_param = l && l->type == AST_IDENTIFIER && l->value &&
                         strcmp(l->value, pname) == 0;
        int r_is_param = r && r->type == AST_IDENTIFIER && r->value &&
                         strcmp(r->value, pname) == 0;

        if (l_is_param || r_is_param) {
            ASTNode *other = l_is_param ? r : l;
            DataType ot = infer_expr_type(other);

            if (strcmp(node->value, "+") == 0 && ot == TYPE_STRING)
                *acc = type_merge(*acc, TYPE_STRING);
            else if (is_comparison_op(node->value) && ot != TYPE_UNKNOWN &&
                     ot != TYPE_BOOL)
                *acc = type_merge(*acc, ot);
            else if (!is_comparison_op(node->value) && ot != TYPE_UNKNOWN)
                *acc = type_merge(*acc, ot);
        }
    }

    /* Iterating it, indexing it, or asking for its length: all three say the
       parameter is an array. Without this a function whose only clue is
       `for v in a` had its parameter defaulted to a number, and the native
       backend then refused to compile the loop -- which meant importing a
       module cost more than calling into it, since an uncalled function has
       no call site to be inferred from. */
    if (node->type == AST_FOR_STMT && node->condition &&
        node->condition->type == AST_IDENTIFIER && node->condition->value &&
        strcmp(node->condition->value, pname) == 0)
        *acc = type_merge(*acc, TYPE_ARRAY);

    if (node->type == AST_ARRAY_ACCESS && node->left &&
        node->left->type == AST_IDENTIFIER && node->left->value &&
        strcmp(node->left->value, pname) == 0)
        *acc = type_merge(*acc, TYPE_ARRAY);

    if (node->type == AST_CALL_EXPR && node->value &&
        (strcmp(node->value, "push") == 0 || strcmp(node->value, "pop") == 0 ||
         strcmp(node->value, "append") == 0) &&
        node->child_count > 0 && node->children[0] &&
        node->children[0]->type == AST_IDENTIFIER &&
        node->children[0]->value &&
        strcmp(node->children[0]->value, pname) == 0)
        *acc = type_merge(*acc, TYPE_ARRAY);

    /* Passing the parameter straight through to another function tells us
       what that function expects. */
    if (node->type == AST_CALL_EXPR && node->value) {
        ASTNode *decl = fn_lookup(node->value);
        if (decl) {
            int n = node->child_count < decl->child_count
                        ? node->child_count : decl->child_count;
            for (int i = 0; i < n; i++) {
                ASTNode *arg = node->children[i];
                if (arg && arg->type == AST_IDENTIFIER && arg->value &&
                    strcmp(arg->value, pname) == 0 && decl->children[i])
                    *acc = type_merge(*acc, decl->children[i]->data_type);
            }
        }
    }

    scan_param_usage(node->left,      pname, acc, seen);
    scan_param_usage(node->right,     pname, acc, seen);
    scan_param_usage(node->condition, pname, acc, seen);
    scan_param_usage(node->body,      pname, acc, seen);
    for (int i = 0; i < node->child_count; i++)
        scan_param_usage(node->children[i], pname, acc, seen);
    scan_param_usage(node->next,      pname, acc, seen);
}

/* ----------------------------------------------------------------
   Driver
   ---------------------------------------------------------------- */

static void collect_functions(ASTNode *node, AstSet *seen) {
    WALK_ENTER(seen, node);

    if (node->type == AST_FUNCTION_DECL && node->value && fn_lookup_index(node->value) < 0)
        fn_register_infer(node);

    collect_functions(node->left, seen);
    collect_functions(node->right, seen);
    collect_functions(node->condition, seen);
    collect_functions(node->body, seen);
    for (int i = 0; i < node->child_count; i++)
        collect_functions(node->children[i], seen);
    collect_functions(node->next, seen);
}

/* Variable declarations are typed from their initializer. Without this,
   `let r = safe_div(10.0, 4.0)` stayed untyped and the C backend fell back
   to `long`, truncating 2.5 to 2. Runs after function signatures settle so
   a call's return type is known. */
static void infer_var_decls(ASTNode *node, AstSet *seen) {
    WALK_ENTER(seen, node);

    if ((node->type == AST_VAR_DECL || node->type == AST_CONST_DECL) &&
        node->right && !node->metadata) {
        /* The semantic pass defaults an un-annotated declaration to int when
           it cannot see the initializer's type - which is every call to a
           user function, since those signatures are only resolved here. A
           NULL metadata means the source wrote no type, so the inferred type
           is the better answer and replaces the default. */
        DataType t = infer_expr_type(node->right);
        if (!type_is_unresolved(t) && t != TYPE_VOID && t != TYPE_NULL)
            node->data_type = t;
    }
    if (node->type == AST_VAR_DECL || node->type == AST_CONST_DECL) {
        var_record(node->value, node->data_type);
        /* An empty literal is recorded as nothing rather than as int: the
           merge of int with a later push of a string is TYPE_GENERIC, which
           is worse than having said nothing at all. */
        int empty_literal = (node->right &&
                             node->right->type == AST_ARRAY_LITERAL &&
                             node->right->child_count == 0);
        if (node->right && !empty_literal &&
            infer_expr_type(node->right) == TYPE_ARRAY)
            elem_record(node->value, infer_elem_type(node->right));
    }
    /* `for w in <collection>` binds w to an element. Recorded before the body
       is walked, so a push(out, w) inside it can see what w is: without this
       `for w in split(s, ",")` left w at the semantic pass's default and the
       typed backends built a list of integers out of the pieces. */
    if (node->type == AST_FOR_STMT && node->value && node->condition) {
        DataType ct = infer_expr_type(node->condition);
        if (ct == TYPE_STRING) {
            var_record(node->value, TYPE_STRING);
        } else if (ct == TYPE_ARRAY) {
            DataType et = infer_elem_type(node->condition);
            if (!type_is_unresolved(et)) var_record(node->value, et);
        }
    }
    /* push(a, x) tells us what a holds just as surely as the literal does,
       and it is often the only thing that does - `let a = []` says nothing. */
    if (node->type == AST_CALL_EXPR && node->value && node->child_count >= 2 &&
        (strcmp(node->value, "push") == 0 || strcmp(node->value, "append") == 0) &&
        node->children[0] && node->children[0]->type == AST_IDENTIFIER)
        elem_record(node->children[0]->value, infer_expr_type(node->children[1]));

    /* Track the enclosing function so initializers that mention parameters
       resolve the same way return expressions do. */
    ASTNode *saved = g_current_fn;
    if (node->type == AST_FUNCTION_DECL) g_current_fn = node;

    infer_var_decls(node->left, seen);
    infer_var_decls(node->right, seen);
    infer_var_decls(node->condition, seen);
    infer_var_decls(node->body, seen);
    for (int i = 0; i < node->child_count; i++)
        infer_var_decls(node->children[i], seen);

    g_current_fn = saved;
    infer_var_decls(node->next, seen);
}

/* `let out = []` is declared before anything is pushed into it, so its type
   has to be written back onto the literal once the pushes have been seen -
   every backend asks the initializer what the elements are, and an empty
   literal on its own can only answer "integers". */
static void stamp_empty_array_elems(ASTNode *node, AstSet *seen) {
    WALK_ENTER(seen, node);

    if ((node->type == AST_VAR_DECL || node->type == AST_CONST_DECL) &&
        node->value && node->right &&
        node->right->type == AST_ARRAY_LITERAL &&
        node->right->child_count == 0) {
        DataType et = infer_elem_type_of_var(node->value);
        if (!type_is_unresolved(et) && et != TYPE_GENERIC)
            node->right->elem_type = et;
    }

    ASTNode *saved = g_current_fn;
    if (node->type == AST_FUNCTION_DECL) g_current_fn = node;

    stamp_empty_array_elems(node->left, seen);
    stamp_empty_array_elems(node->right, seen);
    stamp_empty_array_elems(node->condition, seen);
    stamp_empty_array_elems(node->body, seen);
    for (int i = 0; i < node->child_count; i++)
        stamp_empty_array_elems(node->children[i], seen);

    g_current_fn = saved;
    stamp_empty_array_elems(node->next, seen);
}

/* Second pass: stamp the declaration's type onto every identifier that refers
   to it. The semantic pass leaves identifier nodes carrying its own int
   default, which short-circuits any later lookup - so the type has to be
   written onto the node itself, not just made available for lookup. */
static void propagate_var_types_to_identifiers(ASTNode *node, AstSet *seen) {
    WALK_ENTER(seen, node);

    if (node->type == AST_IDENTIFIER && node->value) {
        /* A parameter of the enclosing function is not the variable of the
           same name somewhere else. The registry is keyed by name and falls
           back to the top-level scope, so `for s in scores` at the top level
           was retyping the `s` parameter of every function in the program --
           io.sb's bold(s: string) printed its argument with %ld. */
        if (!param_of(g_current_fn, node->value)) {
            DataType t = var_lookup(node->value);
            if (!type_is_unresolved(t) && t != TYPE_NULL) node->data_type = t;
        }
    }

    /* Record object property types where they are declared:
         let obj = {"k": expr}       -> obj.k : type(expr)
         obj.k = expr               -> obj.k : type(expr)
         obj["k"] = expr           -> obj.k : type(expr)
         class C { let k = expr }   -> C instances' k : type(expr)
       The use sites (obj.k, obj["k"]) look these up through
       obj_prop_type() so the statically typed backends can read the
       tagged slot as the right type. */
    if (node->type == AST_VAR_DECL && node->right &&
        node->right->type == AST_OBJECT_LITERAL && node->value) {
        for (int i = 0; i < node->right->child_count; i++) {
            ASTNode *pair = node->right->children[i];
            if (pair && pair->type == AST_VAR_DECL && pair->value && pair->right)
                prop_record(node->value, pair->value, infer_expr_type(pair->right));
        }
    }
    if (node->type == AST_CLASS_DECL && node->value) {
        ASTNode *b = node->body;
        if (b && b->child_count == 0) b = NULL; /* not the pair-array form */
        ASTNode *first = node->body ? node->body->body : NULL;
        if (!first && node->body && node->body->child_count > 0 &&
            node->body->children[0] &&
            (node->body->children[0]->type == AST_VAR_DECL ||
             node->body->children[0]->type == AST_CONST_DECL))
            first = node->body->children[0];
        for (ASTNode *f = first; f; f = f->next) {
            if ((f->type == AST_VAR_DECL || f->type == AST_CONST_DECL) &&
                f->value && f->right)
                prop_record(node->value, f->value, infer_expr_type(f->right));
        }
    }
    if ((node->type == AST_VAR_DECL || node->type == AST_CONST_DECL) &&
        node->value && node->right) {
        record_instance_props(node->value, node->right);
    }
    if (node->type == AST_ASSIGN_STMT && node->left && node->right) {
        if (node->left->type == AST_MEMBER_ACCESS &&
            node->left->left && node->left->left->type == AST_IDENTIFIER &&
            node->left->left->value && node->left->value)
            prop_record(node->left->left->value, node->left->value,
                        infer_expr_type(node->right));
        if (node->left->type == AST_ARRAY_ACCESS &&
            node->left->left && node->left->left->type == AST_IDENTIFIER &&
            node->left->left->value &&
            node->left->right && node->left->right->type == AST_LITERAL &&
            node->left->right->data_type == TYPE_STRING && node->left->right->value)
            prop_record(node->left->left->value, node->left->right->value,
                        infer_expr_type(node->right));
    }

    ASTNode *saved = g_current_fn;
    if (node->type == AST_FUNCTION_DECL) g_current_fn = node;

    propagate_var_types_to_identifiers(node->left, seen);
    propagate_var_types_to_identifiers(node->right, seen);
    propagate_var_types_to_identifiers(node->condition, seen);
    propagate_var_types_to_identifiers(node->body, seen);
    for (int i = 0; i < node->child_count; i++)
        propagate_var_types_to_identifiers(node->children[i], seen);

    g_current_fn = saved;
    propagate_var_types_to_identifiers(node->next, seen);
}

/* ----------------------------------------------------------------
   Nullable functions

   `fn safe_div(a, b) { if b == 0 { return null } return a / b }` returns
   either a number or null. Statically typed targets cannot put null in a
   double, so `return null` was emitted verbatim and failed to compile in
   C++, Rust and Java.

   Such a function is recorded here as nullable and widened to float, and the
   backends represent its null as NaN - a value real arithmetic never
   produces, so `r != null` becomes an isnan test. This is why the widening
   matters: an int-returning nullable function has no spare value to use as
   the sentinel, a float one does.
   ---------------------------------------------------------------- */

static struct {
    const char *names[MAX_TRACKED_FUNCTIONS];
    int count;
} g_nullable;

DataType infer_elem_type_of_var(const char *name) {
    DataType t = elem_lookup(name);
    return t == TYPE_GENERIC ? TYPE_UNKNOWN : t;
}

DataType infer_elem_type(ASTNode *expr) {
    if (!expr) return TYPE_INT;

    if (expr->type == AST_ARRAY_LITERAL) {
        /* `let out = []` says nothing about its elements; what the pushes
           into it said is stamped onto the literal by the pass below, and is
           the only thing that can answer here. */
        if (expr->child_count == 0 && !type_is_unresolved(expr->elem_type))
            return expr->elem_type;
        DataType t = TYPE_UNKNOWN;
        for (int i = 0; i < expr->child_count; i++) {
            DataType e = infer_expr_type(expr->children[i]);
            if (e == TYPE_UNKNOWN || e == TYPE_NULL) continue;
            t = type_merge(t, e);
        }
        return type_is_unresolved(t) ? TYPE_INT : t;
    }

    if (expr->type == AST_IDENTIFIER) {
        /* A parameter first: its element type came from the call sites and is
           specific to this function, where the name-keyed registry below is
           shared across all of them and would merge sum_int's `a` with
           sum_float's. */
        DataType p = param_elem_in_current_fn(expr->value);
        if (!type_is_unresolved(p)) return p;
        DataType t = elem_lookup(expr->value);
        if (!type_is_unresolved(t)) return t;
        return TYPE_INT;
    }

    /* split() is the one builtin that builds an array of something other
       than numbers. Without this a `let parts = split(s, ",")` was an array
       of int as far as every typed backend was concerned, and parts[0] came
       back as an integer that was really a char* -- which compiled, and
       segfaulted at the first comparison. */
    if (expr->type == AST_CALL_EXPR) {
        const char *fn = expr->value;
        if (!fn && expr->left && expr->left->type == AST_MEMBER_ACCESS)
            fn = expr->left->value;
        if (fn && strcmp(fn, "split") == 0) return TYPE_STRING;
        if (fn) {
            ASTNode *decl = fn_lookup(fn);
            if (decl && !type_is_unresolved(decl->elem_type))
                return decl->elem_type;
        }
    }

    /* pop(a) yields an element, so its element type is one level further in;
       nothing needs that yet, and int is the safe answer. */
    return TYPE_INT;
}

int exponent_is_negative(ASTNode *expr) {
    if (!expr) return 0;
    if (expr->type == AST_UNARY_EXPR && expr->value &&
        strcmp(expr->value, "-") == 0) return 1;
    if (expr->type == AST_LITERAL && expr->value && expr->value[0] == '-')
        return 1;
    return 0;
}

int expr_is_null_literal(ASTNode *expr) {
    if (!expr) return 0;
    if (expr->data_type == TYPE_NULL) return 1;
    return expr->type == AST_LITERAL && expr->value &&
           (strcmp(expr->value, "null") == 0 || strcmp(expr->value, "nil") == 0);
}

DataType param_type_of(const char *fn_name, int index) {
    ASTNode *fn = fn_lookup(fn_name);
    if (!fn || index < 0 || index >= fn->child_count) return TYPE_UNKNOWN;
    return fn->children[index] ? fn->children[index]->data_type : TYPE_UNKNOWN;
}

int function_is_nullable(const char *fn_name) {
    if (!fn_name) return 0;
    for (int i = 0; i < g_nullable.count; i++)
        if (strcmp(g_nullable.names[i], fn_name) == 0) return 1;
    return 0;
}

static int body_returns_null(ASTNode *node) {
    if (!node) return 0;
    if (node->type == AST_FUNCTION_DECL || node->type == AST_ARROW_FUNCTION)
        return 0;
    if (node->type == AST_RETURN_STMT && expr_is_null_literal(node->right))
        return 1;
    return body_returns_null(node->left)      ||
           body_returns_null(node->right)     ||
           body_returns_null(node->condition) ||
           body_returns_null(node->body)      ||
           body_returns_null(node->next);
}

static int body_returns_value(ASTNode *node) {
    if (!node) return 0;
    if (node->type == AST_FUNCTION_DECL || node->type == AST_ARROW_FUNCTION)
        return 0;
    if (node->type == AST_RETURN_STMT && node->right &&
        !expr_is_null_literal(node->right))
        return 1;
    return body_returns_value(node->left)      ||
           body_returns_value(node->right)     ||
           body_returns_value(node->condition) ||
           body_returns_value(node->body)      ||
           body_returns_value(node->next);
}

static void mark_nullable_functions(void) {
    g_nullable.count = 0;
    for (int i = 0; i < g_fns.count; i++) {
        ASTNode *fn = g_fns.items[i].decl;
        if (!body_returns_null(fn->body) || !body_returns_value(fn->body))
            continue;
        if (g_nullable.count < MAX_TRACKED_FUNCTIONS)
            g_nullable.names[g_nullable.count++] = g_fns.items[i].name;
        /* Widen so the NaN sentinel has somewhere to live. */
        if (fn->data_type == TYPE_INT) fn->data_type = TYPE_FLOAT;
    }
}

/* Stamp each function's parameter types onto the identifier nodes inside its
   body. Backends look at the node in front of them, not at the enclosing
   signature, so without this `b == 0` inside safe_div looked untyped and the
   Rust backend could not tell it needed a float literal on the right. */
static void propagate_param_types_to_identifiers(ASTNode *fn, ASTNode *node,
                                                 AstSet *seen) {
    WALK_ENTER(seen, node);
    /* An inner function has its own parameters; do not reach into it. */
    if (node != fn && node->type == AST_FUNCTION_DECL) return;

    if (node->type == AST_IDENTIFIER && node->value) {
        for (int i = 0; i < fn->child_count; i++) {
            ASTNode *p = fn->children[i];
            if (p && p->value && strcmp(p->value, node->value) == 0) {
                if (!type_is_unresolved(p->data_type)) node->data_type = p->data_type;
                break;
            }
        }
    }

    propagate_param_types_to_identifiers(fn, node->left, seen);
    propagate_param_types_to_identifiers(fn, node->right, seen);
    propagate_param_types_to_identifiers(fn, node->condition, seen);
    propagate_param_types_to_identifiers(fn, node->body, seen);
    for (int i = 0; i < node->child_count; i++)
        propagate_param_types_to_identifiers(fn, node->children[i], seen);
    propagate_param_types_to_identifiers(fn, node->next, seen);
}

void infer_function_signatures(ASTNode *program) {
    g_elems.len = 0;
    g_vars.len = 0;
    g_props.count = 0;
    g_iclass_count = 0;
    iclasses_collect(program);
    if (!program) return;

    free(g_vars.items); free(g_elems.items); free(g_fn_tab);
    memset(&g_vars, 0, sizeof(g_vars));
    memset(&g_elems, 0, sizeof(g_elems));
    memset(&g_fn_tab, 0, sizeof(g_fn_tab));
    g_fns.count = 0;
    g_expr_depth = 0;
    AstSet seen = {NULL, 0, 0};
    collect_functions(program, &seen);
    /* No early return when a program declares no functions. Everything below
       that walks g_fns simply does nothing, but infer_var_decls and
       propagate_var_types_to_identifiers still have work: a script with no
       `fn` in it has variables whose types the backends need just as much.
       Bailing out here left those unanalysed, which is why `let a = []`
       followed by push(a, "x") produced an array of integers. */

    /* The parser stores the written-out type name in metadata whenever the
       source annotated a parameter, so a non-NULL metadata is already the
       "explicitly typed" flag - inference only reads it, never writes it. */

    /* Collect every user call site once, then replay the same list for each
       round - identical merges in identical order to the old whole-program
       walks, but O(sites) per round instead of O(program). */
    collect_call_sites(program);
    for (int round = 0; round < 3; round++) {
        WALK_RESET(seen);
        propagate_call_sites_round();

        for (int i = 0; i < g_fns.count; i++) {
            ASTNode *fn = g_fns.items[i].decl;
            if (type_is_unresolved(fn->data_type)) {
                g_current_fn = fn;
                DataType rt = infer_return_type(fn->body);
                g_current_fn = NULL;
                /* Leave it unresolved for now if the body only told us
                   "some value" - a later round may pin it down properly
                   once callee return types are known. */
                if (!type_is_unresolved(rt) && rt != TYPE_GENERIC) fn->data_type = rt;
            }
        }
    }

    /* Fill any parameter still unresolved from how the body uses it. */
    for (int i = 0; i < g_fns.count; i++) {
        ASTNode *fn = g_fns.items[i].decl;
        for (int p = 0; p < fn->child_count; p++) {
            ASTNode *param = fn->children[p];
            if (!param || param->metadata || param->explicit_type) continue;
            if (!type_is_unresolved(param->data_type)) continue;

            DataType acc = TYPE_UNKNOWN;
            g_current_fn = fn;
            WALK_RESET(seen);
            scan_param_usage(fn->body, param->value, &acc, &seen);
            g_current_fn = NULL;
            if (!type_is_unresolved(acc)) param->data_type = acc;
        }
    }

    /* What an array parameter holds, when no call site said.

       A module is compiled along with the program that imports it, including
       the functions that program never calls -- and an uncalled function has
       no call site to be inferred from. Its array parameter would then be
       taken to hold integers, so sum_float() summed floats into an integer
       and the typed backends rejected the body. The elements themselves say
       what they are: what the loop variable is added to, or what the function
       returns after reading one. */
    for (int round = 0; round < 2; round++) {
        WALK_RESET(seen);
        for (int i = 0; i < g_fns.count; i++) {
            ASTNode *fn = g_fns.items[i].decl;
            for (int p = 0; p < fn->child_count; p++) {
                ASTNode *param = fn->children[p];
                if (!param || param->data_type != TYPE_ARRAY) continue;
                if (!type_is_unresolved(param->elem_type)) continue;

                const char *names[MAX_TRACKED_VARS];
                int n = 0;
                WALK_RESET(seen);
                bind_element_names(fn->body, param->value, names,
                                   MAX_TRACKED_VARS, &n, &seen);

                DataType acc = TYPE_UNKNOWN;
                g_current_fn = fn;
                for (int k = 0; k < n; k++) {
                    WALK_RESET(seen);
                    scan_param_usage(fn->body, names[k], &acc, &seen);
                    if (fn->explicit_type && !type_is_unresolved(fn->data_type)) {
                        WALK_RESET(seen);
                        if (returns_name(fn->body, names[k], &seen))
                            acc = type_merge(acc, fn->data_type);
                    }
                }
                WALK_RESET(seen);
                elem_from_pass_through(fn->body, param->value, &acc, &seen);
                g_current_fn = NULL;
                if (!type_is_unresolved(acc) && acc != TYPE_GENERIC)
                    param->elem_type = acc;
            }
        }

        /* An array handed straight to another function holds whatever that
           function's parameter holds. */
        for (int i = 0; i < g_fns.count; i++)
            propagate_call_sites_in_fn(i);
    }

    /* Anything still unknown is genuinely unconstrained - a numeric default
       matches SUB's most common use and what the C backend always assumed. */
    for (int i = 0; i < g_fns.count; i++) {
        ASTNode *fn = g_fns.items[i].decl;
        for (int p = 0; p < fn->child_count; p++) {
            ASTNode *param = fn->children[p];
            if (!param) continue;
            if (type_is_unresolved(param->data_type))
                param->data_type = TYPE_INT;
        }
        if (type_is_unresolved(fn->data_type)) {
            g_current_fn = fn;
            fn->data_type = infer_return_type(fn->body);
            g_current_fn = NULL;
        }
    }

    mark_nullable_functions();
    for (int i = 0; i < g_fns.count; i++) {
        ASTNode *fn = g_fns.items[i].decl;
        WALK_RESET(seen);
        propagate_param_types_to_identifiers(fn, fn->body, &seen);
    }
    WALK_RESET(seen);
    infer_var_decls(program, &seen);
    WALK_RESET(seen);
    stamp_empty_array_elems(program, &seen);
    WALK_RESET(seen);
    propagate_var_types_to_identifiers(program, &seen);

    /* After the locals: `return out` can only say what it holds once the
       pushes into `out` have been seen. */
    for (int i = 0; i < g_fns.count; i++) {
        ASTNode *fn = g_fns.items[i].decl;
        if (fn->data_type != TYPE_ARRAY || !type_is_unresolved(fn->elem_type))
            continue;
        g_current_fn = fn;
        DataType et = TYPE_UNKNOWN;
        WALK_RESET(seen);
        collect_return_elem(fn->body, &et, &seen);
        g_current_fn = NULL;
        if (!type_is_unresolved(et) && et != TYPE_GENERIC) fn->elem_type = et;
    }

    free(seen.items);

    if (getenv("SUB_INFER_DEBUG")) {
        for (int i = 0; i < g_fns.count; i++) {
            ASTNode *f = g_fns.items[i].decl;
            fprintf(stderr, "[infer] %s ret=%d", f->value, (int)f->data_type);
            for (int p = 0; p < f->child_count; p++)
                fprintf(stderr, " %s=%d", f->children[p]->value, (int)f->children[p]->data_type);
            fprintf(stderr, "\n");
        }
    }

}
