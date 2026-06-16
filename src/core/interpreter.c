#define _GNU_SOURCE
#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static SubVal NULL_VAL = {VAL_NULL};

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
        if (e->val.type == VAL_STRING) free(e->val.sv);
        free(e); e = n;
    }
    free(env);
}

SubVal env_get(Env *env, const char *name) {
    for (Env *s = env; s; s = s->parent)
        for (EnvEntry *e = s->vars; e; e = e->next)
            if (strcmp(e->name, name) == 0) return e->val;
    fprintf(stderr, "Undefined: %s\n", name);
    return NULL_VAL;
}

void env_define(Env *env, const char *name, SubVal val) {
    EnvEntry *e = calloc(1, sizeof(EnvEntry));
    e->name = strdup(name);
    e->val  = val;
    e->next = env->vars;
    env->vars = e;
}

void env_set(Env *env, const char *name, SubVal val) {
    for (Env *s = env; s; s = s->parent)
        for (EnvEntry *e = s->vars; e; e = e->next)
            if (strcmp(e->name, name) == 0) { e->val = val; return; }
    env_define(env, name, val);
}

static SubVal make_int(long long v) { return (SubVal){VAL_INT, .iv=v}; }
static SubVal make_float(double v)  { return (SubVal){VAL_FLOAT, .fv=v}; }
static SubVal make_bool(int v)      { return (SubVal){VAL_BOOL, .bv=v}; }
static SubVal make_str(const char *s) {
    SubVal v = {VAL_STRING}; v.sv = strdup(s ? s : ""); return v;
}

static int is_truthy(SubVal v) {
    switch (v.type) {
        case VAL_BOOL:  return v.bv;
        case VAL_INT:   return v.iv != 0;
        case VAL_FLOAT: return v.fv != 0.0;
        case VAL_STRING:return v.sv && v.sv[0];
        default:        return 0;
    }
}

static void print_val(SubVal v) {
    switch (v.type) {
        case VAL_INT:    printf("%lld\n", v.iv); break;
        case VAL_FLOAT:  printf("%g\n",   v.fv); break;
        case VAL_BOOL:   printf("%s\n",   v.bv ? "true" : "false"); break;
        case VAL_STRING: printf("%s\n",   v.sv ? v.sv : ""); break;
        default:         printf("null\n"); break;
    }
}

static SubVal eval_block(ASTNode *node, Env *env);

static SubVal eval_binary(ASTNode *node, Env *env) {
    const char *op = node->value;
    SubVal L = eval(node->left, env);
    SubVal R = eval(node->right, env);

    /* string concat */
    if (strcmp(op, "+") == 0 && (L.type == VAL_STRING || R.type == VAL_STRING)) {
        char lb[64], rb[64];
        const char *ls = L.type==VAL_STRING ? L.sv : (snprintf(lb,sizeof(lb),L.type==VAL_INT?"%lld":"%g",L.type==VAL_INT?(double)L.iv:L.fv),lb);
        const char *rs = R.type==VAL_STRING ? R.sv : (snprintf(rb,sizeof(rb),R.type==VAL_INT?"%lld":"%g",R.type==VAL_INT?(double)R.iv:R.fv),rb);
        size_t n = strlen(ls)+strlen(rs)+1;
        char *buf = malloc(n); snprintf(buf, n, "%s%s", ls, rs);
        SubVal res = {VAL_STRING}; res.sv = buf; return res;
    }

    double a = L.type==VAL_FLOAT ? L.fv : (double)L.iv;
    double b = R.type==VAL_FLOAT ? R.fv : (double)R.iv;
    int use_float = (L.type==VAL_FLOAT || R.type==VAL_FLOAT);

    if (strcmp(op,"+")==0) return use_float ? make_float(a+b) : make_int((long long)(a+b));
    if (strcmp(op,"-")==0) return use_float ? make_float(a-b) : make_int((long long)(a-b));
    if (strcmp(op,"*")==0) return use_float ? make_float(a*b) : make_int((long long)(a*b));
    if (strcmp(op,"/")==0) return b==0 ? (fprintf(stderr,"Division by zero\n"),NULL_VAL) : use_float ? make_float(a/b) : make_int((long long)(a/b));
    if (strcmp(op,"%")==0) return make_int((long long)a % (long long)b);
    if (strcmp(op,"==")==0) return make_bool(a==b);
    if (strcmp(op,"!=")==0) return make_bool(a!=b);
    if (strcmp(op,"<")==0)  return make_bool(a<b);
    if (strcmp(op,"<=")==0) return make_bool(a<=b);
    if (strcmp(op,">")==0)  return make_bool(a>b);
    if (strcmp(op,">=")==0) return make_bool(a>=b);
    if (strcmp(op,"&&")==0) return make_bool(is_truthy(L)&&is_truthy(R));
    if (strcmp(op,"||")==0) return make_bool(is_truthy(L)||is_truthy(R));
    return NULL_VAL;
}

