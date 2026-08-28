/* ========================================
   SUB Language Multi-Language Code Generator - REAL IMPLEMENTATION
   Actually processes your SUB code, not dummy templates!
   File: codegen_multilang.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "codegen_infer.h"
#include "codegen_switch.h"
#include "codegen_globals.h"
#include "type_system.h"
#include "windows_compat.h"
#include <stdarg.h>
#include <ctype.h>


/* String Builder */
typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} StringBuilder;

/* ----------------------------------------------------------------
   Inferred SUB types -> target language types.

   These backends previously hardcoded `void` returns and Object/Any/
   interface{} parameters, which produced code that did not compile the
   moment a function returned a value or did arithmetic on an argument.
   They now read the types that infer_function_signatures() wrote onto the
   AST. TYPE_GENERIC means inference saw conflicting types, so it maps to
   each language's real top type.
   ---------------------------------------------------------------- */

/* ----------------------------------------------------------------
   SUB builtin conversions -> target language spellings.

   SUB programs call str(), int(), len() and friends. Every backend used to
   emit those names verbatim, so the generated code referred to functions
   that do not exist in the target language ("str is not defined" in JS,
   "cannot find symbol: str" in Java, and so on).

   A builtin is emitted as prefix + argument + suffix, which covers both the
   call-style spellings (String.valueOf(x)) and the method-style ones
   ((x).length()). Anything not in the table is left alone and emitted as an
   ordinary call.
   ---------------------------------------------------------------- */

typedef enum {
    LANG_JS, LANG_JAVA, LANG_SWIFT, LANG_KOTLIN, LANG_GO, LANG_RUBY, LANG_PY
} TargetLang;

typedef struct {
    const char *sub_name;
    const char *prefix;
    const char *suffix;
} BuiltinSpelling;

static const BuiltinSpelling* builtin_spelling(TargetLang lang, const char *name) {
    static const BuiltinSpelling js[] = {
        {"str","String(",")"},        {"to_string","String(",")"},
        {"int","Math.trunc(Number(","))"}, {"float","Number(",")"},
        {"bool","Boolean(",")"},      {"len","(",").length"},
        {"length","(",").length"},    {"abs","Math.abs(",")"},
        {"sqrt","Math.sqrt(",")"},    {"floor","Math.floor(",")"},
        {"ceil","Math.ceil(",")"},    {"round","_subRound(",")"},
        {"upper","(",").toUpperCase()"}, {"lower","(",").toLowerCase()"},
        {"trim","(",").trim()"},      {"pop","_subPop(",")"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling java[] = {
        {"str","_subStr(",")"},        {"to_string","_subStr(",")"},
        {"int","(long)(",")"},         {"float","(double)(",")"},
        {"bool","(boolean)(",")"},     {"len","(",").length()"},
        {"length","(",").length()"},   {"abs","Math.abs(",")"},
        {"sqrt","Math.sqrt(",")"},     {"floor","(long)Math.floor(",")"},
        {"ceil","(long)Math.ceil(",")"}, {"round","_subRound(",")"},
        {"upper","(",").toUpperCase()"},{"lower","(",").toLowerCase()"},
        {"trim","(",").trim()"},       {NULL,NULL,NULL}
    };
    static const BuiltinSpelling swift[] = {
        {"str","_subStr(",")"},        {"to_string","_subStr(",")"},
        {"int","Int(",")"},            {"float","Double(",")"},
        {"bool","Bool(",")"},          {"len","(",").count"},
        {"length","(",").count"},      {"abs","abs(",")"},
        {"sqrt","(Double(",")).squareRoot()"},
        {"upper","(",").uppercased()"},{"lower","(",").lowercased()"},
        {"trim","(",").trimmingCharacters(in: .whitespacesAndNewlines)"},
        {"floor","Int(Double(",").rounded(.down))"},
        {"ceil","Int(Double(",").rounded(.up))"},
        {"round","Int(Double(",").rounded())"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling kotlin[] = {
        {"str","_subStr(",")"},        {"to_string","_subStr(",")"},
        {"int","(",").toLong()"},      {"float","(",").toDouble()"},
        {"len","(",").length.toLong()"},
        {"length","(",").length.toLong()"},
        {"abs","kotlin.math.abs(",")"},{"sqrt","kotlin.math.sqrt((",").toDouble())"},
        {"upper","(",").uppercase()"}, {"lower","(",").lowercase()"},
        {"trim","(",").trim()"},
        {"floor","kotlin.math.floor((",").toDouble()).toLong()"},
        {"ceil","kotlin.math.ceil((",").toDouble()).toLong()"},
        {"round","_subRound((",").toDouble())"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling go[] = {
        {"str","_sub_str(",")"},       {"to_string","_sub_str(",")"},
        {"int","int64(math.Trunc(float64(",")))"},
        {"float","float64(",")"},
        {"len","int64(len(","))"},     {"length","int64(len(","))"},
        {"abs","math.Abs(",")"},       {"sqrt","math.Sqrt(",")"},
        {"floor","int64(math.Floor(","))"}, {"ceil","int64(math.Ceil(","))"},
        {"round","int64(math.Round(","))"},
        {"upper","strings.ToUpper(",")"}, {"lower","strings.ToLower(",")"},
        {"pop","_sub_pop(",")"},
        {"trim","strings.TrimSpace(",")"}, {NULL,NULL,NULL}
    };
    static const BuiltinSpelling ruby[] = {
        {"str","_sub_str(",")"},       {"to_string","_sub_str(",")"},
        {"int","(",").to_i"},          {"float","(",").to_f"},
        {"len","(",").length"},        {"length","(",").length"},
        {"abs","(",").abs"},           {"sqrt","Math.sqrt(",")"},
        {"upper","(",").upcase"},      {"lower","(",").downcase"},
        {"pop","_sub_pop(",")"},
        {"trim","(",").strip"},
        {"floor","(",").floor"},       {"ceil","(",").ceil"},
        {"round","(",").round"},       {NULL,NULL,NULL}
    };
    static const BuiltinSpelling py[] = {
        {"println","print(",")"},      {"show","print(",")"},
        {NULL,NULL,NULL}
    };

    const BuiltinSpelling *table;
    switch (lang) {
        case LANG_JS:     table = js;     break;
        case LANG_JAVA:   table = java;   break;
        case LANG_SWIFT:  table = swift;  break;
        case LANG_KOTLIN: table = kotlin; break;
        case LANG_GO:     table = go;     break;
        case LANG_RUBY:   table = ruby;   break;
        default:          table = py;     break;
    }
    if (!name) return NULL;
    for (int i = 0; table[i].sub_name; i++)
        if (strcmp(table[i].sub_name, name) == 0) return &table[i];
    return NULL;
}

/* Builtins that take more than one argument.
   The table above wraps a single argument in a prefix and a suffix, which
   cannot express min(a, b); these entries wrap the whole comma-separated
   argument list instead. Without them min() and max() were emitted verbatim
   and every target that has no function by that name failed to build. */
static const BuiltinSpelling* builtin_spelling_multi(TargetLang lang, const char *name) {
    static const BuiltinSpelling js[] = {
        {"min","Math.min(",")"},   {"max","Math.max(",")"},
        {"push","_subPush(",")"},  {"append","_subPush(",")"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling java[] = {
        {"min","Math.min(",")"},   {"max","Math.max(",")"},   {NULL,NULL,NULL}
    };
    static const BuiltinSpelling swift[] = {
        {"min","min(",")"},        {"max","max(",")"},
        {"push","_sub_push(",")"},  {"append","_sub_push(",")"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling kotlin[] = {
        {"min","kotlin.math.min(",")"}, {"max","kotlin.math.max(",")"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling go[] = {
        /* min and max are predeclared functions in Go 1.21 and later. */
        {"min","min(",")"},        {"max","max(",")"},
        {"push","_sub_push(",")"},  {"append","_sub_push(",")"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling ruby[] = {
        {"min","[","].min"},       {"max","[","].max"},
        {"push","_sub_push(",")"}, {"append","_sub_push(",")"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling py[] = {
        {"push","_sub_push(",")"}, {"append","_sub_push(",")"},
        {NULL,NULL,NULL}
    };

    const BuiltinSpelling *table;
    switch (lang) {
        case LANG_JS:     table = js;     break;
        case LANG_JAVA:   table = java;   break;
        case LANG_SWIFT:  table = swift;  break;
        case LANG_KOTLIN: table = kotlin; break;
        case LANG_GO:     table = go;     break;
        case LANG_RUBY:   table = ruby;   break;
        default:          table = py;     break;
    }
    if (!name) return NULL;
    for (int i = 0; table[i].sub_name; i++)
        if (strcmp(table[i].sub_name, name) == 0) return &table[i];
    return NULL;
}


/* Return type of the Java function currently being emitted, so a bare
   `return null` can be rendered as the numeric NaN sentinel. */
static DataType g_java_fn_type = TYPE_UNKNOWN;

/* The element's boxed type, for the empty-list case where Java cannot infer
   it from anything. */
static const char* java_elem_box(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return "Double";
    case TYPE_STRING: return "String";
    case TYPE_BOOL:   return "Boolean";
    default:          return "Long";
    }
}

static const char* java_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "long";
        case TYPE_FLOAT:  return "double";
        case TYPE_BOOL:   return "boolean";
        case TYPE_STRING: return "String";
        /* Locals are declared with `var`, so this is only reached for a
           function that returns an array - where the element type is not
           tracked yet, and Long is the common case. */
        case TYPE_ARRAY:  return "java.util.List<Long>";
        case TYPE_VOID:   return "void";
        default:          return "Object";
    }
}

/* The type a top-level variable gets as a static field. A local is declared
   with `var`, which deduces from its initializer, but a field is declared in
   one place and assigned in another and so has to name its type. */
static const char* java_elem_box(DataType elem);

static const char* java_decl_type(ASTNode *node) {
    static char buf[64];
    DataType t = node->data_type;
    if (t == TYPE_UNKNOWN || t == TYPE_AUTO) t = infer_expr_type(node->right);
    if (t == TYPE_ARRAY) {
        snprintf(buf, sizeof buf, "java.util.List<%s>",
                 java_elem_box(infer_elem_type(node->right)));
        return buf;
    }
    return java_type(t);
}

/* Return type of the Swift/Kotlin function currently being emitted, so
   `return null` can be rendered as the numeric NaN sentinel. */
static DataType g_swift_fn_type  = TYPE_UNKNOWN;
static DataType g_kotlin_fn_type = TYPE_UNKNOWN;

static const char* swift_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "Int";
        case TYPE_FLOAT:  return "Double";
        case TYPE_BOOL:   return "Bool";
        case TYPE_STRING: return "String";
        case TYPE_ARRAY:  return "[Int]";
        case TYPE_VOID:   return "Void";
        default:          return "Any";
    }
}

static const char* kotlin_elem_type(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return "Double";
    case TYPE_STRING: return "String";
    case TYPE_BOOL:   return "Boolean";
    default:          return "Long";
    }
}

static const char* kotlin_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "Long";
        case TYPE_FLOAT:  return "Double";
        case TYPE_BOOL:   return "Boolean";
        case TYPE_STRING: return "String";
        /* kotlin_array_type() names the element where it is known; this is
           the fallback for a function that returns an array. */
        case TYPE_ARRAY:  return "MutableList<Long>";
        case TYPE_VOID:   return "Unit";
        default:          return "Any";
    }
}

/* The declaration a top-level variable gets as a Kotlin top-level property.
   A property has to name its type and carry an initializer -- there is no
   declaring one now and initializing it later -- so it is given the zero of
   its type here and its real initializer in main(), where it was written. */
static const char* kotlin_elem_type(DataType elem);

static void kotlin_decl_of(ASTNode *node, const char **type, const char **zero) {
    DataType t = node->data_type;
    static char tbuf[64], zbuf[64];
    if (t == TYPE_UNKNOWN || t == TYPE_AUTO) t = infer_expr_type(node->right);
    switch (t) {
        case TYPE_FLOAT:  *type = "Double";  *zero = "0.0";   return;
        case TYPE_BOOL:   *type = "Boolean"; *zero = "false"; return;
        case TYPE_STRING: *type = "String";  *zero = "\"\"";  return;
        case TYPE_ARRAY: {
            const char *e = kotlin_elem_type(infer_elem_type(node->right));
            snprintf(tbuf, sizeof tbuf, "MutableList<%s>", e);
            snprintf(zbuf, sizeof zbuf, "mutableListOf<%s>()", e);
            *type = tbuf; *zero = zbuf; return;
        }
        default:          *type = "Long";    *zero = "0L";    return;
    }
}

/* Return type of the Go function currently being emitted, so `return null`
   can be rendered as the numeric NaN sentinel. */
static DataType g_go_fn_type = TYPE_UNKNOWN;

static const char* go_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "int64";
        case TYPE_FLOAT:  return "float64";
        case TYPE_BOOL:   return "bool";
        case TYPE_STRING: return "string";
        /* A SUB array is a *pointer to* a slice. append() returns a new
           slice header, so push could not otherwise change the caller's
           array; going through a pointer keeps `push(a, x); println(a)`
           meaning what it means everywhere else. Only used where the element
           type is unknown - go_array_type() names the real one. */
        case TYPE_ARRAY:  return "*[]int64";
        default:          return "interface{}";
    }

}

/* The type a parameter takes in each typed target. An array parameter
   carries the element type its call sites passed (ASTNode.elem_type); without
   it every array parameter is declared as one of integers, and a function
   over floats reads its elements back as ints or is rejected outright. */
static const char *java_param_type(ASTNode *p) {
    static char buf[64];
    if (p->data_type != TYPE_ARRAY) return java_type(p->data_type);
    snprintf(buf, sizeof buf, "java.util.List<%s>", java_elem_box(p->elem_type));
    return buf;
}

static const char *swift_param_type(ASTNode *p) {
    static char buf[64];
    if (p->data_type != TYPE_ARRAY) return swift_type(p->data_type);
    snprintf(buf, sizeof buf, "[%s]", swift_type(p->elem_type == TYPE_UNKNOWN
                                                 ? TYPE_INT : p->elem_type));
    return buf;
}

static const char *kotlin_param_type(ASTNode *p) {
    static char buf[64];
    if (p->data_type != TYPE_ARRAY) return kotlin_type(p->data_type);
    snprintf(buf, sizeof buf, "MutableList<%s>", kotlin_elem_type(p->elem_type));
    return buf;
}

static const char *go_param_type(ASTNode *p) {
    static char buf[64];
    if (p->data_type != TYPE_ARRAY) return go_type(p->data_type);
    snprintf(buf, sizeof buf, "*[]%s", go_type(p->elem_type == TYPE_UNKNOWN
                                               ? TYPE_INT : p->elem_type));
    return buf;
}


static const char* go_array_type(DataType elem) {
    switch (elem) {
    case TYPE_FLOAT:  return "*[]float64";
    case TYPE_STRING: return "*[]string";
    case TYPE_BOOL:   return "*[]bool";
    default:          return "*[]int64";
    }
}

static StringBuilder* sb_create(void) {
    StringBuilder *sb = malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    sb->capacity = 8192;
    sb->size = 0;
    sb->buffer = malloc(sb->capacity);
    if (!sb->buffer) {
        free(sb);
        return NULL;
    }
    sb->buffer[0] = '\0';
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
    
    while (sb->size + (size_t)needed + 1 > sb->capacity) {
        size_t new_cap = sb->capacity * 2;
        if (new_cap <= sb->capacity) {
            /* Overflow detected */
            va_end(args);
            return;
        }
        sb->capacity = new_cap;
        char *new_buffer = realloc(sb->buffer, sb->capacity);
        if (!new_buffer) {
            va_end(args);
            return;
        }
        sb->buffer = new_buffer;
    }
    
    vsnprintf(sb->buffer + sb->size, needed + 1, fmt, args);
    sb->size += (size_t)needed;
    va_end(args);
}

static char* sb_to_string(StringBuilder *sb) {
    if (!sb) return NULL;
    char *result = strdup(sb->buffer);
    free(sb->buffer);
    free(sb);
    return result;
}

/* Helper to parse embedded code blocks from source */
static char* extract_embedded_code(const char *source, const char *lang) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    
    char pattern_start[64];
    snprintf(pattern_start, sizeof(pattern_start), "#embed %s", lang);
    
    const char *ptr = source;
    while ((ptr = strstr(ptr, pattern_start)) != NULL) {
        ptr = strchr(ptr, '\n');
        if (!ptr) break;
        ptr++;
        
        const char *end = strstr(ptr, "#endembed");
        if (!end) {
            // Check for common typo and warn
            if (strstr(ptr, "#embeded")) {
                fprintf(stderr, "Warning: Found '#embeded' at line - did you mean '#endembed'?\n");
            }
            break;
        }
        
        while (ptr < end) {
            sb_append(sb, "%c", *ptr);
            ptr++;
        }
    }
    
    if (sb->size == 0) {
        sb_free(sb);
        return NULL;
    }
    
    return sb_to_string(sb);
}

/* Forward declarations */
static void generate_node_python(StringBuilder *sb, ASTNode *node, int indent);

static void indent_code(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "    ");
    }
}

static ASTNode* block_first(ASTNode *node) {
    if (!node) return NULL;
    if (node->body) return node->body;
    if (node->children && node->child_count > 0) return node->children[0];
    if (node->left) return node->left;
    return NULL;
}

static char* escape_string_for_codegen(const char *raw) {
    if (!raw) {
        return strdup("");
    }

    size_t len = strlen(raw);
    char *escaped = malloc((len * 2) + 1);
    if (!escaped) return NULL;

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        switch ((unsigned char)raw[i]) {
            case '\n':
                escaped[out++] = '\\';
                escaped[out++] = 'n';
                break;
            case '\t':
                escaped[out++] = '\\';
                escaped[out++] = 't';
                break;
            case '\r':
                escaped[out++] = '\\';
                escaped[out++] = 'r';
                break;
            case '\\':
                escaped[out++] = '\\';
                escaped[out++] = '\\';
                break;
            case '"':
                escaped[out++] = '\\';
                escaped[out++] = '"';
                break;
            default:
                escaped[out++] = raw[i];
                break;
        }
    }

    escaped[out] = '\0';
    return escaped;
}

/* ========================================
   PYTHON CODE GENERATOR - REAL
   ======================================== */

static void generate_expr_python(StringBuilder *sb, ASTNode *node);

static void generate_expr_js(StringBuilder *sb, ASTNode *node);
static void generate_expr_java(StringBuilder *sb, ASTNode *node);
static void generate_expr_swift(StringBuilder *sb, ASTNode *node);
static void generate_expr_kotlin(StringBuilder *sb, ASTNode *node);
static void generate_expr_go(StringBuilder *sb, ASTNode *node);
static void generate_expr_ruby(StringBuilder *sb, ASTNode *node);

/* Kotlin and Swift will not coerce an integer literal into a Double, and the
   Kotlin backend additionally suffixes integer literals with L for Long. In a
   float slot both of those are wrong, so the literal is emitted as a float
   when the surrounding context expects one. */
static int is_int_literal_node(ASTNode *n) {
    return n && n->type == AST_LITERAL && n->data_type == TYPE_INT && n->value;
}

static void generate_expr_swift_as(StringBuilder *sb, ASTNode *node, DataType want) {
    if (want == TYPE_FLOAT && is_int_literal_node(node)) {
        sb_append(sb, "%s.0", node->value);
        return;
    }
    generate_expr_swift(sb, node);
}

static void generate_expr_kotlin_as(StringBuilder *sb, ASTNode *node, DataType want) {
    if (want == TYPE_FLOAT && is_int_literal_node(node)) {
        sb_append(sb, "%s.0", node->value);
        return;
    }
    generate_expr_kotlin(sb, node);
}



/* ----------------------------------------------------------------
   Operators that do not survive a verbatim copy into the target.

   `**` is SUB's power operator. Only Python, JS and Ruby spell it that way;
   emitting it verbatim into Java/Go/Swift/Kotlin produced a syntax error.

   `/` is integer division in SUB when both operands are integers (9 / 2 is 4,
   as the interpreter computes it). C, Java, Go, Swift and Kotlin already do
   that for integer operands, but Python's `/` and JavaScript's `/` always
   produce a float, so those two need an explicit integer form to agree with
   every other backend.
   ---------------------------------------------------------------- */

typedef void (*ExprGen)(StringBuilder *, ASTNode *);

static int is_comparison_operator(const char *op) {
    return op && (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                  strcmp(op, "<")  == 0 || strcmp(op, ">")  == 0 ||
                  strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);
}

static int both_int_operands(ASTNode *node) {
    return infer_expr_type(node->left)  == TYPE_INT &&
           infer_expr_type(node->right) == TYPE_INT;
}

/* Emit `arg` wrapped in whichever of the target's formatters its static type
   calls for: `fmt_fn` for a float, `arr_fn` for an array, nothing otherwise.
   Languages with one numeric type (JavaScript) or with no runtime type to
   dispatch on (Go, Rust) cannot decide this at run time, so it is decided
   here, from the same inferred type every other backend uses. Either name
   may be NULL when the target has no such formatter. */
static void gen_printable(StringBuilder *sb, ASTNode *arg, const char *fmt_fn,
                          const char *arr_fn, ExprGen gen) {
    DataType t = infer_expr_type(arg);
    const char *wrap = (t == TYPE_FLOAT) ? fmt_fn
                     : (t == TYPE_ARRAY) ? arr_fn : NULL;
    if (wrap) sb_append(sb, "%s(", wrap);
    gen(sb, arg);
    if (wrap) sb_append(sb, ")");
}

/* True when either side of an arithmetic operator is known to be a float. */
static int any_float_operand(ASTNode *node) {
    return infer_expr_type(node->left)  == TYPE_FLOAT ||
           infer_expr_type(node->right) == TYPE_FLOAT;
}

/* Emits a special form and returns 1, or returns 0 to let the caller emit the
   ordinary infix expression. */
static int emit_special_binop(StringBuilder *sb, ASTNode *node,
                              TargetLang lang, ExprGen gen) {
    const char *op = node->value;
    if (!op) return 0;

    /* Java is the one target where comparing strings with the operators
       does the wrong thing: == compares references rather than contents,
       and <, > and friends do not compile at all. Every other target
       compares string values with the plain operators. */
    if (lang == LANG_JAVA && is_comparison_operator(op) &&
        (infer_expr_type(node->left)  == TYPE_STRING ||
         infer_expr_type(node->right) == TYPE_STRING)) {
        if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
            sb_append(sb, "%s", strcmp(op, "!=") == 0 ? "!" : "");
            sb_append(sb, "java.util.Objects.equals(");
            gen(sb, node->left); sb_append(sb, ", ");
            gen(sb, node->right); sb_append(sb, ")");
        } else {
            sb_append(sb, "((");
            gen(sb, node->left); sb_append(sb, ").compareTo(");
            gen(sb, node->right); sb_append(sb, ") %s 0)", op);
        }
        return 1;
    }

    if (strcmp(op, "**") == 0) {
        /* A negative exponent gives a fraction, so it must stay floating
           point even when both operands are integers. */
        int as_int = both_int_operands(node) && !exponent_is_negative(node->right);
        switch (lang) {
            case LANG_PY:
            case LANG_JS:
                return 0;                      /* ** is native in these */
            case LANG_RUBY:
                /* Ruby returns a Rational for a negative integer exponent,
                   which prints as 1/2 rather than 0.5. */
                sb_append(sb, "_sub_pow(");
                gen(sb, node->left); sb_append(sb, ", ");
                gen(sb, node->right); sb_append(sb, ")");
                return 1;
            case LANG_JAVA:
                sb_append(sb, as_int ? "(long)Math.pow(" : "Math.pow(");
                gen(sb, node->left); sb_append(sb, ", ");
                gen(sb, node->right); sb_append(sb, ")");
                return 1;
            case LANG_GO:
                sb_append(sb, as_int ? "int64(math.Pow(float64(" : "math.Pow(float64(");
                gen(sb, node->left); sb_append(sb, "), float64(");
                gen(sb, node->right); sb_append(sb, as_int ? ")))" : "))");
                return 1;
            case LANG_SWIFT:
                sb_append(sb, as_int ? "Int(pow(Double(" : "pow(Double(");
                gen(sb, node->left); sb_append(sb, "), Double(");
                gen(sb, node->right); sb_append(sb, as_int ? ")))" : "))");
                return 1;
            case LANG_KOTLIN:
                sb_append(sb, "Math.pow((");
                gen(sb, node->left); sb_append(sb, ").toDouble(), (");
                gen(sb, node->right);
                sb_append(sb, as_int ? ").toDouble()).toLong()" : ").toDouble())");
                return 1;
        }
        return 0;
    }

    /* Division and remainder go through a helper in every target.
       Three things have to be the same everywhere and are not the same
       natively: integer division truncates toward zero (Python and Ruby
       floor), float remainder follows fmod (Python and Ruby floor that too,
       and C++ will not compile `%` on doubles at all), and dividing by zero
       is a runtime error. Left to each language, `7 / 0` aborted with SIGFPE
       in C, printed Infinity in JavaScript, threw in Python and quietly
       produced 0 in the native backend - five behaviours for one
       expression. The helpers make it one: the interpreter's error message
       and exit status 70.

       An operand whose type is not known stays on the integer helper, since
       that is what the rest of the toolchain assumes for an unresolved
       numeric type; sending it to the float helper would turn 9 / 2 into
       4.5. */
    if (strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
        int is_div = (strcmp(op, "/") == 0);
        int is_flt = any_float_operand(node);
        const char *fn;
        switch (lang) {
            case LANG_JS:
            case LANG_JAVA:
            case LANG_KOTLIN:
            case LANG_SWIFT:
                fn = is_flt ? (is_div ? "_subFdiv"  : "_subFmod")
                            : (is_div ? "_subIdiv"  : "_subMod");
                break;
            default:
                fn = is_flt ? (is_div ? "_sub_fdiv" : "_sub_fmod")
                            : (is_div ? "_sub_idiv" : "_sub_mod");
                break;
        }
        sb_append(sb, "%s(", fn);
        /* Go has no implicit numeric conversion, so an integer operand of a
           float division has to be converted at the call: round() yields an
           int64, and `round(x * scale) / scale` would not compile without
           this. The other targets promote on their own. */
        int coerce = is_flt && (lang == LANG_GO || lang == LANG_KOTLIN);
        const char *pre = lang == LANG_GO ? "float64(" : "(";
        const char *post = lang == LANG_GO ? ")" : ").toDouble()";
        if (coerce && infer_expr_type(node->left) != TYPE_FLOAT) {
            sb_append(sb, "%s", pre); gen(sb, node->left); sb_append(sb, "%s", post);
        } else {
            gen(sb, node->left);
        }
        sb_append(sb, ", ");
        if (coerce && infer_expr_type(node->right) != TYPE_FLOAT) {
            sb_append(sb, "%s", pre); gen(sb, node->right); sb_append(sb, "%s", post);
        } else {
            gen(sb, node->right);
        }
        sb_append(sb, ")");
        return 1;
    }
    return 0;
}

/* Can control reach the end of this statement?

   Java is the one target that needs to know. It rejects a value-returning
   method whose body can complete normally, and separately rejects any
   statement it can prove unreachable -- so the fallback return the first
   rule sometimes demands is exactly what the second rule sometimes forbids.
   These are Java's own rules (JLS 14.21) for the shapes this backend emits,
   which is why an `if` with a constant condition still counts as completing:
   Java deliberately does not look at the condition's value. */
static int stmt_completes(ASTNode *n) {
    if (!n) return 1;

    switch (n->type) {
    case AST_RETURN_STMT:
    case AST_THROW_STMT:
        return 0;

    case AST_IF_STMT:
        /* Without an else there is a path around it. With one, both sides
           have to be dead ends for the whole thing to be. */
        if (!n->right) return 1;
        return stmt_completes(n->body) || stmt_completes(n->right);

    case AST_WHILE_STMT:
        /* `while (true)` ends only by breaking out of it. */
        if (n->condition && n->condition->type == AST_LITERAL &&
            n->condition->value && strcmp(n->condition->value, "true") == 0)
            return body_has_free_break(n->body);
        return 1;

    case AST_BLOCK: {
        ASTNode *last = NULL;
        if (n->child_count > 0) last = n->children[n->child_count - 1];
        else for (ASTNode *s = block_first(n); s; s = s->next) last = s;
        return stmt_completes(last);
    }

    default:
        /* A switch included: its chain ends in `else if (true)`, which by
           Java's rules leaves a path through. */
        return 1;
    }
}

/* Loop-variable shadowing, for the targets that do not have it.

   `let i = 100` followed by `for i in range(0, 3)` is ordinary SUB: the loop
   has its own `i`, and the outer one still reads 100 afterwards. C, C++, Go,
   Rust, Swift and Kotlin all give a `for` variable its own scope and need
   nothing here. Two targets do:

     - Java forbids a local from shadowing another local outright, so the
       program did not compile at all.
     - Python has no block scope: the loop variable *is* the outer one, so
       the program compiled and quietly printed the wrong number.

   Both are fixed the same way -- a loop whose variable is already in scope
   is generated under a different name, and references to it inside the loop
   are rewritten to match. */
#define SHADOW_MAX 128
static const char *g_scope[SHADOW_MAX];
static int         g_nscope = 0;

static struct { const char *from; char to[48]; } g_rename[SHADOW_MAX];
static int g_nrename = 0;
static int g_rename_seq = 0;

static int name_in_scope(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < g_nscope; i++)
        if (g_scope[i] && strcmp(g_scope[i], name) == 0) return 1;
    return 0;
}

