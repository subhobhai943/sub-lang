/* ========================================
   SUB Language - Rust Code Generator
   Implementation
   File: codegen_rust.c
   ======================================== */

#define _GNU_SOURCE
#include "codegen_rust.h"
#include "codegen_infer.h"
#include "windows_compat.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} StringBuilder;

/* Map SUB's inferred types onto Rust types. Parameters take &str for strings
   so callers can pass string literals without an allocation; returns own their
   data. TYPE_GENERIC means inference saw conflicting types for the slot, which
   Rust cannot express without generics, so fall back to f64 - the widest
   numeric type - rather than emitting something that will not compile. */
/* SUB builtin conversions in Rust spelling. Rust has no free `str(x)`
   function - `str` is a primitive type - so emitting the SUB name verbatim
   was a hard compile error. */
typedef struct { const char *sub_name, *prefix, *suffix; } RustBuiltin;

static const RustBuiltin* rust_builtin(const char *name) {
    static const RustBuiltin table[] = {
        /* str() of a float has to use the same %g shape as printing it. */
        {"str","_sub_str(",")"},         {"to_string","_sub_str(",")"},
        {"int","(",") as i64"},          {"float","(",") as f64"},
        {"len","(",").len() as i64"},    {"length","(",").len() as i64"},
        {"abs","((",") as f64).abs()"},  {"sqrt","((",") as f64).sqrt()"},
        {"floor","_sub_floor(",")"},     {"ceil","_sub_ceil(",")"},
        {"round","_sub_round(",")"},
        {"upper","(",").to_uppercase()"},{"lower","(",").to_lowercase()"},
        {"trim","(",").trim().to_string()"},
        {NULL,NULL,NULL}
    };
    if (!name) return NULL;
    for (int i = 0; table[i].sub_name; i++)
        if (strcmp(table[i].sub_name, name) == 0) return &table[i];
    return NULL;
}

/* Builtins taking more than one argument. Rust spells the numeric minimum
   as a method on floats and a free function on integers, so both go through
   one generic helper rather than being spelled per type at every call. */
static const RustBuiltin* rust_builtin_multi(const char *name) {
    static const RustBuiltin table[] = {
        {"min","_sub_min(",")"}, {"max","_sub_max(",")"}, {NULL,NULL,NULL}
    };
    if (!name) return NULL;
    for (int i = 0; table[i].sub_name; i++)
        if (strcmp(table[i].sub_name, name) == 0) return &table[i];
    return NULL;
}

static const char* rust_type(DataType t, int is_param) {
    (void)is_param;
    switch (t) {
        case TYPE_INT:    return "i64";
        case TYPE_FLOAT:  return "f64";
        case TYPE_BOOL:   return "bool";
        /* Owned on both sides: the expression generator emits string
           literals as String::from(...), so a &str parameter would reject
           every call site. */
        case TYPE_STRING: return "String";
        case TYPE_ARRAY:  return "Vec<i64>";
        case TYPE_VOID:   return "()";
        default:          return "f64";
    }
}

static StringBuilder* sb_create(void) {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    sb->capacity = 8192;
    sb->buffer = malloc(sb->capacity);
    if (!sb->buffer) {
        free(sb);
        return NULL;
    }
    sb->buffer[0] = '\0';
    sb->size = 0;
    return sb;
}

static void sb_free(StringBuilder *sb) {
    if (sb) {
        free(sb->buffer);
        free(sb);
    }
}

static void sb_append(StringBuilder *sb, const char *fmt, ...) {
    if (!sb || !fmt) return;
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0) {
        va_end(args);
        return;
    }
    
    while (sb->size + needed + 1 > sb->capacity) {
        sb->capacity *= 2;
        char *next = realloc(sb->buffer, sb->capacity);
        if (!next) {
            va_end(args);
            return;
        }
        sb->buffer = next;
    }
    
    vsnprintf(sb->buffer + sb->size, needed + 1, fmt, args);
    sb->size += needed;
    va_end(args);
}

