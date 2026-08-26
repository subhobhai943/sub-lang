/* ========================================
   SUB Language Multi-Language Code Generator - REAL IMPLEMENTATION
   Actually processes your SUB code, not dummy templates!
   File: codegen_multilang.c
   ======================================== */

#define _GNU_SOURCE
#include "sub_compiler.h"
#include "codegen_infer.h"
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
        {"ceil","Math.ceil(",")"},    {"round","Math.round(",")"},
        {"upper","(",").toUpperCase()"}, {"lower","(",").toLowerCase()"},
        {"trim","(",").trim()"},      {NULL,NULL,NULL}
    };
    static const BuiltinSpelling java[] = {
        {"str","_subStr(",")"},        {"to_string","_subStr(",")"},
        {"int","(long)(",")"},         {"float","(double)(",")"},
        {"bool","(boolean)(",")"},     {"len","(",").length()"},
        {"length","(",").length()"},   {"abs","Math.abs(",")"},
        {"sqrt","Math.sqrt(",")"},     {"floor","Math.floor(",")"},
        {"ceil","Math.ceil(",")"},     {"round","Math.round(",")"},
        {"upper","(",").toUpperCase()"},{"lower","(",").toLowerCase()"},
        {"trim","(",").trim()"},       {NULL,NULL,NULL}
    };
    static const BuiltinSpelling swift[] = {
        {"str","String(describing: ",")"}, {"to_string","String(describing: ",")"},
        {"int","Int(",")"},            {"float","Double(",")"},
        {"bool","Bool(",")"},          {"len","(",").count"},
        {"length","(",").count"},      {"abs","abs(",")"},
        {"sqrt","(Double(",")).squareRoot()"},
        {"upper","(",").uppercased()"},{"lower","(",").lowercased()"},
        {NULL,NULL,NULL}
    };
    static const BuiltinSpelling kotlin[] = {
        {"str","(",").toString()"},    {"to_string","(",").toString()"},
        {"int","(",").toLong()"},      {"float","(",").toDouble()"},
        {"len","(",").length"},        {"length","(",").length"},
        {"abs","kotlin.math.abs(",")"},{"sqrt","kotlin.math.sqrt((",").toDouble())"},
        {"upper","(",").uppercase()"}, {"lower","(",").lowercase()"},
        {"trim","(",").trim()"},       {NULL,NULL,NULL}
    };
    static const BuiltinSpelling go[] = {
        {"str","fmt.Sprint(",")"},     {"to_string","fmt.Sprint(",")"},
        {"len","int64(len(","))"},     {"length","int64(len(","))"},
        {"abs","math.Abs(",")"},       {"sqrt","math.Sqrt(",")"},
        {"floor","math.Floor(",")"},   {"ceil","math.Ceil(",")"},
        {"upper","strings.ToUpper(",")"}, {"lower","strings.ToLower(",")"},
        {"trim","strings.TrimSpace(",")"}, {NULL,NULL,NULL}
    };
    static const BuiltinSpelling ruby[] = {
        {"str","(",").to_s"},          {"to_string","(",").to_s"},
        {"int","(",").to_i"},          {"float","(",").to_f"},
        {"len","(",").length"},        {"length","(",").length"},
        {"abs","(",").abs"},           {"sqrt","Math.sqrt(",")"},
        {"upper","(",").upcase"},      {"lower","(",").downcase"},
        {"trim","(",").strip"},        {NULL,NULL,NULL}
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

static const char* java_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "long";
        case TYPE_FLOAT:  return "double";
        case TYPE_BOOL:   return "boolean";
        case TYPE_STRING: return "String";
        case TYPE_ARRAY:  return "long[]";
        case TYPE_VOID:   return "void";
        default:          return "Object";
    }
}

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

static const char* kotlin_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "Long";
        case TYPE_FLOAT:  return "Double";
        case TYPE_BOOL:   return "Boolean";
        case TYPE_STRING: return "String";
        case TYPE_ARRAY:  return "LongArray";
        case TYPE_VOID:   return "Unit";
        default:          return "Any";
    }
}