static void scope_push(const char *name) {
    if (name && g_nscope < SHADOW_MAX) g_scope[g_nscope++] = name;
}

/* The name to write for a SUB identifier: the innermost rename wins. */
/* The top-level variables of the program being generated; see
   codegen_globals.h. Every generator in this file fills this in before it
   starts, and each lowers a global the way its target spells one. */
static Globals g_globals;

static const char *shadow_name(const char *name) {
    if (!name) return "var";
    for (int i = g_nrename - 1; i >= 0; i--)
        if (strcmp(g_rename[i].from, name) == 0) return g_rename[i].to;
    return name;
}

/* Give `src` a private name if it is already in scope, and record it either
   way. Returns the name the loop should be generated under. */
static const char *shadow_declare_loop_var(const char *src) {
    const char *var = src;
    if (name_in_scope(src) && g_nrename < SHADOW_MAX) {
        g_rename[g_nrename].from = src;
        snprintf(g_rename[g_nrename].to, sizeof g_rename[0].to,
                 "%s__%d", src, g_rename_seq++);
        var = g_rename[g_nrename].to;
        g_nrename++;
    }
    scope_push(src);
    return var;
}

static void generate_expr_python(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                if (strcmp(node->value, "true") == 0) sb_append(sb, "True");
                else if (strcmp(node->value, "false") == 0) sb_append(sb, "False");
                else if (strcmp(node->value, "null") == 0 || strcmp(node->value, "nil") == 0) sb_append(sb, "None");
                else sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "None");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", shadow_name(node->value));
            break;
        case AST_BINARY_EXPR:
            if (emit_special_binop(sb, node, LANG_PY, generate_expr_python)) break;
            if (node->value && strcmp(node->value, "+") == 0) {
                /* SUB's '+' auto-converts to string concatenation when either
                   operand is a string (matching the interpreter's eval_binary).
                   Types aren't always known at transpile time, so route through
                   a small runtime helper instead of emitting a bare '+', which
                   Python raises TypeError on for e.g. "age: " + 18. */
                sb_append(sb, "_sub_add(");
                generate_expr_python(sb, node->left);
                sb_append(sb, ", ");
                generate_expr_python(sb, node->right);
                sb_append(sb, ")");
                break;
            }
            sb_append(sb, "(");
            generate_expr_python(sb, node->left);
            if (node->value && (strcmp(node->value, "&&") == 0 || strcmp(node->value, "and") == 0)) {
                sb_append(sb, " and ");
            } else if (node->value && (strcmp(node->value, "||") == 0 || strcmp(node->value, "or") == 0)) {
                sb_append(sb, " or ");
            } else {
                sb_append(sb, " %s ", node->value ? node->value : "+");
            }
            generate_expr_python(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            if (node->value && (strcmp(node->value, "!") == 0 || strcmp(node->value, "not") == 0)) {
                sb_append(sb, "(not ");
                generate_expr_python(sb, node->right ? node->right : node->left);
                sb_append(sb, ")");
            } else if (node->value && strcmp(node->value, "~") == 0) {
                sb_append(sb, "(~");
                generate_expr_python(sb, node->right ? node->right : node->left);
                sb_append(sb, ")");
            } else {
                sb_append(sb, "%s", node->value ? node->value : "");
                generate_expr_python(sb, node->right ? node->right : node->left);
            }
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_python(sb, node->left);
            sb_append(sb, " if ");
            generate_expr_python(sb, node->condition);
            sb_append(sb, " else ");
            generate_expr_python(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_MEMBER_ACCESS:
            if (node->value && strcmp(node->value, "length") == 0) {
                sb_append(sb, "len(");
                generate_expr_python(sb, node->left);
                sb_append(sb, ")");
            } else {
                generate_expr_python(sb, node->left);
                sb_append(sb, ".%s", node->value ? node->value : "");
            }
            break;
        case AST_ARRAY_ACCESS:
            sb_append(sb, "_sub_at(");
            generate_expr_python(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_python(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR:
            if (node->left && node->left->type == AST_MEMBER_ACCESS) {
                const char *m = node->left->value;
                if (m && strcmp(m, "push") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".append(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]);
                    sb_append(sb, ")");
                } else if (m && strcmp(m, "pop") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".pop()");
                } else if (m && strcmp(m, "join") == 0) {
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]);
                    else sb_append(sb, "\"\"");
                    sb_append(sb, ".join(");
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ")");
                } else if (m && strcmp(m, "upper") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".upper()");
                } else if (m && strcmp(m, "lower") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".lower()");
                } else if (m && strcmp(m, "trim") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".strip()");
                } else if (m && strcmp(m, "substring") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, "[");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]); else sb_append(sb, "0");
                    sb_append(sb, ":");
                    if (node->child_count > 1) generate_expr_python(sb, node->children[1]);
                    sb_append(sb, "]");
                } else if (m && strcmp(m, "split") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".split(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]);
                    sb_append(sb, ")");
                } else if (m && strcmp(m, "contains") == 0) {
                    sb_append(sb, "(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]); else sb_append(sb, "\"\"");
                    sb_append(sb, " in ");
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ")");
                } else if (m && strcmp(m, "replace") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, ".replace(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]); else sb_append(sb, "\"\"");
                    sb_append(sb, ", ");
                    if (node->child_count > 1) generate_expr_python(sb, node->children[1]); else sb_append(sb, "\"\"");
                    sb_append(sb, ")");
                } else if (m && strcmp(m, "char_at") == 0) {
                    generate_expr_python(sb, node->left->left);
                    sb_append(sb, "[");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]); else sb_append(sb, "0");
                    sb_append(sb, "]");
                } else {
                    generate_expr_python(sb, node->left);
                    sb_append(sb, "(");
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_python(sb, node->children[i]);
                    }
                    sb_append(sb, ")");
                }
            } else if (node->value) {
                const char *fn = node->value;
                if (is_print_builtin(fn)) {
                    sb_append(sb, "_sub_print(");
                } else if (strcmp(fn, "str") == 0 || strcmp(fn, "to_string") == 0) {
                    sb_append(sb, "_sub_str(");
                } else if (strcmp(fn, "upper") == 0) {
                    /* SUB exposes these as plain functions; Python spells them
                       as string methods. */
                    sb_append(sb, "(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]);
                    sb_append(sb, ").upper()");
                    break;
                } else if (strcmp(fn, "lower") == 0) {
                    sb_append(sb, "(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]);
                    sb_append(sb, ").lower()");
                    break;
                } else if (strcmp(fn, "trim") == 0) {
                    sb_append(sb, "(");
                    if (node->child_count > 0) generate_expr_python(sb, node->children[0]);
                    sb_append(sb, ").strip()");
                    break;
                } else if (strcmp(fn, "sqrt") == 0) {
                    sb_append(sb, "math.sqrt(");
                } else if (strcmp(fn, "floor") == 0) {
                    sb_append(sb, "math.floor(");
                } else if (strcmp(fn, "ceil") == 0) {
                    sb_append(sb, "math.ceil(");
                } else if (strcmp(fn, "round") == 0) {
                    /* not Python's round(): see _sub_round in the preamble */
                    sb_append(sb, "_sub_round(");
                } else if (strcmp(fn, "push") == 0 || strcmp(fn, "append") == 0) {
                    /* list.append is a method, and SUB's push evaluates to
                       null rather than to the list */
                    sb_append(sb, "_sub_push(");
                } else if (strcmp(fn, "pop") == 0) {
                    sb_append(sb, "_sub_pop(");
                } else if (strcmp(fn, "type") == 0) {
                    sb_append(sb, "(lambda x: 'int' if isinstance(x, int) and not isinstance(x, bool) else ('float' if isinstance(x, float) else ('string' if isinstance(x, str) else ('bool' if isinstance(x, bool) else ('array' if isinstance(x, list) else ('null' if x is None else type(x).__name__))))))(");
                } else {
                    sb_append(sb, "%s(", fn);
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_python(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "[");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_python(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\": ", pair->value ? pair->value : "");
                generate_expr_python(sb, pair->right);
            }
            sb_append(sb, "}");
            break;
        default:
            break;
    }
}

/* One case body. A `break` the parser could not drop -- one in the middle of
   the body rather than at its end -- needs something to leave, and Python has
   no labelled block, so the body runs inside a loop that iterates once. */
static void gen_clause_python(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_code(sb, indent);
        sb_append(sb, "for _ in range(1):\n");
        indent++;
    }
    generate_node_python(sb, clause->body, indent);
    if (!clause->body || block_first(clause->body) == NULL) {
        indent_code(sb, indent);
        sb_append(sb, "pass\n");
    }
}

