#define _GNU_SOURCE
/* ========================================
   SUB Language - AST to x86-64 machine code
   ----------------------------------------
   A straightforward single-accumulator code generator: every expression
   leaves its result in RAX, and a binary operator evaluates its left side,
   pushes it, evaluates its right side, and pops. That is not the fastest
   shape a compiler can take, but it is the one whose correctness is easiest
   to see, and it means adding a construct never requires thinking about
   register allocation.

   Values are statically typed. The types come from the same inference pass
   the C, Rust, Java, Go, Swift and Kotlin backends use (codegen_infer.c), so
   a program compiled here and the same program transpiled elsewhere agree
   about what every expression is - and a fix to inference fixes all of them
   at once. An integer or boolean occupies RAX directly, a double occupies
   RAX as its raw bit pattern, and a string is a pointer into the bump heap.

   Anything this backend cannot compile is reported through nc_fail() rather
   than mis-compiled; the driver falls back to the C backend and says why.
   ======================================== */

#include "x64_internal.h"
#include "../codegen/codegen_switch.h"
#include "../codegen/codegen_globals.h"
#include "sub_compiler.h"
#include "codegen_infer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T (&C->text)

/* Quiet NaN, used as the "no value" sentinel for functions that can return
   null as well as a number - the same representation the statically typed
   transpiler backends use, so the two agree. */
#define NULL_F64  0x7FF8000000000000ULL

/* `elem` is meaningful only when type is TYPE_ARRAY: SUB arrays are
   homogeneous in practice, and the element type decides how an element is
   loaded, stored and printed. */
/* `global` picks where the value lives: a frame slot off RBP, or a fixed
   cell in the BSS. `slot` indexes whichever it is. */
typedef struct { char *name; int slot; DataType type; DataType elem;
                 int global; } Local;

typedef struct LoopCtx {
    struct LoopCtx *prev;
    size_t *brk;  int nbrk,  cbrk;
    size_t *cont; int ncont, ccont;
    int is_switch;   /* catches `break`, but `continue` belongs to a loop */
} LoopCtx;

typedef struct { char *name; ASTNode *decl; } FnRec;

static NCtx *C;

/* Element type to give the next array literal, when the declaration it is
   being bound to knows better than the literal does - `let a = []` followed
   by push(a, "x"). Consumed by the literal that reads it. */
static DataType g_literal_elem = TYPE_UNKNOWN;

/* Current function being emitted. */
static struct {
    Local   *locals; int nloc, cloc;
    int      peak;   /* most slots live at once: what the frame must hold */
    DataType ret_type;
    int      ret_nullable;
    size_t  *rets;   int nrets, crets;
    LoopCtx *loop;
} F;

static FnRec *g_fns; static int g_nfns, g_cfns;

static void gen_stmt(ASTNode *n);
static void gen_expr(ASTNode *n);
static void gen_as(ASTNode *n, DataType want);
static DataType ty(ASTNode *n);
static void gen_do_while(ASTNode *n);
static void gen_switch(ASTNode *n);

/* ----------------------------------------------------------------
   Small growable arrays
   ---------------------------------------------------------------- */

static void *grow(void *p, int *cap, size_t elem) {
    int n = *cap ? *cap * 2 : 16;
    void *q = realloc(p, (size_t)n * elem);
    if (!q) abort();
    *cap = n;
    return q;
}

static void push_site(size_t **arr, int *n, int *cap, size_t site) {
    if (*n == *cap) *arr = grow(*arr, cap, sizeof(size_t));
    (*arr)[(*n)++] = site;
}

static void here(size_t site) { e_patch_rel32(T, site, C->text.len); }

/* ----------------------------------------------------------------
   Locals
   ---------------------------------------------------------------- */

/* The program's top-level variables. Unlike F.locals these outlive a
   function, so reset_fn() leaves them alone. */
static Local GLOB[X64_G_GLOBAL_N];
static int   NGLOB = 0;
static Globals g_globals;

static Local *glob_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < NGLOB; i++)
        if (strcmp(GLOB[i].name, name) == 0) return &GLOB[i];
    return NULL;
}

/* Backwards, so an inner declaration shadows an outer one of the same name.
   A local always wins over a global of the same name, which is what a
   parameter or a `let` inside the function means. */
static Local *loc_find(const char *name) {
    if (!name) return NULL;
    for (int i = F.nloc - 1; i >= 0; i--)
        if (strcmp(F.locals[i].name, name) == 0) return &F.locals[i];
    for (int i = 0; i < NGLOB; i++)
        if (strcmp(GLOB[i].name, name) == 0) return &GLOB[i];
    return NULL;
}

/* Slots live until the block that declared them ends. A loop body is a block,
   which is what makes `for i in ...` around an outer `i` shadow it rather
   than overwrite it -- the interpreter gives each iteration its own scope, so
   the outer name still holds its old value once the loop is done. */
static int loc_mark(void) { return F.nloc; }

static void loc_release(int mark) {
    for (int i = mark; i < F.nloc; i++) free(F.locals[i].name);
    F.nloc = mark;
}

/* Locals only. A global is never what a declaration or a parameter inside a
   function means -- `fn takes(shadowed)` binds the parameter, and a `let` of
   the name binds a new local -- so this must not see one. */
static Local *loc_find_local(const char *name) {
    if (!name) return NULL;
    for (int i = F.nloc - 1; i >= 0; i--)
        if (strcmp(F.locals[i].name, name) == 0) return &F.locals[i];
    return NULL;
}

static Local *loc_add(const char *name, DataType t) {
    Local *e = loc_find_local(name);
    if (e) { if (e->type == TYPE_UNKNOWN) e->type = t; return e; }
    if (F.nloc == F.cloc) F.locals = grow(F.locals, &F.cloc, sizeof(Local));
    e = &F.locals[F.nloc];
    e->name = strdup(name ? name : "_");
    e->slot = F.nloc;
    e->type = t;
    e->elem = TYPE_INT;
    /* grow() does not zero what it hands back, and a stale non-zero here
       sends every read of this local to the globals area instead of the
       frame. */
    e->global = 0;
    F.nloc++;
    if (F.nloc > F.peak) F.peak = F.nloc;
    return e;
}

/* A fresh slot even when the name is already taken: what a loop variable
   needs, so that assigning to it cannot reach the outer one. */
static Local *loc_declare(const char *name, DataType t) {
    if (F.nloc == F.cloc) F.locals = grow(F.locals, &F.cloc, sizeof(Local));
    Local *e = &F.locals[F.nloc];
    e->name = strdup(name ? name : "_");
    e->slot = F.nloc;
    e->type = t;
    e->elem = TYPE_INT;
    /* grow() does not zero what it hands back, and a stale non-zero here
       sends every read of this local to the globals area instead of the
       frame. */
    e->global = 0;
    F.nloc++;
    if (F.nloc > F.peak) F.peak = F.nloc;
    return e;
}

/* A name no SUB program can write, for the array and index a `for x in a`
   loop needs to hold across iterations. */
static Local *loc_add_hidden(const char *what) {
    static int n = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), " %s%d", what, n++);
    return loc_add(buf, TYPE_INT);
}

/* Frame displacement of a slot. Taking the slot number rather than the Local
   is what lets a caller keep it across gen_block(): declaring a name inside
   the body can grow F.locals, and a realloc leaves every Local* into it
   dangling. */
static int32_t disp(int slot) { return -8 * (slot + 1); }

static int32_t slot_disp(const Local *l) { return disp(l->slot); }

/* Read or write a variable wherever it lives. R11 is the scratch the entry
   stub and the allocator already use for a BSS address, and nothing holds a
   value in it across these. */
static void var_load(int reg, const Local *l) {
    if (!l->global) { e_mov_r_mem(T, reg, RBP, slot_disp(l)); return; }
    e_mov_r_imm64(T, R11, X64_G_GLOBALS + 8ULL * (uint64_t)l->slot);
    e_mov_r_mem(T, reg, R11, 0);
}

static void var_store(const Local *l, int reg) {
    if (!l->global) { e_mov_mem_r(T, RBP, slot_disp(l), reg); return; }
    e_mov_r_imm64(T, R11, X64_G_GLOBALS + 8ULL * (uint64_t)l->slot);
    e_mov_mem_r(T, R11, 0, reg);
}