SubVal eval(ASTNode *node, Env *env) {
    if (!node || env->returning) return NULL_VAL;

    switch (node->type) {

    case AST_PROGRAM:
    case AST_BLOCK:
        return eval_block(node, env);

    case AST_LITERAL: {
        if (node->data_type == TYPE_STRING) return make_str(node->value);
        if (node->data_type == TYPE_BOOL)   return make_bool(node->value && strcmp(node->value,"true")==0);
        if (node->data_type == TYPE_FLOAT)  return make_float(atof(node->value ? node->value : "0"));
        return make_int(atoll(node->value ? node->value : "0"));
    }

    case AST_IDENTIFIER:
        return env_get(env, node->value);

    case AST_VAR_DECL:
    case AST_CONST_DECL: {
        SubVal val = node->right ? eval(node->right, env) : NULL_VAL;
        env_define(env, node->value, val);
        return val;
    }

    case AST_ASSIGN_STMT: {
        SubVal val = eval(node->right, env);
        if (node->left && node->left->type == AST_IDENTIFIER)
            env_set(env, node->left->value, val);
        return val;
    }

    case AST_BINARY_EXPR:
        return eval_binary(node, env);

    case AST_UNARY_EXPR: {
        SubVal v = eval(node->right, env);
        if (node->value && strcmp(node->value, "-")==0)
            return v.type==VAL_FLOAT ? make_float(-v.fv) : make_int(-v.iv);
        if (node->value && strcmp(node->value, "!")==0)
            return make_bool(!is_truthy(v));
        return v;
    }

    case AST_IF_STMT: {
        SubVal cond = eval(node->condition, env);
        if (is_truthy(cond)) return eval(node->body, env);
        if (node->right)      return eval(node->right, env);
        return NULL_VAL;
    }

    case AST_WHILE_STMT: {
        SubVal r = NULL_VAL;
        while (!env->returning) {
            SubVal c = eval(node->condition, env);
            if (!is_truthy(c)) break;
            Env *loop = env_new(env);
            r = eval(node->body, loop);
            int ret = loop->returning; SubVal rv = loop->ret_val;
            env_free(loop);
            if (ret) { env->returning = 1; env->ret_val = rv; break; }
        }
        return r;
    }

    case AST_FOR_STMT: {
        if (!node->value) return NULL_VAL;
        ASTNode *range = (node->children && node->child_count > 0) ? node->children[0] : NULL;
        long long start = 0, end_v = 10;
        if (range && range->type == AST_RANGE_EXPR) {
            if (range->left && range->right) {
                start = eval(range->left, env).iv;
                end_v = eval(range->right, env).iv;
            } else if (range->left) {
                end_v = eval(range->left, env).iv;
            }
        }
        for (long long i = start; i < end_v && !env->returning; i++) {
            Env *loop = env_new(env);
            env_define(loop, node->value, make_int(i));
            eval(node->body, loop);
            int ret = loop->returning; SubVal rv = loop->ret_val;
            env_free(loop);
            if (ret) { env->returning = 1; env->ret_val = rv; break; }
        }
        return NULL_VAL;
    }

    case AST_FUNCTION_DECL: {
        SubVal fv = {VAL_FUNC}; fv.fn = node;
        env_define(env, node->value, fv);
        return NULL_VAL;
    }

    case AST_RETURN_STMT: {
        SubVal rv = node->right ? eval(node->right, env) : NULL_VAL;
        env->returning = 1;
        env->ret_val   = rv;
        return rv;
    }

    case AST_CALL_EXPR: {
        const char *fn = node->value;
        /* Built-ins: print() and show() are identical */
        if (fn && (strcmp(fn, "print") == 0 || strcmp(fn, "show") == 0)) {
            for (int i = 0; i < node->child_count; i++)
                print_val(eval(node->children[i], env));
            return NULL_VAL;
        }
        if (fn && strcmp(fn, "str") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            char buf[64];
            if (v.type==VAL_INT)         snprintf(buf, sizeof(buf), "%lld", v.iv);
            else if (v.type==VAL_FLOAT)  snprintf(buf, sizeof(buf), "%g", v.fv);
            else if (v.type==VAL_STRING) return v;
            else snprintf(buf, sizeof(buf), "null");
            return make_str(buf);
        }
        if (fn && strcmp(fn, "int") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            if (v.type==VAL_STRING) return make_int(atoll(v.sv ? v.sv : "0"));
            if (v.type==VAL_FLOAT)  return make_int((long long)v.fv);
            return v;
        }
        if (fn && strcmp(fn, "float") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            if (v.type==VAL_STRING) return make_float(atof(v.sv ? v.sv : "0"));
            if (v.type==VAL_INT)    return make_float((double)v.iv);
            return v;
        }
        if (fn && strcmp(fn, "len") == 0 && node->child_count > 0) {
            SubVal v = eval(node->children[0], env);
            return make_int(v.type==VAL_STRING ? (long long)strlen(v.sv ? v.sv : "") : 0);
        }
        if (fn && strcmp(fn, "input") == 0) {
            if (node->child_count > 0) print_val(eval(node->children[0], env));
            char buf[1024];
            if (!fgets(buf, sizeof(buf), stdin)) return make_str("");
            size_t l = strlen(buf);
            if (l > 0 && buf[l-1] == '\n') buf[l-1] = '\0';
            return make_str(buf);
        }
        /* User-defined function call */
        SubVal fv = env_get(env, fn ? fn : "");
        if (fv.type != VAL_FUNC || !fv.fn) {
            fprintf(stderr, "Not a function: %s\n", fn ? fn : "(null)");
            return NULL_VAL;
        }
        ASTNode *fn_decl = fv.fn;
        Env *fn_env = env_new(env);
        /* Bind parameters */
        if (fn_decl->params) {
            for (int i = 0; i < fn_decl->param_count && i < node->child_count; i++) {
                SubVal arg = eval(node->children[i], env);
                env_define(fn_env, fn_decl->params[i], arg);
            }
        }
        eval(fn_decl->body, fn_env);
        SubVal ret = fn_env->returning ? fn_env->ret_val : NULL_VAL;
        env_free(fn_env);
        return ret;
    }

    default:
        return NULL_VAL;
    }
}

static SubVal eval_block(ASTNode *node, Env *env) {
    if (!node) return NULL_VAL;
    SubVal last = NULL_VAL;
    for (int i = 0; i < node->child_count && !env->returning; i++)
        last = eval(node->children[i], env);
    return last;
}

int interpret_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *src = malloc(sz + 1);
    if (!src) { fclose(f); return 1; }
    fread(src, 1, sz, f); src[sz] = '\0'; fclose(f);
    int ntok;
    Token *toks = lexer_tokenize(src, &ntok);
    ASTNode *ast = parser_parse(toks, ntok);
    if (!ast) { free(src); lexer_free_tokens(toks, ntok); return 1; }
    if (!semantic_analyze(ast)) {
        fprintf(stderr, "Semantic error\n");
        parser_free_ast(ast); lexer_free_tokens(toks, ntok); free(src);
        return 1;
    }
    Env *global = env_new(NULL);
    eval(ast, global);
    env_free(global);
    parser_free_ast(ast);
    lexer_free_tokens(toks, ntok);
    free(src);
    return 0;
}