static void generate_node_python(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_python(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            scope_push(node->value);
            sb_append(sb, "%s = ", shadow_name(node->value ? node->value : "var"));
            if (node->right) {
                generate_expr_python(sb, node->right);
            } else {
                sb_append(sb, "None");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            scope_push(node->value);
            sb_append(sb, "%s = ", shadow_name(node->value ? node->value : "CONST"));
            if (node->right) {
                generate_expr_python(sb, node->right);
            } else {
                sb_append(sb, "None");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FUNCTION_DECL: {
            int scope_mark = g_nscope, rename_mark = g_nrename;
            g_nscope = g_nrename = 0;
            sb_append(sb, "\ndef %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "%s", node->children[i]->value ? node->children[i]->value : "arg");
                scope_push(node->children[i]->value);
            }
            sb_append(sb, "):\n");
            {
                /* Assigning a name in a Python function makes it a local
                   unless the function says otherwise, so a SUB function that
                   writes a top-level variable needs this or it raises
                   UnboundLocalError on the read in `calls = calls + 1`. */
                const char *gl[GLOBALS_MAX];
                int n = fn_assigned_globals(&g_globals, node, gl, GLOBALS_MAX);
                if (n > 0) {
                    indent_code(sb, indent + 1);
                    sb_append(sb, "global ");
                    for (int i = 0; i < n; i++)
                        sb_append(sb, "%s%s", i ? ", " : "", gl[i]);
                    sb_append(sb, "\n");
                }
            }
            if (node->body) {
                generate_node_python(sb, node->body, indent + 1);
            }
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            g_nscope = scope_mark; g_nrename = rename_mark;
        }
            sb_append(sb, "\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if ");
            generate_expr_python(sb, node->condition);
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    indent_code(sb, indent);
                    sb_append(sb, "elif ");
                    generate_expr_python(sb, node->right->condition);
                    sb_append(sb, ":\n");
                    generate_node_python(sb, node->right->body, indent + 1);
                    if (!node->right->body || block_first(node->right->body) == NULL) {
                        indent_code(sb, indent + 1);
                        sb_append(sb, "pass\n");
                    }
                    if (node->right->right) {
                        indent_code(sb, indent);
                        sb_append(sb, "else:\n");
                        generate_node_python(sb, node->right->right, indent + 1);
                        if (!node->right->right || block_first(node->right->right) == NULL) {
                            indent_code(sb, indent + 1);
                            sb_append(sb, "pass\n");
                        }
                    }
                } else {
                    indent_code(sb, indent);
                    sb_append(sb, "else:\n");
                    generate_node_python(sb, node->right, indent + 1);
                    if (!node->right || block_first(node->right) == NULL) {
                        indent_code(sb, indent + 1);
                        sb_append(sb, "pass\n");
                    }
                }
            }
            break;
            
        case AST_FOR_STMT: {
            int scope_mark = g_nscope, rename_mark = g_nrename;
            indent_code(sb, indent);
            sb_append(sb, "for %s in ",
                      shadow_declare_loop_var(node->value ? node->value : "i"));
            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    sb_append(sb, "range(");
                    if (range->left) generate_expr_python(sb, range->left);
                    if (range->right) {
                        sb_append(sb, ", ");
                        generate_expr_python(sb, range->right);
                    }
                    sb_append(sb, ")");
                } else {
                    generate_expr_python(sb, range);
                }
            } else if (node->condition) {
                generate_expr_python(sb, node->condition);
            } else {
                sb_append(sb, "range(10)");
            }
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            g_nscope = scope_mark; g_nrename = rename_mark;
            break;
        }

        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while ");
            generate_expr_python(sb, node->condition);
            sb_append(sb, ":\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            break;

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while True:\n");
            generate_node_python(sb, node->body, indent + 1);
            indent_code(sb, indent + 1);
            sb_append(sb, "if not (");
            generate_expr_python(sb, node->condition);
            sb_append(sb, "): break\n");
            break;

        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break\n");
            break;

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue\n");
            break;

        case AST_SWITCH_STMT: {
            /* An if/elif chain, not `match`: `match` needs Python 3.10 and
               its case patterns cannot be arbitrary expressions. */
            static int sw_py = 0;
            int id = sw_py++;
            ASTNode *deflt = switch_default_clause(node);
            int emitted = 0;

            indent_code(sb, indent);
            sb_append(sb, "_sw%d = ", id);
            generate_expr_python(sb, node->condition);
            sb_append(sb, "\n");

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_code(sb, indent);
                sb_append(sb, "%s ", emitted ? "elif" : "if");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " or ");
                    sb_append(sb, "_sw%d == (", id);
                    generate_expr_python(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, ":\n");
                gen_clause_python(sb, c, indent + 1);
                emitted = 1;
            }
            if (deflt) {
                indent_code(sb, indent);
                sb_append(sb, "%s\n", emitted ? "else:" : "if True:");
                gen_clause_python(sb, deflt, indent + 1);
                emitted = 1;
            }
            if (!emitted) {
                indent_code(sb, indent);
                sb_append(sb, "pass\n");
            }
            break;
        }

        case AST_TRY_STMT:
            indent_code(sb, indent);
            sb_append(sb, "try:\n");
            generate_node_python(sb, node->body, indent + 1);
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
            }
            if (node->right && node->right->type == AST_CATCH_CLAUSE) {
                indent_code(sb, indent);
                sb_append(sb, "except Exception as %s:\n", node->right->value ? node->right->value : "e");
                generate_node_python(sb, node->right->body, indent + 1);
                if (!node->right->body || block_first(node->right->body) == NULL) {
                    indent_code(sb, indent + 1);
                    sb_append(sb, "pass\n");
                }
            }
            break;

        case AST_THROW_STMT:
            indent_code(sb, indent);
            sb_append(sb, "raise Exception(");
            if (node->right) generate_expr_python(sb, node->right);
            sb_append(sb, ")\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_python(sb, node->right);
            }
            sb_append(sb, "\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_python(sb, node);
            sb_append(sb, "\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "_sub_put(");
                generate_expr_python(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_python(sb, node->left->right);
                sb_append(sb, ", ");
                generate_expr_python(sb, node->right);
                sb_append(sb, ")\n");
                break;
            }
            generate_expr_python(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_python(sb, node->right);
            sb_append(sb, "\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_python(sb, stmt, indent);
            }
            break;
            
        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            if (node->value && (!node->metadata || strcasecmp((const char*)node->metadata, "python") == 0)) {
                sb_append(sb, "# Embedded Python code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;
            
        default:
            break;
    }
}

char* codegen_python(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    globals_collect(&g_globals, ast);
    
    sb_append(sb, "#!/usr/bin/env python3\n");
    sb_append(sb, "import sys\n");
    sb_append(sb, "# Generated by SUB Language Compiler\n");
    sb_append(sb, "import math\n");
    /* SUB writes booleans as true/false. Python's str() writes True/False, so
       the same program printed different text depending on the backend.
       These helpers keep Python's output identical to the interpreter's. */
    sb_append(sb, "\n\ndef _sub_str(v):\n");
    sb_append(sb, "    if isinstance(v, bool):\n");
    sb_append(sb, "        return \"true\" if v else \"false\"\n");
    sb_append(sb, "    if v is None:\n");
    sb_append(sb, "        return \"null\"\n");
    /* The interpreter prints floats with printf's %g: six significant
       digits, trailing zeros dropped. Python's str() prints all seventeen,
       so 1.0 / 3.0 came out 0.3333333333333333 where every other backend
       said 0.333333. */
    sb_append(sb, "    if isinstance(v, float):\n");
    sb_append(sb, "        return \"%%g\" %% v\n");
    /* SUB prints an array as [a, b, c] with no quotes around strings;
       Python's str() of a list quotes them. */
    sb_append(sb, "    if isinstance(v, list):\n");
    sb_append(sb, "        return '[' + ', '.join(_sub_str(x) for x in v) + ']'\n");
    sb_append(sb, "    return str(v)\n");
    /* SUB divides and takes the remainder the way C, Java, Go and the
       interpreter do: truncated toward zero, so -7 / 2 is -3 and -7 % 3 is
       -1. Python's // and % floor instead, giving -4 and 2. */
    sb_append(sb, "\n\ndef _sub_die(msg):\n");   /* see the note above */
    sb_append(sb, "    print('RuntimeError: ' + msg, file=sys.stderr)\n");
    sb_append(sb, "    sys.exit(70)\n");
    sb_append(sb, "\n\ndef _sub_idiv(a, b):\n");
    sb_append(sb, "    if b == 0: _sub_die('division by zero')\n");
    sb_append(sb, "    q = abs(a) // abs(b)\n");
    sb_append(sb, "    return -q if (a < 0) != (b < 0) else q\n");
    sb_append(sb, "\n\ndef _sub_mod(a, b):\n");
    sb_append(sb, "    if b == 0: _sub_die('modulo by zero')\n");
    sb_append(sb, "    return a - _sub_idiv(a, b) * b\n");
    sb_append(sb, "\n\ndef _sub_fdiv(a, b):\n");
    sb_append(sb, "    if b == 0: _sub_die('division by zero')\n");
    sb_append(sb, "    return a / b\n");
    sb_append(sb, "\n\ndef _sub_fmod(a, b):\n");
    sb_append(sb, "    if b == 0: _sub_die('modulo by zero')\n");
    sb_append(sb, "    return math.fmod(a, b)\n");
    /* Indexing goes through a helper so that a negative index counts from
       the end and an out-of-range one reports what the interpreter reports.
       Left to the language, Python raised IndexError and exited 1 while
       Ruby and JavaScript quietly produced null. */
    sb_append(sb, "\n\ndef _sub_at(c, i):\n");
    sb_append(sb, "    n = len(c)\n");
    sb_append(sb, "    if i < 0: i += n\n");
    sb_append(sb, "    if i < 0 or i >= n:\n");
    sb_append(sb, "        if isinstance(c, str):\n");
    sb_append(sb, "            _sub_die('string index %%d out of bounds' %% i)\n");
    sb_append(sb, "        _sub_die('array index %%d out of bounds [0, %%d)' %% (i, n))\n");
    sb_append(sb, "    return c[i]\n");
    sb_append(sb, "\n\ndef _sub_put(a, i, v):\n");
    sb_append(sb, "    n = len(a)\n");
    sb_append(sb, "    if i < 0: i += n\n");
    sb_append(sb, "    if i < 0 or i >= n:\n");
    sb_append(sb, "        _sub_die('array index %%d out of bounds [0, %%d)' %% (i, n))\n");
    sb_append(sb, "    a[i] = v\n");
    sb_append(sb, "\n\ndef _sub_push(a, v):\n    a.append(v)\n");
    sb_append(sb, "\n\ndef _sub_pop(a):\n");
    sb_append(sb, "    if not a: _sub_die('pop from empty array')\n");
    sb_append(sb, "    return a.pop()");
    sb_append(sb, "\n\ndef _sub_print(*a):\n");
    sb_append(sb, "    print(*[_sub_str(x) for x in a])\n\n");
    sb_append(sb, "def _sub_add(a, b):\n");
    sb_append(sb, "    # SUB's '+' concatenates when either side is a string,\n");
    sb_append(sb, "    # otherwise adds numerically (mirrors the SUB interpreter).\n");
    sb_append(sb, "    if isinstance(a, str) or isinstance(b, str):\n");
    sb_append(sb, "        return str(a) + str(b)\n");
    sb_append(sb, "    return a + b\n\n");

    sb_append(sb, "def _sub_round(x):\n");
    sb_append(sb, "    # Python's round() is banker's rounding: round(2.5)\n");
    sb_append(sb, "    # is 2 and round(-2.5) is -2. SUB rounds halves away\n");
    sb_append(sb, "    # from zero, as C's round() does.\n");
    sb_append(sb, "    return int(math.floor(x + 0.5)) if x >= 0 "
                  "else int(math.ceil(x - 0.5))\n\n");

    char *embedded = extract_embedded_code(source, "python");
    if (embedded) {
        sb_append(sb, "# Embedded Python code from SUB\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }

    // Generate from AST
    generate_node_python(sb, ast, 0);
    
    return sb_to_string(sb);
}

/* ========================================
   JAVASCRIPT CODE GENERATOR - REAL
   ======================================== */

static void generate_expr_js(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "null");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;
        case AST_BINARY_EXPR:
            if (emit_special_binop(sb, node, LANG_JS, generate_expr_js)) break;
            sb_append(sb, "(");
            generate_expr_js(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_js(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_js(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_js(sb, node->condition);
            sb_append(sb, " ? ");
            generate_expr_js(sb, node->left);
            sb_append(sb, " : ");
            generate_expr_js(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR:
            {
                const BuiltinSpelling *bs = builtin_spelling(LANG_JS, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_js(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
                bs = builtin_spelling_multi(LANG_JS, node->value);
                if (bs && node->child_count >= 2) {
                    sb_append(sb, "%s", bs->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_js(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (node->value && (strcmp(node->value, "show") == 0 ||
                                strcmp(node->value, "str") == 0 ||
                                strcmp(node->value, "to_string") == 0)) {
                int as_str = (node->value[0] == 's' && node->value[1] == 't');
                sb_append(sb, as_str ? "_subStr(" : "console.log(_subStr(");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_js(sb, node->children[i]);
                }
                sb_append(sb, as_str ? ")" : "))");
                break;
            } else if (node->value) {
                sb_append(sb, "%s(", node->value);
            } else {
                generate_expr_js(sb, node->left);
                sb_append(sb, "(");
            }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_js(sb, node->children[i]);
            }
            sb_append(sb, ")");
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "[");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_js(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\": ", pair->value ? pair->value : "");
                generate_expr_js(sb, pair->right);
            }
            sb_append(sb, "}");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_js(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            sb_append(sb, "_subAt(");
            generate_expr_js(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_js(sb, node->right);
            sb_append(sb, ")");
            break;
        default:
            break;
    }
}

static void generate_node_js(StringBuilder *sb, ASTNode *node, int indent);

/* See gen_clause_python: a `break` left in the middle of a case body needs
   a construct to leave, and a one-iteration loop is one in every language. */
static void gen_clause_js(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_code(sb, indent);
        sb_append(sb, "do {\n");
        indent++;
    }
    generate_node_js(sb, clause->body, indent);
    if (loop) {
        indent_code(sb, indent - 1);
        sb_append(sb, "} while (false);\n");
    }
}

static void generate_node_js(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        /*  A dropped `break` turns a loop that terminates into one
           that does not, so this must never fall through to the default. */

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "do {\n");
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "} while (");
            generate_expr_js(sb, node->condition);
            sb_append(sb, ");\n");
            break;
        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break;\n");
            break;

        case AST_SWITCH_STMT: {
            static int sw_js = 0;
            int id = sw_js++;
            ASTNode *deflt = switch_default_clause(node);
            int emitted = 0;

            indent_code(sb, indent);
            sb_append(sb, "{\n");
            indent_code(sb, indent + 1);
            sb_append(sb, "const _sw%d = ", id);
            generate_expr_js(sb, node->condition);
            sb_append(sb, ";\n");

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_code(sb, indent + 1);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    sb_append(sb, "_sw%d === (", id);
                    generate_expr_js(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, ") {\n");
                gen_clause_js(sb, c, indent + 2);
                emitted = 1;
            }
            if (deflt) {
                if (emitted) {
                    indent_code(sb, indent + 1);
                    sb_append(sb, "} else {\n");
                    gen_clause_js(sb, deflt, indent + 2);
                } else {
                    /* Nothing for it to be the `else` of. A bare `{ }` here
                       is a block in most of these languages but a closure
                       expression in Swift, so the body is simply emitted. */
                    gen_clause_js(sb, deflt, indent + 1);
                }
            }
            if (emitted) {
                indent_code(sb, indent + 1);
                sb_append(sb, "}\n");
            }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        }

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue;\n");
            break;

        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_js(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "let %s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_js(sb, node->right);
            } else {
                sb_append(sb, "null");
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "const %s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_js(sb, node->right);
            } else {
                sb_append(sb, "null");
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            indent_code(sb, indent);
            sb_append(sb, "function %s(", node->value ? node->value : "func");
            // Parameters
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    sb_append(sb, "%s%s", i > 0 ? ", " : "", node->children[i]->value);
                }
            }
            sb_append(sb, ") {\n");
            
            if (node->body) generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n\n");
            break;
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expr_js(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if (");
                    generate_expr_js(sb, node->right->condition);
                    sb_append(sb, ") {\n");
                    generate_node_js(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        sb_append(sb, " else {\n");
                        generate_node_js(sb, node->right->right, indent + 1);
                        indent_code(sb, indent);
                        sb_append(sb, "}");
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_js(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                }
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            // Check for range expression
            if (node->children && node->child_count > 0 && node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for (let %s = ", node->value ? node->value : "i");
                if (range->right) {
                    if (range->left) generate_expr_js(sb, range->left); else sb_append(sb, "0");
                } else {
                    sb_append(sb, "0");
                }
                sb_append(sb, "; %s < ", node->value ? node->value : "i");
                if (range->right) {
                    generate_expr_js(sb, range->right);
                } else if (range->left) {
                    generate_expr_js(sb, range->left);
                } else {
                    sb_append(sb, "10");
                }
                sb_append(sb, "; %s++) {\n", node->value ? node->value : "i");
            } 
            // Check for collection iteration
            else if (node->condition) {
                sb_append(sb, "for (let %s of ", node->value ? node->value : "item");
                generate_expr_js(sb, node->condition);
                sb_append(sb, ") {\n");
            }
            // Fallback
            else {
                sb_append(sb, "for (let %s = 0; %s < 10; %s++) {\n", 
                         node->value ? node->value : "i",
                         node->value ? node->value : "i",
                         node->value ? node->value : "i");
            }
            
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expr_js(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_js(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_js(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            // Map print to console.log
            if (is_print_builtin(node->value)) {
                /* Always hand console.log a string. Given a number, a
                   boolean or an array it applies its own inspection
                   formatting - which spells booleans and arrays differently
                   from SUB, and wraps values in ANSI colour codes whenever
                   FORCE_COLOR is set, even into a pipe. A program's output
                   should not depend on either. */
                sb_append(sb, "console.log(_subStr(");
                if (node->child_count > 0)
                    generate_expr_js(sb, node->children[0]);
                sb_append(sb, "))");
            } else {
                generate_expr_js(sb, node);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "_subPut(");
                generate_expr_js(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_js(sb, node->left->right);
                sb_append(sb, ", ");
                generate_expr_js(sb, node->right);
                sb_append(sb, ");\n");
                break;
            }
            generate_expr_js(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_js(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_js(sb, stmt, indent);
            }
            break;
            
        case AST_EMBED_CODE:
            if (node->value) {
                sb_append(sb, "// Embedded JavaScript\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;
            
        default:
            break;
    }
}

char* codegen_javascript(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    globals_collect(&g_globals, ast);
    
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");

    /* SUB's round() breaks ties away from zero, following C's round().
       Math.round() breaks them toward +Infinity, so round(-2.5) came out
       -2 where every other SUB backend says -3. */
    /* Integer / and % must truncate toward zero and must fail at zero the
       way the interpreter does, rather than yielding Infinity and NaN. */
    /* printf's %g, as the interpreter prints floats. JavaScript has one
       number type, so which values get this treatment is decided at
       transpile time from the inferred type, not at runtime. */
    sb_append(sb, "function _subFmt(d) {\n");
    sb_append(sb, "    if (Number.isNaN(d)) return 'nan';\n");
    sb_append(sb, "    if (!Number.isFinite(d)) return d < 0 ? '-inf' : 'inf';\n");
    sb_append(sb, "    if (d === 0) return Object.is(d, -0) ? '-0' : '0';\n");
    sb_append(sb, "    let s = d.toPrecision(6);\n");
    sb_append(sb, "    if (s.indexOf('e') >= 0) {\n");
    sb_append(sb, "        let [m, e] = s.split('e');\n");
    sb_append(sb, "        if (m.indexOf('.') >= 0) m = m.replace(/0+$/, '').replace(/\\.$/, '');\n");
    sb_append(sb, "        const sign = e[0] === '-' ? '-' : '+';\n");
    sb_append(sb, "        const dig = e.replace(/^[+-]/, '').padStart(2, '0');\n");
    sb_append(sb, "        return m + 'e' + sign + dig;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    if (s.indexOf('.') >= 0) s = s.replace(/0+$/, '').replace(/\\.$/, '');\n");
    sb_append(sb, "    return s;\n");
    sb_append(sb, "}\n\n");
    /* One place that turns a value into SUB's spelling of it: true/false
       rather than JavaScript's, null rather than undefined, [a, b] with no
       quotes, and %g for a non-integral number. */
    sb_append(sb, "function _subStr(v) {\n");
    sb_append(sb, "    if (v === null || v === undefined) return 'null';\n");
    sb_append(sb, "    if (typeof v === 'boolean') return v ? 'true' : 'false';\n");
    sb_append(sb, "    if (Array.isArray(v)) return '[' + v.map(_subStr).join(', ') + ']';\n");
    sb_append(sb, "    if (typeof v === 'number')\n");
    sb_append(sb, "        return Number.isInteger(v) ? String(v) : _subFmt(v);\n");
    sb_append(sb, "    return String(v);\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subAt(c, i) {\n");
    sb_append(sb, "    const n = c.length;\n");
    sb_append(sb, "    if (i < 0) i += n;\n");
    sb_append(sb, "    if (i < 0 || i >= n)\n");
    sb_append(sb, "        _subDie(typeof c === 'string'\n");
    sb_append(sb, "            ? `string index ${i} out of bounds`\n");
    sb_append(sb, "            : `array index ${i} out of bounds [0, ${n})`);\n");
    sb_append(sb, "    return c[i];\n}\n\n");
    sb_append(sb, "function _subPut(a, i, v) {\n");
    sb_append(sb, "    const n = a.length;\n");
    sb_append(sb, "    if (i < 0) i += n;\n");
    sb_append(sb, "    if (i < 0 || i >= n)\n");
    sb_append(sb, "        _subDie(`array index ${i} out of bounds [0, ${n})`);\n");
    sb_append(sb, "    a[i] = v;\n}\n\n");
    sb_append(sb, "function _subPush(a, v) { a.push(v); }\n\n");
    sb_append(sb, "function _subPop(a) {\n");
    sb_append(sb, "    if (a.length === 0) _subDie('pop from empty array');\n");
    sb_append(sb, "    return a.pop();\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subDie(msg) {\n");
    sb_append(sb, "    console.error('RuntimeError: ' + msg);\n");
    sb_append(sb, "    process.exit(70);\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subIdiv(a, b) {\n");
    sb_append(sb, "    if (b === 0) _subDie('division by zero');\n");
    sb_append(sb, "    return Math.trunc(a / b);\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subMod(a, b) {\n");
    sb_append(sb, "    if (b === 0) _subDie('modulo by zero');\n");
    sb_append(sb, "    return a %% b;\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subFdiv(a, b) {\n");
    sb_append(sb, "    if (b === 0) _subDie('division by zero');\n");
    sb_append(sb, "    return a / b;\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subFmod(a, b) {\n");
    sb_append(sb, "    if (b === 0) _subDie('modulo by zero');\n");
    sb_append(sb, "    return a %% b;\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "function _subRound(x) {\n");
    sb_append(sb, "    return x < 0 ? -Math.round(-x) : Math.round(x);\n");
    sb_append(sb, "}\n\n");

    // Check for embedded JavaScript
    char *embedded = extract_embedded_code(source, "javascript");
    if (embedded) {
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }
    generate_node_js(sb, ast, 0);
    
    return sb_to_string(sb);
}

/* ========================================
   JAVA CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_java(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "null");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "%s", shadow_name(node->value));
            break;
        case AST_BINARY_EXPR:
            /* `x == null` / `x != null` become NaN tests to match how a
               nullable numeric is represented (see the literal case). */
            if (node->value && (strcmp(node->value, "==") == 0 ||
                                strcmp(node->value, "!=") == 0) &&
                (expr_is_null_literal(node->left) || expr_is_null_literal(node->right))) {
                ASTNode *val = expr_is_null_literal(node->left) ? node->right : node->left;
                DataType vt = infer_expr_type(val);
                if (vt == TYPE_FLOAT || vt == TYPE_INT) {
                    sb_append(sb, "%sDouble.isNaN(", strcmp(node->value, "!=") == 0 ? "!" : "");
                    generate_expr_java(sb, val);
                    sb_append(sb, ")");
                    break;
                }
            }
            if (emit_special_binop(sb, node, LANG_JAVA, generate_expr_java)) break;
            sb_append(sb, "(");
            generate_expr_java(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_java(sb, node->right);
            break;
        case AST_TERNARY_EXPR:
            sb_append(sb, "(");
            generate_expr_java(sb, node->condition);
            sb_append(sb, " ? ");
            generate_expr_java(sb, node->left);
            sb_append(sb, " : ");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_CALL_EXPR: {
            const char *fn = node->value ? node->value : "func";
            {
                /* Ahead of the table, whose len() spells String.length(). */
                if (node->value && node->child_count >= 1 && node->children[0] &&
                    infer_expr_type(node->children[0]) == TYPE_ARRAY) {
                    DataType el = infer_elem_type(node->children[0]);
                    if ((!strcmp(node->value, "push") ||
                         !strcmp(node->value, "append")) && node->child_count >= 2) {
                        sb_append(sb, "_subPush(");
                        generate_expr_java(sb, node->children[0]);
                        sb_append(sb, ", ");
                        if (el == TYPE_INT)        sb_append(sb, "(Long)(long)(");
                        else if (el == TYPE_FLOAT) sb_append(sb, "(Double)(double)(");
                        else                       sb_append(sb, "(");
                        generate_expr_java(sb, node->children[1]);
                        sb_append(sb, "))");
                        break;
                    }
                    if (!strcmp(node->value, "pop")) {
                        sb_append(sb, "_subPop(");
                        generate_expr_java(sb, node->children[0]);
                        sb_append(sb, ")");
                        break;
                    }
                    if (!strcmp(node->value, "len") || !strcmp(node->value, "length")) {
                        sb_append(sb, "(long)(");
                        generate_expr_java(sb, node->children[0]);
                        sb_append(sb, ").size()");
                        break;
                    }
                }
                const BuiltinSpelling *bs = builtin_spelling(LANG_JAVA, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_java(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
                bs = builtin_spelling_multi(LANG_JAVA, node->value);
                if (bs && node->child_count >= 2) {
                    sb_append(sb, "%s", bs->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_java(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(fn)) {
                sb_append(sb, "_subPrint(");
                if (node->child_count > 0) generate_expr_java(sb, node->children[0]);
                sb_append(sb, ")");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", fn);
                } else {
                    generate_expr_java(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_java(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }
        case AST_ARRAY_LITERAL: {
            /* List.of() is immutable, so push() and a[i] = v both threw at
               run time. An ArrayList can be changed, which is what a SUB
               array is. */
            DataType elem = infer_elem_type(node);
            sb_append(sb, "new java.util.ArrayList<%s>(java.util.List.of(",
                      java_elem_box(elem));
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                /* A bare 1 is an int; the list holds Long. */
                if (elem == TYPE_INT)        sb_append(sb, "(Long)(long)(");
                else if (elem == TYPE_FLOAT) sb_append(sb, "(Double)(double)(");
                else                         sb_append(sb, "(");
                generate_expr_java(sb, node->children[i]);
                sb_append(sb, ")");
            }
            sb_append(sb, "))");
            break;
        }
        case AST_OBJECT_LITERAL:
            sb_append(sb, "java.util.Map.of(");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\", ", pair->value ? pair->value : "");
                generate_expr_java(sb, pair->right);
            }
            sb_append(sb, ")");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_java(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            sb_append(sb, "_subAt(");
            generate_expr_java(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        default:
            fprintf(stderr, "Warning: Unsupported expression node %d in Java generator\n", node->type);
            break;
    }
}

static void generate_node_java(StringBuilder *sb, ASTNode *node, int indent);

/* See gen_clause_python. */
static void gen_clause_java(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_code(sb, indent);
        sb_append(sb, "do {\n");
        indent++;
    }
    generate_node_java(sb, clause->body, indent);
    if (loop) {
        indent_code(sb, indent - 1);
        sb_append(sb, "} while (false);\n");
    }
}

static void generate_node_java(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        /*  A dropped `break` turns a loop that terminates into one
           that does not, so this must never fall through to the default. */

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "do {\n");
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "} while (");
            generate_expr_java(sb, node->condition);
            sb_append(sb, ");\n");
            break;
        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break;\n");
            break;

        case AST_SWITCH_STMT: {
            static int sw_java = 0;
            int id = sw_java++;
            ASTNode *deflt = switch_default_clause(node);
            int is_str = infer_expr_type(node->condition) == TYPE_STRING;
            int emitted = 0;
            (void)is_str;

            indent_code(sb, indent);
            sb_append(sb, "{\n");
            indent_code(sb, indent + 1);
            sb_append(sb, "var _sw%d = ", id);
            generate_expr_java(sb, node->condition);
            sb_append(sb, ";\n");

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_code(sb, indent + 1);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    if (is_str) {
                        sb_append(sb, "_sw%d.equals(", id);
                        generate_expr_java(sb, c->children[j]);
                        sb_append(sb, ")");
                    } else {
                        sb_append(sb, "_sw%d == (", id);
                        generate_expr_java(sb, c->children[j]);
                        sb_append(sb, ")");
                    }
                }
                sb_append(sb, ") {\n");
                gen_clause_java(sb, c, indent + 2);
                emitted = 1;
            }
            if (deflt) {
                if (emitted) {
                    /* `else if (true)` rather than `else`. Java rejects a
                       statement it can prove unreachable, and a chain whose
                       every branch returns makes whatever follows the switch
                       exactly that -- so `fn f() { switch ... } return "x" }`
                       failed to compile. JLS 14.21 deliberately ignores a
                       constant condition on an `if`, so this is reachable
                       again while running in precisely the same cases. */
                    indent_code(sb, indent + 1);
                    sb_append(sb, "} else if (true) {\n");
                    gen_clause_java(sb, deflt, indent + 2);
                } else {
                    gen_clause_java(sb, deflt, indent + 1);
                }
            }
            if (emitted) {
                indent_code(sb, indent + 1);
                sb_append(sb, "}\n");
            }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        }

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue;\n");
            break;

        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
        case AST_CONST_DECL: {
            /* `var x = 0` gives Java an int, and SUB integers are 64-bit -
               so `total = total + x` where x came out of a list of Longs
               would not compile, and anything past 2^31 would have wrapped
               silently. Name the inferred type instead of leaving it to
               var; arrays keep var, since the literal carries its own. */
            indent_code(sb, indent);
            const char *kw = (node->type == AST_CONST_DECL) ? "final " : "";
            DataType dt = node->data_type;
            if (dt == TYPE_UNKNOWN || dt == TYPE_AUTO)
                dt = infer_expr_type(node->right);
            const char *name = node->value ? node->value
                             : (node->type == AST_CONST_DECL ? "CONST" : "var");
            scope_push(node->value);
            if (globals_is_decl(&g_globals, node)) {
                /* Declared as a static field above; only the initializer is
                   left, as an assignment. */
                sb_append(sb, "%s = ", name);
                if (node->right) generate_expr_java(sb, node->right);
                else sb_append(sb, dt == TYPE_STRING ? "null" : "0");
            } else if (dt == TYPE_INT || dt == TYPE_FLOAT ||
                dt == TYPE_BOOL || dt == TYPE_STRING) {
                sb_append(sb, "%s%s %s = ", kw, java_type(dt), name);
                if (dt == TYPE_INT || dt == TYPE_FLOAT)
                    sb_append(sb, "(%s)(", java_type(dt));
                if (node->right) generate_expr_java(sb, node->right);
                else sb_append(sb, "0");
                if (dt == TYPE_INT || dt == TYPE_FLOAT) sb_append(sb, ")");
            } else {
                sb_append(sb, "%svar %s = ", kw, name);
                if (node->right) generate_expr_java(sb, node->right);
                else sb_append(sb, "null");
            }
            sb_append(sb, ";\n");
            break;
        }
            
        case AST_FUNCTION_DECL: {
            /* A method is its own scope: names from another one are not
               visible here, and the parameters are. */
            int scope_mark  = g_nscope;
            int rename_mark = g_nrename;
            g_nscope = g_nrename = 0;

            sb_append(sb, "\n");
            indent_code(sb, indent);
            g_java_fn_type = node->data_type;
            sb_append(sb, "public static %s %s(", java_type(node->data_type),
                      node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "%s %s", java_param_type(node->children[i]),
                              node->children[i]->value ? node->children[i]->value : "arg");
                    scope_push(node->children[i]->value);
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) generate_node_java(sb, node->body, indent + 1);

            /* Java is alone in checking both that a value-returning method
               cannot fall off its end and that no statement is unreachable.
               A method ending in a switch whose every case returns satisfies
               neither on its own -- see the `else if (true)` above, which
               keeps anything written after the switch reachable and in doing
               so makes the method look as though it can complete. A fallback
               return settles it, and is only written when the body does not
               already end in one, so it can never be the unreachable
               statement Java would reject. */
            if (node->data_type != TYPE_VOID && stmt_completes(node->body)) {
                indent_code(sb, indent + 1);
                switch (node->data_type) {
                case TYPE_INT:    sb_append(sb, "return 0L;\n");          break;
                case TYPE_FLOAT:  sb_append(sb, "return Double.NaN;\n");  break;
                case TYPE_BOOL:   sb_append(sb, "return false;\n");       break;
                default:          sb_append(sb, "return null;\n");        break;
                }
            }
            indent_code(sb, indent);
            sb_append(sb, "}\n");

            g_nscope  = scope_mark;
            g_nrename = rename_mark;
            break;
        }
            
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expr_java(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if (");
                    generate_expr_java(sb, node->right->condition);
                    sb_append(sb, ") {\n");
                    generate_node_java(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        sb_append(sb, " else {\n");
                        generate_node_java(sb, node->right->right, indent + 1);
                        indent_code(sb, indent);
                        sb_append(sb, "}");
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_java(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                }
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FOR_STMT: {
            int scope_mark  = g_nscope;
            int rename_mark = g_nrename;
            indent_code(sb, indent);
            {
                const char *var =
                    shadow_declare_loop_var(node->value ? node->value : "i");
                if (node->children && node->child_count > 0 &&
                    node->children[0]->type == AST_RANGE_EXPR) {
                    ASTNode *range = node->children[0];
                    sb_append(sb, "for (int %s = ", var);
                    if (range->right) {
                        if (range->left) generate_expr_java(sb, range->left);
                        else sb_append(sb, "0");
                        sb_append(sb, "; %s < ", var);
                        generate_expr_java(sb, range->right);
                    } else if (range->left) {
                        sb_append(sb, "0; %s < ", var);
                        generate_expr_java(sb, range->left);
                    } else {
                        sb_append(sb, "0; %s < 10", var);
                    }
                    sb_append(sb, "; %s++) {\n", var);
                } else if (node->condition) {
                    /* `for x in <collection>` had no branch here at all, so
                       it fell through to the 0..9 fallback below and every
                       such loop silently iterated ten times over nothing. */
                    sb_append(sb, "for (var %s : ", var);
                    generate_expr_java(sb, node->condition);
                    sb_append(sb, ") {\n");
                } else {
                    sb_append(sb, "for (int %s = 0; %s < 10; %s++) {\n", var, var, var);
                }
            }
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            g_nscope  = scope_mark;
            g_nrename = rename_mark;
            break;
        }

        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expr_java(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                /* Java cannot put null in a double. A function that returns
                   both null and a number is represented with NaN as the
                   "no value" sentinel (see codegen_infer.c). */
                if (expr_is_null_literal(node->right) &&
                    (g_java_fn_type == TYPE_FLOAT || g_java_fn_type == TYPE_INT))
                    sb_append(sb, "Double.NaN");
                else
                    generate_expr_java(sb, node->right);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_CALL_EXPR:
            indent_code(sb, indent);
            generate_expr_java(sb, node);
            sb_append(sb, ";\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                /* _subAt returns a value, not a slot, so a write cannot go
                   through it the way it can in C++. */
                DataType el = infer_elem_type(node->left->left);
                sb_append(sb, "_subPut(");
                generate_expr_java(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_java(sb, node->left->right);
                sb_append(sb, ", ");
                if (el == TYPE_INT)        sb_append(sb, "(Long)(long)(");
                else if (el == TYPE_FLOAT) sb_append(sb, "(Double)(double)(");
                else                       sb_append(sb, "(");
                generate_expr_java(sb, node->right);
                sb_append(sb, "));\n");
                break;
            }
            generate_expr_java(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_java(sb, node->right);
            sb_append(sb, ";\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent);
            }
            break;
            
        default:
            fprintf(stderr, "Warning: Unsupported AST node %d in Java generator\n", node->type);
            break;
    }
}

char* codegen_java(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    globals_collect(&g_globals, ast);
    
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");

    char *embedded = extract_embedded_code(source, "java");
    if (embedded) {
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }

    sb_append(sb, "public class SubProgram {\n");
    /* SUB prints booleans as true/false and drops the trailing .0 on a float
       with no fractional part. Java's println does neither, so route printing
       through a helper to keep output identical to the interpreter's. */
    /* printf's %g: six significant digits, trailing zeros dropped, and
       exponent form outside 1e-4 .. 1e+6. Java's String.format("%g") keeps
       the trailing zeros and picks the notation by different rules, so the
       shape has to be built by hand to agree with the interpreter. */
    sb_append(sb, "\n    static String _subFmt(double d) {\n");
    sb_append(sb, "        if (Double.isNaN(d)) return \"nan\";\n");
    sb_append(sb, "        if (Double.isInfinite(d)) return d < 0 ? \"-inf\" : \"inf\";\n");
    sb_append(sb, "        if (d == 0) return (1 / d < 0) ? \"-0\" : \"0\";\n");
    sb_append(sb, "        java.math.BigDecimal b = new java.math.BigDecimal(d)\n");
    sb_append(sb, "            .round(new java.math.MathContext(6));\n");
    sb_append(sb, "        int exp = b.precision() - b.scale() - 1;\n");
    sb_append(sb, "        if (exp < -4 || exp >= 6) {\n");
    sb_append(sb, "            String m = b.movePointLeft(exp).stripTrailingZeros().toPlainString();\n");
    sb_append(sb, "            int a = Math.abs(exp);\n");
    sb_append(sb, "            return m + \"e\" + (exp < 0 ? \"-\" : \"+\")\n");
    sb_append(sb, "                     + (a < 10 ? \"0\" : \"\") + a;\n");
    sb_append(sb, "        }\n");
    sb_append(sb, "        return b.stripTrailingZeros().toPlainString();\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static String _subStr(Object v) {\n");
    sb_append(sb, "        if (v instanceof Double) return _subFmt((Double) v);\n");
    sb_append(sb, "        if (v instanceof Float) return _subFmt((Float) v);\n");
    /* SUB spells a list [a, b, c] with no quotes; Java's toString spells it
       the same way for numbers but not for anything needing conversion. */
    sb_append(sb, "        if (v instanceof java.util.List) {\n");
    sb_append(sb, "            StringBuilder b = new StringBuilder(\"[\");\n");
    sb_append(sb, "            java.util.List<?> l = (java.util.List<?>) v;\n");
    sb_append(sb, "            for (int i = 0; i < l.size(); i++) {\n");
    sb_append(sb, "                if (i > 0) b.append(\", \");\n");
    sb_append(sb, "                b.append(_subStr(l.get(i)));\n");
    sb_append(sb, "            }\n");
    sb_append(sb, "            return b.append(\"]\").toString();\n        }\n");
    sb_append(sb, "        return String.valueOf(v);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static void _subPrint(Object v) {\n");
    sb_append(sb, "        System.out.println(_subStr(v));\n");
    sb_append(sb, "    }\n");

    /* Arrays. Generic, so one set covers every element type. Out of range
       and popping an empty list report the interpreter's message and exit
       70 rather than throwing Java's own exception. */
    sb_append(sb, "\n    static int _subIdx(int n, long i) {\n");
    sb_append(sb, "        if (i < 0) i += n;          // a[-1] is the last\n");
    sb_append(sb, "        if (i < 0 || i >= n) _subDieIndex(i, n);\n");
    sb_append(sb, "        return (int) i;\n    }\n");
    sb_append(sb, "\n    static <T> T _subAt(java.util.List<T> a, long i) "
                  "{ return a.get(_subIdx(a.size(), i)); }\n");
    sb_append(sb, "\n    static <T> void _subPut(java.util.List<T> a, long i, T v) "
                  "{ a.set(_subIdx(a.size(), i), v); }\n");
    sb_append(sb, "\n    static <T> void _subPush(java.util.List<T> a, T v) "
                  "{ a.add(v); }\n");
    sb_append(sb, "\n    static <T> T _subPop(java.util.List<T> a) {\n");
    sb_append(sb, "        if (a.isEmpty()) _subDie(\"pop from empty array\");\n");
    sb_append(sb, "        return a.remove(a.size() - 1);\n    }\n");
    sb_append(sb, "\n    static void _subDieIndex(long i, int n) {\n");
    sb_append(sb, "        System.err.println(\"RuntimeError: array index \" + i\n");
    sb_append(sb, "            + \" out of bounds [0, \" + n + \")\");\n");
    sb_append(sb, "        System.exit(70);\n    }\n");

    /* Integer division by zero throws ArithmeticException in Java; SUB
       reports a runtime error and exits 70, the same as the interpreter. */
    sb_append(sb, "\n    static void _subDie(String msg) {\n");
    sb_append(sb, "        System.err.println(\"RuntimeError: \" + msg);\n");
    sb_append(sb, "        System.exit(70);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static long _subIdiv(long a, long b) {\n");
    sb_append(sb, "        if (b == 0) _subDie(\"division by zero\");\n");
    sb_append(sb, "        return a / b;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static long _subMod(long a, long b) {\n");
    sb_append(sb, "        if (b == 0) _subDie(\"modulo by zero\");\n");
    sb_append(sb, "        return a %% b;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static double _subFdiv(double a, double b) {\n");
    sb_append(sb, "        if (b == 0) _subDie(\"division by zero\");\n");
    sb_append(sb, "        return a / b;\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static double _subFmod(double a, double b) {\n");
    sb_append(sb, "        if (b == 0) _subDie(\"modulo by zero\");\n");
    sb_append(sb, "        return a %% b;\n");
    sb_append(sb, "    }\n");

    /* Math.round() breaks ties toward +Infinity; SUB breaks them away from
       zero, so -2.5 must round to -3 rather than -2. */
    sb_append(sb, "\n    static long _subRound(double x) {\n");
    sb_append(sb, "        return x < 0 ? -Math.round(-x) : Math.round(x);\n");
    sb_append(sb, "    }\n");

    /* Two-pass approach: functions and non-function statements separated */
    StringBuilder *main_sb = sb_create();
    if (!main_sb) {
        sb_free(sb);
        return NULL;
    }

    if (ast->type == AST_PROGRAM) {
        /* Pass 0: the top-level variables, as static fields, so that the
           methods below can name them. Uninitialized: the initializer stays
           in main() where it was written; see codegen_globals.h. */
        for (int i = 0; i < g_globals.count; i++) {
            if (!globals_is_first(&g_globals, i)) continue;
            ASTNode *d = g_globals.decls[i];
            sb_append(sb, "    static %s %s;\n", java_decl_type(d), d->value);
        }

        /* Pass 1: emit function declarations as static methods */
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                /* Resolve identifiers in the body against this function's
                   parameters; see infer_enter_function(). */
                ASTNode *prev_fn = infer_enter_function(stmt);
                generate_node_java(sb, stmt, 1);
                infer_enter_function(prev_fn);
            }
        }
        /* Pass 2: collect all non-function statements for main */
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type != AST_FUNCTION_DECL) {
                generate_node_java(main_sb, stmt, 2);
            }
        }
    }

    sb_append(sb, "\n    public static void main(String[] args) {\n");
    sb_append(sb, "%s", main_sb->buffer);
    sb_append(sb, "    }\n");
    sb_append(sb, "}\n");

    sb_free(main_sb);
    return sb_to_string(sb);
}

/* ========================================
   SWIFT CODE GENERATOR - FULL AST
   ======================================== */

/* Swift reserves words that SUB does not, and `guard`, `repeat`, `where` and
   `defer` are all plausible variable names. Swift's own escape for exactly
   this is a backtick pair, which changes nothing but how the name parses. */
static const char *swift_ident(const char *name) {
    static const char *kw[] = {
        "associatedtype", "case", "catch", "class", "defer", "deinit", "do",
        "else", "enum", "extension", "fallthrough", "fileprivate", "for",
        "func", "guard", "if", "import", "in", "init", "inout", "internal",
        "let", "operator", "private", "protocol", "public", "repeat",
        "rethrows", "return", "self", "static", "struct", "subscript",
        "super", "switch", "throw", "throws", "try", "typealias", "var",
        "where", "while", "Any", "Protocol", "Self", "Type", "as", "is",
        "nil", "true", "false", "some", "any", NULL
    };
    static char buf[4][64];
    static int slot = 0;

    if (!name) return "v";
    for (int i = 0; kw[i]; i++) {
        if (strcmp(name, kw[i]) != 0) continue;
        /* A few live at once -- a declaration names one while its
           initialiser reads another -- so the buffers rotate. */
        slot = (slot + 1) % 4;
        snprintf(buf[slot], sizeof buf[0], "`%s`", name);
        return buf[slot];
    }
    return name;
}

static void generate_expr_swift(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else {
                sb_append(sb, "%s", node->value ? node->value : "nil");
            }
            break;
        case AST_IDENTIFIER: sb_append(sb, "%s", swift_ident(node->value)); break;
        case AST_ARRAY_LITERAL: {
            /* The element type is written out: an empty literal has nothing
               for Swift to infer from, and Int is 64-bit here as in SUB. */
            DataType elem = infer_elem_type(node);
            sb_append(sb, "[%s]([", swift_type(elem));
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_swift_as(sb, node->children[i], elem);
            }
            sb_append(sb, "])");
            break;
        }
        case AST_ARRAY_ACCESS:
            sb_append(sb, "_subAt(");
            generate_expr_swift(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_swift(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_BINARY_EXPR:
            /* `x == null` / `x != null` become NaN tests (see the return
               statement, where null is emitted as the NaN sentinel). */
            if (node->value && (strcmp(node->value, "==") == 0 ||
                                strcmp(node->value, "!=") == 0) &&
                (expr_is_null_literal(node->left) || expr_is_null_literal(node->right))) {
                ASTNode *val = expr_is_null_literal(node->left) ? node->right : node->left;
                DataType vt = infer_expr_type(val);
                if (vt == TYPE_FLOAT || vt == TYPE_INT) {
                    sb_append(sb, "%s(", strcmp(node->value, "!=") == 0 ? "!" : "");
                    generate_expr_swift(sb, val);
                    sb_append(sb, ").isNaN");
                    break;
                }
            }
            if (emit_special_binop(sb, node, LANG_SWIFT, generate_expr_swift)) break;
            {
                DataType lt = infer_expr_type(node->left);
                DataType rt = infer_expr_type(node->right);
                DataType want = (lt == TYPE_FLOAT || rt == TYPE_FLOAT)
                                    ? TYPE_FLOAT : TYPE_UNKNOWN;
                sb_append(sb, "("); generate_expr_swift_as(sb, node->left, want);
                sb_append(sb, " %s ", node->value ? node->value : "+");
                generate_expr_swift_as(sb, node->right, want); sb_append(sb, ")");
            }
            break;
        case AST_UNARY_EXPR:
            /* Without this case the operand vanished entirely, so abs(-7)
               generated an empty argument list. */
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_swift(sb, node->right ? node->right : node->left);
            break;
        case AST_CALL_EXPR:
            {
                /* Swift arrays are values, so mutating one needs inout. */
                if (node->value && node->child_count >= 1 && node->children[0] &&
                    infer_expr_type(node->children[0]) == TYPE_ARRAY) {
                    DataType el = infer_elem_type(node->children[0]);
                    if ((!strcmp(node->value, "push") ||
                         !strcmp(node->value, "append")) && node->child_count >= 2) {
                        sb_append(sb, "_subPush(&");
                        generate_expr_swift(sb, node->children[0]);
                        sb_append(sb, ", ");
                        generate_expr_swift_as(sb, node->children[1], el);
                        sb_append(sb, ")");
                        break;
                    }
                    if (!strcmp(node->value, "pop")) {
                        sb_append(sb, "_subPop(&");
                        generate_expr_swift(sb, node->children[0]);
                        sb_append(sb, ")");
                        break;
                    }
                    if (!strcmp(node->value, "len") || !strcmp(node->value, "length")) {
                        sb_append(sb, "(");
                        generate_expr_swift(sb, node->children[0]);
                        sb_append(sb, ").count");
                        break;
                    }
                }
                const BuiltinSpelling *bs = builtin_spelling(LANG_SWIFT, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_swift(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
                bs = builtin_spelling_multi(LANG_SWIFT, node->value);
                if (bs && node->child_count >= 2) {
                    sb_append(sb, "%s", bs->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_swift(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(node->value)) sb_append(sb, "_subPrint(");
            else if (node->value) sb_append(sb, "%s(", node->value);
            else { generate_expr_swift(sb, node->left); sb_append(sb, "("); }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                /* Match the callee's parameter type so an integer literal
                   lands in a Double slot as a float literal. */
                generate_expr_swift_as(sb, node->children[i],
                                       param_type_of(node->value, i));
            }
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_swift(StringBuilder *sb, ASTNode *node, int indent);

/* See gen_clause_python. */
static void gen_clause_swift(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_code(sb, indent);
        sb_append(sb, "repeat {\n");
        indent++;
    }
    generate_node_swift(sb, clause->body, indent);
    if (loop) {
        indent_code(sb, indent - 1);
        sb_append(sb, "} while false\n");
    }
}

static void generate_node_swift(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        /*  A dropped `break` turns a loop that terminates into one
           that does not, so this must never fall through to the default. */

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "repeat {\n");
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "} while ");
            generate_expr_swift(sb, node->condition);
            sb_append(sb, "\n");
            break;
        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break\n");
            break;

        case AST_SWITCH_STMT: {
            static int sw_swift = 0;
            int id = sw_swift++;
            ASTNode *deflt = switch_default_clause(node);
            int is_str = infer_expr_type(node->condition) == TYPE_STRING;
            int emitted = 0;
            (void)is_str;

            indent_code(sb, indent);
            sb_append(sb, "do {\n");
            indent_code(sb, indent + 1);
            sb_append(sb, "let _sw%d = ", id);
            generate_expr_swift(sb, node->condition);
            sb_append(sb, ";\n");
            /* A switch of nothing but a default never reads the scrutinee,
               and Swift warns about a `let` that is never used. */
            indent_code(sb, indent + 1);
            sb_append(sb, "_ = _sw%d\n", id);

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_code(sb, indent + 1);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    sb_append(sb, "_sw%d == (", id);
                    generate_expr_swift(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, ") {\n");
                gen_clause_swift(sb, c, indent + 2);
                emitted = 1;
            }
            if (deflt) {
                if (emitted) {
                    indent_code(sb, indent + 1);
                    sb_append(sb, "} else {\n");
                    gen_clause_swift(sb, deflt, indent + 2);
                } else {
                    /* Nothing for it to be the `else` of. A bare `{ }` here
                       is a block in most of these languages but a closure
                       expression in Swift, so the body is simply emitted. */
                    gen_clause_swift(sb, deflt, indent + 1);
                }
            }
            if (emitted) {
                indent_code(sb, indent + 1);
                sb_append(sb, "}\n");
            }
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        }

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue\n");
            break;

        case AST_PROGRAM: 
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            /* `const` is a declaration like any other. Without this case it
               fell through to `default:` and vanished, so a top-level const
               was declared and never given its value. */
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", swift_ident(node->value ? node->value : "v"));
            if (node->right) generate_expr_swift(sb, node->right); else sb_append(sb, "nil");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            g_swift_fn_type = node->data_type;
            sb_append(sb, "\nfunc %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "_ %s: %s", swift_ident(node->children[i]->value ? node->children[i]->value : "arg"),
                              swift_param_type(node->children[i]));
                }
            }
            sb_append(sb, ")");
            if (node->data_type != TYPE_VOID && node->data_type != TYPE_UNKNOWN)
                sb_append(sb, " -> %s", swift_type(node->data_type));
            sb_append(sb, " {\n");
            if (node->body) generate_node_swift(sb, node->body, indent + 1);
            sb_append(sb, "}\n"); break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            if (node->children && node->child_count > 0 &&
                node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for %s in ", swift_ident(node->value ? node->value : "i"));
                if (range->right) {
                    if (range->left) generate_expr_swift(sb, range->left);
                    else sb_append(sb, "0");
                    sb_append(sb, "..<");
                    generate_expr_swift(sb, range->right);
                } else if (range->left) {
                    sb_append(sb, "0..<");
                    generate_expr_swift(sb, range->left);
                } else {
                    sb_append(sb, "0..<10");
                }
                sb_append(sb, " {\n");
            } else if (node->condition) {
                sb_append(sb, "for %s in ", swift_ident(node->value ? node->value : "i"));
                generate_expr_swift(sb, node->condition);
                sb_append(sb, " {\n");
            } else {
                sb_append(sb, "for %s in 0..<10 {\n", swift_ident(node->value ? node->value : "i"));
            }
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n"); break;
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if ");
            generate_expr_swift(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if ");
                    generate_expr_swift(sb, node->right->condition);
                    sb_append(sb, " {\n");
                    generate_node_swift(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        ASTNode *branch = node->right->right;
                        while (branch && branch->type == AST_IF_STMT) {
                            sb_append(sb, " else if ");
                            generate_expr_swift(sb, branch->condition);
                            sb_append(sb, " {\n");
                            generate_node_swift(sb, branch->body, indent + 1);
                            indent_code(sb, indent);
                            sb_append(sb, "}");
                            branch = branch->right;
                        }
                        if (branch) {
                            sb_append(sb, " else {\n");
                            generate_node_swift(sb, branch, indent + 1);
                            indent_code(sb, indent);
                            sb_append(sb, "}");
                        }
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_swift(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                }
            }
            sb_append(sb, "\n");
            break;
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while ");
            generate_expr_swift(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_swift(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                /* A numeric type has no room for null; a function returning
                   both null and a number uses NaN as the sentinel. */
                if (expr_is_null_literal(node->right) &&
                    (g_swift_fn_type == TYPE_FLOAT || g_swift_fn_type == TYPE_INT))
                    sb_append(sb, "Double.nan");
                else
                    generate_expr_swift(sb, node->right);
            }
            sb_append(sb, "\n");
            break;
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "_subPut(&");
                generate_expr_swift(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_swift(sb, node->left->right);
                sb_append(sb, ", ");
                generate_expr_swift_as(sb, node->right,
                                       infer_elem_type(node->left->left));
                sb_append(sb, ")\n");
                break;
            }
            generate_expr_swift(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_swift(sb, node->right);
            sb_append(sb, "\n");
            break;
        case AST_BLOCK:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent); generate_expr_swift(sb, node); sb_append(sb, "\n"); break;
        default: break;
    }
}

char* codegen_swift(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    globals_collect(&g_globals, ast);
    sb_append(sb, "// Generated by SUB\n\n");
    /* Foundation supplies exit() and trimmingCharacters(in:). Swift does not
       complain about an import it does not end up needing. */
    sb_append(sb, "import Foundation\n\n");
    /* Foundation's String(format:) is C's, so %g is available directly. */
    sb_append(sb, "func _subFmt(_ d: Double) -> String "
                  "{ return String(format: \"%%g\", d) }\n");
    sb_append(sb, "func _subStr(_ v: Any) -> String {\n");
    sb_append(sb, "    if let d = v as? Double { return _subFmt(d) }\n");
    sb_append(sb, "    if let b = v as? Bool { return b ? \"true\" : \"false\" }\n");
    /* String(describing:) quotes the strings inside an array; SUB does not. */
    sb_append(sb, "    if let a = v as? [Any] "
                  "{ return \"[\" + a.map { _subStr($0) }.joined(separator: \", \") + \"]\" }\n");
    sb_append(sb, "    return String(describing: v)\n}\n");
    sb_append(sb, "func _subIdx(_ n: Int, _ i: Int) -> Int {\n");
    sb_append(sb, "    let k = i < 0 ? i + n : i\n");
    sb_append(sb, "    if k < 0 || k >= n {\n");
    sb_append(sb, "        FileHandle.standardError.write((\"RuntimeError: array index \"\n");
    sb_append(sb, "            + String(k) + \" out of bounds [0, \" + String(n) + \")\\n\")\n");
    sb_append(sb, "            .data(using: .utf8)!)\n");
    sb_append(sb, "        exit(70)\n    }\n    return k\n}\n");
    sb_append(sb, "func _subAt<T>(_ a: [T], _ i: Int) -> T "
                  "{ return a[_subIdx(a.count, i)] }\n");
    sb_append(sb, "func _subPut<T>(_ a: inout [T], _ i: Int, _ v: T) "
                  "{ a[_subIdx(a.count, i)] = v }\n");
    sb_append(sb, "func _subPush<T>(_ a: inout [T], _ v: T) { a.append(v) }\n");
    sb_append(sb, "func _subPop<T>(_ a: inout [T]) -> T {\n");
    sb_append(sb, "    if a.isEmpty { _subDie(\"pop from empty array\") }\n");
    sb_append(sb, "    return a.removeLast()\n}\n");
    sb_append(sb, "func _subPrint(_ v: Any) { print(_subStr(v)) }\n\n");
    sb_append(sb, "func _subDie(_ msg: String) -> Never {\n");
    sb_append(sb, "    FileHandle.standardError.write("
                  "(\"RuntimeError: \" + msg + \"\\n\").data(using: .utf8)!)\n");
    sb_append(sb, "    exit(70)\n}\n\n");
    sb_append(sb, "func _subIdiv(_ a: Int, _ b: Int) -> Int {\n");
    sb_append(sb, "    if b == 0 { _subDie(\"division by zero\") }\n");
    sb_append(sb, "    return a / b\n}\n\n");
    sb_append(sb, "func _subMod(_ a: Int, _ b: Int) -> Int {\n");
    sb_append(sb, "    if b == 0 { _subDie(\"modulo by zero\") }\n");
    sb_append(sb, "    return a %% b\n}\n\n");
    sb_append(sb, "func _subFdiv(_ a: Double, _ b: Double) -> Double {\n");
    sb_append(sb, "    if b == 0 { _subDie(\"division by zero\") }\n");
    sb_append(sb, "    return a / b\n}\n\n");
    sb_append(sb, "func _subFmod(_ a: Double, _ b: Double) -> Double {\n");
    sb_append(sb, "    if b == 0 { _subDie(\"modulo by zero\") }\n");
    sb_append(sb, "    return a.truncatingRemainder(dividingBy: b)\n}\n\n");
    char *e = extract_embedded_code(source, "swift");
    if (e) {
        sb_append(sb, "%s\n", e);
        free(e);
    }
    generate_node_swift(sb, ast, 0);
    return sb_to_string(sb);
}

/* ========================================
   KOTLIN CODE GENERATOR - FULL AST
   ======================================== */

static void generate_expr_kotlin(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->data_type == TYPE_INT && node->value) {
                /* SUB integers are 64-bit, so parameters and returns are Long.
                   Kotlin does not widen an Int literal to Long implicitly, so
                   a bare `2` fails to type-check against a Long parameter. */
                sb_append(sb, "%sL", node->value);
            } else {
                sb_append(sb, "%s", node->value ? node->value : "null");
            }
            break;
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_ARRAY_LITERAL: {
            DataType elem = infer_elem_type(node);
            sb_append(sb, "mutableListOf<%s>(", kotlin_elem_type(elem));
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_kotlin_as(sb, node->children[i], elem);
            }
            sb_append(sb, ")");
            break;
        }
        case AST_ARRAY_ACCESS:
            sb_append(sb, "_subAt(");
            generate_expr_kotlin(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_kotlin(sb, node->right);
            sb_append(sb, ")");
            break;
        case AST_BINARY_EXPR:
            /* `x == null` / `x != null` become NaN tests (see the return
               statement, where null is emitted as the NaN sentinel). */
            if (node->value && (strcmp(node->value, "==") == 0 ||
                                strcmp(node->value, "!=") == 0) &&
                (expr_is_null_literal(node->left) || expr_is_null_literal(node->right))) {
                ASTNode *val = expr_is_null_literal(node->left) ? node->right : node->left;
                DataType vt = infer_expr_type(val);
                if (vt == TYPE_FLOAT || vt == TYPE_INT) {
                    sb_append(sb, "%s(", strcmp(node->value, "!=") == 0 ? "!" : "");
                    generate_expr_kotlin(sb, val);
                    sb_append(sb, ").isNaN()");
                    break;
                }
            }
            if (emit_special_binop(sb, node, LANG_KOTLIN, generate_expr_kotlin)) break;
            {
                DataType lt = infer_expr_type(node->left);
                DataType rt = infer_expr_type(node->right);
                DataType want = (lt == TYPE_FLOAT || rt == TYPE_FLOAT)
                                    ? TYPE_FLOAT : TYPE_UNKNOWN;
                sb_append(sb, "("); generate_expr_kotlin_as(sb, node->left, want);
                sb_append(sb, " %s ", node->value ? node->value : "+");
                generate_expr_kotlin_as(sb, node->right, want); sb_append(sb, ")");
            }
            break;
        case AST_UNARY_EXPR:
            /* Without this case the operand vanished entirely, so abs(-7)
               generated an empty argument list. */
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_kotlin(sb, node->right ? node->right : node->left);
            break;
        case AST_CALL_EXPR:
            {
                /* Ahead of the table, whose len() spells String.length. */
                if (node->value && node->child_count >= 1 && node->children[0] &&
                    infer_expr_type(node->children[0]) == TYPE_ARRAY) {
                    DataType el = infer_elem_type(node->children[0]);
                    if ((!strcmp(node->value, "push") ||
                         !strcmp(node->value, "append")) && node->child_count >= 2) {
                        sb_append(sb, "_subPush(");
                        generate_expr_kotlin(sb, node->children[0]);
                        sb_append(sb, ", ");
                        generate_expr_kotlin_as(sb, node->children[1], el);
                        sb_append(sb, ")");
                        break;
                    }
                    if (!strcmp(node->value, "pop")) {
                        sb_append(sb, "_subPop(");
                        generate_expr_kotlin(sb, node->children[0]);
                        sb_append(sb, ")");
                        break;
                    }
                    if (!strcmp(node->value, "len") || !strcmp(node->value, "length")) {
                        sb_append(sb, "(");
                        generate_expr_kotlin(sb, node->children[0]);
                        sb_append(sb, ").size.toLong()");
                        break;
                    }
                }
                const BuiltinSpelling *bs = builtin_spelling(LANG_KOTLIN, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_kotlin(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
                bs = builtin_spelling_multi(LANG_KOTLIN, node->value);
                if (bs && node->child_count >= 2) {
                    sb_append(sb, "%s", bs->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_kotlin(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(node->value)) sb_append(sb, "_subPrint(");
            else if (node->value) sb_append(sb, "%s(", node->value);
            else { generate_expr_kotlin(sb, node->left); sb_append(sb, "("); }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                /* Match the callee's parameter type so an integer literal
                   lands in a Double slot as a float literal, not as `0L`. */
                generate_expr_kotlin_as(sb, node->children[i],
                                        param_type_of(node->value, i));
            }
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_kotlin(StringBuilder *sb, ASTNode *node, int indent);

/* See gen_clause_python. */
static void gen_clause_kotlin(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_code(sb, indent);
        sb_append(sb, "do {\n");
        indent++;
    }
    generate_node_kotlin(sb, clause->body, indent);
    if (loop) {
        indent_code(sb, indent - 1);
        sb_append(sb, "} while (false)\n");
    }
}

static void generate_node_kotlin(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        /*  A dropped `break` turns a loop that terminates into one
           that does not, so this must never fall through to the default. */

        case AST_DO_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "do {\n");
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "} while (");
            generate_expr_kotlin(sb, node->condition);
            sb_append(sb, ")\n");
            break;
        case AST_BREAK_STMT:
            indent_code(sb, indent);
            sb_append(sb, "break\n");
            break;

        case AST_SWITCH_STMT: {
            /* No enclosing `run { }`: it is an inline lambda, and Kotlin
               rejects a `break` or `continue` that crosses one -- a case body
               that continues the loop around the switch would not compile.
               The scrutinee is named uniquely instead, so it needs no scope
               of its own. */
            static int sw_kotlin = 0;
            int id = sw_kotlin++;
            ASTNode *deflt = switch_default_clause(node);
            int emitted = 0;

            indent_code(sb, indent);
            sb_append(sb, "val _sw%d = ", id);
            generate_expr_kotlin(sb, node->condition);
            sb_append(sb, "\n");

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_code(sb, indent);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    sb_append(sb, "_sw%d == (", id);
                    generate_expr_kotlin(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, ") {\n");
                gen_clause_kotlin(sb, c, indent + 1);
                emitted = 1;
            }
            if (deflt) {
                if (emitted) {
                    indent_code(sb, indent);
                    sb_append(sb, "} else {\n");
                    gen_clause_kotlin(sb, deflt, indent + 1);
                } else {
                    /* Nothing to be an `else` of: the default is all there
                       is, so it is simply the code that runs. */
                    gen_clause_kotlin(sb, deflt, indent);
                }
            }
            if (emitted) {
                indent_code(sb, indent);
                sb_append(sb, "}\n");
            }
            break;
        }

        case AST_CONTINUE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "continue\n");
            break;

        case AST_PROGRAM: 
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            /* `const` is a declaration like any other. Without this case it
               fell through to `default:` and vanished: io.sb's `const ESC`
               got its property but never its value, so every colour helper
               produced an escape sequence with the escape missing. */
            indent_code(sb, indent);
            /* A top-level declaration was already written as a top-level
               property, so here only its initializer is left. */
            if (globals_is_decl(&g_globals, node))
                sb_append(sb, "%s = ", node->value);
            else
                sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_kotlin(sb, node->right); else sb_append(sb, "null");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            g_kotlin_fn_type = node->data_type;
            sb_append(sb, "\nfun %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "%s: %s", node->children[i]->value ? node->children[i]->value : "arg",
                              kotlin_param_type(node->children[i]));
                }
            }
            sb_append(sb, ")");
            if (node->data_type != TYPE_VOID && node->data_type != TYPE_UNKNOWN)
                sb_append(sb, ": %s", kotlin_type(node->data_type));
            sb_append(sb, " {\n");
            if (node->body) generate_node_kotlin(sb, node->body, indent + 1);
            sb_append(sb, "}\n"); break;
        case AST_FOR_STMT:
            indent_code(sb, indent);
            if (node->children && node->child_count > 0 &&
                node->children[0]->type == AST_RANGE_EXPR) {
                ASTNode *range = node->children[0];
                sb_append(sb, "for (%s in ", node->value ? node->value : "i");
                if (range->right) {
                    if (range->left) generate_expr_kotlin(sb, range->left);
                    else sb_append(sb, "0");
                    sb_append(sb, " until ");
                    generate_expr_kotlin(sb, range->right);
                } else if (range->left) {
                    sb_append(sb, "0 until ");
                    generate_expr_kotlin(sb, range->left);
                } else {
                    sb_append(sb, "0 until 10");
                }
                sb_append(sb, ") {\n");
            } else if (node->condition) {
                sb_append(sb, "for (%s in ", node->value ? node->value : "i");
                generate_expr_kotlin(sb, node->condition);
                sb_append(sb, ") {\n");
            } else {
                sb_append(sb, "for (%s in 0 until 10) {\n", node->value ? node->value : "i");
            }
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n"); break;
        case AST_IF_STMT:
            indent_code(sb, indent);
            sb_append(sb, "if (");
            generate_expr_kotlin(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}");
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    sb_append(sb, " else if (");
                    generate_expr_kotlin(sb, node->right->condition);
                    sb_append(sb, ") {\n");
                    generate_node_kotlin(sb, node->right->body, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                    if (node->right->right) {
                        ASTNode *branch = node->right->right;
                        while (branch && branch->type == AST_IF_STMT) {
                            sb_append(sb, " else if (");
                            generate_expr_kotlin(sb, branch->condition);
                            sb_append(sb, ") {\n");
                            generate_node_kotlin(sb, branch->body, indent + 1);
                            indent_code(sb, indent);
                            sb_append(sb, "}");
                            branch = branch->right;
                        }
                        if (branch) {
                            sb_append(sb, " else {\n");
                            generate_node_kotlin(sb, branch, indent + 1);
                            indent_code(sb, indent);
                            sb_append(sb, "}");
                        }
                    }
                } else {
                    sb_append(sb, " else {\n");
                    generate_node_kotlin(sb, node->right, indent + 1);
                    indent_code(sb, indent);
                    sb_append(sb, "}");
                }
            }
            sb_append(sb, "\n");
            break;
        case AST_WHILE_STMT:
            indent_code(sb, indent);
            sb_append(sb, "while (");
            generate_expr_kotlin(sb, node->condition);
            sb_append(sb, ") {\n");
            generate_node_kotlin(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
        case AST_RETURN_STMT:
            indent_code(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                /* A numeric type has no room for null; a function returning
                   both null and a number uses NaN as the sentinel. */
                if (expr_is_null_literal(node->right) &&
                    (g_kotlin_fn_type == TYPE_FLOAT || g_kotlin_fn_type == TYPE_INT))
                    sb_append(sb, "Double.NaN");
                else
                    generate_expr_kotlin(sb, node->right);
            }
            sb_append(sb, "\n");
            break;
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "_subPut(");
                generate_expr_kotlin(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_kotlin(sb, node->left->right);
                sb_append(sb, ", ");
                generate_expr_kotlin_as(sb, node->right,
                                        infer_elem_type(node->left->left));
                sb_append(sb, ")\n");
                break;
            }
            generate_expr_kotlin(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_kotlin(sb, node->right);
            sb_append(sb, "\n");
            break;
        case AST_BLOCK:
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_CALL_EXPR:
            indent_code(sb, indent); generate_expr_kotlin(sb, node); sb_append(sb, "\n"); break;
        default: break;
    }
}

char* codegen_kotlin(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    globals_collect(&g_globals, ast);
    sb_append(sb, "// Generated by SUB\n\n");
    /* kotlin.math.round() breaks ties toward +Infinity; SUB breaks them
       away from zero. */
    /* printf's %g, as the interpreter prints floats. */
    sb_append(sb, "fun _subFmt(d: Double): String {\n");
    sb_append(sb, "    if (d.isNaN()) return \"nan\"\n");
    sb_append(sb, "    if (d.isInfinite()) return if (d < 0) \"-inf\" else \"inf\"\n");
    sb_append(sb, "    if (d == 0.0) return if (1 / d < 0) \"-0\" else \"0\"\n");
    sb_append(sb, "    val b = java.math.BigDecimal(d).round(java.math.MathContext(6))\n");
    sb_append(sb, "    val exp = b.precision() - b.scale() - 1\n");
    sb_append(sb, "    if (exp < -4 || exp >= 6) {\n");
    sb_append(sb, "        val m = b.movePointLeft(exp).stripTrailingZeros().toPlainString()\n");
    sb_append(sb, "        val a = kotlin.math.abs(exp)\n");
    sb_append(sb, "        return m + \"e\" + (if (exp < 0) \"-\" else \"+\") +\n");
    sb_append(sb, "               (if (a < 10) \"0\" else \"\") + a\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "    return b.stripTrailingZeros().toPlainString()\n}\n\n");
    sb_append(sb, "fun _subStr(v: Any?): String = when (v) {\n");
    sb_append(sb, "    null -> \"null\"\n");
    sb_append(sb, "    is Double -> _subFmt(v)\n");
    sb_append(sb, "    is Float -> _subFmt(v.toDouble())\n");
    sb_append(sb, "    is List<*> -> v.joinToString(\", \", \"[\", \"]\") "
                  "{ _subStr(it) }\n");
    sb_append(sb, "    else -> v.toString()\n}\n\n");
    sb_append(sb, "fun _subIdx(n: Int, i: Long): Int {\n");
    sb_append(sb, "    val k = if (i < 0) i + n else i\n");
    sb_append(sb, "    if (k < 0 || k >= n) {\n");
    sb_append(sb, "        System.err.println(\"RuntimeError: array index \" + k +\n");
    sb_append(sb, "            \" out of bounds [0, \" + n + \")\")\n");
    sb_append(sb, "        kotlin.system.exitProcess(70)\n    }\n");
    sb_append(sb, "    return k.toInt()\n}\n\n");
    sb_append(sb, "fun <T> _subAt(a: MutableList<T>, i: Long): T = a[_subIdx(a.size, i)]\n");
    sb_append(sb, "fun <T> _subPut(a: MutableList<T>, i: Long, v: T) "
                  "{ a[_subIdx(a.size, i)] = v }\n");
    sb_append(sb, "fun <T> _subPush(a: MutableList<T>, v: T) { a.add(v) }\n");
    sb_append(sb, "fun <T> _subPop(a: MutableList<T>): T {\n");
    sb_append(sb, "    if (a.isEmpty()) _subDie(\"pop from empty array\")\n");
    sb_append(sb, "    return a.removeAt(a.size - 1)\n}\n\n");
    sb_append(sb, "fun _subPrint(v: Any?) = println(_subStr(v))\n\n");
    sb_append(sb, "fun _subDie(msg: String): Nothing {\n");
    sb_append(sb, "    System.err.println(\"RuntimeError: \" + msg)\n");
    sb_append(sb, "    kotlin.system.exitProcess(70)\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "fun _subIdiv(a: Long, b: Long): Long {\n");
    sb_append(sb, "    if (b == 0L) _subDie(\"division by zero\")\n");
    sb_append(sb, "    return a / b\n}\n\n");
    sb_append(sb, "fun _subMod(a: Long, b: Long): Long {\n");
    sb_append(sb, "    if (b == 0L) _subDie(\"modulo by zero\")\n");
    sb_append(sb, "    return a %% b\n}\n\n");
    sb_append(sb, "fun _subFdiv(a: Double, b: Double): Double {\n");
    sb_append(sb, "    if (b == 0.0) _subDie(\"division by zero\")\n");
    sb_append(sb, "    return a / b\n}\n\n");
    sb_append(sb, "fun _subFmod(a: Double, b: Double): Double {\n");
    sb_append(sb, "    if (b == 0.0) _subDie(\"modulo by zero\")\n");
    sb_append(sb, "    return a %% b\n}\n\n");
    /* kotlin.math.round breaks ties towards the even integer, so round(2.5)
       came out 2 where SUB says 3. Half away from zero, spelled with floor
       and ceil, is what C's round() does and what every other backend does. */
    sb_append(sb, "fun _subRound(x: Double): Long =\n");
    sb_append(sb, "    if (x < 0) kotlin.math.ceil(x - 0.5).toLong() "
                  "else kotlin.math.floor(x + 0.5).toLong()\n\n");
    char *e = extract_embedded_code(source, "kotlin");
    if (e) {
        sb_append(sb, "%s\n", e);
        free(e);
    }

    /* Two-pass: functions at top-level, executable stmts in main() */
    StringBuilder *main_sb = sb_create();
    if (!main_sb) {
        sb_free(sb);
        return NULL;
    }

    for (int i = 0; i < g_globals.count; i++) {
        if (!globals_is_first(&g_globals, i)) continue;
        ASTNode *d = g_globals.decls[i];
        const char *type, *zero;
        kotlin_decl_of(d, &type, &zero);
        sb_append(sb, "var %s: %s = %s\n", d->value, type, zero);
    }
    if (g_globals.count > 0) sb_append(sb, "\n");

    if (ast->type == AST_PROGRAM) {
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                /* Resolve identifiers in the body against this function's
                   parameters; see infer_enter_function(). */
                ASTNode *prev_fn = infer_enter_function(stmt);
                generate_node_kotlin(sb, stmt, 0);
                infer_enter_function(prev_fn);
            } else {
                generate_node_kotlin(main_sb, stmt, 1);
            }
        }
    }

    sb_append(sb, "\nfun main() {\n");
    sb_append(sb, "%s", main_sb->buffer);
    sb_append(sb, "}\n");

    sb_free(main_sb);
    return sb_to_string(sb);
}
/* ========================================
   CSS CODE GENERATOR
   ======================================== */
static void generate_css_ast(StringBuilder *sb, ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt; stmt = stmt->next) {
                generate_css_ast(sb, stmt);
            }
            break;
        case AST_UI_COMPONENT: {
            const char *comp = node->value ? node->value : "component";
            sb_append(sb, ".sub-%s {\n", comp);
            sb_append(sb, "    display: block;\n");
            sb_append(sb, "    box-sizing: border-box;\n");
            sb_append(sb, "    margin: 8px 0;\n");
            sb_append(sb, "    padding: 10px 14px;\n");
            sb_append(sb, "    font-family: inherit;\n");
            sb_append(sb, "}\n\n");
            if (node->body) generate_css_ast(sb, node->body);
            break;
        }
        case AST_CALL_EXPR:
            if (node->left && node->left->type == AST_MEMBER_ACCESS) {
                const char *m = node->left->value;
                sb_append(sb, ".ui-%s {\n", m ? m : "element");
                sb_append(sb, "    display: inline-block;\n");
                sb_append(sb, "    padding: 6px 12px;\n");
                sb_append(sb, "}\n\n");
            }
            break;
        default:
            break;
    }
}

char* codegen_css(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;
    sb_append(sb, "/* Generated CSS by SUB Compiler */\n\n");
    sb_append(sb, ":root {\n");
    sb_append(sb, "    --font-family: system-ui, -apple-system, sans-serif;\n");
    sb_append(sb, "}\n\n");
    sb_append(sb, "body {\n");
    sb_append(sb, "    font-family: var(--font-family);\n");
    sb_append(sb, "    margin: 0;\n");
    sb_append(sb, "    padding: 20px;\n");
    sb_append(sb, "}\n\n");
    
    char *embedded = extract_embedded_code(source, "css");
    if (embedded) {
        sb_append(sb, "/* Embedded CSS */\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }
    
    generate_css_ast(sb, ast);
    return sb_to_string(sb);
}

/* ========================================
   ASSEMBLY (x86-64 NASM) CODE GENERATOR
   ======================================== */
static void generate_asm_expr(StringBuilder *sb, ASTNode *node, int *str_lbl, StringBuilder *data_sb) {
    if (!node) return;
    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                int lbl = (*str_lbl)++;
                sb_append(data_sb, "    str_%d: db \"%s\", 0\n", lbl, node->value ? node->value : "");
                sb_append(sb, "    lea rax, [rel str_%d]\n", lbl);
            } else {
                sb_append(sb, "    mov rax, %s\n", node->value ? node->value : "0");
            }
            break;
        case AST_IDENTIFIER:
            sb_append(sb, "    mov rax, [rel var_%s]\n", node->value ? node->value : "x");
            break;
        case AST_BINARY_EXPR:
            generate_asm_expr(sb, node->left, str_lbl, data_sb);
            sb_append(sb, "    push rax\n");
            generate_asm_expr(sb, node->right, str_lbl, data_sb);
            sb_append(sb, "    mov rbx, rax\n");
            sb_append(sb, "    pop rax\n");
            if (node->value && strcmp(node->value, "+") == 0) {
                sb_append(sb, "    add rax, rbx\n");
            } else if (node->value && strcmp(node->value, "-") == 0) {
                sb_append(sb, "    sub rax, rbx\n");
            } else if (node->value && strcmp(node->value, "*") == 0) {
                sb_append(sb, "    imul rax, rbx\n");
            } else if (node->value && strcmp(node->value, "/") == 0) {
                sb_append(sb, "    cqo\n    idiv rbx\n");
            } else if (node->value && strcmp(node->value, "%") == 0) {
                sb_append(sb, "    cqo\n    idiv rbx\n    mov rax, rdx\n");
            } else {
                sb_append(sb, "    add rax, rbx\n");
            }
            break;
        case AST_UNARY_EXPR:
            generate_asm_expr(sb, node->right ? node->right : node->left, str_lbl, data_sb);
            if (node->value && strcmp(node->value, "-") == 0) sb_append(sb, "    neg rax\n");
            else if (node->value && strcmp(node->value, "~") == 0) sb_append(sb, "    not rax\n");
            break;
        case AST_CALL_EXPR:
            if (is_print_builtin(node->value)) {
                if (node->child_count > 0) {
                    ASTNode *arg = node->children[0];
                    generate_asm_expr(sb, arg, str_lbl, data_sb);
                    if (arg->data_type == TYPE_STRING) {
                        sb_append(sb, "    mov rsi, rax\n");
                        sb_append(sb, "    lea rdi, [rel fmt_str]\n");
                    } else {
                        sb_append(sb, "    mov rsi, rax\n");
                        sb_append(sb, "    lea rdi, [rel fmt_int]\n");
                    }
                    sb_append(sb, "    xor eax, eax\n");
                    sb_append(sb, "    call printf\n");
                }
            } else if (node->value) {
                sb_append(sb, "    call func_%s\n", node->value);
            }
            break;
        default:
            break;
    }
}

static void generate_asm_node(StringBuilder *sb, ASTNode *node, int *str_lbl, StringBuilder *data_sb, StringBuilder *bss_sb) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt; stmt = stmt->next) {
                generate_asm_node(sb, stmt, str_lbl, data_sb, bss_sb);
            }
            break;
        case AST_VAR_DECL:
        case AST_CONST_DECL:
            sb_append(bss_sb, "    var_%s: resq 1\n", node->value ? node->value : "v");
            if (node->right) {
                generate_asm_expr(sb, node->right, str_lbl, data_sb);
                sb_append(sb, "    mov [rel var_%s], rax\n", node->value ? node->value : "v");
            }
            break;
        case AST_ASSIGN_STMT:
            if (node->left && node->left->type == AST_IDENTIFIER) {
                generate_asm_expr(sb, node->right, str_lbl, data_sb);
                sb_append(sb, "    mov [rel var_%s], rax\n", node->left->value ? node->left->value : "v");
            }
            break;
        case AST_CALL_EXPR:
            generate_asm_expr(sb, node, str_lbl, data_sb);
            break;
        case AST_RETURN_STMT:
            if (node->right) generate_asm_expr(sb, node->right, str_lbl, data_sb);
            sb_append(sb, "    ret\n");
            break;
        default:
            break;
    }
}

char* codegen_assembly(ASTNode *ast, const char *source) {
    (void)source;
    StringBuilder *sb = sb_create();
    StringBuilder *data_sb = sb_create();
    StringBuilder *bss_sb = sb_create();
    if (!sb || !data_sb || !bss_sb) return NULL;
    
    int str_lbl = 0;
    sb_append(data_sb, "section .data\n");
    sb_append(data_sb, "    fmt_int: db \"%%ld\", 10, 0\n");
    sb_append(data_sb, "    fmt_str: db \"%%s\", 10, 0\n");
    
    sb_append(bss_sb, "\nsection .bss\n");
    
    sb_append(sb, "\nsection .text\n");
    sb_append(sb, "    extern printf\n");
    sb_append(sb, "    global main\n");
    sb_append(sb, "main:\n");
    sb_append(sb, "    push rbp\n");
    sb_append(sb, "    mov rbp, rsp\n");
    
    generate_asm_node(sb, ast, &str_lbl, data_sb, bss_sb);
    
    sb_append(sb, "    xor eax, eax\n");
    sb_append(sb, "    leave\n");
    sb_append(sb, "    ret\n");
    
    StringBuilder *out = sb_create();
    sb_append(out, "; Generated x86-64 NASM Assembly by SUB Compiler\n");
    sb_append(out, "%s", data_sb->buffer);
    sb_append(out, "%s", bss_sb->buffer);
    sb_append(out, "%s", sb->buffer);
    
    sb_free(sb);
    sb_free(data_sb);
    sb_free(bss_sb);
    return sb_to_string(out);
}

/* Ruby code generator continues from previous implementation... */
static void indent_ruby(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "  ");
    }
}

/* Ruby's top-level variables are locals of the main script, and a `def` body
   cannot see them, so a SUB global becomes a Ruby global: `$name`. The
   rename has to skip a name the current function has of its own. */
static const char *g_rb_shadow[GLOBALS_MAX];
static int g_rb_nshadow = 0;

/* Ruby reserves words SUB does not, and `next`, `end`, `def`, `then` and
   `unless` are all plausible variable names. Ruby has no escape for a local
   of a reserved name -- Swift's backticks have no equivalent -- so the name
   is suffixed instead. Consistently, at declaration and at every use.

       let next = a + b     ->     next_ = (a + b)

   Without this a `let next` anywhere in a program made the whole Ruby
   translation a syntax error. */
static int ruby_reserved(const char *name) {
    static const char *kw[] = {
        "alias", "and", "begin", "break", "case", "class", "def", "defined?",
        "do", "else", "elsif", "end", "ensure", "false", "for", "if", "in",
        "module", "next", "nil", "not", "or", "redo", "rescue", "retry",
        "return", "self", "super", "then", "true", "undef", "unless",
        "until", "when", "while", "yield", "__FILE__", "__LINE__", NULL
    };
    if (!name) return 0;
    for (int i = 0; kw[i]; i++)
        if (strcmp(name, kw[i]) == 0) return 1;
    return 0;
}

static int ruby_is_global(const char *name) {
    if (!name || !globals_has(&g_globals, name)) return 0;
    for (int i = 0; i < g_rb_nshadow; i++)
        if (strcmp(g_rb_shadow[i], name) == 0) return 0;
    return 1;
}

/* The name to write, `$`-prefixed when it is a global. Rotates a few buffers
   so that two of them can be live in one sb_append. */
static const char *ruby_name(const char *name) {
    static char buf[4][128];
    static int slot = 0;
    if (!name) return "var";
    int global = ruby_is_global(name);
    int reserved = ruby_reserved(name);
    if (!global && !reserved) return name;
    slot = (slot + 1) % 4;
    snprintf(buf[slot], sizeof buf[0], "%s%s%s",
             global ? "$" : "", name, reserved ? "_" : "");
    return buf[slot];
}

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent);

static void generate_expr_ruby(StringBuilder *sb, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (expr_is_null_literal(node)) {
                /* Ruby spells the empty value nil. */
                sb_append(sb, "nil");
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "nil");
            }
            break;

        case AST_IDENTIFIER:
            sb_append(sb, "%s", ruby_name(node->value));
            break;

        case AST_BINARY_EXPR:
            if (emit_special_binop(sb, node, LANG_RUBY, generate_expr_ruby)) break;
            sb_append(sb, "(");
            generate_expr_ruby(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, ")");
            break;

        case AST_CALL_EXPR: {
            const char *func_name = node->value ? node->value : "func";
            {
                const BuiltinSpelling *bs = builtin_spelling(LANG_RUBY, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_ruby(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
                bs = builtin_spelling_multi(LANG_RUBY, node->value);
                if (bs && node->child_count >= 2) {
                    sb_append(sb, "%s", bs->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_ruby(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(func_name)) {
                sb_append(sb, "_sub_puts");
                if (node->child_count > 0) {
                    sb_append(sb, " ");
                    generate_expr_ruby(sb, node->children[0]);
                }
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", func_name);
                } else {
                    generate_expr_ruby(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    generate_expr_ruby(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }
        case AST_UNARY_EXPR:
            /* Without this case the operand vanished entirely: abs(-7)
               generated `().abs` and classify(-5) generated `classify()`. */
            sb_append(sb, "%s", node->value ? node->value : "");
            generate_expr_ruby(sb, node->right ? node->right : node->left);
            break;
        case AST_ARRAY_LITERAL:
            sb_append(sb, "[");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_ruby(sb, node->children[i]);
            }
            sb_append(sb, "]");
            break;
        case AST_OBJECT_LITERAL:
            sb_append(sb, "{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children[i];
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\" => ", pair->value ? pair->value : "");
                generate_expr_ruby(sb, pair->right);
            }
            sb_append(sb, "}");
            break;
        case AST_MEMBER_ACCESS:
            generate_expr_ruby(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;
        case AST_ARRAY_ACCESS:
            sb_append(sb, "_sub_at(");
            generate_expr_ruby(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, ")");
            break;

        default:
            break;
    }
}

/* See gen_clause_python. `1.times` is Ruby's one-iteration loop, and `break`
   leaves the block it is written in. */
static void gen_clause_ruby(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_ruby(sb, indent);
        sb_append(sb, "1.times do\n");
        indent++;
    }
    generate_node_ruby(sb, clause->body, indent);
    if (loop) {
        indent_ruby(sb, indent - 1);
        sb_append(sb, "end\n");
    }
}

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        /* Ruby spells `continue` as `next`. A dropped `break` turns a loop that terminates into one
           that does not, so this must never fall through to the default. */

        case AST_DO_WHILE_STMT:
            /* Ruby's `begin ... end while` runs the body first, but it is a
               quirk of `begin`, not something that reads as a loop. */
            indent_ruby(sb, indent);
            sb_append(sb, "while true\n");
            generate_node_ruby(sb, node->body, indent + 1);
            indent_ruby(sb, indent + 1);
            sb_append(sb, "break unless (");
            generate_expr_ruby(sb, node->condition);
            sb_append(sb, ")\n");
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;
        case AST_BREAK_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "break\n");
            break;

        case AST_SWITCH_STMT: {
            /* Not `case/when`: Ruby's `when` compares with ===, which for a
               Range or a Class means something other than equality. */
            static int sw_ruby = 0;
            int id = sw_ruby++;
            ASTNode *deflt = switch_default_clause(node);
            int emitted = 0;

            indent_ruby(sb, indent);
            sb_append(sb, "_sw%d = ", id);
            generate_expr_ruby(sb, node->condition);
            sb_append(sb, "\n");

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_ruby(sb, indent);
                sb_append(sb, "%s ", emitted ? "elsif" : "if");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    sb_append(sb, "_sw%d == (", id);
                    generate_expr_ruby(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, "\n");
                gen_clause_ruby(sb, c, indent + 1);
                emitted = 1;
            }
            if (deflt) {
                indent_ruby(sb, indent);
                sb_append(sb, "%s\n", emitted ? "else" : "if true");
                gen_clause_ruby(sb, deflt, indent + 1);
                emitted = 1;
            }
            if (emitted) {
                indent_ruby(sb, indent);
                sb_append(sb, "end\n");
            }
            break;
        }

        case AST_CONTINUE_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "next\n");
            break;

        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_ruby(sb, stmt, indent);
            }
            break;

        case AST_VAR_DECL:
            indent_ruby(sb, indent);
            sb_append(sb, "%s = ", node->value ? ruby_name(node->value) : "var");
            if (node->right) {
                generate_expr_ruby(sb, node->right);
            } else {
                sb_append(sb, "nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_CONST_DECL:
            indent_ruby(sb, indent);
            sb_append(sb, "%s = ", node->value ? ruby_name(node->value) : "CONST");
            if (node->right) {
                generate_expr_ruby(sb, node->right);
            } else {
                sb_append(sb, "nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_ruby(sb, indent);
            sb_append(sb, "def %s", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                sb_append(sb, "(");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children[i] && node->children[i]->value) {
                        /* A parameter named for a Ruby keyword needs the same
                           suffix its uses get. */
                        sb_append(sb, "%s", ruby_reserved(node->children[i]->value)
                                  ? ruby_name(node->children[i]->value)
                                  : node->children[i]->value);
                    }
                }
                sb_append(sb, ")");
            }
            sb_append(sb, "\n");

            if (node->body) {
                /* Names this function has of its own keep their plain
                   spelling inside it, whatever the top level called them. */
                int mark = g_rb_nshadow;
                g_rb_nshadow += fn_shadowed_globals(&g_globals, node,
                                                    g_rb_shadow + mark,
                                                    GLOBALS_MAX - mark);
                generate_node_ruby(sb, node->body, indent + 1);
                g_rb_nshadow = mark;
            }
            if (!node->body) {
                indent_ruby(sb, indent + 1);
                sb_append(sb, "# TODO: implement\n");
            }

            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_IF_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "if ");
            generate_expr_ruby(sb, node->condition);
            sb_append(sb, "\n");
            generate_node_ruby(sb, node->body, indent + 1);
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    indent_ruby(sb, indent);
                    sb_append(sb, "elsif ");
                    generate_expr_ruby(sb, node->right->condition);
                    sb_append(sb, "\n");
                    generate_node_ruby(sb, node->right->body, indent + 1);
                    if (node->right->right) {
                        ASTNode *else_branch = node->right->right;
                        while (else_branch && else_branch->type == AST_IF_STMT) {
                            indent_ruby(sb, indent);
                            sb_append(sb, "elsif ");
                            generate_expr_ruby(sb, else_branch->condition);
                            sb_append(sb, "\n");
                            generate_node_ruby(sb, else_branch->body, indent + 1);
                            else_branch = else_branch->right;
                        }
                        if (else_branch) {
                            indent_ruby(sb, indent);
                            sb_append(sb, "else\n");
                            generate_node_ruby(sb, else_branch, indent + 1);
                        }
                    }
                } else {
                    indent_ruby(sb, indent);
                    sb_append(sb, "else\n");
                    generate_node_ruby(sb, node->right, indent + 1);
                }
            }
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_FOR_STMT:
            indent_ruby(sb, indent);
            // Generate range from AST children if available
            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    sb_append(sb, "(");
                    if (range->right) {
                        if (range->left) generate_expr_ruby(sb, range->left);
                        else sb_append(sb, "0");
                        sb_append(sb, "...");
                        generate_expr_ruby(sb, range->right);
                    } else if (range->left) {
                        sb_append(sb, "0...");
                        generate_expr_ruby(sb, range->left);
                    } else {
                        sb_append(sb, "0...10");
                    }
                    sb_append(sb, ")");
                } else {
                    generate_expr_ruby(sb, range);
                }
            } else if (node->condition) {
                generate_expr_ruby(sb, node->condition);
            } else {
                sb_append(sb, "(0...10)");  // Legacy fallback
            }
            sb_append(sb, ".each do |%s|\n", node->value ? node->value : "i");
            {
                /* The block parameter is the loop's own, so inside the body
                   the name means it and not a `$`-prefixed global. */
                int mark = g_rb_nshadow;
                if (node->value && globals_has(&g_globals, node->value) &&
                    g_rb_nshadow < GLOBALS_MAX)
                    g_rb_shadow[g_rb_nshadow++] = node->value;
                generate_node_ruby(sb, node->body, indent + 1);
                g_rb_nshadow = mark;
            }
            if (!node->body) {
                indent_ruby(sb, indent + 1);
                sb_append(sb, "# empty loop\n");
            }
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_WHILE_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "while ");
            generate_expr_ruby(sb, node->condition);
            sb_append(sb, "\n");
            generate_node_ruby(sb, node->body, indent + 1);
            indent_ruby(sb, indent);
            sb_append(sb, "end\n");
            break;

        case AST_RETURN_STMT:
            indent_ruby(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                generate_expr_ruby(sb, node->right);
            }
            sb_append(sb, "\n");
            break;

        case AST_CALL_EXPR:
            indent_ruby(sb, indent);
            generate_expr_ruby(sb, node);
            sb_append(sb, "\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_ruby(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "_sub_put(");
                generate_expr_ruby(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_ruby(sb, node->left->right);
                sb_append(sb, ", ");
                generate_expr_ruby(sb, node->right);
                sb_append(sb, ")\n");
                break;
            }
            generate_expr_ruby(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, "\n");
            break;
            
        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_ruby(sb, stmt, indent);
            }
            break;

        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            if (node->value) {
                indent_ruby(sb, indent);
                sb_append(sb, "# Embedded code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;

        case AST_UI_COMPONENT:
            indent_ruby(sb, indent);
            sb_append(sb, "# UI: %s\n", node->value ? node->value : "component");
            break;

        default:
            break;
    }
}

char* codegen_ruby(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    globals_collect(&g_globals, ast);

    sb_append(sb, "#!/usr/bin/env ruby\n");
    /* SUB prints a float with no fractional part as an integer (sqrt(81.0)
       is 9, not 9.0) and spells booleans true/false, which Ruby already
       does. Route printing through a helper for the float case. */
    sb_append(sb, "\ndef _sub_str(v)\n");
    sb_append(sb, "  return \"null\" if v.nil?\n");
    /* printf's %g, matching the interpreter: six significant digits. */
    sb_append(sb, "  return sprintf(\"%%g\", v) if v.is_a?(Float)\n");
    sb_append(sb, "  return '[' + v.map { |x| _sub_str(x) }.join(', ') + ']' "
                  "if v.is_a?(Array)\n");
    sb_append(sb, "  v.to_s\nend\n");
    /* Ruby's / and % floor like Python's; SUB truncates toward zero.
       Dividing by zero is a runtime error with status 70 in every backend. */
    sb_append(sb, "\ndef _sub_die(msg)\n");
    sb_append(sb, "  STDERR.puts \"RuntimeError: #{msg}\"\n  exit 70\nend\n");
    sb_append(sb, "\ndef _sub_idiv(a, b)\n");
    sb_append(sb, "  _sub_die('division by zero') if b == 0\n");
    sb_append(sb, "  q = a.abs / b.abs\n");
    sb_append(sb, "  (a < 0) != (b < 0) ? -q : q\nend\n");
    sb_append(sb, "\ndef _sub_mod(a, b)\n");
    sb_append(sb, "  _sub_die('modulo by zero') if b == 0\n");
    sb_append(sb, "  a - _sub_idiv(a, b) * b\nend\n");
    sb_append(sb, "\ndef _sub_fdiv(a, b)\n");
    sb_append(sb, "  _sub_die('division by zero') if b == 0\n");
    sb_append(sb, "  a.to_f / b.to_f\nend\n");
    /* Ruby's Float#% floors like Python's; SUB follows C's fmod. */
    sb_append(sb, "\ndef _sub_fmod(a, b)\n");
    sb_append(sb, "  _sub_die('modulo by zero') if b == 0\n");
    sb_append(sb, "  a.to_f.remainder(b.to_f)\nend\n");
    sb_append(sb, "\ndef _sub_at(c, i)\n");
    sb_append(sb, "  n = c.length\n  i += n if i < 0\n");
    sb_append(sb, "  if i < 0 || i >= n\n");
    sb_append(sb, "    _sub_die(c.is_a?(String) ? \"string index #{i} out of bounds\" :\n");
    sb_append(sb, "             \"array index #{i} out of bounds [0, #{n})\")\n  end\n");
    sb_append(sb, "  c[i]\nend\n");
    sb_append(sb, "\ndef _sub_put(a, i, v)\n");
    sb_append(sb, "  n = a.length\n  i += n if i < 0\n");
    sb_append(sb, "  if i < 0 || i >= n\n");
    sb_append(sb, "    _sub_die(\"array index #{i} out of bounds [0, #{n})\")\n  end\n");
    sb_append(sb, "  a[i] = v\nend\n");
    sb_append(sb, "\ndef _sub_push(a, v)\n  a.push(v)\n  nil\nend\n");
    sb_append(sb, "\ndef _sub_pop(a)\n");
    sb_append(sb, "  _sub_die('pop from empty array') if a.empty?\n");
    sb_append(sb, "  a.pop\nend\n");
    sb_append(sb, "\ndef _sub_pow(a, b)\n");
    sb_append(sb, "  r = a ** b\n");
    sb_append(sb, "  r.is_a?(Rational) ? r.to_f : r\nend\n");
    sb_append(sb, "\ndef _sub_puts(v)\n  puts _sub_str(v)\nend\n\n");
    sb_append(sb, "# Generated by SUB Language Compiler\n\n");

    char *embedded = extract_embedded_code(source, "ruby");
    if (embedded) {
        sb_append(sb, "# Embedded Ruby code from SUB\n");
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }
    generate_node_ruby(sb, ast, 0);

    return sb_to_string(sb);
}

/* ========================================
   GO CODE GENERATOR
   ======================================== */

static void indent_go(StringBuilder *sb, int level) {
    for (int i = 0; i < level; i++) {
        sb_append(sb, "\t");
    }
}

/* Go refuses to build with an unused import and equally refuses to build with
   a missing one, so the import block has to match what the body actually
   emits. The math/strings helpers come from the builtin table and the power
   operator. */
/* True when the program divides or takes a remainder of two integers, and
   so needs the _sub_idiv / _sub_mod helpers - which in turn need fmt and os.
   Go rejects an unused import, so this has to be known before the import
   block is written. */
static bool ast_uses_int_divmod(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_BINARY_EXPR && node->value &&
        (strcmp(node->value, "/") == 0 || strcmp(node->value, "%") == 0))
        return true;
    if (ast_uses_int_divmod(node->left))      return true;
    if (ast_uses_int_divmod(node->right))     return true;
    if (ast_uses_int_divmod(node->condition)) return true;
    if (ast_uses_int_divmod(node->body))      return true;
    if (ast_uses_int_divmod(node->next))      return true;
    for (int i = 0; i < node->child_count; i++)
        if (node->children && ast_uses_int_divmod(node->children[i])) return true;
    return false;
}

static bool ast_uses_go_pkg(ASTNode *node, const char *pkg) {
    if (!node) return false;

    if (node->type == AST_CALL_EXPR && node->value) {
        const BuiltinSpelling *bs = builtin_spelling(LANG_GO, node->value);
        if (bs && strstr(bs->prefix, pkg) != NULL) return true;
    }
    /* `return null` from a numeric function becomes math.NaN() */
    if (strcmp(pkg, "math") == 0 && node->type == AST_RETURN_STMT &&
        expr_is_null_literal(node->right)) return true;
    /* a ** b becomes math.Pow(...) */
    if (strcmp(pkg, "math") == 0 && node->type == AST_BINARY_EXPR &&
        node->value && (strcmp(node->value, "**") == 0 ||
                        strcmp(node->value, "%") == 0)) {
        if (strcmp(node->value, "**") == 0) return true;
        /* % on floats becomes math.Mod */
        if (infer_expr_type(node->left)  == TYPE_FLOAT ||
            infer_expr_type(node->right) == TYPE_FLOAT) return true;
    }

    if (ast_uses_go_pkg(node->left, pkg))      return true;
    if (ast_uses_go_pkg(node->right, pkg))     return true;
    if (ast_uses_go_pkg(node->condition, pkg)) return true;
    if (ast_uses_go_pkg(node->body, pkg))      return true;
    if (ast_uses_go_pkg(node->next, pkg))      return true;
    for (int i = 0; i < node->child_count; i++)
        if (node->children && ast_uses_go_pkg(node->children[i], pkg)) return true;
    return false;
}

static bool ast_needs_fmt(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_CALL_EXPR && node->value &&
        is_print_builtin(node->value))
        return true;
    if (ast_needs_fmt(node->left)) return true;
    if (ast_needs_fmt(node->right)) return true;
    if (ast_needs_fmt(node->condition)) return true;
    if (ast_needs_fmt(node->body)) return true;
    if (ast_needs_fmt(node->next)) return true;
    for (int i = 0; i < node->child_count; i++) {
        if (node->children && ast_needs_fmt(node->children[i]))
            return true;
    }
    return false;
}

static bool is_go_package_level_node(ASTNode *node) {
    if (!node) return false;

    switch (node->type) {
        case AST_FUNCTION_DECL:
        case AST_VAR_DECL:
        case AST_CONST_DECL:
        case AST_CLASS_DECL:
        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
        case AST_UI_COMPONENT:
            return true;
        default:
            return false;
    }
}

static void generate_node_go(StringBuilder *sb, ASTNode *node, int indent);

static void generate_expr_go(StringBuilder *sb, ASTNode *node) {
    if (!node) return;

    switch (node->type) {
        case AST_LITERAL:
            if (node->data_type == TYPE_STRING) {
                char *escaped = escape_string_for_codegen(node->value ? node->value : "");
                sb_append(sb, "\"%s\"", escaped ? escaped : "");
                free(escaped);
            } else if (node->value) {
                sb_append(sb, "%s", node->value);
            } else {
                sb_append(sb, "nil");
            }
            break;

        case AST_IDENTIFIER:
            sb_append(sb, "%s", node->value ? node->value : "v");
            break;

        case AST_BINARY_EXPR:
            /* `x == null` / `x != null` become NaN tests, matching how a
               nullable numeric is represented. */
            if (node->value && (strcmp(node->value, "==") == 0 ||
                                strcmp(node->value, "!=") == 0) &&
                (expr_is_null_literal(node->left) || expr_is_null_literal(node->right))) {
                ASTNode *val = expr_is_null_literal(node->left) ? node->right : node->left;
                DataType vt = infer_expr_type(val);
                if (vt == TYPE_FLOAT || vt == TYPE_INT) {
                    sb_append(sb, "%smath.IsNaN(", strcmp(node->value, "!=") == 0 ? "!" : "");
                    generate_expr_go(sb, val);
                    sb_append(sb, ")");
                    break;
                }
            }
            /* Go's % is integer-only. */
            if (node->value && strcmp(node->value, "%") == 0 &&
                (infer_expr_type(node->left)  == TYPE_FLOAT ||
                 infer_expr_type(node->right) == TYPE_FLOAT)) {
                sb_append(sb, "math.Mod(");
                generate_expr_go(sb, node->left);
                sb_append(sb, ", ");
                generate_expr_go(sb, node->right);
                sb_append(sb, ")");
                break;
            }
            if (emit_special_binop(sb, node, LANG_GO, generate_expr_go)) break;
            sb_append(sb, "(");
            generate_expr_go(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_go(sb, node->right);
            sb_append(sb, ")");
            break;

        case AST_UNARY_EXPR:
            sb_append(sb, "%s", node->value ? node->value : "!");
            generate_expr_go(sb, node->right);
            break;

        case AST_TERNARY_EXPR:
            sb_append(sb, "func() interface{} { if ");
            generate_expr_go(sb, node->condition);
            sb_append(sb, " { return ");
            generate_expr_go(sb, node->left);
            sb_append(sb, " }; return ");
            generate_expr_go(sb, node->right);
            sb_append(sb, " }()");
            break;

        case AST_CALL_EXPR: {
            const char *func_name = node->value ? node->value : "fn";
            {
                /* Ahead of the table: len() and str() of an array need the
                   array helpers, not the string spellings the table holds. */
                if (node->value && node->child_count == 1 &&
                    (!strcmp(node->value, "len") || !strcmp(node->value, "length") ||
                     !strcmp(node->value, "str") || !strcmp(node->value, "to_string")) &&
                    infer_expr_type(node->children[0]) == TYPE_ARRAY) {
                    sb_append(sb, node->value[0] == 'l' ? "_sub_len(" : "_sub_arr(");
                    generate_expr_go(sb, node->children[0]);
                    sb_append(sb, ")");
                    break;
                }
                const BuiltinSpelling *bs = builtin_spelling(LANG_GO, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_go(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
                bs = builtin_spelling_multi(LANG_GO, node->value);
                if (bs && node->child_count >= 2) {
                    sb_append(sb, "%s", bs->prefix);
                    for (int i = 0; i < node->child_count; i++) {
                        if (i > 0) sb_append(sb, ", ");
                        generate_expr_go(sb, node->children[i]);
                    }
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(func_name)) {
                sb_append(sb, "fmt.Println(");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children)
                        gen_printable(sb, node->children[i], "_sub_fmt",
                                      "_sub_arr", generate_expr_go);
                }
                sb_append(sb, ")");
            } else {
                if (node->value) {
                    sb_append(sb, "%s(", func_name);
                } else {
                    generate_expr_go(sb, node->left);
                    sb_append(sb, "(");
                }
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children)
                        generate_expr_go(sb, node->children[i]);
                }
                sb_append(sb, ")");
            }
            break;
        }

        case AST_ARRAY_LITERAL: {
            /* &[]T{...} - addressable, so push can append through it. */
            const char *t = go_array_type(infer_elem_type(node));
            sb_append(sb, "&%s{", t + 1);          /* skip the leading '*' */
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                if (node->children)
                    generate_expr_go(sb, node->children[i]);
            }
            sb_append(sb, "}");
            break;
        }

        case AST_OBJECT_LITERAL:
            sb_append(sb, "map[string]interface{}{");
            for (int i = 0; i < node->child_count; i++) {
                ASTNode *pair = node->children ? node->children[i] : NULL;
                if (!pair) continue;
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "\"%s\": ", pair->value ? pair->value : "");
                generate_expr_go(sb, pair->right);
            }
            sb_append(sb, "}");
            break;

        case AST_MEMBER_ACCESS:
            generate_expr_go(sb, node->left);
            sb_append(sb, ".%s", node->value ? node->value : "");
            break;

        case AST_ARRAY_ACCESS:
            sb_append(sb, "_sub_at(");
            generate_expr_go(sb, node->left);
            sb_append(sb, ", ");
            generate_expr_go(sb, node->right);
            sb_append(sb, ")");
            break;

        default:
            break;
    }
}