/* ----------------------------------------------------------------
   Function registry
   ---------------------------------------------------------------- */

static ASTNode *fn_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_nfns; i++)
        if (strcmp(g_fns[i].name, name) == 0) return g_fns[i].decl;
    return NULL;
}

static void fn_register(ASTNode *n) {
    if (!n) return;
    if (n->type == AST_FUNCTION_DECL && n->value) {
        if (g_nfns == g_cfns) g_fns = grow(g_fns, &g_cfns, sizeof(FnRec));
        g_fns[g_nfns].name = strdup(n->value);
        g_fns[g_nfns].decl = n;
        g_nfns++;
    }
    for (ASTNode *s = n->body; s; s = s->next) fn_register(s);
    for (int i = 0; i < n->child_count; i++) fn_register(n->children[i]);
    if (n->type != AST_FUNCTION_DECL) {
        if (n->left)  fn_register(n->left);
        if (n->right) fn_register(n->right);
    }
}

/* ----------------------------------------------------------------
   Types
   ---------------------------------------------------------------- */

static int is_cmp_op(const char *op) {
    return op && (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") ||
                  !strcmp(op, ">")  || !strcmp(op, "<=") || !strcmp(op, ">="));
}

static int is_logic_op(const char *op) {
    return op && (!strcmp(op, "&&") || !strcmp(op, "||") ||
                  !strcmp(op, "and") || !strcmp(op, "or"));
}

/* Builtins whose result type does not depend on the argument. */
static int builtin_fixed_type(const char *name, DataType *out) {
    static const struct { const char *n; DataType t; } tab[] = {
        {"str", TYPE_STRING}, {"to_string", TYPE_STRING}, {"upper", TYPE_STRING},
        {"lower", TYPE_STRING}, {"trim", TYPE_STRING}, {"type", TYPE_STRING},
        {"int", TYPE_INT}, {"len", TYPE_INT}, {"floor", TYPE_INT},
        {"ceil", TYPE_INT}, {"round", TYPE_INT},
        {"float", TYPE_FLOAT}, {"sqrt", TYPE_FLOAT},
        {"print", TYPE_VOID}, {"println", TYPE_VOID}, {"show", TYPE_VOID},
        {NULL, TYPE_UNKNOWN}
    };
    for (int i = 0; tab[i].n; i++)
        if (strcmp(name, tab[i].n) == 0) { *out = tab[i].t; return 1; }
    return 0;
}

static DataType elem_type_of(ASTNode *n);

static DataType num_merge(DataType a, DataType b) {
    if (a == TYPE_FLOAT || b == TYPE_FLOAT) return TYPE_FLOAT;
    return TYPE_INT;
}

static DataType ty(ASTNode *n) {
    if (!n) return TYPE_UNKNOWN;
    switch (n->type) {
    case AST_ARRAY_LITERAL:
        return TYPE_ARRAY;
    case AST_ARRAY_ACCESS:
        return elem_type_of(n->left);
    case AST_IDENTIFIER: {
        Local *l = loc_find(n->value);
        if (l && l->type != TYPE_UNKNOWN) return l->type;
        break;
    }
    case AST_BINARY_EXPR: {
        const char *op = n->value;
        if (is_cmp_op(op) || is_logic_op(op)) return TYPE_BOOL;
        DataType lt = ty(n->left), rt = ty(n->right);
        if (op && !strcmp(op, "+") && (lt == TYPE_STRING || rt == TYPE_STRING))
            return TYPE_STRING;
        if (op && !strcmp(op, "**")) {
            if (lt == TYPE_INT && rt == TYPE_INT && !exponent_is_negative(n->right))
                return TYPE_INT;
            return TYPE_FLOAT;
        }
        return num_merge(lt, rt);
    }
    case AST_UNARY_EXPR: {
        const char *op = n->value;
        if (op && (!strcmp(op, "!") || !strcmp(op, "not"))) return TYPE_BOOL;
        return ty(n->right ? n->right : n->left);
    }
    case AST_TERNARY_EXPR: {
        DataType a = ty(n->left), b = ty(n->right);
        if (a == b) return a;
        if (a == TYPE_UNKNOWN || a == TYPE_NULL) return b;
        if (b == TYPE_UNKNOWN || b == TYPE_NULL) return a;
        return num_merge(a, b);
    }
    case AST_CALL_EXPR: {
        DataType t;
        if (n->value && builtin_fixed_type(n->value, &t)) return t;
        if (n->value && (!strcmp(n->value, "abs") || !strcmp(n->value, "min") ||
                         !strcmp(n->value, "max"))) {
            DataType a = ty(n->child_count > 0 ? n->children[0] : NULL);
            DataType b = n->child_count > 1 ? ty(n->children[1]) : a;
            return num_merge(a, b);
        }
        if (n->value && !strcmp(n->value, "pop") && n->child_count > 0)
            return elem_type_of(n->children[0]);
        if (n->value && (!strcmp(n->value, "push") ||
                         !strcmp(n->value, "append")))
            return TYPE_NULL;
        ASTNode *d = fn_find(n->value);
        if (d && d->data_type != TYPE_UNKNOWN && d->data_type != TYPE_AUTO)
            return d->data_type;
        break;
    }
    default: break;
    }
    DataType t = infer_expr_type(n);
    if (t == TYPE_AUTO || t == TYPE_GENERIC) t = TYPE_UNKNOWN;
    return t;
}

/* The element type of an array-valued expression. */
static DataType elem_type_of(ASTNode *n) {
    if (!n) return TYPE_INT;
    if (n->type == AST_ARRAY_LITERAL) {
        /* `let out = []` says nothing on its own; the shared inference pass
           writes what the pushes into it revealed onto the literal. */
        if (n->child_count == 0 && n->elem_type != TYPE_UNKNOWN &&
            n->elem_type != TYPE_AUTO)
            return n->elem_type;
        DataType t = TYPE_UNKNOWN;
        for (int i = 0; i < n->child_count; i++) {
            DataType e = ty(n->children[i]);
            if (e == TYPE_UNKNOWN || e == TYPE_NULL) continue;
            if (t == TYPE_UNKNOWN) { t = e; continue; }
            if (t == e) continue;
            if ((t == TYPE_INT && e == TYPE_FLOAT) ||
                (t == TYPE_FLOAT && e == TYPE_INT)) { t = TYPE_FLOAT; continue; }
            nc_fail(C, "an array mixing %s and %s elements is not supported by "
                       "the native backend (line %d)",
                    t == TYPE_STRING ? "text" : "numbers",
                    e == TYPE_STRING ? "text" : "numbers", n->line);
            return TYPE_INT;
        }
        return t == TYPE_UNKNOWN ? TYPE_INT : t;
    }
    if (n->type == AST_IDENTIFIER) {
        Local *l = loc_find(n->value);
        if (l && l->type == TYPE_ARRAY && l->elem != TYPE_UNKNOWN) return l->elem;
        DataType t = infer_elem_type_of_var(n->value);
        if (t != TYPE_UNKNOWN) return t;
    }
    /* split() builds an array of strings, and a user function that returns an
       array carries what it holds on its declaration. */
    if (n->type == AST_CALL_EXPR && n->value) {
        if (!strcmp(n->value, "split")) return TYPE_STRING;
        ASTNode *decl = fn_find(n->value);
        if (decl && decl->elem_type != TYPE_UNKNOWN &&
            decl->elem_type != TYPE_AUTO)
            return decl->elem_type;
    }
    return TYPE_INT;
}

/* The tag stored in an array header, so the runtime can print elements. */
static int array_kind(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return AK_FLOAT;
    case TYPE_STRING: return AK_STRING;
    case TYPE_BOOL:   return AK_BOOL;
    default:          return AK_INT;
    }
}

/* An unresolved type is compiled as an integer: that is what the semantic
   pass already assumes for un-annotated declarations, and it keeps a stray
   TYPE_UNKNOWN from silently producing a different representation here than
   in the other backends. */
static DataType concrete(DataType t) {
    switch (t) {
    case TYPE_FLOAT: case TYPE_STRING: case TYPE_BOOL:
    case TYPE_INT:   case TYPE_ARRAY:  return t;
    default: return TYPE_INT;
    }
}

/* Names for the AST node kinds this backend may refuse, so a fallback
   message says "an array literal" rather than "expression form 29". */
