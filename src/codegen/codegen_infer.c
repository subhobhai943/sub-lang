/* ========================================
   SUB Language - Shared Signature Inference
   See codegen_infer.h for why this exists.
   ======================================== */

#include "codegen_infer.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------------------------------------------
   Function registry
   ---------------------------------------------------------------- */

#define MAX_TRACKED_FUNCTIONS 512

typedef struct {
    ASTNode *decl;        /* the AST_FUNCTION_DECL node */
    const char *name;
} FnEntry;

typedef struct {
    FnEntry items[MAX_TRACKED_FUNCTIONS];
    int count;
} FnTable;

static FnTable g_fns;

static ASTNode* fn_lookup(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_fns.count; i++) {
        if (g_fns.items[i].name && strcmp(g_fns.items[i].name, name) == 0)
            return g_fns.items[i].decl;
    }
    return NULL;
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
   Variable registry

   An identifier node carries no type of its own; the type lives on the
   declaration. Without somewhere to look it up, `println(r)` could not tell
   that r was a double, and the C backend picked the %ld format for it -
   printing the double's bit pattern instead of the number.

   Scope is deliberately flat: SUB programs are small and this only has to
   pick a printf format and a declaration type, so a whole-program map of
   name -> type is enough. A name declared twice with different types falls
   back to the merged type, which is the conservative answer.
   ---------------------------------------------------------------- */

#define MAX_TRACKED_VARS 1024

/* The function whose body is being walked or generated, NULL at top level.
   Declared here because the type tables below are keyed by it. */
static ASTNode *g_current_fn;

typedef struct {
    const char *name;
    DataType    type;
    /* Which function declared it, NULL for a top-level variable. Two
       functions may each have a local called `out` of different types --
       arrays.sb and strings.sb both do -- and a table keyed by name alone
       answers for whichever was recorded last. That is how len(out) on an
       array came out as strlen(). */
    ASTNode    *owner;
} VarEntry;

static struct {
    VarEntry items[MAX_TRACKED_VARS];
    int count;
} g_vars;

/* Element types of array-valued variables, kept beside the variable types
   for the same reason: the declaration knows, and the use site does not. */
static struct {
    VarEntry items[MAX_TRACKED_VARS];
    int count;
} g_elems;

static void elem_record(const char *name, DataType t) {
    if (!name || t == TYPE_UNKNOWN) return;
    for (int i = 0; i < g_elems.count; i++)
        if (g_elems.items[i].owner == g_current_fn &&
            strcmp(g_elems.items[i].name, name) == 0) {
            g_elems.items[i].type = type_merge(g_elems.items[i].type, t);
            return;
        }
    if (g_elems.count >= MAX_TRACKED_VARS) return;
    g_elems.items[g_elems.count].name  = name;
    g_elems.items[g_elems.count].type  = t;
    g_elems.items[g_elems.count].owner = g_current_fn;
    g_elems.count++;
}

static DataType elem_lookup(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    /* This function's own first, then a top-level one. */
    for (int i = 0; i < g_elems.count; i++)
        if (g_elems.items[i].owner == g_current_fn &&
            strcmp(g_elems.items[i].name, name) == 0)
            return g_elems.items[i].type;
    for (int i = 0; i < g_elems.count; i++)
        if (g_elems.items[i].owner == NULL &&
            strcmp(g_elems.items[i].name, name) == 0)
            return g_elems.items[i].type;
    return TYPE_UNKNOWN;
}

static void var_record(const char *name, DataType t) {
    if (!name || t == TYPE_UNKNOWN) return;
    for (int i = 0; i < g_vars.count; i++) {
        if (g_vars.items[i].owner == g_current_fn &&
            strcmp(g_vars.items[i].name, name) == 0) {
            g_vars.items[i].type = type_merge(g_vars.items[i].type, t);
            return;
        }
    }
    if (g_vars.count >= MAX_TRACKED_VARS) return;
    g_vars.items[g_vars.count].name  = name;
    g_vars.items[g_vars.count].type  = t;
    g_vars.items[g_vars.count].owner = g_current_fn;
    g_vars.count++;
}