static void generate_node_go(StringBuilder *sb, ASTNode *node, int indent);

/* See gen_clause_python. */
static void gen_clause_go(StringBuilder *sb, ASTNode *clause, int indent) {
    int loop = switch_clause_breaks(clause);
    if (loop) {
        indent_go(sb, indent);
        sb_append(sb, "for _once := 0; _once < 1; _once++ {\n");
        indent++;
    }
    generate_node_go(sb, clause->body, indent);
    if (loop) {
        indent_go(sb, indent - 1);
        sb_append(sb, "}\n");
    }
}

static void generate_node_go(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt; stmt = stmt->next) {
                generate_node_go(sb, stmt, indent);
            }
            break;

        case AST_VAR_DECL:
        case AST_CONST_DECL: {
            /* Go has to be told the type. `var a = 10` infers `int`, and the
               rest of the generated program uses int64 everywhere - so the
               variable could not then be passed to any generated function.
               Naming the inferred type keeps one integer width in play. */
            indent_go(sb, indent);
            DataType dt = node->data_type;
            if (dt == TYPE_UNKNOWN || dt == TYPE_AUTO)
                dt = infer_expr_type(node->right);
            sb_append(sb, "var %s", node->value ? node->value : "v");
            if (node->right) {
                if (dt == TYPE_ARRAY)
                    sb_append(sb, " %s", go_array_type(infer_elem_type(node->right)));
                else if (dt == TYPE_INT || dt == TYPE_FLOAT ||
                         dt == TYPE_BOOL || dt == TYPE_STRING)
                    sb_append(sb, " %s", go_type(dt));
                sb_append(sb, " = ");
                if (dt == TYPE_INT || dt == TYPE_FLOAT) {
                    /* An untyped constant needs the conversion spelled out
                       when the expression is a bare literal. */
                    sb_append(sb, "%s(", go_type(dt));
                    generate_expr_go(sb, node->right);
                    sb_append(sb, ")");
                } else {
                    generate_expr_go(sb, node->right);
                }
            } else {
                sb_append(sb, " interface{} = nil");
            }
            sb_append(sb, "\n");
            break;
        }

        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_go(sb, indent);
            g_go_fn_type = node->data_type;
            sb_append(sb, "func %s(", node->value ? node->value : "fn");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children[i] && node->children[i]->value) {
                        /* Go needs a type per parameter, not one trailing type
                           shared by every name. */
                        sb_append(sb, "%s %s", node->children[i]->value,
                                  go_param_type(node->children[i]));
                    }
                }
            }
            sb_append(sb, ")");
            if (node->data_type != TYPE_VOID && node->data_type != TYPE_UNKNOWN)
                sb_append(sb, " %s", go_type(node->data_type));
            sb_append(sb, " {\n");
            if (node->body) {
                generate_node_go(sb, node->body, indent + 1);
            }
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_IF_STMT:
            indent_go(sb, indent);
            sb_append(sb, "if ");
            generate_expr_go(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_go(sb, node->body, indent + 1);
            if (node->right) {
                if (node->right->type == AST_IF_STMT) {
                    indent_go(sb, indent);
                    sb_append(sb, "} else if ");
                    generate_expr_go(sb, node->right->condition);
                    sb_append(sb, " {\n");
                    generate_node_go(sb, node->right->body, indent + 1);
                    if (node->right->right) {
                        ASTNode *branch = node->right->right;
                        while (branch && branch->type == AST_IF_STMT) {
                            indent_go(sb, indent);
                            sb_append(sb, "} else if ");
                            generate_expr_go(sb, branch->condition);
                            sb_append(sb, " {\n");
                            generate_node_go(sb, branch->body, indent + 1);
                            branch = branch->right;
                        }
                        if (branch) {
                            indent_go(sb, indent);
                            sb_append(sb, "} else {\n");
                            generate_node_go(sb, branch, indent + 1);
                        }
                    }
                } else {
                    indent_go(sb, indent);
                    sb_append(sb, "} else {\n");
                    generate_node_go(sb, node->right, indent + 1);
                }
            }
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_FOR_STMT:
            indent_go(sb, indent);
            if (node->children && node->child_count > 0) {
                ASTNode *range = node->children[0];
                if (range && range->type == AST_RANGE_EXPR) {
                    const char *var = node->value ? node->value : "i";
                    /* Declare the loop variable int64: SUB integers are
                       64-bit, and Go will not pass its default `int` to a
                       function whose parameter is int64. */
                    sb_append(sb, "for %s := int64(", var);
                    if (range->right) {
                        /* range(start, end) */
                        generate_expr_go(sb, range->left);
                        sb_append(sb, "); %s < ", var);
                        generate_expr_go(sb, range->right);
                    } else if (range->left) {
                        /* range(n) → 0..n */
                        sb_append(sb, "0); %s < ", var);
                        generate_expr_go(sb, range->left);
                    } else {
                        sb_append(sb, "0); %s < 10", var);
                    }
                    sb_append(sb, "; %s++ {\n", var);
                } else {
                    /* An array is a *[]T, so ranging over it dereferences. */
                    sb_append(sb, "for _, %s := range *(",
                              node->value ? node->value : "item");
                    generate_expr_go(sb, range);
                    sb_append(sb, ") {\n");
                }
            } else if (node->condition) {
                sb_append(sb, "for _, %s := range *(",
                          node->value ? node->value : "item");
                generate_expr_go(sb, node->condition);
                sb_append(sb, ") {\n");
            } else {
                sb_append(sb, "for %s := int64(0); %s < 10; %s++ {\n",
                          node->value ? node->value : "i",
                          node->value ? node->value : "i",
                          node->value ? node->value : "i");
            }
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_WHILE_STMT:
            indent_go(sb, indent);
            sb_append(sb, "for ");
            generate_expr_go(sb, node->condition);
            sb_append(sb, " {\n");
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_DO_WHILE_STMT:
            indent_go(sb, indent);
            sb_append(sb, "for {\n");
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent + 1);
            sb_append(sb, "if !(");
            generate_expr_go(sb, node->condition);
            sb_append(sb, ") {\n");
            indent_go(sb, indent + 2);
            sb_append(sb, "break\n");
            indent_go(sb, indent + 1);
            sb_append(sb, "}\n");
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;

        case AST_RETURN_STMT:
            indent_go(sb, indent);
            sb_append(sb, "return");
            if (node->right) {
                sb_append(sb, " ");
                /* Go cannot put nil in a float64; a function returning both
                   null and a number uses NaN as the "no value" sentinel. */
                if (expr_is_null_literal(node->right) &&
                    (g_go_fn_type == TYPE_FLOAT || g_go_fn_type == TYPE_INT))
                    sb_append(sb, "math.NaN()");
                else
                    generate_expr_go(sb, node->right);
            }
            sb_append(sb, "\n");
            break;

        case AST_BREAK_STMT:
            indent_go(sb, indent);
            sb_append(sb, "break\n");
            break;

        case AST_SWITCH_STMT: {
            static int sw_go = 0;
            int id = sw_go++;
            ASTNode *deflt = switch_default_clause(node);
            DataType st = infer_expr_type(node->condition);
            int is_str = st == TYPE_STRING;
            int emitted = 0;
            (void)is_str;

            indent_go(sb, indent);
            sb_append(sb, "{\n");
            indent_go(sb, indent + 1);
            /* Named type, not `:=`. An integer literal scrutinee would
               otherwise be Go's `int` while every SUB integer around it is
               `int64`, and Go does not mix the two. */
            sb_append(sb, "var _sw%d %s = ", id, go_type(st));
            generate_expr_go(sb, node->condition);
            sb_append(sb, ";\n");
            /* Go rejects a declared-and-unused variable, and a switch that is
               nothing but a default never reads the scrutinee. */
            indent_go(sb, indent + 1);
            sb_append(sb, "_ = _sw%d\n", id);

            for (int i = 0; i < node->child_count; i++) {
                ASTNode *c = node->children[i];
                if (!c || c->type != AST_CASE_CLAUSE || c->child_count == 0) continue;
                indent_go(sb, indent + 1);
                sb_append(sb, "%sif (", emitted ? "} else " : "");
                for (int j = 0; j < c->child_count; j++) {
                    if (j > 0) sb_append(sb, " || ");
                    sb_append(sb, "_sw%d == (", id);
                    generate_expr_go(sb, c->children[j]);
                    sb_append(sb, ")");
                }
                sb_append(sb, ") {\n");
                gen_clause_go(sb, c, indent + 2);
                emitted = 1;
            }
            if (deflt) {
                if (emitted) {
                    indent_go(sb, indent + 1);
                    sb_append(sb, "} else {\n");
                    gen_clause_go(sb, deflt, indent + 2);
                } else {
                    /* Nothing for it to be the `else` of. A bare `{ }` here
                       is a block in most of these languages but a closure
                       expression in Swift, so the body is simply emitted. */
                    gen_clause_go(sb, deflt, indent + 1);
                }
            }
            if (emitted) {
                indent_go(sb, indent + 1);
                sb_append(sb, "}\n");
            }
            indent_go(sb, indent);
            sb_append(sb, "}\n");
            break;
        }

        case AST_CONTINUE_STMT:
            indent_go(sb, indent);
            sb_append(sb, "continue\n");
            break;

        case AST_CALL_EXPR:
            indent_go(sb, indent);
            generate_expr_go(sb, node);
            sb_append(sb, "\n");
            break;

        case AST_ASSIGN_STMT:
            indent_go(sb, indent);
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                sb_append(sb, "_sub_put(");
                generate_expr_go(sb, node->left->left);
                sb_append(sb, ", ");
                generate_expr_go(sb, node->left->right);
                sb_append(sb, ", ");
                generate_expr_go(sb, node->right);
                sb_append(sb, ")\n");
                break;
            }
            generate_expr_go(sb, node->left);
            sb_append(sb, " = ");
            generate_expr_go(sb, node->right);
            sb_append(sb, "\n");
            break;

        case AST_BLOCK:
            for (ASTNode *stmt = block_first(node); stmt; stmt = stmt->next) {
                generate_node_go(sb, stmt, indent);
            }
            break;

        case AST_CLASS_DECL:
            indent_go(sb, indent);
            sb_append(sb, "type %s struct {}\n",
                      node->value ? node->value : "Object");
            break;

        case AST_TRY_STMT:
            indent_go(sb, indent);
            sb_append(sb, "func() {\n");
            if (node->right) {
                indent_go(sb, indent + 1);
                sb_append(sb, "defer func() {\n");
                indent_go(sb, indent + 2);
                sb_append(sb, "if r := recover(); r != nil {\n");
                generate_node_go(sb, node->right, indent + 3);
                indent_go(sb, indent + 2);
                sb_append(sb, "}\n");
                indent_go(sb, indent + 1);
                sb_append(sb, "}()\n");
            }
            generate_node_go(sb, node->body, indent + 1);
            indent_go(sb, indent);
            sb_append(sb, "}()\n");
            break;

        case AST_THROW_STMT:
            indent_go(sb, indent);
            sb_append(sb, "panic(");
            if (node->right) {
                generate_expr_go(sb, node->right);
            } else {
                sb_append(sb, "\"error\"");
            }
            sb_append(sb, ")\n");
            break;

        case AST_EMBED_CODE:
        case AST_EMBED_CPP:
        case AST_EMBED_C:
            if (node->value) {
                indent_go(sb, indent);
                sb_append(sb, "// Embedded code\n");
                sb_append(sb, "%s\n", node->value);
            }
            break;

        case AST_UI_COMPONENT:
            indent_go(sb, indent);
            sb_append(sb, "// UI: %s\n", node->value ? node->value : "component");
            break;

        default:
            break;
    }
}