static const char *ast_kind_name(ASTNodeType t) {
    switch (t) {
    case AST_ARRAY_LITERAL:   return "an array literal";
    case AST_OBJECT_LITERAL:  return "an object literal";
    case AST_ARRAY_ACCESS:    return "array indexing";
    case AST_MEMBER_ACCESS:   return "member access";
    case AST_NEW_EXPR:        return "`new`";
    case AST_ARROW_FUNCTION:  return "an arrow function";
    case AST_CLASS_DECL:      return "a class declaration";
    case AST_TRY_STMT:        return "try/catch";
    case AST_THROW_STMT:      return "throw";
    case AST_SWITCH_STMT:     return "switch";
    case AST_DO_WHILE_STMT:   return "do/while";
    case AST_ARRAY_ITERATION: return "iterating a collection";
    case AST_UI_COMPONENT:    return "a UI declaration";
    case AST_EMBED_CODE:
    case AST_EMBED_C:
    case AST_EMBED_CPP:       return "an embedded code block";
    case AST_RANGE_EXPR:      return "a bare range()";
    default:                  return NULL;
    }
}

/* Report an unsupported construct by name where one is known. */
static void fail_unsupported(ASTNode *n, const char *what) {
    const char *name = ast_kind_name(n->type);
    if (name)
        nc_fail(C, "%s is not supported by the native backend (line %d)",
                name, n->line);
    else
        nc_fail(C, "this %s is not supported by the native backend "
                   "(node kind %d, line %d)", what, (int)n->type, n->line);
}

/* ----------------------------------------------------------------
   Conversions - `from` is in RAX, leave the `want` form in RAX
   ---------------------------------------------------------------- */

static void convert(DataType from, DataType want, ASTNode *src) {
    from = concrete(from);
    want = concrete(want);
    if (from == want) return;

    if (want == TYPE_FLOAT && (from == TYPE_INT || from == TYPE_BOOL)) {
        e_cvtsi2sd(T, XMM0, RAX);
        e_movq_r_x(T, RAX, XMM0);
        return;
    }
    if (want == TYPE_INT && from == TYPE_FLOAT) {
        e_movq_x_r(T, XMM0, RAX);
        e_cvttsd2si(T, RAX, XMM0);
        return;
    }
    if (want == TYPE_INT && from == TYPE_BOOL) return;
    if (want == TYPE_BOOL && (from == TYPE_INT)) return;

    if (want == TYPE_STRING) {
        e_mov_r_r(T, RDI, RAX);
        if (from == TYPE_FLOAT)      nc_call_rt(C, RT_F2S);
        else if (from == TYPE_BOOL)  nc_call_rt(C, RT_B2S);
        else if (from == TYPE_ARRAY) nc_call_rt(C, RT_ARR_STR);
        else                         nc_call_rt(C, RT_I2S);
        return;
    }
    if (from == TYPE_ARRAY || want == TYPE_ARRAY) {
        nc_fail(C, "an array cannot be used as a number here (line %d)",
                src ? src->line : 0);
        return;
    }
    if (from == TYPE_STRING) {
        nc_fail(C, "cannot convert a string to a number at line %d",
                src ? src->line : 0);
        return;
    }
    nc_fail(C, "unsupported conversion at line %d", src ? src->line : 0);
}

/* Bind an array-valued expression: everything but a fresh literal is copied,
   because binding a SUB array copies it. A literal has no other owner yet,
   so copying it would be pure waste. */
static void gen_bind(ASTNode *n, DataType want) {
    gen_as(n, want);
    if (concrete(want) == TYPE_ARRAY && n && n->type != AST_ARRAY_LITERAL) {
        e_mov_r_r(T, RDI, RAX);
        nc_call_rt(C, RT_ARR_COPY);
    }
}

static void gen_as(ASTNode *n, DataType want) {
    DataType have = ty(n);
    /* `null` in a numeric context is the NaN sentinel, not zero, so that a
       nullable function's result can be told apart from a real 0.0. */
    if (expr_is_null_literal(n) && concrete(want) == TYPE_FLOAT) {
        e_mov_r_imm64(T, RAX, NULL_F64);
        return;
    }
    gen_expr(n);
    convert(have, want, n);
}

/* ----------------------------------------------------------------
   Literals
   ---------------------------------------------------------------- */

static void gen_literal(ASTNode *n) {
    const char *v = n->value ? n->value : "0";
    DataType t = ty(n);

    if (t == TYPE_STRING) { nc_load_cstr(C, RAX, v); return; }
    if (expr_is_null_literal(n)) { e_xor_r_r(T, RAX, RAX); return; }
    if (!strcmp(v, "true"))  { e_mov_r_imm64(T, RAX, 1); return; }
    if (!strcmp(v, "false")) { e_xor_r_r(T, RAX, RAX); return; }

    if (t == TYPE_FLOAT) {
        double d = strtod(v, NULL);
        uint64_t bits;
        memcpy(&bits, &d, 8);
        e_mov_r_imm64(T, RAX, bits);
        return;
    }
    e_mov_r_imm64(T, RAX, (uint64_t)strtoll(v, NULL, 10));
}

/* ----------------------------------------------------------------
   Conditions
   ---------------------------------------------------------------- */

/* Leave 0 or 1 in RAX for the truthiness of `n`, matching the interpreter:
   a string is true when it is non-empty, a number when it is non-zero. */
static void gen_truth(ASTNode *n) {
    DataType t = concrete(ty(n));
    gen_expr(n);
    if (t == TYPE_INT || t == TYPE_BOOL) {
        e_test_r_r(T, RAX, RAX);
        e_setcc(T, CC_NE, RAX);
        e_movzx_r_r8(T, RAX, RAX);
    } else if (t == TYPE_FLOAT) {
        e_movq_x_r(T, XMM0, RAX);
        e_xorpd(T, XMM1, XMM1);
        e_ucomisd(T, XMM0, XMM1);
        e_setcc(T, CC_NE, RAX);          /* != 0, and NaN is unordered */
        e_setcc(T, CC_P, RCX);           /* ... which counts as true too */
        e_or_r_r(T, RAX, RCX);
        e_movzx_r_r8(T, RAX, RAX);
    } else if (t == TYPE_ARRAY) {
        e_mov_r_mem(T, RAX, RAX, ARR_COUNT);
        e_test_r_r(T, RAX, RAX);
        e_setcc(T, CC_NE, RAX);
        e_movzx_r_r8(T, RAX, RAX);
    } else {
        e_test_r_r(T, RAX, RAX);
        size_t empty = e_jcc(T, CC_E);
        e_movzx_r_mem8(T, RAX, RAX, 0);
        here(empty);
        e_test_r_r(T, RAX, RAX);
        e_setcc(T, CC_NE, RAX);
        e_movzx_r_r8(T, RAX, RAX);
    }
}

/* Emit a jump taken when `n` is false; returns the site to patch. */
static size_t gen_jump_if_false(ASTNode *n) {
    gen_truth(n);
    e_test_r_r(T, RAX, RAX);
    return e_jcc(T, CC_E);
}

/* ----------------------------------------------------------------
   Binary operators
   ---------------------------------------------------------------- */

static int cmp_cc(const char *op, int is_float) {
    /* ucomisd sets the flags as if the comparison were unsigned, so the
       float forms use the below/above conditions rather than less/greater. */
    if (!strcmp(op, "==")) return CC_E;
    if (!strcmp(op, "!=")) return CC_NE;
    if (!strcmp(op, "<"))  return is_float ? CC_B  : CC_L;
    if (!strcmp(op, ">"))  return is_float ? CC_A  : CC_G;
    if (!strcmp(op, "<=")) return is_float ? CC_BE : CC_LE;
    return is_float ? CC_AE : CC_GE;
}

/* `x == null` / `x != null`. For a float that means testing for the NaN
   sentinel; for anything else, testing against zero. */
static int gen_null_compare(ASTNode *n) {
    const char *op = n->value;
    if (!op || (strcmp(op, "==") && strcmp(op, "!="))) return 0;

    ASTNode *val = NULL;
    if (expr_is_null_literal(n->right))      val = n->left;
    else if (expr_is_null_literal(n->left))  val = n->right;
    else return 0;

    int eq = !strcmp(op, "==");
    DataType vt = concrete(ty(val));
    gen_expr(val);
    if (vt == TYPE_FLOAT) {
        e_movq_x_r(T, XMM0, RAX);
        e_ucomisd(T, XMM0, XMM0);        /* only NaN is unordered with itself */
        e_setcc(T, eq ? CC_P : CC_NP, RAX);
    } else {
        e_test_r_r(T, RAX, RAX);
        e_setcc(T, eq ? CC_E : CC_NE, RAX);
    }
    e_movzx_r_r8(T, RAX, RAX);
    return 1;
}