static char* sb_to_string(StringBuilder *sb) {
    if (!sb) return NULL;
    char *result = strdup(sb->buffer);
    sb_free(sb);
    return result;
}

static void indent_code(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "    ");
    }
}

static char* escape_string_for_rust(const char *raw) {
    if (!raw) return strdup("");
    size_t len = strlen(raw);
    char *escaped = malloc((len * 2) + 1);
    if (!escaped) return NULL;
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        switch ((unsigned char)raw[i]) {
            case '\\': escaped[out++] = '\\'; escaped[out++] = '\\'; break;
            case '"':  escaped[out++] = '\\'; escaped[out++] = '"';  break;
            case '\n': escaped[out++] = '\\'; escaped[out++] = 'n'; break;
            case '\t': escaped[out++] = '\\'; escaped[out++] = 't'; break;
            case '\r': escaped[out++] = '\\'; escaped[out++] = 'r'; break;
            default:   escaped[out++] = raw[i]; break;
        }
    }
    escaped[out] = '\0';
    return escaped;
}

static void generate_expr_rust(StringBuilder *sb, ASTNode *node);

static ASTNode* block_first(ASTNode *node) {
    if (!node) return NULL;
    if (node->body) return node->body;
    if (node->children && node->child_count > 0) return node->children[0];
    if (node->left) return node->left;
    return NULL;
}

static bool ast_contains_object(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_OBJECT_LITERAL) return true;
    if (node->left && ast_contains_object(node->left)) return true;
    if (node->right && ast_contains_object(node->right)) return true;
    if (node->condition && ast_contains_object(node->condition)) return true;
    if (node->body && ast_contains_object(node->body)) return true;
    if (node->next && ast_contains_object(node->next)) return true;
    if (node->children) {
        for (int i = 0; i < node->child_count; i++) {
            if (ast_contains_object(node->children[i])) return true;
        }
    }
    return false;
}