static const char* go_type(DataType t) {
    switch (t) {
        case TYPE_INT:    return "int64";
        case TYPE_FLOAT:  return "float64";
        case TYPE_BOOL:   return "bool";
        case TYPE_STRING: return "string";
        case TYPE_ARRAY:  return "[]int64";
        default:          return "interface{}";
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

static int both_int_operands(ASTNode *node) {
    return infer_expr_type(node->left)  == TYPE_INT &&
           infer_expr_type(node->right) == TYPE_INT;
}

/* Emits a special form and returns 1, or returns 0 to let the caller emit the
   ordinary infix expression. */
static int emit_special_binop(StringBuilder *sb, ASTNode *node,
                              TargetLang lang, ExprGen gen) {
    const char *op = node->value;
    if (!op) return 0;

    if (strcmp(op, "**") == 0) {
        int as_int = both_int_operands(node);
        switch (lang) {
            case LANG_PY:
            case LANG_RUBY:
            case LANG_JS:
                return 0;                      /* ** is native in these */
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

    if (strcmp(op, "/") == 0 && both_int_operands(node)) {
        if (lang == LANG_PY) {
            sb_append(sb, "(");
            gen(sb, node->left); sb_append(sb, " // ");
            gen(sb, node->right); sb_append(sb, ")");
            return 1;
        }
        if (lang == LANG_JS) {
            sb_append(sb, "Math.trunc(");
            gen(sb, node->left); sb_append(sb, " / ");
            gen(sb, node->right); sb_append(sb, ")");
            return 1;
        }
    }
    return 0;
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
            sb_append(sb, "%s", node->value ? node->value : "var");
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
            generate_expr_python(sb, node->left);
            sb_append(sb, "[");
            generate_expr_python(sb, node->right);
            sb_append(sb, "]");
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
            sb_append(sb, "%s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_python(sb, node->right);
            } else {
                sb_append(sb, "None");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "CONST");
            if (node->right) {
                generate_expr_python(sb, node->right);
            } else {
                sb_append(sb, "None");
            }
            sb_append(sb, "\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\ndef %s(", node->value ? node->value : "func");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                sb_append(sb, "%s", node->children[i]->value ? node->children[i]->value : "arg");
            }
            sb_append(sb, "):\n");
            if (node->body) {
                generate_node_python(sb, node->body, indent + 1);
            }
            if (!node->body || block_first(node->body) == NULL) {
                indent_code(sb, indent + 1);
                sb_append(sb, "pass\n");
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
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            sb_append(sb, "for %s in ", node->value ? node->value : "i");
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
            break;
            
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
    
    sb_append(sb, "#!/usr/bin/env python3\n");
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
    /* SUB prints a float with no fractional part as an integer (sqrt(16.0)
       is 4, not 4.0), so Python has to drop the trailing .0 to agree. */
    sb_append(sb, "    if isinstance(v, float) and v.is_integer():\n");
    sb_append(sb, "        return str(int(v))\n");
    sb_append(sb, "    return str(v)\n");
    sb_append(sb, "\n\ndef _sub_print(*a):\n");
    sb_append(sb, "    print(*[_sub_str(x) for x in a])\n\n");
    sb_append(sb, "import sys\n\n");
    sb_append(sb, "def _sub_add(a, b):\n");
    sb_append(sb, "    # SUB's '+' concatenates when either side is a string,\n");
    sb_append(sb, "    # otherwise adds numerically (mirrors the SUB interpreter).\n");
    sb_append(sb, "    if isinstance(a, str) or isinstance(b, str):\n");
    sb_append(sb, "        return str(a) + str(b)\n");
    sb_append(sb, "    return a + b\n\n");

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
            }
            if (node->value && strcmp(node->value, "show") == 0) {
                sb_append(sb, "console.log(");
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
            generate_expr_js(sb, node->left);
            sb_append(sb, "[");
            generate_expr_js(sb, node->right);
            sb_append(sb, "]");
            break;
        default:
            break;
    }
}

static void generate_node_js(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
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
                sb_append(sb, "console.log(");
                if (node->child_count > 0) generate_expr_js(sb, node->children[0]);
                sb_append(sb, ")");
            } else {
                generate_expr_js(sb, node);
            }
            sb_append(sb, ";\n");
            break;
            
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
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
    
    sb_append(sb, "// Generated by SUB Language Compiler\n\n");
    
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
            sb_append(sb, "%s", node->value ? node->value : "var");
            break;
        case AST_BINARY_EXPR:
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
                const BuiltinSpelling *bs = builtin_spelling(LANG_JAVA, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_java(sb, node->children[0]);
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
        case AST_ARRAY_LITERAL:
            sb_append(sb, "java.util.List.of(");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_java(sb, node->children[i]);
            }
            sb_append(sb, ")");
            break;
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
            generate_expr_java(sb, node->left);
            sb_append(sb, ".get(");
            generate_expr_java(sb, node->right);
            sb_append(sb, ")");
            break;
        default:
            fprintf(stderr, "Warning: Unsupported expression node %d in Java generator\n", node->type);
            break;
    }
}

static void generate_node_java(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    
    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_java(sb, stmt, indent);
            }
            break;
            
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_java(sb, node->right);
            else sb_append(sb, "null");
            sb_append(sb, ";\n");
            break;
            
        case AST_CONST_DECL:
            indent_code(sb, indent);
            sb_append(sb, "final var %s = ", node->value ? node->value : "CONST");
            if (node->right) generate_expr_java(sb, node->right);
            else sb_append(sb, "null");
            sb_append(sb, ";\n");
            break;
            
        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_code(sb, indent);
            sb_append(sb, "public static %s %s(", java_type(node->data_type),
                      node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "%s %s", java_type(node->children[i]->data_type),
                              node->children[i]->value ? node->children[i]->value : "arg");
                }
            }
            sb_append(sb, ") {\n");
            if (node->body) generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
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
            
        case AST_FOR_STMT:
            indent_code(sb, indent);
            {
                const char *var = node->value ? node->value : "i";
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
                } else {
                    sb_append(sb, "for (int %s = 0; %s < 10; %s++) {\n", var, var, var);
                }
            }
            generate_node_java(sb, node->body, indent + 1);
            indent_code(sb, indent);
            sb_append(sb, "}\n");
            break;
            
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
    sb_append(sb, "\n    static String _subStr(Object v) {\n");
    sb_append(sb, "        if (v instanceof Double) {\n");
    sb_append(sb, "            double d = (Double) v;\n");
    sb_append(sb, "            if (d == Math.floor(d) && !Double.isInfinite(d))\n");
    sb_append(sb, "                return String.valueOf((long) d);\n");
    sb_append(sb, "        }\n");
    sb_append(sb, "        return String.valueOf(v);\n");
    sb_append(sb, "    }\n");
    sb_append(sb, "\n    static void _subPrint(Object v) {\n");
    sb_append(sb, "        System.out.println(_subStr(v));\n");
    sb_append(sb, "    }\n");

    /* Two-pass approach: functions and non-function statements separated */
    StringBuilder *main_sb = sb_create();
    if (!main_sb) {
        sb_free(sb);
        return NULL;
    }

    if (ast->type == AST_PROGRAM) {
        /* Pass 1: emit function declarations as static methods */
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                generate_node_java(sb, stmt, 1);
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
        case AST_IDENTIFIER: sb_append(sb, "%s", node->value ? node->value : "var"); break;
        case AST_BINARY_EXPR:
            if (emit_special_binop(sb, node, LANG_SWIFT, generate_expr_swift)) break;
            sb_append(sb, "("); generate_expr_swift(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_swift(sb, node->right); sb_append(sb, ")"); break;
        case AST_CALL_EXPR:
            {
                const BuiltinSpelling *bs = builtin_spelling(LANG_SWIFT, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_swift(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(node->value)) sb_append(sb, "print(");
            else if (node->value) sb_append(sb, "%s(", node->value);
            else { generate_expr_swift(sb, node->left); sb_append(sb, "("); }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_swift(sb, node->children[i]);
            }
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_swift(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: 
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_swift(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_swift(sb, node->right); else sb_append(sb, "nil");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfunc %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "_ %s: %s", node->children[i]->value ? node->children[i]->value : "arg",
                              swift_type(node->children[i]->data_type));
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
                sb_append(sb, "for %s in ", node->value ? node->value : "i");
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
            } else {
                sb_append(sb, "for %s in 0..<10 {\n", node->value ? node->value : "i");
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
                generate_expr_swift(sb, node->right);
            }
            sb_append(sb, "\n");
            break;
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
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
    sb_append(sb, "// Generated by SUB\n\n");
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
        case AST_BINARY_EXPR:
            if (emit_special_binop(sb, node, LANG_KOTLIN, generate_expr_kotlin)) break;
            sb_append(sb, "("); generate_expr_kotlin(sb, node->left);
            sb_append(sb, " %s ", node->value ? node->value : "+");
            generate_expr_kotlin(sb, node->right); sb_append(sb, ")"); break;
        case AST_CALL_EXPR:
            {
                const BuiltinSpelling *bs = builtin_spelling(LANG_KOTLIN, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_kotlin(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(node->value)) sb_append(sb, "println(");
            else if (node->value) sb_append(sb, "%s(", node->value);
            else { generate_expr_kotlin(sb, node->left); sb_append(sb, "("); }
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                generate_expr_kotlin(sb, node->children[i]);
            }
            sb_append(sb, ")"); break;
        default: break;
    }
}

static void generate_node_kotlin(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;
    switch (node->type) {
        case AST_PROGRAM: 
            for (ASTNode *s = block_first(node); s; s = s->next) {
                generate_node_kotlin(sb, s, indent);
            }
            break;
        case AST_VAR_DECL:
            indent_code(sb, indent);
            sb_append(sb, "var %s = ", node->value ? node->value : "var");
            if (node->right) generate_expr_kotlin(sb, node->right); else sb_append(sb, "null");
            sb_append(sb, "\n"); break;
        case AST_FUNCTION_DECL:
            sb_append(sb, "\nfun %s(", node->value ? node->value : "func");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    sb_append(sb, "%s: %s", node->children[i]->value ? node->children[i]->value : "arg",
                              kotlin_type(node->children[i]->data_type));
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
                generate_expr_kotlin(sb, node->right);
            }
            sb_append(sb, "\n");
            break;
        case AST_ASSIGN_STMT:
            indent_code(sb, indent);
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
    sb_append(sb, "// Generated by SUB\n\n");
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

    if (ast->type == AST_PROGRAM) {
        for (ASTNode *stmt = block_first(ast); stmt != NULL; stmt = stmt->next) {
            if (stmt->type == AST_FUNCTION_DECL) {
                generate_node_kotlin(sb, stmt, 0);
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

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent);

static void generate_expr_ruby(StringBuilder *sb, ASTNode *node) {
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
            sb_append(sb, "%s", node->value ? node->value : "var");
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
            }
            if (is_print_builtin(func_name)) {
                sb_append(sb, "puts");
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
            generate_expr_ruby(sb, node->left);
            sb_append(sb, "[");
            generate_expr_ruby(sb, node->right);
            sb_append(sb, "]");
            break;

        default:
            break;
    }
}

static void generate_node_ruby(StringBuilder *sb, ASTNode *node, int indent) {
    if (!node) return;

    switch (node->type) {
        case AST_PROGRAM:
            for (ASTNode *stmt = block_first(node); stmt != NULL; stmt = stmt->next) {
                generate_node_ruby(sb, stmt, indent);
            }
            break;

        case AST_VAR_DECL:
            indent_ruby(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "var");
            if (node->right) {
                generate_expr_ruby(sb, node->right);
            } else {
                sb_append(sb, "nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_CONST_DECL:
            indent_ruby(sb, indent);
            sb_append(sb, "%s = ", node->value ? node->value : "CONST");
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
                        sb_append(sb, "%s", node->children[i]->value);
                    }
                }
                sb_append(sb, ")");
            }
            sb_append(sb, "\n");

            if (node->body) {
                generate_node_ruby(sb, node->body, indent + 1);
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
            generate_node_ruby(sb, node->body, indent + 1);
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

    sb_append(sb, "#!/usr/bin/env ruby\n");
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
                const BuiltinSpelling *bs = builtin_spelling(LANG_GO, node->value);
                if (bs && node->child_count == 1) {
                    sb_append(sb, "%s", bs->prefix);
                    generate_expr_go(sb, node->children[0]);
                    sb_append(sb, "%s", bs->suffix);
                    break;
                }
            }
            if (is_print_builtin(func_name)) {
                sb_append(sb, "fmt.Println(");
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children)
                        generate_expr_go(sb, node->children[i]);
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

        case AST_ARRAY_LITERAL:
            sb_append(sb, "[]interface{}{");
            for (int i = 0; i < node->child_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                if (node->children)
                    generate_expr_go(sb, node->children[i]);
            }
            sb_append(sb, "}");
            break;

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
            generate_expr_go(sb, node->left);
            sb_append(sb, "[");
            generate_expr_go(sb, node->right);
            sb_append(sb, "]");
            break;

        default:
            break;
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
            indent_go(sb, indent);
            sb_append(sb, "var %s", node->value ? node->value : "v");
            if (node->right) {
                sb_append(sb, " = ");
                generate_expr_go(sb, node->right);
            } else {
                sb_append(sb, " interface{} = nil");
            }
            sb_append(sb, "\n");
            break;

        case AST_CONST_DECL:
            indent_go(sb, indent);
            if (node->right && node->right->type == AST_LITERAL) {
                sb_append(sb, "const %s = ", node->value ? node->value : "C");
                generate_expr_go(sb, node->right);
            } else {
                sb_append(sb, "var %s", node->value ? node->value : "C");
                if (node->right) {
                    sb_append(sb, " = ");
                    generate_expr_go(sb, node->right);
                } else {
                    sb_append(sb, " interface{} = nil");
                }
            }
            sb_append(sb, "\n");
            break;

        case AST_FUNCTION_DECL:
            sb_append(sb, "\n");
            indent_go(sb, indent);
            sb_append(sb, "func %s(", node->value ? node->value : "fn");
            if (node->children && node->child_count > 0) {
                for (int i = 0; i < node->child_count; i++) {
                    if (i > 0) sb_append(sb, ", ");
                    if (node->children[i] && node->children[i]->value) {
                        /* Go needs a type per parameter, not one trailing type
                           shared by every name. */
                        sb_append(sb, "%s %s", node->children[i]->value,
                                  go_type(node->children[i]->data_type));
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
                    sb_append(sb, "for %s := ", var);
                    if (range->right) {
                        /* range(start, end) */
                        generate_expr_go(sb, range->left);
                        sb_append(sb, "; %s < ", var);
                        generate_expr_go(sb, range->right);
                    } else if (range->left) {
                        /* range(n) → 0..n */
                        sb_append(sb, "0; %s < ", var);
                        generate_expr_go(sb, range->left);
                    } else {
                        sb_append(sb, "0; %s < 10", var);
                    }
                    sb_append(sb, "; %s++ {\n", var);
                } else {
                    sb_append(sb, "for _, %s := range ",
                              node->value ? node->value : "item");
                    generate_expr_go(sb, range);
                    sb_append(sb, " {\n");
                }
            } else if (node->condition) {
                sb_append(sb, "for _, %s := range ",
                          node->value ? node->value : "item");
                generate_expr_go(sb, node->condition);
                sb_append(sb, " {\n");
            } else {
                sb_append(sb, "for %s := 0; %s < 10; %s++ {\n",
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
                generate_expr_go(sb, node->right);
            }
            sb_append(sb, "\n");
            break;

        case AST_BREAK_STMT:
            indent_go(sb, indent);
            sb_append(sb, "break\n");
            break;

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

    bool needs_fmt = ast_needs_fmt(ast);
    if (needs_fmt) {
        sb_append(sb, "import \"fmt\"\n\n");
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
            generate_node_go(sb, stmt, 0);
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