static void gen_logic(ASTNode *n) {
    int is_and = (!strcmp(n->value, "&&") || !strcmp(n->value, "and"));
    gen_truth(n->left);
    e_test_r_r(T, RAX, RAX);
    size_t shortcut = e_jcc(T, is_and ? CC_E : CC_NE);
    gen_truth(n->right);
    size_t done = e_jmp(T);
    here(shortcut);
    e_mov_r_imm64(T, RAX, is_and ? 0 : 1);
    here(done);
}

static void gen_binary(ASTNode *n) {
    const char *op = n->value ? n->value : "+";

    if (is_logic_op(op))    { gen_logic(n); return; }
    if (gen_null_compare(n)) return;

    DataType lt = ty(n->left), rt = ty(n->right);

    /* String concatenation and string comparison */
    if (lt == TYPE_STRING || rt == TYPE_STRING) {
        if (!strcmp(op, "+")) {
            gen_as(n->left, TYPE_STRING);
            e_push(T, RAX);
            gen_as(n->right, TYPE_STRING);
            e_mov_r_r(T, RSI, RAX);
            e_pop(T, RDI);
            nc_call_rt(C, RT_CONCAT);
            return;
        }
        if (is_cmp_op(op)) {
            gen_as(n->left, TYPE_STRING);
            e_push(T, RAX);
            gen_as(n->right, TYPE_STRING);
            e_mov_r_r(T, RSI, RAX);
            e_pop(T, RDI);
            nc_call_rt(C, RT_STRCMP);
            e_cmp_r_imm(T, RAX, 0);
            e_setcc(T, cmp_cc(op, 0), RAX);
            e_movzx_r_r8(T, RAX, RAX);
            return;
        }
        nc_fail(C, "operator '%s' is not defined for strings (line %d)",
                op, n->line);
        return;
    }

    /* `**` on two non-negative integers stays integral; everything else
       about it is floating point, which is what the interpreter does. */
    if (!strcmp(op, "**")) {
        int as_int = (concrete(lt) == TYPE_INT && concrete(rt) == TYPE_INT &&
                      !exponent_is_negative(n->right));
        DataType t = as_int ? TYPE_INT : TYPE_FLOAT;
        gen_as(n->left, t);
        e_push(T, RAX);
        gen_as(n->right, as_int ? TYPE_INT : TYPE_FLOAT);
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, as_int ? RT_IPOW : RT_FPOW);
        return;
    }

    DataType common = num_merge(concrete(lt), concrete(rt));
    int isf = (common == TYPE_FLOAT);

    gen_as(n->left, common);
    e_push(T, RAX);
    gen_as(n->right, common);
    e_mov_r_r(T, RCX, RAX);
    e_pop(T, RAX);                        /* RAX = left, RCX = right */

    if (is_cmp_op(op)) {
        if (isf) {
            e_movq_x_r(T, XMM0, RAX);
            e_movq_x_r(T, XMM1, RCX);
            e_ucomisd(T, XMM0, XMM1);
        } else {
            e_cmp_r_r(T, RAX, RCX);
        }
        e_setcc(T, cmp_cc(op, isf), RAX);
        e_movzx_r_r8(T, RAX, RAX);
        return;
    }

    if (!strcmp(op, "/")) {
        /* Both forms go through the runtime so that dividing by zero
           reports the interpreter's error rather than trapping (integers)
           or yielding an infinity (floats). */
        e_mov_r_r(T, RDI, RAX);
        e_mov_r_r(T, RSI, RCX);
        nc_call_rt(C, isf ? RT_FDIV : RT_IDIV);
        return;
    }
    if (!strcmp(op, "%")) {
        e_mov_r_r(T, RDI, RAX);
        e_mov_r_r(T, RSI, RCX);
        nc_call_rt(C, isf ? RT_FMOD : RT_IMOD);
        return;
    }

    if (isf) {
        e_movq_x_r(T, XMM0, RAX);
        e_movq_x_r(T, XMM1, RCX);
        if (!strcmp(op, "+"))      e_addsd(T, XMM0, XMM1);
        else if (!strcmp(op, "-")) e_subsd(T, XMM0, XMM1);
        else if (!strcmp(op, "*")) e_mulsd(T, XMM0, XMM1);
        else { nc_fail(C, "operator '%s' is not supported (line %d)", op, n->line); return; }
        e_movq_r_x(T, RAX, XMM0);
        return;
    }

    if (!strcmp(op, "+"))      e_add_r_r(T, RAX, RCX);
    else if (!strcmp(op, "-")) e_sub_r_r(T, RAX, RCX);
    else if (!strcmp(op, "*")) e_imul_r_r(T, RAX, RCX);
    else nc_fail(C, "operator '%s' is not supported (line %d)", op, n->line);
}

/* ----------------------------------------------------------------
   Unary operators
   ---------------------------------------------------------------- */

static void gen_unary(ASTNode *n) {
    ASTNode *v = n->right ? n->right : n->left;
    const char *op = n->value ? n->value : "-";

    if (!strcmp(op, "!") || !strcmp(op, "not")) {
        gen_truth(v);
        e_xor_r_r(T, RCX, RCX);
        e_cmp_r_imm(T, RAX, 0);
        e_setcc(T, CC_E, RAX);
        e_movzx_r_r8(T, RAX, RAX);
        return;
    }
    if (!strcmp(op, "+")) { gen_expr(v); return; }
    if (strcmp(op, "-") != 0) {
        nc_fail(C, "unary '%s' is not supported by the native backend (line %d)",
                op, n->line);
        return;
    }

    DataType t = concrete(ty(v));
    gen_expr(v);
    if (t == TYPE_FLOAT) {
        /* flipping the sign bit also negates zero and NaN correctly */
        e_mov_r_imm64(T, RCX, 0x8000000000000000ULL);
        e_xor_r_r(T, RAX, RCX);
    } else {
        e_neg_r(T, RAX);
    }
}

/* ----------------------------------------------------------------
   Calls
   ---------------------------------------------------------------- */

static const int ARG_REGS[6] = { RDI, RSI, RDX, RCX, R8, R9 };

static void gen_user_call(ASTNode *n, ASTNode *decl) {
    int argc = n->child_count;
    if (argc > 6) {
        nc_fail(C, "the native backend supports at most 6 arguments; "
                   "'%s' takes %d (line %d)", n->value, argc, n->line);
        return;
    }
    if (argc > decl->child_count) argc = decl->child_count;

    for (int i = 0; i < argc; i++) {
        DataType pt = decl->children[i]->data_type;
        gen_bind(n->children[i], concrete(pt));
        e_push(T, RAX);
    }
    for (int i = argc - 1; i >= 0; i--) e_pop(T, ARG_REGS[i]);
    nc_call_fn(C, n->value);
}

/* print / show put each argument on its own line; println puts them all on
   one line separated by spaces. */
static void gen_print(ASTNode *n, int one_line) {
    for (int i = 0; i < n->child_count; i++) {
        if (one_line && i > 0) {
            nc_load_cstr(C, RDI, " ");
            e_mov_r_imm64(T, RSI, 1);
            nc_call_rt(C, RT_WRITE);
        }
        gen_as(n->children[i], TYPE_STRING);
        e_mov_r_r(T, RDI, RAX);
        nc_call_rt(C, one_line ? RT_PUTSN : RT_PUTS);
    }
    if (one_line) {
        nc_load_cstr(C, RDI, "\n");
        e_mov_r_imm64(T, RSI, 1);
        nc_call_rt(C, RT_WRITE);
    }
}

static void gen_abs(ASTNode *arg) {
    DataType t = concrete(ty(arg));
    gen_expr(arg);
    if (t == TYPE_FLOAT) {
        e_mov_r_imm64(T, RCX, 0x7FFFFFFFFFFFFFFFULL);
        e_and_r_r(T, RAX, RCX);
    } else {
        /* branchless |x|: the sign mask is added and then xored away */
        e_mov_r_r(T, RCX, RAX);
        e_sar_r_imm8(T, RCX, 63);
        e_xor_r_r(T, RAX, RCX);
        e_sub_r_r(T, RAX, RCX);
    }
}