char* codegen_go(ASTNode *ast, const char *source) {
    StringBuilder *sb = sb_create();
    if (!sb) return NULL;

    sb_append(sb, "// Generated by SUB Language Compiler\n\n");

    sb_append(sb, "package main\n\n");

    bool needs_divmod   = ast_uses_int_divmod(ast);
    bool needs_fmt      = ast_needs_fmt(ast) || needs_divmod;
    bool needs_os       = true;   /* _sub_die always uses os.Exit */
    bool needs_math     = ast_uses_go_pkg(ast, "math") || needs_divmod;
    bool needs_strings  = ast_uses_go_pkg(ast, "strings");
    bool needs_strconv  = true;   /* _sub_fmt always uses it */
    needs_fmt = true;             /* _sub_str always uses fmt.Sprint */
    int  import_count   = (needs_fmt ? 1 : 0) + (needs_math ? 1 : 0) +
                          (needs_strings ? 1 : 0) + (needs_os ? 1 : 0) +
                          (needs_strconv ? 1 : 0);
    if (import_count == 1) {
        sb_append(sb, "import \"%s\"\n\n",
                  needs_fmt ? "fmt" : needs_math ? "math" :
                  needs_os ? "os" : "strings");
    } else if (import_count > 1) {
        sb_append(sb, "import (\n");
        if (needs_fmt)     sb_append(sb, "\t\"fmt\"\n");
        if (needs_math)    sb_append(sb, "\t\"math\"\n");
        if (needs_os)      sb_append(sb, "\t\"os\"\n");
        if (needs_strconv) sb_append(sb, "\t\"strconv\"\n");
        if (needs_strings) sb_append(sb, "\t\"strings\"\n");
        sb_append(sb, ")\n\n");
    }

    /* Arrays. Generic, so one set of helpers covers every element type.
       Indexing and popping stop the program the way the interpreter does
       rather than panicking with Go's own message. */
    sb_append(sb, "func _sub_idx[T any](a *[]T, i int64) int64 {\n");
    sb_append(sb, "\tif i < 0 {\n\t\ti += int64(len(*a))\n\t}   // a[-1] is the last\n");
    sb_append(sb, "\tif a == nil || i < 0 || i >= int64(len(*a)) {\n");
    sb_append(sb, "\t\tfmt.Fprintf(os.Stderr, \"RuntimeError: array index %%d "
                  "out of bounds [0, %%d)\\n\", i, len(*a))\n");
    sb_append(sb, "\t\tos.Exit(70)\n\t}\n\treturn i\n}\n\n");
    sb_append(sb, "func _sub_at[T any](a *[]T, i int64) T "
                  "{ return (*a)[_sub_idx(a, i)] }\n\n");
    sb_append(sb, "func _sub_put[T any](a *[]T, i int64, v T) "
                  "{ (*a)[_sub_idx(a, i)] = v }\n\n");
    sb_append(sb, "func _sub_push[T any](a *[]T, v T) { *a = append(*a, v) }\n\n");
    sb_append(sb, "func _sub_pop[T any](a *[]T) T {\n");
    sb_append(sb, "\tif a == nil || len(*a) == 0 {\n\t\t_sub_die(\"pop from empty array\")\n\t}\n");
    sb_append(sb, "\tv := (*a)[len(*a)-1]\n\t*a = (*a)[:len(*a)-1]\n\treturn v\n}\n\n");
    sb_append(sb, "func _sub_len[T any](a *[]T) int64 "
                  "{ if a == nil { return 0 }; return int64(len(*a)) }\n\n");
    sb_append(sb, "func _sub_arr[T any](a *[]T) string {\n");
    sb_append(sb, "\ts := \"[\"\n\tfor i, v := range *a {\n");
    sb_append(sb, "\t\tif i > 0 {\n\t\t\ts += \", \"\n\t\t}\n");
    sb_append(sb, "\t\ts += _sub_str(v)\n\t}\n\treturn s + \"]\"\n}\n\n");

    /* printf's %g, as the interpreter prints floats. Go's default float
       formatting prints every digit it needs to round-trip, so 1.0 / 3.0
       came out 0.3333333333333333. */
    sb_append(sb, "func _sub_fmt(d float64) string "
                  "{ return strconv.FormatFloat(d, 'g', 6, 64) }\n\n");
    sb_append(sb, "func _sub_str(v interface{}) string {\n");
    sb_append(sb, "\tif d, ok := v.(float64); ok {\n\t\treturn _sub_fmt(d)\n\t}\n");
    sb_append(sb, "\treturn fmt.Sprint(v)\n}\n\n");

    /* Both the array helpers and integer division report runtime errors, so
       this is unconditional and os is always imported. */
    sb_append(sb, "func _sub_die(msg string) {\n");
    sb_append(sb, "\tfmt.Fprintln(os.Stderr, \"RuntimeError: \"+msg)\n");
    sb_append(sb, "\tos.Exit(70)\n}\n\n");

    if (needs_divmod) {
        /* Go panics on integer division by zero; SUB reports a runtime error
           and exits 70, as the interpreter does. */
        sb_append(sb, "func _sub_idiv(a int64, b int64) int64 {\n");
        sb_append(sb, "\tif b == 0 {\n\t\t_sub_die(\"division by zero\")\n\t}\n");
        sb_append(sb, "\treturn a / b\n}\n\n");
        sb_append(sb, "func _sub_mod(a int64, b int64) int64 {\n");
        sb_append(sb, "\tif b == 0 {\n\t\t_sub_die(\"modulo by zero\")\n\t}\n");
        sb_append(sb, "\treturn a %% b\n}\n\n");
        sb_append(sb, "func _sub_fdiv(a float64, b float64) float64 {\n");
        sb_append(sb, "\tif b == 0 {\n\t\t_sub_die(\"division by zero\")\n\t}\n");
        sb_append(sb, "\treturn a / b\n}\n\n");
        sb_append(sb, "func _sub_fmod(a float64, b float64) float64 {\n");
        sb_append(sb, "\tif b == 0 {\n\t\t_sub_die(\"modulo by zero\")\n\t}\n");
        sb_append(sb, "\treturn math.Mod(a, b)\n}\n\n");
    }

    char *embedded = extract_embedded_code(source, "go");
    if (embedded) {
        sb_append(sb, "%s\n", embedded);
        free(embedded);
    }

    /* Pass 1: emit package-level declarations, track main and executable stmts */
    bool has_user_main = false;
    bool has_exec_stmts = false;
    bool emitted_package_level = false;
    for (ASTNode *stmt = block_first(ast); stmt; stmt = stmt->next) {
        if (stmt->type == AST_FUNCTION_DECL) {
            if (stmt->value && strcmp(stmt->value, "main") == 0)
                has_user_main = true;
        }

        if (is_go_package_level_node(stmt)) {
            /* Resolve identifiers in a function body against its parameters;
               see infer_enter_function(). */
            ASTNode *prev_fn = stmt->type == AST_FUNCTION_DECL
                             ? infer_enter_function(stmt) : NULL;
            generate_node_go(sb, stmt, 0);
            if (stmt->type == AST_FUNCTION_DECL) infer_enter_function(prev_fn);
            emitted_package_level = true;
        } else {
            has_exec_stmts = true;
        }
    }

    /* Pass 2: wrap executable top-level stmts in init() or main() */
    if (has_exec_stmts || !has_user_main) {
        if (emitted_package_level) {
            sb_append(sb, "\n");
        }
        sb_append(sb, has_user_main ? "func init() {\n" : "func main() {\n");
        for (ASTNode *stmt = block_first(ast); stmt; stmt = stmt->next) {
            if (!is_go_package_level_node(stmt)) {
                generate_node_go(sb, stmt, 1);
            }
        }
        sb_append(sb, "}\n");
    }

    return sb_to_string(sb);
}