static DataType var_lookup(const char *name) {
    if (!name) return TYPE_UNKNOWN;
    /* This function's own first, then a top-level one. */
    for (int i = 0; i < g_vars.count; i++)
        if (g_vars.items[i].owner == g_current_fn &&
            strcmp(g_vars.items[i].name, name) == 0) return g_vars.items[i].type;
    for (int i = 0; i < g_vars.count; i++)
        if (g_vars.items[i].owner == NULL &&
            strcmp(g_vars.items[i].name, name) == 0) return g_vars.items[i].type;
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
               picked its printf format from the node's stale annotation. */
            result = infer_elem_type(expr->left);
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
static void collect_return_types(ASTNode *node, DataType *acc, int *saw_value_return) {
    if (!node) return;

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

    collect_return_types(node->left,      acc, saw_value_return);
    collect_return_types(node->right,     acc, saw_value_return);
    collect_return_types(node->condition, acc, saw_value_return);
    collect_return_types(node->body,      acc, saw_value_return);
    for (int i = 0; i < node->child_count; i++)
        collect_return_types(node->children[i], acc, saw_value_return);
    collect_return_types(node->next,      acc, saw_value_return);
}

DataType infer_return_type(ASTNode *body) {
    DataType acc = TYPE_UNKNOWN;
    int saw_value_return = 0;
    collect_return_types(body, &acc, &saw_value_return);

    if (!saw_value_return) return TYPE_VOID;
    /* Returns a value we could not classify - let the backend pick its widest
       type rather than emitting `void`, which miscompiles on every target
       that checks return types. */
    if (type_is_unresolved(acc) || acc == TYPE_NULL) return TYPE_GENERIC;
    return acc;
}

/* ----------------------------------------------------------------
   Parameter inference from call sites
   ---------------------------------------------------------------- */

static void propagate_call_sites(ASTNode *node) {
    if (!node) return;

    if (node->type == AST_CALL_EXPR && node->value) {
        ASTNode *decl = fn_lookup(node->value);
        if (decl) {
            int n = node->child_count < decl->child_count
                        ? node->child_count : decl->child_count;
            for (int i = 0; i < n; i++) {
                ASTNode *param = decl->children[i];
                ASTNode *arg   = node->children[i];
                if (!param || !arg) continue;
                /* Never override a type the programmer wrote down. */
                if (param->metadata || param->explicit_type) continue;
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
        }
    }

    propagate_call_sites(node->left);
    propagate_call_sites(node->right);
    propagate_call_sites(node->condition);
    propagate_call_sites(node->body);
    for (int i = 0; i < node->child_count; i++)
        propagate_call_sites(node->children[i]);
    propagate_call_sites(node->next);
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
                               const char *out[], int max, int *count) {
    if (!n || *count >= max) return;

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
        bind_element_names(n->children[i], arr, out, max, count);
    bind_element_names(n->left, arr, out, max, count);
    bind_element_names(n->right, arr, out, max, count);
    bind_element_names(n->condition, arr, out, max, count);
    bind_element_names(n->body, arr, out, max, count);
    bind_element_names(n->next, arr, out, max, count);
}

/* An array handed straight to another function holds whatever that
   function's parameter was found to hold. mean_float() gives no other clue:
   it only passes its array to sum_float() and divides by its length. */
static void elem_from_pass_through(ASTNode *n, const char *pname, DataType *acc) {
    if (!n) return;
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
        elem_from_pass_through(n->children[i], pname, acc);
    elem_from_pass_through(n->left, pname, acc);
    elem_from_pass_through(n->right, pname, acc);
    elem_from_pass_through(n->condition, pname, acc);
    elem_from_pass_through(n->body, pname, acc);
    elem_from_pass_through(n->next, pname, acc);
}

/* Whether the body has a bare `return <name>`. With an explicit return type
   that pins the name down, which is the only clue min_float() gives about
   what its array holds. */
static int returns_name(ASTNode *n, const char *name) {
    if (!n || !name) return 0;
    if (n->type == AST_RETURN_STMT && n->right &&
        n->right->type == AST_IDENTIFIER && n->right->value &&
        strcmp(n->right->value, name) == 0) return 1;
    for (int i = 0; i < n->child_count; i++)
        if (returns_name(n->children[i], name)) return 1;
    return returns_name(n->left, name) || returns_name(n->right, name) ||
           returns_name(n->condition, name) || returns_name(n->body, name) ||
           returns_name(n->next, name);
}

static void scan_param_usage(ASTNode *node, const char *pname, DataType *acc) {
    if (!node || !pname) return;

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

    scan_param_usage(node->left,      pname, acc);
    scan_param_usage(node->right,     pname, acc);
    scan_param_usage(node->condition, pname, acc);
    scan_param_usage(node->body,      pname, acc);
    for (int i = 0; i < node->child_count; i++)
        scan_param_usage(node->children[i], pname, acc);
    scan_param_usage(node->next,      pname, acc);
}

/* ----------------------------------------------------------------
   Driver
   ---------------------------------------------------------------- */

static void collect_functions(ASTNode *node) {
    if (!node) return;

    if (node->type == AST_FUNCTION_DECL && node->value &&
        g_fns.count < MAX_TRACKED_FUNCTIONS) {
        if (!fn_lookup(node->value)) {
            g_fns.items[g_fns.count].decl = node;
            g_fns.items[g_fns.count].name = node->value;
            g_fns.count++;
        }
    }

    collect_functions(node->left);
    collect_functions(node->right);
    collect_functions(node->condition);
    collect_functions(node->body);
    for (int i = 0; i < node->child_count; i++)
        collect_functions(node->children[i]);
    collect_functions(node->next);
}

/* Variable declarations are typed from their initializer. Without this,
   `let r = safe_div(10.0, 4.0)` stayed untyped and the C backend fell back
   to `long`, truncating 2.5 to 2. Runs after function signatures settle so
   a call's return type is known. */
static void infer_var_decls(ASTNode *node) {
    if (!node) return;

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

    infer_var_decls(node->left);
    infer_var_decls(node->right);
    infer_var_decls(node->condition);
    infer_var_decls(node->body);
    for (int i = 0; i < node->child_count; i++)
        infer_var_decls(node->children[i]);

    g_current_fn = saved;
    infer_var_decls(node->next);
}

/* Second pass: stamp the declaration's type onto every identifier that refers
   to it. The semantic pass leaves identifier nodes carrying its own int
   default, which short-circuits any later lookup - so the type has to be
   written onto the node itself, not just made available for lookup. */
static void propagate_var_types_to_identifiers(ASTNode *node) {
    if (!node) return;

    if (node->type == AST_IDENTIFIER && node->value) {
        DataType t = var_lookup(node->value);
        if (!type_is_unresolved(t) && t != TYPE_NULL) node->data_type = t;
    }

    propagate_var_types_to_identifiers(node->left);
    propagate_var_types_to_identifiers(node->right);
    propagate_var_types_to_identifiers(node->condition);
    propagate_var_types_to_identifiers(node->body);
    for (int i = 0; i < node->child_count; i++)
        propagate_var_types_to_identifiers(node->children[i]);
    propagate_var_types_to_identifiers(node->next);
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
static void propagate_param_types_to_identifiers(ASTNode *fn, ASTNode *node) {
    if (!node) return;
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

    propagate_param_types_to_identifiers(fn, node->left);
    propagate_param_types_to_identifiers(fn, node->right);
    propagate_param_types_to_identifiers(fn, node->condition);
    propagate_param_types_to_identifiers(fn, node->body);
    for (int i = 0; i < node->child_count; i++)
        propagate_param_types_to_identifiers(fn, node->children[i]);
    propagate_param_types_to_identifiers(fn, node->next);
}

void infer_function_signatures(ASTNode *program) {
    g_elems.count = 0;
    if (!program) return;

    g_fns.count = 0;
    g_vars.count = 0;
    g_expr_depth = 0;
    collect_functions(program);
    /* No early return when a program declares no functions. Everything below
       that walks g_fns simply does nothing, but infer_var_decls and
       propagate_var_types_to_identifiers still have work: a script with no
       `fn` in it has variables whose types the backends need just as much.
       Bailing out here left those unanalysed, which is why `let a = []`
       followed by push(a, "x") produced an array of integers. */

    /* The parser stores the written-out type name in metadata whenever the
       source annotated a parameter, so a non-NULL metadata is already the
       "explicitly typed" flag - inference only reads it, never writes it. */

    /* Types flow both ways: a call site pins down a parameter, which fixes a
       return type, which pins down a parameter one level up. Iterate to a
       fixpoint with a small bound - three rounds settles every shape we
       generate, and the bound keeps mutual recursion from looping. */
    for (int round = 0; round < 3; round++) {
        propagate_call_sites(program);

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
            scan_param_usage(fn->body, param->value, &acc);
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
        for (int i = 0; i < g_fns.count; i++) {
            ASTNode *fn = g_fns.items[i].decl;
            for (int p = 0; p < fn->child_count; p++) {
                ASTNode *param = fn->children[p];
                if (!param || param->data_type != TYPE_ARRAY) continue;
                if (!type_is_unresolved(param->elem_type)) continue;

                const char *names[MAX_TRACKED_VARS];
                int n = 0;
                bind_element_names(fn->body, param->value, names,
                                   MAX_TRACKED_VARS, &n);

                DataType acc = TYPE_UNKNOWN;
                g_current_fn = fn;
                for (int k = 0; k < n; k++) {
                    scan_param_usage(fn->body, names[k], &acc);
                    if (fn->explicit_type && !type_is_unresolved(fn->data_type) &&
                        returns_name(fn->body, names[k]))
                        acc = type_merge(acc, fn->data_type);
                }
                elem_from_pass_through(fn->body, param->value, &acc);
                g_current_fn = NULL;
                if (!type_is_unresolved(acc) && acc != TYPE_GENERIC)
                    param->elem_type = acc;
            }
        }

        /* An array handed straight to another function holds whatever that
           function's parameter holds. */
        for (int i = 0; i < g_fns.count; i++)
            propagate_call_sites(g_fns.items[i].decl->body);
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
        propagate_param_types_to_identifiers(fn, fn->body);
    }
    infer_var_decls(program);
    propagate_var_types_to_identifiers(program);

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