static void gen_minmax(ASTNode *n, int want_max) {
    DataType t = num_merge(concrete(ty(n->children[0])),
                           concrete(ty(n->children[1])));
    gen_as(n->children[0], t);
    e_push(T, RAX);
    gen_as(n->children[1], t);
    e_mov_r_r(T, RCX, RAX);
    e_pop(T, RAX);
    if (t == TYPE_FLOAT) {
        e_movq_x_r(T, XMM0, RAX);
        e_movq_x_r(T, XMM1, RCX);
        e_ucomisd(T, XMM0, XMM1);
        e_cmovcc_r_r(T, want_max ? CC_B : CC_A, RAX, RCX);
    } else {
        e_cmp_r_r(T, RAX, RCX);
        e_cmovcc_r_r(T, want_max ? CC_L : CC_G, RAX, RCX);
    }
}

/* floor / ceil / round all return an integer, as the interpreter does. */
static void gen_rounding(ASTNode *arg, const char *which) {
    gen_as(arg, TYPE_FLOAT);
    e_movq_x_r(T, XMM0, RAX);
    e_cvttsd2si(T, RAX, XMM0);           /* trunc toward zero */
    e_cvtsi2sd(T, XMM1, RAX);

    if (!strcmp(which, "floor")) {
        /* truncation is already floor for non-negatives; below zero it
           overshoots by one whenever a fraction was dropped */
        e_ucomisd(T, XMM1, XMM0);
        size_t ok = e_jcc(T, CC_BE);
        e_sub_r_imm(T, RAX, 1);
        here(ok);
    } else if (!strcmp(which, "ceil")) {
        e_ucomisd(T, XMM1, XMM0);
        size_t ok = e_jcc(T, CC_AE);
        e_add_r_imm(T, RAX, 1);
        here(ok);
    } else {
        /* round(): half away from zero, like C's round() */
        e_mov_r_imm64(T, RCX, 0x3FE0000000000000ULL);   /* 0.5 */
        e_movq_x_r(T, XMM2, RCX);
        e_mov_r_imm64(T, RCX, 0x8000000000000000ULL);
        e_movq_r_x(T, RDX, XMM0);
        e_and_r_r(T, RDX, RCX);
        e_movq_r_x(T, RCX, XMM2);
        e_or_r_r(T, RCX, RDX);                          /* copysign(0.5, x) */
        e_movq_x_r(T, XMM2, RCX);
        e_addsd(T, XMM0, XMM2);
        e_cvttsd2si(T, RAX, XMM0);
    }
}

static void gen_call(ASTNode *n) {
    const char *fn = n->value;
    if (!fn) { nc_fail(C, "call with no callee (line %d)", n->line); return; }

    if (!strcmp(fn, "print") || !strcmp(fn, "show")) { gen_print(n, 0); return; }
    if (!strcmp(fn, "println"))                      { gen_print(n, 1); return; }

    ASTNode *a0 = n->child_count > 0 ? n->children[0] : NULL;

    if (!strcmp(fn, "str") || !strcmp(fn, "to_string")) {
        if (!a0) { nc_load_cstr(C, RAX, ""); return; }
        gen_as(a0, TYPE_STRING); return;
    }
    if (!strcmp(fn, "int"))   { if (a0) gen_as(a0, TYPE_INT);   else e_xor_r_r(T, RAX, RAX); return; }
    if (!strcmp(fn, "float")) { if (a0) gen_as(a0, TYPE_FLOAT); else e_xor_r_r(T, RAX, RAX); return; }

    if (!strcmp(fn, "len")) {
        DataType at = concrete(ty(a0));
        if (at == TYPE_ARRAY) {
            gen_expr(a0);
            e_mov_r_mem(T, RAX, RAX, ARR_COUNT);
            return;
        }
        if (at != TYPE_STRING) {
            nc_fail(C, "len() of a %s is not supported by the native "
                       "backend (line %d)",
                    at == TYPE_FLOAT ? "float" : "number", n->line);
            return;
        }
        gen_expr(a0);
        e_mov_r_r(T, RDI, RAX);
        nc_call_rt(C, RT_STRLEN);
        return;
    }

    if ((!strcmp(fn, "push") || !strcmp(fn, "append")) && n->child_count >= 2) {
        if (concrete(ty(a0)) != TYPE_ARRAY) {
            nc_fail(C, "%s() needs an array (line %d)", fn, n->line);
            return;
        }
        gen_expr(a0);
        e_push(T, RAX);
        gen_as(n->children[1], elem_type_of(a0));
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_ARR_PUSH);
        e_xor_r_r(T, RAX, RAX);          /* push() evaluates to null */
        return;
    }

    if (!strcmp(fn, "pop") && a0) {
        if (concrete(ty(a0)) != TYPE_ARRAY) {
            nc_fail(C, "pop() needs an array (line %d)", n->line);
            return;
        }
        gen_expr(a0);
        e_mov_r_r(T, RDI, RAX);
        nc_call_rt(C, RT_ARR_POP);
        return;
    }
    if (!strcmp(fn, "upper") || !strcmp(fn, "lower")) {
        gen_as(a0, TYPE_STRING);
        e_mov_r_r(T, RDI, RAX);
        e_mov_r_imm64(T, RSI, fn[0] == 'u' ? 1 : 0);
        nc_call_rt(C, RT_STRCASE);
        return;
    }
    if (!strcmp(fn, "trim")) {
        gen_as(a0, TYPE_STRING);
        e_mov_r_r(T, RDI, RAX);
        nc_call_rt(C, RT_TRIM);
        return;
    }

    /* The character-level string builtins. Each argument is evaluated and
       pushed before the next, because gen_expr uses RAX and the argument
       registers freely; the runtime routines themselves are in
       x64_runtime.c. */
    if (!strcmp(fn, "substring") && n->child_count >= 2) {
        gen_as(a0, TYPE_STRING);
        e_push(T, RAX);
        gen_as(n->children[1], TYPE_INT);
        e_push(T, RAX);
        if (n->child_count > 2) {
            gen_as(n->children[2], TYPE_INT);
            e_mov_r_r(T, RDX, RAX);
            e_pop(T, RSI);
            e_pop(T, RDI);
        } else {
            /* No end: the whole rest of the string. */
            e_pop(T, RSI);
            e_pop(T, RDI);
            e_push(T, RDI);
            e_push(T, RSI);
            nc_call_rt(C, RT_STRLEN);
            e_mov_r_r(T, RDX, RAX);
            e_pop(T, RSI);
            e_pop(T, RDI);
        }
        nc_call_rt(C, RT_SUBSTR);
        return;
    }
    if (!strcmp(fn, "char_at") && n->child_count >= 2) {
        gen_as(a0, TYPE_STRING);
        e_push(T, RAX);
        gen_as(n->children[1], TYPE_INT);
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_CHARAT);
        return;
    }
    if (!strcmp(fn, "contains") && n->child_count >= 2) {
        gen_as(a0, TYPE_STRING);
        e_push(T, RAX);
        gen_as(n->children[1], TYPE_STRING);
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_CONTAINS);
        return;
    }
    if (!strcmp(fn, "replace") && n->child_count >= 3) {
        gen_as(a0, TYPE_STRING);
        e_push(T, RAX);
        gen_as(n->children[1], TYPE_STRING);
        e_push(T, RAX);
        gen_as(n->children[2], TYPE_STRING);
        e_mov_r_r(T, RDX, RAX);
        e_pop(T, RSI);
        e_pop(T, RDI);
        nc_call_rt(C, RT_REPLACE);
        return;
    }
    if (!strcmp(fn, "split") && n->child_count >= 1) {
        gen_as(a0, TYPE_STRING);
        e_push(T, RAX);
        if (n->child_count > 1) gen_as(n->children[1], TYPE_STRING);
        else nc_load_cstr(C, RAX, " ");
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_SPLIT);
        return;
    }
    if (!strcmp(fn, "join") && n->child_count >= 1) {
        gen_expr(a0);
        e_push(T, RAX);
        if (n->child_count > 1) gen_as(n->children[1], TYPE_STRING);
        else nc_load_cstr(C, RAX, "");
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_JOIN);
        return;
    }
    if (!strcmp(fn, "sqrt")) {
        gen_as(a0, TYPE_FLOAT);
        e_movq_x_r(T, XMM0, RAX);
        e_sqrtsd(T, XMM0, XMM0);
        e_movq_r_x(T, RAX, XMM0);
        return;
    }
    if (!strcmp(fn, "abs"))   { gen_abs(a0); return; }
    if (!strcmp(fn, "min") && n->child_count >= 2) { gen_minmax(n, 0); return; }
    if (!strcmp(fn, "max") && n->child_count >= 2) { gen_minmax(n, 1); return; }
    if (!strcmp(fn, "floor") || !strcmp(fn, "ceil") || !strcmp(fn, "round")) {
        gen_rounding(a0, fn);
        return;
    }
    if (!strcmp(fn, "pow") && n->child_count >= 2) {
        gen_as(n->children[0], TYPE_FLOAT);
        e_push(T, RAX);
        gen_as(n->children[1], TYPE_FLOAT);
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_FPOW);
        return;
    }
    if (!strcmp(fn, "type")) {
        DataType t = concrete(ty(a0));
        nc_load_cstr(C, RAX,
                     t == TYPE_STRING ? "string" :
                     t == TYPE_FLOAT  ? "float"  :
                     t == TYPE_BOOL   ? "bool"   : "int");
        return;
    }

    ASTNode *decl = fn_find(fn);
    if (decl) { gen_user_call(n, decl); return; }

    nc_fail(C, "'%s' is not available in the native backend (line %d)",
            fn, n->line);
}

