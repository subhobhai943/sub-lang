#define _GNU_SOURCE
#include "sub_compiler.h"
#include "interpreter.h"
#include "common.h"
#include "logo.h"
#include "windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── REPL Configuration ─────────────────────────────────────── */

#define REPL_INPUT_MAX 4096
#define REPL_PROMPT "sub> "

/* ── Print a SubVal to stdout ───────────────────────────────── */

static void repl_print_val(SubVal v) {
    switch (v.type) {
        case VAL_INT:    printf("%lld\n", v.iv); break;
        case VAL_FLOAT:  printf("%g\n", v.fv);   break;
        case VAL_BOOL:   printf("%s\n", v.bv ? "true" : "false"); break;
        case VAL_STRING: printf("\"%s\"\n", v.sv ? v.sv : ""); break;
        case VAL_NULL:   printf("null\n"); break;
        case VAL_FUNC:   printf("<function>\n"); break;
        case VAL_ARRAY:  printf("<array>\n"); break;
        case VAL_OBJECT: printf("<object>\n"); break;
    }
}

/* ── Help text ──────────────────────────────────────────────── */

static void print_repl_help(void) {
    printf("SUB Interpreter - REPL Commands:\n");
    printf("  :help          Show this help message\n");
    printf("  :quit, :exit   Exit the interpreter\n");
    printf("  :q             Exit the interpreter (short form)\n");
    printf("  :load <file>   Load and execute a .sb file in the current session\n");
    printf("\n");
    printf("Type any SUB expression or statement to evaluate it.\n");
}

/* ── Read a single line of input ────────────────────────────── */

static char* read_line(const char *prompt) {
    fputs(prompt, stdout);
    fflush(stdout);

    char *buf = malloc(REPL_INPUT_MAX);
    if (!buf) return NULL;

    if (!fgets(buf, REPL_INPUT_MAX, stdin)) {
        free(buf);
        return NULL;
    }

    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    return buf;
}

/* ── Evaluate a single REPL line and print the result ──────── */

static void repl_eval_line(const char *line, Env *env) {
    /* Drop any abort left over from the previous line's runtime error. */
    interp_clear_abort();

    int ntok = 0;
    Token *toks = lexer_tokenize(line, &ntok);
    if (!toks || ntok == 0) return;

    ASTNode *ast = parser_parse(toks, ntok);
    if (!ast) {
        /* Parser error already printed to stderr */
        lexer_free_tokens(toks, ntok);
        return;
    }

    /* Soft semantic check */
    if (!semantic_analyze(ast)) {
        /* Warnings already printed to stderr; continue execution */
    }

    /* Evaluate */
    SubVal result = eval(ast, env);

    /* Print result for expression-like top-level nodes. A line that hit a
       runtime error has no meaningful value — printing one would just add a
       bare "null" under the error message. */
    if (result.type != VAL_NULL && !interp_aborted()) {
        ASTNode *last = ast->body;
        if (!last) {
            repl_print_val(result);
        } else {
            while (last->next) last = last->next;
            if (last->type == AST_BINARY_EXPR || last->type == AST_UNARY_EXPR ||
                last->type == AST_LITERAL || last->type == AST_IDENTIFIER ||
                last->type == AST_CALL_EXPR || last->type == AST_TERNARY_EXPR ||
                last->type == AST_MEMBER_ACCESS || last->type == AST_ARRAY_ACCESS ||
                last->type == AST_ARRAY_LITERAL || last->type == AST_OBJECT_LITERAL ||
                last->type == AST_NEW_EXPR || last->type == AST_RANGE_EXPR) {
                repl_print_val(result);
            }
        }
    }

    parser_free_ast(ast);
    lexer_free_tokens(toks, ntok);
}

/* ── :load command handler ──────────────────────────────────── */

static int repl_load_file(const char *filename, Env *env) {
    char *source = sub_read_file(filename);
    if (!source) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return 1;
    }

    int result = interpret_source(source, env);
    free(source);
    return result;
}

/* ── Main REPL loop ─────────────────────────────────────────── */

static int run_repl(void) {
    /* A runtime error should cost the user the current line, not the whole
       session and everything defined in it. */
    interp_set_repl_mode(1);

    Env *global_env = env_new(NULL);
    if (!global_env) {
        fprintf(stderr, "Error: failed to create REPL environment\n");
        return 1;
    }

    /* Multi-line accumulation buffer */
    char *accum = NULL;
    size_t accum_cap = 0;
    size_t accum_len = 0;
    int brace_depth = 0;

    printf(SUB_LOGO);
    printf("SUB Interactive REPL v" SUB_VERSION "\n");
    printf("Type :help for available commands.\n");
    printf("Type :quit or :exit to leave.\n\n");

    while (1) {
        const char *prompt = (accum || brace_depth > 0) ? "....> " : REPL_PROMPT;
        char *line = read_line(prompt);
        if (!line) {
            /* EOF (Ctrl+D) */
            printf("\n");
            break;
        }

        /* Skip empty lines (but not when accumulating) */
        if (line[0] == '\0' && !accum && brace_depth == 0) {
            free(line);
            continue;
        }

        /* REPL commands only at top level */
        if (line[0] == ':' && !accum && brace_depth == 0) {
            if (strcmp(line, ":quit") == 0 ||
                strcmp(line, ":exit") == 0 ||
                strcmp(line, ":q") == 0) {
                free(line);
                break;
            }

            if (strcmp(line, ":help") == 0) {
                print_repl_help();
                free(line);
                continue;
            }

            if (strncmp(line, ":load ", 6) == 0) {
                const char *filename = line + 6;
                while (*filename == ' ' || *filename == '\t') filename++;
                if (*filename) {
                    repl_load_file(filename, global_env);
                } else {
                    fprintf(stderr, "Usage: :load <file.sb>\n");
                }
                free(line);
                continue;
            }

            fprintf(stderr, "Unknown command: %s\n", line);
            fprintf(stderr, "Type :help for available commands.\n");
            free(line);
            continue;
        }

        /* Track brace/bracket/paren depth for multi-line support */
        for (const char *p = line; *p; p++) {
            if (*p == '{' || *p == '[' || *p == '(') brace_depth++;
            else if (*p == '}' || *p == ']' || *p == ')') {
                if (brace_depth > 0) brace_depth--;
            }
        }

        /* Append line to accumulator */
        size_t line_len = strlen(line);
        size_t needed = accum_len + line_len + 2; /* newline + null */
        if (needed > accum_cap) {
            size_t new_cap = accum_cap ? accum_cap * 2 : 256;
            while (new_cap < needed) new_cap *= 2;
            char *new_accum = realloc(accum, new_cap);
            if (!new_accum) {
                fprintf(stderr, "Error: out of memory\n");
                free(line);
                break;
            }
            accum = new_accum;
            accum_cap = new_cap;
        }
        if (accum_len > 0) {
            accum[accum_len++] = '\n';
        }
        memcpy(accum + accum_len, line, line_len + 1);
        accum_len += line_len;
        free(line);

        /* Evaluate when we're back to depth 0 */
        if (brace_depth <= 0) {
            repl_eval_line(accum, global_env);
            free(accum);
            accum = NULL;
            accum_len = 0;
            accum_cap = 0;
        }
    }

    /* Cleanup */
    if (accum) free(accum);
    env_free(global_env);
    return 0;
}

/* ── Entry Point ────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    sub_console_init_utf8();
    if (argc < 2) {
        return run_repl();
    }
    return interpret_file(argv[1]);
}
