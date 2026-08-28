#include <string.h>
#include "codegen_globals.h"

/* The statements of a program or block. A block keeps them on the ->next
   chain off ->body, or in children[] -- the same either/or the rest of
   codegen walks. */
static ASTNode *first_stmt(ASTNode *n) {
    if (!n) return NULL;
    if (n->body) return n->body;
    if (n->child_count > 0) return n->children[0];
    return NULL;
}

void globals_collect(Globals *g, ASTNode *program) {
    g->count = 0;
    if (!program) return;
    if (program->type != AST_PROGRAM && program->type != AST_BLOCK) return;

    /* Only the top level. A `let` inside a function or a loop is a local,
       and lifting it to file scope would both rename a variable the program
       never made global and run its initializer at the wrong time. */
    for (ASTNode *s = first_stmt(program); s; s = s->next) {
        if (s->type != AST_VAR_DECL && s->type != AST_CONST_DECL) continue;
        if (!s->value) continue;
        if (g->count < GLOBALS_MAX) g->decls[g->count++] = s;
    }
}

int globals_has(const Globals *g, const char *name) {
    if (!g || !name) return 0;
    for (int i = 0; i < g->count; i++)
        if (g->decls[i]->value && strcmp(g->decls[i]->value, name) == 0)
            return 1;
    return 0;
}

int globals_is_decl(const Globals *g, ASTNode *node) {
    if (!g || !node) return 0;
    for (int i = 0; i < g->count; i++)
        if (g->decls[i] == node) return 1;
    return 0;
}

int globals_is_first(const Globals *g, int i) {
    if (!g || i < 0 || i >= g->count) return 0;
    const char *name = g->decls[i]->value;
    for (int j = 0; j < i; j++)
        if (g->decls[j]->value && strcmp(g->decls[j]->value, name) == 0)
            return 0;
    return 1;
}

/* Walk a function body looking for assignments to a global.

   A local declaration shadows the global from there on, but SUB has no way
   to write `let x = ...` in a function and then mean the global `x` later in
   the same function, so a body that declares the name is treated as using
   its own throughout. That is the same answer Python's `global` statement
   forces and the same one the interpreter gives, since the local binding is
   created when the function's scope is entered. */
/* Whether ->body still needs visiting after children[] has been.

   A node uses ->body for one of two things. A block keeps its statements
   there, chained by ->next, and mirrors them in children[] -- walking both
   would visit every statement once per sibling. Everything else -- a loop, a
   function -- hangs a subordinate block off ->body that children[] does not
   contain, and skipping it loses the whole body. Comparing against
   children[0] is what tells them apart. */
static int walk_body(ASTNode *n) {
    if (!n->body) return 0;
    return n->child_count == 0 || n->children[0] != n->body;
}

static int declares(ASTNode *n, const char *name) {
    if (!n) return 0;
    if ((n->type == AST_VAR_DECL || n->type == AST_CONST_DECL) &&
        n->value && strcmp(n->value, name) == 0) return 1;
    if (n->type == AST_FUNCTION_DECL || n->type == AST_ARROW_FUNCTION) return 0;
    for (int i = 0; i < n->child_count; i++)
        if (declares(n->children[i], name)) return 1;
    if (declares(n->left, name) || declares(n->right, name) ||
        declares(n->condition, name)) return 1;
    if (!walk_body(n)) return 0;
    if (n->child_count == 0) {
        for (ASTNode *s = n->body; s; s = s->next)
            if (declares(s, name)) return 1;
    } else if (declares(n->body, name)) {
        return 1;
    }
    return 0;
}

/* Whether a parameter of `fn` shadows the name. */
static int is_param(ASTNode *fn, const char *name) {
    if (!fn) return 0;
    for (int i = 0; i < fn->child_count; i++) {
        ASTNode *p = fn->children[i];
        if (p && p->value && strcmp(p->value, name) == 0) return 1;
    }
    return 0;
}

/* Walk a function body collecting mentions of a global. `writes_only` picks
   assignment targets; otherwise any identifier counts. */
static void collect_used(ASTNode *n, const Globals *g, int writes_only,
                         const char *out[], int max, int *count) {
    if (!n || *count >= max) return;

    /* A nested function has a body of its own and is walked as part of that
       function, not this one. */
    if (n->type == AST_FUNCTION_DECL || n->type == AST_ARROW_FUNCTION) return;

    const char *hit = NULL;
    if (writes_only) {
        if (n->type == AST_ASSIGN_STMT && n->left &&
            n->left->type == AST_IDENTIFIER && n->left->value &&
            globals_has(g, n->left->value))
            hit = n->left->value;
    } else if (n->type == AST_IDENTIFIER && n->value && globals_has(g, n->value)) {
        hit = n->value;
    }
    if (hit) {
        int seen = 0;
        for (int i = 0; i < *count; i++)
            if (strcmp(out[i], hit) == 0) { seen = 1; break; }
        if (!seen && *count < max) out[(*count)++] = hit;
    }

    for (int i = 0; i < n->child_count; i++)
        collect_used(n->children[i], g, writes_only, out, max, count);
    collect_used(n->left, g, writes_only, out, max, count);
    collect_used(n->right, g, writes_only, out, max, count);
    collect_used(n->condition, g, writes_only, out, max, count);
    if (!walk_body(n)) return;
    if (n->child_count == 0) {
        for (ASTNode *s = n->body; s; s = s->next)
            collect_used(s, g, writes_only, out, max, count);
    } else {
        collect_used(n->body, g, writes_only, out, max, count);
    }
}

static int fn_globals(const Globals *g, ASTNode *fn, int writes_only,
                      const char *out[], int max) {
    int count = 0;
    if (!g || !fn || !fn->body) return 0;
    collect_used(fn->body, g, writes_only, out, max, &count);

    int kept = 0;
    for (int i = 0; i < count; i++)
        if (!is_param(fn, out[i]) && !declares(fn->body, out[i]))
            out[kept++] = out[i];
    return kept;
}

int fn_assigned_globals(const Globals *g, ASTNode *fn,
                        const char *out[], int max) {
    return fn_globals(g, fn, 1, out, max);
}

int fn_shadowed_globals(const Globals *g, ASTNode *fn,
                        const char *out[], int max) {
    int count = 0;
    if (!g || !fn) return 0;
    for (int i = 0; i < g->count && count < max; i++) {
        const char *name = g->decls[i]->value;
        if (!name) continue;
        int seen = 0;
        for (int j = 0; j < count; j++)
            if (strcmp(out[j], name) == 0) { seen = 1; break; }
        if (seen) continue;
        if (is_param(fn, name) || declares(fn->body, name)) out[count++] = name;
    }
    return count;
}

int fn_uses_global(const Globals *g, ASTNode *fn) {
    const char *names[GLOBALS_MAX];
    return fn_globals(g, fn, 0, names, GLOBALS_MAX) > 0;
}