/* ----------------------------------------------------------------
   Expressions
   ---------------------------------------------------------------- */

static void gen_expr(ASTNode *n) {
    if (!n || C->failed) return;
    switch (n->type) {
    case AST_LITERAL:     gen_literal(n); break;
    case AST_BINARY_EXPR: gen_binary(n);  break;
    case AST_UNARY_EXPR:  gen_unary(n);   break;
    case AST_CALL_EXPR:   gen_call(n);    break;

    case AST_IDENTIFIER: {
        Local *l = loc_find(n->value);
        if (!l) {
            nc_fail(C, "'%s' is used before it is defined (line %d)",
                    n->value ? n->value : "?", n->line);
            return;
        }
        var_load(RAX, l);
        break;
    }

    case AST_ARRAY_LITERAL: {
        DataType elem = g_literal_elem != TYPE_UNKNOWN ? g_literal_elem
                                                       : elem_type_of(n);
        g_literal_elem = TYPE_UNKNOWN;
        e_mov_r_imm64(T, RDI, (uint64_t)(n->child_count ? n->child_count : 4));
        e_mov_r_imm64(T, RSI, (uint64_t)array_kind(elem));
        nc_call_rt(C, RT_ARR_NEW);
        e_push(T, RAX);
        for (int i = 0; i < n->child_count; i++) {
            gen_as(n->children[i], elem);
            e_mov_r_r(T, RSI, RAX);
            e_mov_r_mem(T, RDI, RSP, 0);
            nc_call_rt(C, RT_ARR_PUSH);
        }
        e_pop(T, RAX);
        break;
    }

    case AST_ARRAY_ACCESS: {
        gen_expr(n->left);
        e_push(T, RAX);
        gen_as(n->right, TYPE_INT);
        e_mov_r_r(T, RSI, RAX);
        e_pop(T, RDI);
        nc_call_rt(C, RT_ARR_GET);
        break;
    }

    case AST_TERNARY_EXPR: {
        DataType t = concrete(ty(n));
        size_t els = gen_jump_if_false(n->condition);
        gen_as(n->left, t);
        size_t done = e_jmp(T);
        here(els);
        gen_as(n->right, t);
        here(done);
        break;
    }

    default:
        fail_unsupported(n, "expression");
        break;
    }
}

/* ----------------------------------------------------------------
   Statements
   ---------------------------------------------------------------- */

static ASTNode *block_first(ASTNode *n) {
    if (!n) return NULL;
    if (n->body) return n->body;
    if (n->children && n->child_count > 0) return n->children[0];
    if (n->left) return n->left;
    return NULL;
}

static void gen_block(ASTNode *n) {
    for (ASTNode *s = block_first(n); s; s = s->next) gen_stmt(s);
}

static void gen_assign_to(const char *name, ASTNode *value, int line) {
    Local *l = loc_find(name);
    if (!l) {
        nc_fail(C, "assignment to undeclared '%s' (line %d)",
                name ? name : "?", line);
        return;
    }
    gen_bind(value, l->type);
    var_store(l, RAX);
}

static void gen_if(ASTNode *n) {
    size_t els = gen_jump_if_false(n->condition);
    gen_block(n->body);
    if (n->right) {
        size_t done = e_jmp(T);
        here(els);
        /* `elif` arrives as a nested if in ->right; a plain `else` as a block */
        if (n->right->type == AST_IF_STMT) gen_stmt(n->right);
        else gen_block(n->right);
        here(done);
    } else {
        here(els);
    }
}

static void loop_enter(LoopCtx *lc) {
    memset(lc, 0, sizeof(*lc));
    lc->prev = F.loop;
    F.loop = lc;
}

static void loop_leave(LoopCtx *lc, size_t cont_target, size_t brk_target) {
    for (int i = 0; i < lc->ncont; i++) e_patch_rel32(T, lc->cont[i], cont_target);
    for (int i = 0; i < lc->nbrk;  i++) e_patch_rel32(T, lc->brk[i],  brk_target);
    free(lc->cont);
    free(lc->brk);
    F.loop = lc->prev;
}

static void gen_while(ASTNode *n) {
    LoopCtx lc; loop_enter(&lc);
    size_t top = C->text.len;
    size_t out = gen_jump_if_false(n->condition);
    gen_block(n->body);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);
    here(out);
    loop_leave(&lc, top, C->text.len);
}

static void gen_do_while(ASTNode *n) {
    LoopCtx lc; loop_enter(&lc);
    size_t top = C->text.len;
    gen_block(n->body);
    /* `continue` in a do/while goes to the test, not back to the top. */
    size_t test = C->text.len;
    gen_truth(n->condition);
    e_test_r_r(T, RAX, RAX);
    size_t again = e_jcc(T, CC_NE);
    e_patch_rel32(T, again, top);
    loop_leave(&lc, test, C->text.len);
}

/* Compare the scrutinee, held in `sw`, against the value in RAX and leave the
   flags set so that CC_E means "these match". */
static void gen_switch_cmp(Local *sw, DataType st) {
    if (st == TYPE_STRING) {
        e_mov_r_r(T, RSI, RAX);
        e_mov_r_mem(T, RDI, RBP, slot_disp(sw));
        nc_call_rt(C, RT_STRCMP);
        e_cmp_r_imm(T, RAX, 0);
    } else if (st == TYPE_FLOAT) {
        e_mov_r_mem(T, RCX, RBP, slot_disp(sw));
        e_movq_x_r(T, XMM0, RCX);
        e_movq_x_r(T, XMM1, RAX);
        e_ucomisd(T, XMM0, XMM1);
    } else {
        e_mov_r_mem(T, RCX, RBP, slot_disp(sw));
        e_cmp_r_r(T, RCX, RAX);
    }
}

static void gen_switch(ASTNode *n) {
    DataType st = concrete(ty(n->condition));
    if (st == TYPE_ARRAY) {
        nc_fail(C, "a switch on an array is not supported by the native "
                   "backend (line %d)", n->line);
        return;
    }

    /* The scrutinee is evaluated once, into a slot the case tests read. */
    Local *sw = loc_add_hidden("sw");
    sw->type = st;
    gen_as(n->condition, st);
    e_mov_mem_r(T, RBP, slot_disp(sw), RAX);

    ASTNode *deflt = switch_default_clause(n);

    /* A switch catches `break` the way a loop does; nothing continues it. */
    LoopCtx lc; loop_enter(&lc); lc.is_switch = 1;

    size_t *ends = NULL; int nend = 0, cend = 0;

    for (int i = 0; i < n->child_count; i++) {
        ASTNode *c = n->children[i];
        if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;

        /* Each value that matches jumps to the body; falling off the end of
           the tests moves on to the next clause. */
        size_t *hits = NULL; int nhit = 0, chit = 0;
        for (int j = 0; j < c->child_count; j++) {
            gen_as(c->children[j], st);
            gen_switch_cmp(sw, st);
            push_site(&hits, &nhit, &chit, e_jcc(T, CC_E));
        }
        size_t miss = e_jmp(T);
        for (int j = 0; j < nhit; j++) here(hits[j]);
        free(hits);

        gen_block(c->body);
        push_site(&ends, &nend, &cend, e_jmp(T));
        here(miss);
    }

    if (deflt) gen_block(deflt->body);

    for (int i = 0; i < nend; i++) here(ends[i]);
    free(ends);

    loop_leave(&lc, C->text.len, C->text.len);
}