static void generate_node_rust(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        /*  A dropped `break` turns a loop that terminates into one
           that does not, so this must never fall through to the default. */
        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break;\n");
            break;

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue;\n");
            break;

        case AST_PROGRAM:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_rust(sb, s, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let mut %s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_rust(sb, node->right);
            } else {
                sb_append(sb, "0"); // Default value
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfn %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "%s: %s", node->children[i]->value ? node->children[i]->value : "arg",
                          rust_type(node->children[i]->data_type, 1));
            }
            sb_append(sb, ")");
            /* Rust requires the return type in the signature; omitting it
               declares `-> ()` and every `return <value>` fails to compile. */
            if (node->data_type != TYPE_VOID && node->data_type != TYPE_UNKNOWN)
                sb_append(sb, " -> %s", rust_type(node->data_type, 0));
            sb_append(sb, " {\n");
            if (node->body) {
                generate_node_rust(sb, node->body, indent + 1);
            }
            sb_append(sb, "}\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if ");
            generate_expr_rust(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                sb_append(sb, " else {\n");
                generate_node_rust(sb, node->right, indent + 1);
                indent_code(sb, indent);
                sb_append(sb, "}");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while ");
            generate_expr_rust(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_FOR_STMT:
            indent_code(sb, indent);
            if (node->children && node->child_count > 0 && node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for %s in ", node->value ? node->value : "i");
                if (range->right) {
                    generate_expr_rust(sb, range->left);
                    sb_append(sb, "..");
                    generate_expr_rust(sb, range->right);
                } else {
                    sb_append(sb, "0..");
                    generate_expr_rust(sb, range->left);
                }
                sb_append(sb, " {\n");
            } else if (node->condition) {
                sb_append(sb, "for %s in ", node->value ? node->value : "item");
                generate_expr_rust(sb, node->condition);
                sb_append(sb, " {\n");
            } else {
                sb_append(sb, "for %s in 0..10 {\n", node->value ? node->value : "i");
            }
            generate_node_rust(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_rust(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_rust(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_rust(sb, s, indent);
            }
            break;

        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            generate_expr_rust(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_rust(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        default:
            break;
    }
}

static void generate_expr_rust(StringBuilder *sb, ASTNode *node);

/* Rust does not coerce an integer literal to f64, so `b == 0` against an f64
   and `safe_div(1.0, 0)` both fail to compile. Emit such a literal as a float
   when the surrounding context wants one. */
static int is_int_literal(ASTNode *n) {
    return n && n->type == AST_LITERAL && n->data_type == TYPE_INT && n->value;
}

static void generate_expr_rust_as(StringBuilder *sb, ASTNode *node, DataType want) {
    if (want == TYPE_FLOAT && is_int_literal(node)) {
        sb_append(sb, "%s.0", node->value);
        return;
    }
    generate_expr_rust(sb, node);
}

static void generate_expr_rust(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_rust(node->value ? node->value : "");
                sb_append(sb, "String::from(\"%s\")", escaped ? escaped : "");
                free(escaped);
            } else if (expr_is_null_literal(node)) {
                /* Rust has no `null`; SUB's null in a numeric slot is NaN. */
                sb_append(sb, "f64::NAN");
            } else {
                sb_append(sb, "%s", node->value ? node->value : "0");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value);
            break;
        case AST_BINARY_EXPR:
            /* `x == null` / `x != null` become NaN tests, matching how the
               null sentinel is represented above. */
            if (node->value && (strcmp(node->value, "==") == 0 ||
                                strcmp(node->value, "!=") == 0) &&
                (expr_is_null_literal(node->left) || expr_is_null_literal(node->right))) {
                ASTNode *val = expr_is_null_literal(node->left) ? node->right : node->left;
                sb_append(sb, "%s(", strcmp(node->value, "!=") == 0 ? "!" : "");
                generate_expr_rust(sb, val);
                sb_append(sb, ").is_nan()");
                break;
            }
            /* Integer / and % go through helpers so that dividing by zero
               reports the interpreter's runtime error and exit status
               instead of panicking with Rust's own. */
            if (node->value &&
                (strcmp(node->value, "/") == 0 || strcmp(node->value, "%") == 0)) {
                int is_div = (strcmp(node->value, "/") == 0);
                int is_flt = (infer_expr_type(node->left)  == TYPE_FLOAT ||
                              infer_expr_type(node->right) == TYPE_FLOAT);
                const char *ty = is_flt ? "f64" : "i64";
                sb_append(sb, "%s((", is_flt ? (is_div ? "_sub_fdiv" : "_sub_fmod")
                                             : (is_div ? "_sub_idiv" : "_sub_mod"));
                generate_expr_rust(sb, node->left);
                sb_append(sb, ") as %s, (", ty);
                generate_expr_rust(sb, node->right);
                sb_append(sb, ") as %s)", ty);
                break;
            }
            /* Rust has no ** operator, and integer literals need an explicit
               type before a method like .pow()/.abs() can be resolved. */
            if (node->value && strcmp(node->value, "**") == 0) {
                int as_int = (infer_expr_type(node->left)  == TYPE_INT &&
                              infer_expr_type(node->right) == TYPE_INT);
                /* i64::pow takes a u32, so a negative exponent cannot use it
                   at all - and the result is a fraction anyway. */
                if (as_int && exponent_is_negative(node->right)) as_int = 0;
                if (as_int) {
                    sb_append(sb, "((");
                    generate_expr_rust(sb, node->left);
                    sb_append(sb, ") as i64).pow((");
                    generate_expr_rust(sb, node->right);
                    sb_append(sb, ") as u32)");
                } else {
                    sb_append(sb, "((");
                    generate_expr_rust(sb, node->left);
                    sb_append(sb, ") as f64).powf((");
                    generate_expr_rust(sb, node->right);
                    sb_append(sb, ") as f64)");
                }
                break;
            }
            if (node->value && strcmp(node->value, "+") == 0 &&
                (node->data_type == TYPE_STRING ||
                 (node->left && node->left->data_type == TYPE_STRING) ||
                 (node->right && node->right->data_type == TYPE_STRING))) {
                /* String concatenation: use format! because Rust cannot
                   use + between &str and String without explicit .to_string() */
                sb_append(sb, "format!(\"{}{}\", ");
                generate_expr_rust(sb, node->left);
                sb_append(sb, ", ");
                generate_expr_rust(sb, node->right);
                sb_append(sb, ")");
            } else {
                /* Rust will not coerce an integer literal to f64, so widen it
                   when the other operand is a float. */
                DataType lt = infer_expr_type(node->left);
                DataType rt = infer_expr_type(node->right);
                DataType want = (lt == TYPE_FLOAT || rt == TYPE_FLOAT)
                                    ? TYPE_FLOAT : TYPE_UNKNOWN;
                sb_append(sb, "(");
                generate_expr_rust_as(sb, node->left, want);
                sb_append(sb, " %s ", node->value ? node->value : "+");
                generate_expr_rust_as(sb, node->right, want);
                sb_append(sb, ")");
            }
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_rust(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "if ");
            generate_expr_rust(sb, node->condition);
            sb_append(sb, " { ");
            generate_expr_rust(sb, node->left);
            sb_append(sb, " } else { ");
            generate_expr_rust(sb, node->right);
            sb_append(sb, " }");
            break;
        case AST_CALL_EXPR:
            {
                if (node->value &&
                    (strcmp(node->value, "str") == 0 ||
                     strcmp(node->value, "to_string") == 0) &&
                    node->child_count == 1 &&
                    infer_expr_type(node->children[0]) == TYPE_FLOAT) {
                    sb_append(sb, "_sub_fmt(");
                    generate_expr_rust(sb, node->children[0]);
                    sb_append(sb, ")");
                    break;
                }
                const RustBuiltin *rb = rust_builtin(node->value);
                if (rb && node->child_count == 1) {
                    sb_append(sb, "%s", rb->prefix);
                    generate_expr_rust(sb, node->children[0]);
                    sb_append(sb, "%s", rb->suffix);
                    break;
                }
                rb = rust_builtin_multi(node->value);
                if (rb && node->child_count >= 2) {
                    sb_append(sb, "%s", rb->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_rust(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", rb->suffix);
                    break;
                }
            }
            if (is_print_builtin(node->value)) {
                sb_append(sb, "println!(\"{}\", ");
                if (node->child_count > 0) {
                    int flt = (infer_expr_type(node->children[0]) == TYPE_FLOAT);
                    if (flt) sb_append(sb, "_sub_fmt(");
                    generate_expr_rust(sb, node->children[0]);
                    if (flt) sb_append(sb, ")");
                }
                sb_append(sb, ")");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", node->value);
                } else {
                    generate_expr_rust(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_rust_as(sb, node->children[i],
                                          param_type_of(node->value, i));
                }
                sb_append(sb, ")");
            }
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "vec![");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_rust(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "HashMap::from([");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                char *key_escaped = escape_string_for_rust(pair->value ? pair->value : "");
                sb_append(sb, "(String::from(\"%s\"), ", key_escaped ? key_escaped : "");
                free(key_escaped);
                generate_expr_rust(sb, pair->right);
                sb_append(sb, ")");
            }
            sb_append(sb, "])");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_rust(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            generate_expr_rust(sb, node->left);
            sb_append(sb, "[");
            generate_expr_rust(sb, node->right);
            sb_append(sb, "]");
            break;
        default:
            break;
    }
}

char* codegen_rust(ASTNode *ast, const char *source) {
    (void)source; // Source is not used yet in Rust codegen
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    sb_append(sb, "// Generated by SUB Language Compiler (Rust Target)\n\n");

    /* SUB's floor/ceil/round yield an integer, and round() breaks ties away
       from zero - which is what f64::round already does. min/max are generic
       so one helper serves both the integer and the floating-point call. */
    sb_append(sb, "#[allow(dead_code)] fn _sub_str<T: std::fmt::Display>(v: T) -> String "
                  "{ v.to_string() }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_floor(x: f64) -> i64 "
                  "{ x.floor() as i64 }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_ceil(x: f64) -> i64 "
                  "{ x.ceil() as i64 }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_round(x: f64) -> i64 "
                  "{ x.round() as i64 }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_min<T: PartialOrd>(a: T, b: T) -> T "
                  "{ if a < b { a } else { b } }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_max<T: PartialOrd>(a: T, b: T) -> T "
                  "{ if a > b { a } else { b } }\n");

    /* Rust panics on integer division by zero, with a message and exit
       status of its own. SUB reports the interpreter's error and exits 70,
       so every backend ends the same way. */
    sb_append(sb, "#[allow(dead_code)] fn _sub_die(msg: &str) -> ! "
                  "{ eprintln!(\"RuntimeError: {}\", msg); std::process::exit(70) }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_idiv(a: i64, b: i64) -> i64 "
                  "{ if b == 0 { _sub_die(\"division by zero\") } a / b }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_mod(a: i64, b: i64) -> i64 "
                  "{ if b == 0 { _sub_die(\"modulo by zero\") } a %% b }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_fdiv(a: f64, b: f64) -> f64 "
                  "{ if b == 0.0 { _sub_die(\"division by zero\") } a / b }\n");
    sb_append(sb, "#[allow(dead_code)] fn _sub_fmod(a: f64, b: f64) -> f64 "
                  "{ if b == 0.0 { _sub_die(\"modulo by zero\") } a %% b }\n");

    /* printf's %g, as the interpreter prints floats: six significant digits,
       trailing zeros dropped, exponent form outside 1e-4 .. 1e+6. Rust's
       Display for f64 prints every digit needed to round-trip, so 1.0 / 3.0
       came out 0.3333333333333333. */
    sb_append(sb, "#[allow(dead_code)] fn _sub_fmt(x: f64) -> String {\n");
    sb_append(sb, "    if x.is_nan() { return \"nan\".to_string() }\n");
    sb_append(sb, "    if x.is_infinite() { return (if x < 0.0 { \"-inf\" } "
                  "else { \"inf\" }).to_string() }\n");
    sb_append(sb, "    if x == 0.0 { return (if x.is_sign_negative() { \"-0\" } "
                  "else { \"0\" }).to_string() }\n");
    sb_append(sb, "    let mut e = x.abs().log10().floor() as i32;\n");
    sb_append(sb, "    if x.abs() / 10f64.powi(e) >= 10.0 { e += 1 }\n");
    sb_append(sb, "    if x.abs() / 10f64.powi(e) < 1.0 { e -= 1 }\n");
    sb_append(sb, "    let strip = |s: String| -> String {\n");
    sb_append(sb, "        if s.contains('.') {\n");
    sb_append(sb, "            let t = s.trim_end_matches('0').to_string();\n");
    sb_append(sb, "            return t.trim_end_matches('.').to_string();\n");
    sb_append(sb, "        }\n        s\n    };\n");
    sb_append(sb, "    if e < -4 || e >= 6 {\n");
    sb_append(sb, "        let m = strip(format!(\"{:.5}\", x / 10f64.powi(e)));\n");
    sb_append(sb, "        return format!(\"{}e{}{:02}\", m, "
                  "if e < 0 { \"-\" } else { \"+\" }, e.abs());\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    let d = if 5 - e > 0 { (5 - e) as usize } else { 0 };\n");
    sb_append(sb, "    strip(format!(\"{:.*}\", d, x))\n}\n\n");
    if (ast_contains_object(ast)) {
        sb_append(sb, "use std::collections::HashMap;\n\n");
    }

    StringBuilder *main_sb = sb_create();
    if (!main_sb) {
        sb_free(sb);
        return NULL;
    }

    if (ast->type == AST_PROGRAM) {
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                generate_node_rust(sb, stmt, 0);
            } else {
                generate_node_rust(main_sb, stmt, 1);
            }
        }
    }

    sb_append(sb, "fn main() {\n");
    sb_append(sb, "%s", main_sb->buffer);
    sb_append(sb, "}\n");

    sb_free(main_sb);
    return sb_to_string(sb);
}