static void gen_for(ASTNode *n) {
    /* The loop variable and anything the body declares belong to the loop. */
    int mark = loc_mark();
    ASTNode *range = (n->child_count > 0 && n->children[0] &&
                      n->children[0]->type == AST_RANGE_EXPR)
                     ? n->children[0] : NULL;

    /* `for x in <array>`: the array and the index live in hidden slots so
       they survive the body, which is free to reassign anything visible. */
    /* An unknown collection type is taken to be an array. A function that is
       imported but never called has no call site to infer its parameter from,
       and refusing to compile it would make importing a module cost more than
       using it. Strings are the only other thing `for x in y` accepts and
       this backend does not support iterating one either way. */
    DataType coll = n->condition ? ty(n->condition) : TYPE_UNKNOWN;
    if (!range && n->condition &&
        (coll == TYPE_ARRAY || coll == TYPE_UNKNOWN)) {
        DataType elem = elem_type_of(n->condition);
        int arr, idx, var;
        {
            Local *a = loc_add_hidden("arr");
            Local *i = loc_add_hidden("idx");
            Local *v = loc_declare(n->value ? n->value : "item", elem);
            v->type = elem;
            if (elem == TYPE_ARRAY) v->elem = TYPE_INT;
            arr = a->slot; idx = i->slot; var = v->slot;
        }

        gen_expr(n->condition);
        e_mov_mem_r(T, RBP, disp(arr), RAX);
        e_xor_r_r(T, RAX, RAX);
        e_mov_mem_r(T, RBP, disp(idx), RAX);

        LoopCtx lc; loop_enter(&lc);
        size_t top = C->text.len;
        e_mov_r_mem(T, RAX, RBP, disp(arr));
        e_mov_r_mem(T, RAX, RAX, ARR_COUNT);
        e_mov_r_mem(T, RCX, RBP, disp(idx));
        e_cmp_r_r(T, RCX, RAX);
        size_t out = e_jcc(T, CC_GE);

        e_mov_r_mem(T, RDI, RBP, disp(arr));
        e_mov_r_r(T, RSI, RCX);
        nc_call_rt(C, RT_ARR_GET);
        e_mov_mem_r(T, RBP, disp(var), RAX);

        gen_block(n->body);

        size_t step = C->text.len;
        e_mov_r_mem(T, RAX, RBP, disp(idx));
        e_add_r_imm(T, RAX, 1);
        e_mov_mem_r(T, RBP, disp(idx), RAX);
        size_t back = e_jmp(T); e_patch_rel32(T, back, top);
        here(out);
        loop_leave(&lc, step, C->text.len);
        loc_release(mark);
        return;
    }

    if (!range) {
        loc_release(mark);
        nc_fail(C, "the native backend supports `for x in range(...)` and "
                   "`for x in <array>` (line %d)", n->line);
        return;
    }
    ASTNode *start = range->right ? range->left  : NULL;
    ASTNode *end   = range->right ? range->right : range->left;

    int slot;
    {
        Local *l = loc_declare(n->value ? n->value : "i", TYPE_INT);
        l->type = TYPE_INT;
        slot = l->slot;
    }

    if (start) gen_as(start, TYPE_INT);
    else       e_xor_r_r(T, RAX, RAX);
    e_mov_mem_r(T, RBP, disp(slot), RAX);

    LoopCtx lc; loop_enter(&lc);
    size_t top = C->text.len;
    if (end) gen_as(end, TYPE_INT);
    else     e_mov_r_imm64(T, RAX, 10);
    e_mov_r_mem(T, RCX, RBP, disp(slot));
    e_cmp_r_r(T, RCX, RAX);
    size_t out = e_jcc(T, CC_GE);

    gen_block(n->body);

    size_t step = C->text.len;
    e_mov_r_mem(T, RAX, RBP, disp(slot));
    e_add_r_imm(T, RAX, 1);
    e_mov_mem_r(T, RBP, disp(slot), RAX);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);
    here(out);
    loop_leave(&lc, step, C->text.len);
    loc_release(mark);
}

static void gen_return(ASTNode *n) {
    ASTNode *v = n->right ? n->right : n->left;
    if (!v) v = n->body;
    if (v) gen_as(v, F.ret_type);
    else if (F.ret_type == TYPE_FLOAT && F.ret_nullable)
        e_mov_r_imm64(T, RAX, NULL_F64);
    else
        e_xor_r_r(T, RAX, RAX);
    push_site(&F.rets, &F.nrets, &F.crets, e_jmp(T));
}

static void gen_stmt(ASTNode *n) {
    if (!n || C->failed) return;
    switch (n->type) {
    case AST_PROGRAM:
    case AST_BLOCK:
        gen_block(n);
        break;

    case AST_FUNCTION_DECL:
        /* emitted separately, at the top level */
        break;

    case AST_VAR_DECL:
    case AST_CONST_DECL: {
        /* ty() knows about array literals where the semantic pass does not,
           so it wins when it says the initialiser is an array. */
        DataType rt = ty(n->right);
        DataType t = concrete(rt == TYPE_ARRAY ? TYPE_ARRAY
                              : (n->data_type != TYPE_UNKNOWN &&
                                 n->data_type != TYPE_AUTO)
                                ? n->data_type : rt);
        /* At the top level this declaration *is* the global's, so it fills
           in the BSS cell the functions already resolve to. Anywhere else a
           `let` of the same name is a new local. */
        Local *l = globals_is_decl(&g_globals, n) ? glob_find(n->value)
                                                  : loc_add(n->value, t);
        if (!l) l = loc_add(n->value, t);
        l->type = t;
        if (t == TYPE_ARRAY) {
            /* What the variable is later pushed to beats what the literal
               shows, which for an empty literal is nothing. */
            DataType shared = infer_elem_type_of_var(n->value);
            l->elem = (shared != TYPE_UNKNOWN) ? shared : elem_type_of(n->right);
            g_literal_elem = l->elem;
        }
        if (n->right) {
            gen_bind(n->right, t);
            g_literal_elem = TYPE_UNKNOWN;
            var_store(l, RAX);
        } else {
            e_xor_r_r(T, RAX, RAX);
            var_store(l, RAX);
        }
        break;
    }

    case AST_ASSIGN_STMT:
        if (n->left && n->left->type == AST_ARRAY_ACCESS) {
            ASTNode *acc = n->left;
            if (concrete(ty(acc->left)) != TYPE_ARRAY) {
                nc_fail(C, "indexed assignment needs an array (line %d)", n->line);
                break;
            }
            gen_expr(acc->left);
            e_push(T, RAX);
            gen_as(acc->right, TYPE_INT);
            e_push(T, RAX);
            gen_as(n->right, elem_type_of(acc->left));
            e_mov_r_r(T, RDX, RAX);
            e_pop(T, RSI);
            e_pop(T, RDI);
            nc_call_rt(C, RT_ARR_SET);
            break;
        }
        if (n->left && n->left->type == AST_IDENTIFIER)
            gen_assign_to(n->left->value, n->right, n->line);
        else if (n->value)
            gen_assign_to(n->value, n->right, n->line);
        else
            nc_fail(C, "only assignment to a plain variable is supported "
                       "(line %d)", n->line);
        break;

    case AST_IF_STMT:    gen_if(n);    break;
    case AST_WHILE_STMT: gen_while(n); break;
    case AST_DO_WHILE_STMT: gen_do_while(n); break;
    case AST_SWITCH_STMT: gen_switch(n); break;
    case AST_FOR_STMT:   gen_for(n);   break;
    case AST_RETURN_STMT: gen_return(n); break;

    case AST_BREAK_STMT:
        if (!F.loop) { nc_fail(C, "break outside a loop or switch (line %d)", n->line); break; }
        push_site(&F.loop->brk, &F.loop->nbrk, &F.loop->cbrk, e_jmp(T));
        break;

    case AST_CONTINUE_STMT: {
        /* A switch is breakable but not continuable, so `continue` inside a
           case belongs to whatever loop encloses the switch. */
        LoopCtx *l = F.loop;
        while (l && l->is_switch) l = l->prev;
        if (!l) { nc_fail(C, "continue outside a loop (line %d)", n->line); break; }
        push_site(&l->cont, &l->ncont, &l->ccont, e_jmp(T));
        break;
    }

    case AST_CALL_EXPR:
        gen_call(n);
        break;

    default:
        fail_unsupported(n, "statement");
        break;
    }
}

/* ----------------------------------------------------------------
   Function emission
   ----------------------------------------------------------------
   Frame size is not known until the body has been walked, because locals are
   discovered as they are declared. The prologue therefore reserves a fixed,
   generous slab rather than being patched afterwards; a SUB function with
   more than MAX_SLOTS distinct names is rejected instead of silently
   overflowing its frame. */

#define MAX_SLOTS 256

static void reset_fn(DataType ret, int nullable) {
    for (int i = 0; i < F.nloc; i++) free(F.locals[i].name);
    free(F.locals); free(F.rets);
    memset(&F, 0, sizeof(F));
    F.ret_type     = ret;
    F.ret_nullable = nullable;
}

static void emit_function(ASTNode *fn, const char *name) {
    DataType ret = concrete(fn && fn->data_type != TYPE_VOID
                            ? fn->data_type : TYPE_INT);
    reset_fn(ret, fn && fn->value ? function_is_nullable(fn->value) : 0);

    nc_define_fn(C, name);
    e_push(T, RBP);
    e_mov_r_r(T, RBP, RSP);
    e_sub_r_imm(T, RSP, 8 * MAX_SLOTS);

    if (fn) {
        int argc = fn->child_count > 6 ? 6 : fn->child_count;
        for (int i = 0; i < argc; i++) {
            ASTNode *p = fn->children[i];
            /* Left unknown rather than defaulted to int when the parameter
               has no inferred type, so that a use site can still tell "no
               information" from "an integer". */
            Local *l = loc_declare(p->value, concrete(p->data_type));
            l->type = p->data_type != TYPE_UNKNOWN ? concrete(p->data_type)
                                                   : TYPE_UNKNOWN;
            /* An array parameter also carries what is in the array, or every
               element is read back as an integer -- which turns a float into
               its bit pattern. */
            if (l->type == TYPE_ARRAY && p->elem_type != TYPE_UNKNOWN)
                l->elem = p->elem_type;
            e_mov_mem_r(T, RBP, slot_disp(l), ARG_REGS[i]);
        }
        if (fn->child_count > 6)
            nc_fail(C, "'%s' has %d parameters; the native backend supports 6",
                    name, fn->child_count);
        gen_block(fn->body);
    }

    /* value returned when control reaches the end without a `return` */
    if (F.ret_type == TYPE_FLOAT && F.ret_nullable)
        e_mov_r_imm64(T, RAX, NULL_F64);
    else
        e_xor_r_r(T, RAX, RAX);

    for (int i = 0; i < F.nrets; i++) e_patch_rel32(T, F.rets[i], C->text.len);
    if (F.peak > MAX_SLOTS)
        nc_fail(C, "'%s' declares %d names; the native backend supports %d",
                name, F.peak, MAX_SLOTS);

    e_leave(T);
    e_ret(T);
    reset_fn(TYPE_INT, 0);
}

/* Emits everything that is not a function declaration as the program body. */
static void emit_main(ASTNode *program) {
    reset_fn(TYPE_INT, 0);
    nc_define_fn(C, "__sub_main");
    e_push(T, RBP);
    e_mov_r_r(T, RBP, RSP);
    e_sub_r_imm(T, RSP, 8 * MAX_SLOTS);

    for (ASTNode *s = block_first(program); s; s = s->next)
        if (s->type != AST_FUNCTION_DECL) gen_stmt(s);

    e_xor_r_r(T, RAX, RAX);
    for (int i = 0; i < F.nrets; i++) e_patch_rel32(T, F.rets[i], C->text.len);
    if (F.peak > MAX_SLOTS)
        nc_fail(C, "the program declares %d top-level names; the native "
                   "backend supports %d", F.peak, MAX_SLOTS);
    e_leave(T);
    e_ret(T);
    reset_fn(TYPE_INT, 0);
}

/* ----------------------------------------------------------------
   Entry point
   ---------------------------------------------------------------- */

/* Compile `program` to a native executable at `out_path`.
   Returns 0 on success; on failure `err` (if given) receives the reason. */
/* Give every top-level variable a BSS cell before anything is emitted. The
   functions are compiled first and may read or assign one, so the name has
   to resolve by then -- and to the same cell the top-level code writes. */
static void globals_bind(ASTNode *program) {
    Globals g;
    globals_collect(&g_globals, program);
    g = g_globals;
    NGLOB = 0;
    for (int i = 0; i < g.count; i++) {
        if (!globals_is_first(&g, i)) continue;
        ASTNode *d = g.decls[i];
        if (NGLOB >= X64_G_GLOBAL_N) {
            nc_fail(C, "'%s' is global number %d; the native backend supports %d",
                    d->value ? d->value : "?", NGLOB + 1, X64_G_GLOBAL_N);
            return;
        }
        DataType t = d->data_type;
        if (t == TYPE_UNKNOWN || t == TYPE_AUTO) t = infer_expr_type(d->right);
        Local *l = &GLOB[NGLOB];
        l->name   = strdup(d->value ? d->value : "_");
        l->slot   = NGLOB;
        l->type   = concrete(t);
        l->global = 1;
        DataType elem = infer_elem_type_of_var(d->value);
        l->elem = (elem != TYPE_UNKNOWN) ? elem : infer_elem_type(d->right);
        NGLOB++;
    }
}

int native_compile(ASTNode *program, const char *out_path,
                   char *err, size_t err_size) {
    NCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    buf_init(&ctx.text);
    buf_init(&ctx.rodata);
    C = &ctx;

    memset(&F, 0, sizeof(F));
    g_fns = NULL; g_nfns = g_cfns = 0;
    fn_register(program);
    globals_bind(program);

    /* The entry stub sits at offset 0 so the ELF entry point is simply the
       start of the text: point the heap at the BSS, run the program, exit. */
    e_mov_r_imm64(T, R11, X64_G_HEAPPTR);
    e_mov_r_imm64(T, RAX, X64_HEAP_START);
    e_mov_mem_r(T, R11, 0, RAX);
    nc_call_fn(C, "__sub_main");
    e_mov_r_imm64(T, RAX, 60);           /* exit_group */
    e_xor_r_r(T, RDI, RDI);
    e_syscall(T);

    nc_emit_runtime(C);

    for (int i = 0; i < g_nfns; i++)
        emit_function(g_fns[i].decl, g_fns[i].name);
    emit_main(program);

    for (int i = 0; i < NGLOB; i++) free(GLOB[i].name);
    NGLOB = 0;

    size_t rodata_off = ctx.text.len;
    buf_bytes(&ctx.text, ctx.rodata.data, ctx.rodata.len);
    nc_resolve(C, rodata_off);

    int rc = 0;
    if (ctx.failed) {
        if (err && err_size) snprintf(err, err_size, "%s", ctx.err);
        rc = 1;
    } else if (elf64_write(out_path, &ctx.text, 0) != 0) {
        if (err && err_size)
            snprintf(err, err_size, "could not write %s", out_path);
        rc = 1;
    }

    for (int i = 0; i < g_nfns; i++) free(g_fns[i].name);
    free(g_fns); g_fns = NULL; g_nfns = g_cfns = 0;
    reset_fn(TYPE_INT, 0);
    buf_free(&ctx.text);
    buf_free(&ctx.rodata);
    for (int i = 0; i < ctx.fnfix_n; i++) free(ctx.fnfix[i].name);
    for (int i = 0; i < ctx.fns_n;   i++) free(ctx.fns[i].name);
    free(ctx.rtfix); free(ctx.fnfix); free(ctx.fns); free(ctx.dfix);
    C = NULL;
    return rc;
}
