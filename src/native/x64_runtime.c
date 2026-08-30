#define _GNU_SOURCE
/* ========================================
   SUB Language - native runtime, emitted as machine code
   ----------------------------------------
   A binary produced by this backend links against nothing. Everything a SUB
   program needs at runtime - allocation, writing to stdout, turning numbers
   into text, string handling - is emitted here as x86-64 instructions and
   copied into the executable ahead of the user's own code.

   The routines follow the SUB native ABI described in x64_internal.h. They
   are written against a deliberately small instruction repertoire and use no
   libc, so the only thing between a running SUB program and the kernel is a
   `syscall`.
   ======================================== */

#include "x64_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T (&c->text)

/* ----------------------------------------------------------------
   Context bookkeeping
   ---------------------------------------------------------------- */

static void *grow(void *p, int *cap, size_t elem) {
    int n = *cap ? *cap * 2 : 16;
    void *q = realloc(p, (size_t)n * elem);
    if (!q) abort();
    *cap = n;
    return q;
}

void nc_call_rt(NCtx *c, RtId id) {
    size_t site = e_call(T);
    if (c->rtfix_n == c->rtfix_cap)
        c->rtfix = grow(c->rtfix, &c->rtfix_cap, sizeof(RtFix));
    c->rtfix[c->rtfix_n].site = site;
    c->rtfix[c->rtfix_n].id   = (int)id;
    c->rtfix_n++;
}

void nc_call_fn(NCtx *c, const char *name) {
    size_t site = e_call(T);
    if (c->fnfix_n == c->fnfix_cap)
        c->fnfix = grow(c->fnfix, &c->fnfix_cap, sizeof(FnFix));
    c->fnfix[c->fnfix_n].site = site;
    c->fnfix[c->fnfix_n].name = strdup(name ? name : "");
    c->fnfix_n++;
}

void nc_define_fn(NCtx *c, const char *name) {
    if (c->fns_n == c->fns_cap)
        c->fns = grow(c->fns, &c->fns_cap, sizeof(FnDef));
    c->fns[c->fns_n].name = strdup(name ? name : "");
    c->fns[c->fns_n].off  = c->text.len;
    c->fns_n++;
}

size_t nc_intern(NCtx *c, const void *bytes, size_t n) {
    /* Sharing identical literals keeps repeated string constants from
       bloating the binary, and costs one linear scan per literal. */
    if (c->rodata.data && n <= c->rodata.len) {
        for (size_t i = 0; i + n <= c->rodata.len; i++)
            if (memcmp(c->rodata.data + i, bytes, n) == 0) return i;
    }
    size_t off = c->rodata.len;
    buf_bytes(&c->rodata, bytes, n);
    return off;
}

size_t nc_intern_str(NCtx *c, const char *s) {
    return nc_intern(c, s, strlen(s) + 1);
}

void nc_load_data(NCtx *c, int reg, size_t rodata_off) {
    e_mov_r_imm64(T, reg, 0);
    size_t site = c->text.len - 8;
    if (c->dfix_n == c->dfix_cap)
        c->dfix = grow(c->dfix, &c->dfix_cap, sizeof(DataFix));
    c->dfix[c->dfix_n].site = site;
    c->dfix[c->dfix_n].off  = rodata_off;
    c->dfix_n++;
}

void nc_load_cstr(NCtx *c, int reg, const char *s) {
    nc_load_data(c, reg, nc_intern_str(c, s));
}

void nc_fail(NCtx *c, const char *fmt, ...) {
    if (c->failed) return;           /* keep the first reason, not the last */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->err, sizeof(c->err), fmt, ap);
    va_end(ap);
    c->failed = 1;
}

void nc_resolve(NCtx *c, size_t rodata_off) {
    for (int i = 0; i < c->rtfix_n; i++)
        e_patch_rel32(T, c->rtfix[i].site, c->rt_off[c->rtfix[i].id]);

    for (int i = 0; i < c->fnfix_n; i++) {
        size_t target = 0;
        int found = 0;
        for (int j = 0; j < c->fns_n; j++)
            if (strcmp(c->fns[j].name, c->fnfix[i].name) == 0) {
                target = c->fns[j].off; found = 1; break;
            }
        if (!found) nc_fail(c, "call to undefined function '%s'", c->fnfix[i].name);
        e_patch_rel32(T, c->fnfix[i].site, target);
    }

    uint64_t base = X64_TEXT_BASE + elf64_text_offset() + rodata_off;
    for (int i = 0; i < c->dfix_n; i++) {
        uint64_t addr = base + c->dfix[i].off;
        memcpy(c->text.data + c->dfix[i].site, &addr, 8);
    }
}

/* ----------------------------------------------------------------
   Small emit helpers used by several routines
   ---------------------------------------------------------------- */

/* Store the literal byte `ch` at [cursor] and advance cursor. Clobbers RDX. */
static void put_char(NCtx *c, int cursor, int ch) {
    e_mov_r_imm64(T, RDX, (uint64_t)ch);
    e_mov_mem8_r(T, cursor, 0, RDX);
    e_add_r_imm(T, cursor, 1);
}

/* Jump target bookkeeping. A "site" is the offset of a rel32 to be filled in
   once the destination is known; `here` closes it at the current position. */
static void here(NCtx *c, size_t site) { e_patch_rel32(T, site, c->text.len); }

/* ----------------------------------------------------------------
   RT_ALLOC - bump allocator over the BSS heap
   ---------------------------------------------------------------- */

static void emit_alloc(NCtx *c) {
    /* Nothing is ever freed. SUB programs are short-lived and the heap is
       64 MB of lazily-faulted zero pages, so a bump pointer buys simplicity
       at a cost that has not yet mattered. */
    e_mov_r_imm64(T, R11, X64_G_HEAPPTR);
    e_mov_r_mem(T, RAX, R11, 0);
    e_add_r_imm(T, RDI, 15);
    e_and_r_imm(T, RDI, -16);
    e_add_r_r(T, RDI, RAX);
    e_mov_mem_r(T, R11, 0, RDI);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_WRITE - write(1, rdi, rsi)
   ---------------------------------------------------------------- */

static void emit_write(NCtx *c) {
    e_mov_r_r(T, RDX, RSI);
    e_mov_r_r(T, RSI, RDI);
    e_mov_r_imm64(T, RDI, 1);
    e_mov_r_imm64(T, RAX, 1);
    e_syscall(T);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_STRLEN
   ---------------------------------------------------------------- */

static void emit_strlen(NCtx *c) {
    e_mov_r_r(T, RCX, RDI);
    size_t top = T->len;
    e_movzx_r_mem8(T, RDX, RCX, 0);
    e_test_r_r(T, RDX, RDX);
    size_t done = e_jcc(T, CC_E);
    e_add_r_imm(T, RCX, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);
    here(c, done);
    e_mov_r_r(T, RAX, RCX);
    e_sub_r_r(T, RAX, RDI);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_PUTSN / RT_PUTS
   ---------------------------------------------------------------- */

static void emit_putsn(NCtx *c) {
    e_push(T, RDI);
    nc_call_rt(c, RT_STRLEN);
    e_pop(T, RDI);
    e_mov_r_r(T, RSI, RAX);
    nc_call_rt(c, RT_WRITE);
    e_ret(T);
}

static void emit_puts(NCtx *c) {
    nc_call_rt(c, RT_PUTSN);
    nc_load_cstr(c, RDI, "\n");
    e_mov_r_imm64(T, RSI, 1);
    nc_call_rt(c, RT_WRITE);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_I2S - signed 64-bit integer to decimal
   ---------------------------------------------------------------- */

static void emit_i2s(NCtx *c) {
    e_push(T, RBX);
    e_push(T, R12);
    e_mov_r_r(T, R12, RDI);              /* value */
    e_mov_r_imm64(T, RDI, 32);
    nc_call_rt(c, RT_ALLOC);
    e_mov_r_r(T, RBX, RAX);              /* buffer */

    e_mov_r_r(T, RCX, RBX);
    e_add_r_imm(T, RCX, 31);
    e_xor_r_r(T, RDX, RDX);
    e_mov_mem8_r(T, RCX, 0, RDX);        /* terminating NUL */

    e_xor_r_r(T, R8, R8);                /* negative? */
    e_mov_r_r(T, RAX, R12);
    e_test_r_r(T, RAX, RAX);
    size_t pos = e_jcc(T, CC_NS);
    e_mov_r_imm64(T, R8, 1);
    e_neg_r(T, RAX);
    here(c, pos);

    e_mov_r_imm64(T, R10, 10);
    size_t loop = T->len;
    e_xor_r_r(T, RDX, RDX);
    e_div_r(T, R10);                     /* rax = q, rdx = digit */
    e_add_r_imm(T, RDX, '0');
    e_sub_r_imm(T, RCX, 1);
    e_mov_mem8_r(T, RCX, 0, RDX);
    e_test_r_r(T, RAX, RAX);
    size_t again = e_jcc(T, CC_NE);
    e_patch_rel32(T, again, loop);

    e_test_r_r(T, R8, R8);
    size_t done = e_jcc(T, CC_E);
    e_sub_r_imm(T, RCX, 1);
    e_mov_r_imm64(T, RDX, '-');
    e_mov_mem8_r(T, RCX, 0, RDX);
    here(c, done);

    e_mov_r_r(T, RAX, RCX);
    e_pop(T, R12);
    e_pop(T, RBX);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_B2S
   ---------------------------------------------------------------- */

static void emit_b2s(NCtx *c) {
    nc_load_cstr(c, RAX, "false");
    nc_load_cstr(c, RCX, "true");
    e_test_r_r(T, RDI, RDI);
    e_cmovcc_r_r(T, CC_NE, RAX, RCX);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_CONCAT
   ---------------------------------------------------------------- */

static void emit_concat(NCtx *c) {
    e_push(T, R12); e_push(T, R13); e_push(T, R14); e_push(T, R15);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_r(T, R13, RSI);

    e_mov_r_r(T, RDI, R12); nc_call_rt(c, RT_STRLEN); e_mov_r_r(T, R14, RAX);
    e_mov_r_r(T, RDI, R13); nc_call_rt(c, RT_STRLEN); e_mov_r_r(T, R15, RAX);

    e_mov_r_r(T, RDI, R14);
    e_add_r_r(T, RDI, R15);
    e_add_r_imm(T, RDI, 1);
    nc_call_rt(c, RT_ALLOC);             /* rax = destination, kept intact */

    e_mov_r_r(T, RCX, RAX);              /* write cursor */
    for (int part = 0; part < 2; part++) {
        e_mov_r_r(T, RDX, part == 0 ? R12 : R13);
        size_t top = T->len;
        e_movzx_r_mem8(T, R8, RDX, 0);
        e_test_r_r(T, R8, R8);
        size_t out = e_jcc(T, CC_E);
        e_mov_mem8_r(T, RCX, 0, R8);
        e_add_r_imm(T, RCX, 1);
        e_add_r_imm(T, RDX, 1);
        size_t back = e_jmp(T); e_patch_rel32(T, back, top);
        here(c, out);
    }
    e_xor_r_r(T, R8, R8);
    e_mov_mem8_r(T, RCX, 0, R8);

    e_pop(T, R15); e_pop(T, R14); e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_STRCMP
   ---------------------------------------------------------------- */

static void emit_strcmp(NCtx *c) {
    size_t top = T->len;
    e_movzx_r_mem8(T, RAX, RDI, 0);
    e_movzx_r_mem8(T, RCX, RSI, 0);
    e_cmp_r_r(T, RAX, RCX);
    size_t diff = e_jcc(T, CC_NE);
    e_test_r_r(T, RAX, RAX);
    size_t eq = e_jcc(T, CC_E);
    e_add_r_imm(T, RDI, 1);
    e_add_r_imm(T, RSI, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);

    here(c, diff);
    e_sub_r_r(T, RAX, RCX);
    e_ret(T);

    here(c, eq);
    e_xor_r_r(T, RAX, RAX);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_STRCASE - upper (rsi=1) / lower (rsi=0)
   ---------------------------------------------------------------- */

static void emit_strcase(NCtx *c) {
    e_push(T, R12); e_push(T, R13);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_r(T, R13, RSI);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, RDI, RAX);
    e_add_r_imm(T, RDI, 1);
    nc_call_rt(c, RT_ALLOC);

    e_mov_r_r(T, RCX, RAX);
    e_mov_r_r(T, RDX, R12);
    size_t top = T->len;
    e_movzx_r_mem8(T, R8, RDX, 0);
    e_test_r_r(T, R8, R8);
    size_t out = e_jcc(T, CC_E);

    e_test_r_r(T, R13, R13);
    size_t to_lower = e_jcc(T, CC_E);
    /* upper: 'a'..'z' -> subtract 32 */
    e_cmp_r_imm(T, R8, 'a');
    size_t u1 = e_jcc(T, CC_L);
    e_cmp_r_imm(T, R8, 'z');
    size_t u2 = e_jcc(T, CC_G);
    e_sub_r_imm(T, R8, 32);
    size_t to_store = e_jmp(T);
    here(c, u1); here(c, u2);
    size_t to_store2 = e_jmp(T);
    /* lower: 'A'..'Z' -> add 32 */
    here(c, to_lower);
    e_cmp_r_imm(T, R8, 'A');
    size_t l1 = e_jcc(T, CC_L);
    e_cmp_r_imm(T, R8, 'Z');
    size_t l2 = e_jcc(T, CC_G);
    e_add_r_imm(T, R8, 32);
    here(c, l1); here(c, l2);
    here(c, to_store); here(c, to_store2);

    e_mov_mem8_r(T, RCX, 0, R8);
    e_add_r_imm(T, RCX, 1);
    e_add_r_imm(T, RDX, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);

    here(c, out);
    e_xor_r_r(T, R8, R8);
    e_mov_mem8_r(T, RCX, 0, R8);
    e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_TRIM
   ---------------------------------------------------------------- */

/* Emits: compare the byte in `reg` against space/tab/newline/CR and jump to
   the collected sites when it is whitespace. */
static void ws_checks(NCtx *c, int reg, size_t *sites, int *n) {
    static const int ws[] = { ' ', '\t', '\n', '\r' };
    for (int i = 0; i < 4; i++) {
        e_cmp_r_imm(T, reg, ws[i]);
        sites[(*n)++] = e_jcc(T, CC_E);
    }
}

static void emit_trim(NCtx *c) {
    e_push(T, R12); e_push(T, R13);

    e_mov_r_r(T, RCX, RDI);
    size_t lead = T->len;
    e_movzx_r_mem8(T, RDX, RCX, 0);
    size_t sites[4]; int n = 0;
    ws_checks(c, RDX, sites, &n);
    size_t lead_done = e_jmp(T);
    for (int i = 0; i < n; i++) here(c, sites[i]);
    e_add_r_imm(T, RCX, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, lead);
    here(c, lead_done);

    e_mov_r_r(T, RSI, RCX);
    size_t find = T->len;
    e_movzx_r_mem8(T, RDX, RSI, 0);
    e_test_r_r(T, RDX, RDX);
    size_t found = e_jcc(T, CC_E);
    e_add_r_imm(T, RSI, 1);
    back = e_jmp(T); e_patch_rel32(T, back, find);
    here(c, found);

    size_t bk = T->len;
    e_cmp_r_r(T, RSI, RCX);
    size_t len_done = e_jcc(T, CC_BE);
    e_movzx_r_mem8(T, RDX, RSI, -1);
    n = 0;
    ws_checks(c, RDX, sites, &n);
    size_t len_done2 = e_jmp(T);
    for (int i = 0; i < n; i++) here(c, sites[i]);
    e_sub_r_imm(T, RSI, 1);
    back = e_jmp(T); e_patch_rel32(T, back, bk);
    here(c, len_done); here(c, len_done2);

    e_mov_r_r(T, R12, RCX);              /* start */
    e_mov_r_r(T, R13, RSI);
    e_sub_r_r(T, R13, R12);              /* length */

    e_mov_r_r(T, RDI, R13);
    e_add_r_imm(T, RDI, 1);
    nc_call_rt(c, RT_ALLOC);

    e_mov_r_r(T, RCX, RAX);
    e_mov_r_r(T, RDX, R12);
    size_t top = T->len;
    e_test_r_r(T, R13, R13);
    size_t out = e_jcc(T, CC_E);
    e_movzx_r_mem8(T, R8, RDX, 0);
    e_mov_mem8_r(T, RCX, 0, R8);
    e_add_r_imm(T, RCX, 1);
    e_add_r_imm(T, RDX, 1);
    e_sub_r_imm(T, R13, 1);
    back = e_jmp(T); e_patch_rel32(T, back, top);
    here(c, out);
    e_xor_r_r(T, R8, R8);
    e_mov_mem8_r(T, RCX, 0, R8);

    e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* ----------------------------------------------------------------
   Integer division and modulo
   ----------------------------------------------------------------
   SUB's `/` on two integers truncates toward zero, matching the interpreter.
   Division by zero yields 0 rather than trapping, so a SUB program cannot be
   killed by SIGFPE; and `x / -1` is special-cased because IDIV raises #DE on
   LLONG_MIN / -1. */

/* ----------------------------------------------------------------
   RT_FIND, RT_SUBSTR, RT_CHARAT, RT_CONTAINS, RT_REPLACE, RT_SPLIT, RT_JOIN

   The character-level string builtins. The interpreter is the specification
   for all of them, and it is stricter than the obvious implementation in
   several places: substring clamps both bounds into the string rather than
   counting a negative one from the end, replace leaves the string alone when
   the pattern is empty, split keeps empty fields and always yields at least
   one, and char_at counts a negative index from the end but stops the
   program when the result is still outside.
   ---------------------------------------------------------------- */

/* rdi=haystack, rsi=needle, rdx=start -> rax = index, or -1.
   An empty needle matches at `start`, which is what makes contains(s, "")
   true the way strstr does. */
static void emit_find(NCtx *c) {
    e_push(T, R12); e_push(T, R13); e_push(T, R14);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_r(T, R13, RSI);
    e_mov_r_r(T, R14, RDX);              /* i */

    size_t loop = T->len;
    e_mov_r_r(T, RCX, R12);
    e_add_r_r(T, RCX, R14);              /* p = hay + i */
    e_mov_r_r(T, RDX, R13);              /* q = needle */

    size_t inner = T->len;
    e_movzx_r_mem8(T, R8, RDX, 0);
    e_test_r_r(T, R8, R8);
    size_t found = e_jcc(T, CC_E);       /* needle ran out: a match */
    e_movzx_r_mem8(T, R9, RCX, 0);
    e_test_r_r(T, R9, R9);
    size_t missing = e_jcc(T, CC_E);     /* haystack ran out: no match */
    e_cmp_r_r(T, R8, R9);
    size_t advance = e_jcc(T, CC_NE);
    e_add_r_imm(T, RCX, 1);
    e_add_r_imm(T, RDX, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, inner);

    here(c, advance);
    e_add_r_imm(T, R14, 1);
    size_t again = e_jmp(T); e_patch_rel32(T, again, loop);

    here(c, found);
    e_mov_r_r(T, RAX, R14);
    size_t out = e_jmp(T);

    here(c, missing);
    e_mov_r_imm64(T, RAX, (uint64_t)-1);

    here(c, out);
    e_pop(T, R14); e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* rdi=ptr, rsi=start, rdx=end -> rax = a fresh copy of [start, end) */
static void emit_substr(NCtx *c) {
    e_push(T, RBX); e_push(T, R12); e_push(T, R13); e_push(T, R14);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_r(T, R13, RSI);              /* start */
    e_mov_r_r(T, R14, RDX);              /* end */

    e_mov_r_r(T, RDI, R12);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, RBX, RAX);              /* n */

    /* Both bounds into [0, n], then start no further along than end. */
    e_xor_r_r(T, RCX, RCX);
    e_cmp_r_r(T, R13, RCX);
    e_cmovcc_r_r(T, CC_L, R13, RCX);
    e_cmp_r_r(T, R14, RCX);
    e_cmovcc_r_r(T, CC_L, R14, RCX);
    e_cmp_r_r(T, R13, RBX);
    e_cmovcc_r_r(T, CC_G, R13, RBX);
    e_cmp_r_r(T, R14, RBX);
    e_cmovcc_r_r(T, CC_G, R14, RBX);
    e_cmp_r_r(T, R13, R14);
    e_cmovcc_r_r(T, CC_G, R13, R14);

    e_sub_r_r(T, R14, R13);              /* length */
    e_mov_r_r(T, RDI, R14);
    e_add_r_imm(T, RDI, 1);
    nc_call_rt(c, RT_ALLOC);

    e_mov_r_r(T, RCX, RAX);              /* write cursor; rax is the answer */
    e_mov_r_r(T, RDX, R12);
    e_add_r_r(T, RDX, R13);              /* read cursor */
    e_xor_r_r(T, R9, R9);
    size_t top = T->len;
    e_cmp_r_r(T, R9, R14);
    size_t done = e_jcc(T, CC_GE);
    e_movzx_r_mem8(T, R8, RDX, 0);
    e_mov_mem8_r(T, RCX, 0, R8);
    e_add_r_imm(T, RCX, 1);
    e_add_r_imm(T, RDX, 1);
    e_add_r_imm(T, R9, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);
    here(c, done);
    e_xor_r_r(T, R8, R8);
    e_mov_mem8_r(T, RCX, 0, R8);

    e_pop(T, R14); e_pop(T, R13); e_pop(T, R12); e_pop(T, RBX);
    e_ret(T);
}

/* rdi=ptr, rsi=index -> rax = a one-character string. A negative index
   counts from the end; anything still outside stops the program. */
static void emit_charat(NCtx *c) {
    e_push(T, R12); e_push(T, R13); e_push(T, R14);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_r(T, R13, RSI);

    e_mov_r_r(T, RDI, R12);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, R14, RAX);              /* n */

    e_xor_r_r(T, RCX, RCX);
    e_cmp_r_r(T, R13, RCX);
    size_t nonneg = e_jcc(T, CC_GE);
    e_add_r_r(T, R13, R14);
    here(c, nonneg);

    e_xor_r_r(T, RCX, RCX);
    e_cmp_r_r(T, R13, RCX);
    size_t low = e_jcc(T, CC_L);
    e_cmp_r_r(T, R13, R14);
    size_t high = e_jcc(T, CC_GE);
    size_t ok = e_jmp(T);
    here(c, low); here(c, high);
    nc_load_cstr(c, RDI, "char_at index out of range");
    nc_call_rt(c, RT_DIE);
    here(c, ok);

    e_mov_r_imm64(T, RDI, 2);
    nc_call_rt(c, RT_ALLOC);
    e_mov_r_r(T, RCX, R12);
    e_add_r_r(T, RCX, R13);
    e_movzx_r_mem8(T, R8, RCX, 0);
    e_mov_mem8_r(T, RAX, 0, R8);
    e_xor_r_r(T, R8, R8);
    e_mov_mem8_r(T, RAX, 1, R8);

    e_pop(T, R14); e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* rdi=haystack, rsi=needle -> rax = 0 or 1 */
static void emit_contains(NCtx *c) {
    e_xor_r_r(T, RDX, RDX);
    nc_call_rt(c, RT_FIND);
    e_mov_r_imm64(T, RCX, (uint64_t)-1);
    e_cmp_r_r(T, RAX, RCX);
    e_setcc(T, CC_NE, RAX);
    e_movzx_r_r8(T, RAX, RAX);
    e_ret(T);
}

/* rdi=ptr, rsi=old, rdx=new -> rax = ptr. An empty pattern gives the string
   back unchanged rather than being inserted between every character.

   Built by concatenation -- the run before each hit, then the replacement.
   That allocates more than a counted single pass would, and it is a great
   deal easier to be sure of; RBP is pressed into service as a sixth
   callee-saved register to hold the search position across the calls. */
static void emit_replace(NCtx *c) {
    e_push(T, RBX); e_push(T, RBP); e_push(T, R12);
    e_push(T, R13); e_push(T, R14); e_push(T, R15);
    e_mov_r_r(T, R12, RDI);              /* s */
    e_mov_r_r(T, R13, RSI);              /* old */
    e_mov_r_r(T, R14, RDX);              /* new */

    e_mov_r_r(T, RDI, R13);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, R15, RAX);              /* old length */
    e_test_r_r(T, R15, R15);
    size_t nonempty = e_jcc(T, CC_NE);
    e_mov_r_r(T, RAX, R12);
    size_t bail = e_jmp(T);
    here(c, nonempty);

    nc_load_cstr(c, RBX, "");            /* result so far */
    e_xor_r_r(T, RBP, RBP);              /* pos */

    size_t loop = T->len;
    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, R13);
    e_mov_r_r(T, RDX, RBP);
    nc_call_rt(c, RT_FIND);
    e_mov_r_imm64(T, RCX, (uint64_t)-1);
    e_cmp_r_r(T, RAX, RCX);
    size_t tail = e_jcc(T, CC_E);
    e_mov_r_r(T, R9, RAX);               /* hit */

    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, RBP);
    e_mov_r_r(T, RDX, R9);
    e_mov_r_r(T, RBP, R9);
    e_add_r_r(T, RBP, R15);              /* pos = hit + old length */
    nc_call_rt(c, RT_SUBSTR);
    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RDI, RAX);
    e_mov_r_r(T, RSI, R14);
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RBX, RAX);
    size_t again = e_jmp(T); e_patch_rel32(T, again, loop);

    here(c, tail);
    e_mov_r_r(T, RDI, R12);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, RDX, RAX);
    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, RBP);
    nc_call_rt(c, RT_SUBSTR);
    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_CONCAT);

    here(c, bail);
    e_pop(T, R15); e_pop(T, R14); e_pop(T, R13);
    e_pop(T, R12); e_pop(T, RBP); e_pop(T, RBX);
    e_ret(T);
}

/* rdi=ptr, rsi=sep -> rax = array of strings. Every field is kept, and the
   result always has at least one element. An empty separator cuts the string
   into single characters. */
static void emit_split(NCtx *c) {
    e_push(T, RBX); e_push(T, RBP); e_push(T, R12);
    e_push(T, R13); e_push(T, R14); e_push(T, R15);
    e_mov_r_r(T, R12, RDI);              /* s */
    e_mov_r_r(T, R13, RSI);              /* sep */

    e_mov_r_imm64(T, RDI, 4);
    e_mov_r_imm64(T, RSI, AK_STRING);
    nc_call_rt(c, RT_ARR_NEW);
    e_mov_r_r(T, RBX, RAX);              /* the array */

    e_mov_r_r(T, RDI, R13);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, R15, RAX);              /* separator length */
    e_test_r_r(T, R15, R15);
    size_t by_sep = e_jcc(T, CC_NE);

    /* No separator: one element per character. */
    e_xor_r_r(T, R14, R14);
    size_t ch_loop = T->len;
    e_mov_r_r(T, RCX, R12);
    e_add_r_r(T, RCX, R14);
    e_movzx_r_mem8(T, R8, RCX, 0);
    e_test_r_r(T, R8, R8);
    size_t ch_done = e_jcc(T, CC_E);
    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, R14);
    e_mov_r_r(T, RDX, R14);
    e_add_r_imm(T, RDX, 1);
    nc_call_rt(c, RT_SUBSTR);
    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_ARR_PUSH);
    e_add_r_imm(T, R14, 1);
    size_t ch_back = e_jmp(T); e_patch_rel32(T, ch_back, ch_loop);
    here(c, ch_done);
    size_t finish = e_jmp(T);

    here(c, by_sep);
    e_xor_r_r(T, RBP, RBP);              /* pos */
    size_t loop = T->len;
    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, R13);
    e_mov_r_r(T, RDX, RBP);
    nc_call_rt(c, RT_FIND);
    e_mov_r_imm64(T, RCX, (uint64_t)-1);
    e_cmp_r_r(T, RAX, RCX);
    size_t last = e_jcc(T, CC_E);
    e_mov_r_r(T, R14, RAX);              /* hit */

    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, RBP);
    e_mov_r_r(T, RDX, R14);
    nc_call_rt(c, RT_SUBSTR);
    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_ARR_PUSH);
    e_mov_r_r(T, RBP, R14);
    e_add_r_r(T, RBP, R15);              /* pos = hit + separator length */
    size_t again = e_jmp(T); e_patch_rel32(T, again, loop);

    here(c, last);
    e_mov_r_r(T, RDI, R12);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, RDX, RAX);
    e_mov_r_r(T, RDI, R12);
    e_mov_r_r(T, RSI, RBP);
    nc_call_rt(c, RT_SUBSTR);
    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_ARR_PUSH);

    here(c, finish);
    e_mov_r_r(T, RAX, RBX);
    e_pop(T, R15); e_pop(T, R14); e_pop(T, R13);
    e_pop(T, R12); e_pop(T, RBP); e_pop(T, RBX);
    e_ret(T);
}

/* rdi=array, rsi=separator -> rax = ptr. The same walk as printing an array,
   without the brackets and with the caller's separator. */
static void emit_join(NCtx *c) {
    e_push(T, RBX); e_push(T, R12); e_push(T, R13);
    e_push(T, R14); e_push(T, R15);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_r(T, R15, RSI);

    nc_load_cstr(c, RBX, "");

    e_xor_r_r(T, R13, R13);
    size_t loop = T->len;
    e_mov_r_mem(T, RCX, R12, ARR_COUNT);
    e_cmp_r_r(T, R13, RCX);
    size_t done = e_jcc(T, CC_GE);

    e_test_r_r(T, R13, R13);
    size_t first = e_jcc(T, CC_E);
    e_mov_r_r(T, RDI, RBX);
    e_mov_r_r(T, RSI, R15);
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RBX, RAX);
    here(c, first);

    e_mov_r_mem(T, RCX, R12, ARR_DATA);
    e_mov_r_r(T, RDX, R13);
    e_shl_r_imm8(T, RDX, 3);
    e_add_r_r(T, RCX, RDX);
    e_mov_r_mem(T, RDI, RCX, 0);
    e_mov_r_mem(T, R14, R12, ARR_KIND);

    e_cmp_r_imm(T, R14, AK_FLOAT);
    size_t not_f = e_jcc(T, CC_NE);
    nc_call_rt(c, RT_F2S);
    size_t have = e_jmp(T);
    here(c, not_f);
    e_cmp_r_imm(T, R14, AK_STRING);
    size_t not_s = e_jcc(T, CC_NE);
    e_mov_r_r(T, RAX, RDI);
    size_t have2 = e_jmp(T);
    here(c, not_s);
    e_cmp_r_imm(T, R14, AK_BOOL);
    size_t not_b = e_jcc(T, CC_NE);
    nc_call_rt(c, RT_B2S);
    size_t have3 = e_jmp(T);
    here(c, not_b);
    nc_call_rt(c, RT_I2S);
    here(c, have); here(c, have2); here(c, have3);

    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RBX, RAX);

    e_add_r_imm(T, R13, 1);
    size_t again = e_jmp(T); e_patch_rel32(T, again, loop);
    here(c, done);

    e_mov_r_r(T, RAX, RBX);
    e_pop(T, R15); e_pop(T, R14); e_pop(T, R13);
    e_pop(T, R12); e_pop(T, RBX);
    e_ret(T);
}

/* RT_DIE - report a runtime error the way the interpreter does and stop.
   The interpreter exits 70 on `7 / 0`; a compiled program that returned 0
   and carried on would be a different language.

   The writes go straight to fd 2 rather than through RT_WRITE, which is
   hard-wired to stdout. Nothing here returns, so the register discipline
   only has to hold until the exit syscall. */
static void emit_die(NCtx *c) {
    e_mov_r_r(T, R12, RDI);              /* the message */

    nc_load_cstr(c, RSI, "RuntimeError: ");
    e_mov_r_imm64(T, RDX, 14);
    e_mov_r_imm64(T, RDI, 2);
    e_mov_r_imm64(T, RAX, 1);
    e_syscall(T);

    e_mov_r_r(T, RDI, R12);
    nc_call_rt(c, RT_STRLEN);
    e_mov_r_r(T, RDX, RAX);
    e_mov_r_r(T, RSI, R12);
    e_mov_r_imm64(T, RDI, 2);
    e_mov_r_imm64(T, RAX, 1);
    e_syscall(T);

    nc_load_cstr(c, RSI, "\n");
    e_mov_r_imm64(T, RDX, 1);
    e_mov_r_imm64(T, RDI, 2);
    e_mov_r_imm64(T, RAX, 1);
    e_syscall(T);

    e_mov_r_imm64(T, RAX, 60);           /* exit_group */
    e_mov_r_imm64(T, RDI, 70);
    e_syscall(T);
    e_ret(T);                            /* unreachable */
}

static void emit_idiv(NCtx *c) {
    e_test_r_r(T, RSI, RSI);
    size_t zero = e_jcc(T, CC_E);
    e_cmp_r_imm(T, RSI, -1);
    size_t neg1 = e_jcc(T, CC_E);
    e_mov_r_r(T, RAX, RDI);
    e_cqo(T);
    e_idiv_r(T, RSI);
    e_ret(T);
    here(c, neg1);
    e_mov_r_r(T, RAX, RDI);
    e_neg_r(T, RAX);
    e_ret(T);
    here(c, zero);
    nc_load_cstr(c, RDI, "division by zero");
    nc_call_rt(c, RT_DIE);
    e_ret(T);
}

static void emit_imod(NCtx *c) {
    e_test_r_r(T, RSI, RSI);
    size_t zero = e_jcc(T, CC_E);
    e_cmp_r_imm(T, RSI, -1);
    size_t neg1 = e_jcc(T, CC_E);
    e_mov_r_r(T, RAX, RDI);
    e_cqo(T);
    e_idiv_r(T, RSI);
    e_mov_r_r(T, RAX, RDX);
    e_ret(T);
    here(c, neg1);
    e_xor_r_r(T, RAX, RAX);
    e_ret(T);
    here(c, zero);
    nc_load_cstr(c, RDI, "modulo by zero");
    nc_call_rt(c, RT_DIE);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_IPOW - integer exponentiation by repeated multiplication
   ---------------------------------------------------------------- */

static void emit_ipow(NCtx *c) {
    e_mov_r_imm64(T, RAX, 1);
    e_cmp_r_imm(T, RSI, 0);
    size_t done = e_jcc(T, CC_LE);
    size_t loop = T->len;
    e_imul_r_r(T, RAX, RDI);
    e_sub_r_imm(T, RSI, 1);
    size_t again = e_jcc(T, CC_NE);
    e_patch_rel32(T, again, loop);
    here(c, done);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_FMOD - fmod(a, b) = a - trunc(a/b)*b
   ---------------------------------------------------------------- */

/* Dividing by zero is a runtime error whatever the operand types, so the
   float path checks for it too rather than quietly producing an infinity. */
static void emit_fzero_check(NCtx *c, const char *what) {
    e_mov_r_r(T, RCX, RSI);
    e_mov_r_imm64(T, RDX, 0x7FFFFFFFFFFFFFFFULL);
    e_and_r_r(T, RCX, RDX);              /* strip the sign, so -0.0 counts */
    e_test_r_r(T, RCX, RCX);
    size_t ok = e_jcc(T, CC_NE);
    e_push(T, RDI);
    nc_load_cstr(c, RDI, what);
    nc_call_rt(c, RT_DIE);
    e_pop(T, RDI);
    here(c, ok);
}

static void emit_fdiv(NCtx *c) {
    emit_fzero_check(c, "division by zero");
    e_movq_x_r(T, XMM0, RDI);
    e_movq_x_r(T, XMM1, RSI);
    e_divsd(T, XMM0, XMM1);
    e_movq_r_x(T, RAX, XMM0);
    e_ret(T);
}

static void emit_fmod(NCtx *c) {
    emit_fzero_check(c, "modulo by zero");
    e_movq_x_r(T, XMM0, RDI);
    e_movq_x_r(T, XMM1, RSI);
    e_movsd_x_x(T, XMM2, XMM0);
    e_divsd(T, XMM2, XMM1);
    e_cvttsd2si(T, RAX, XMM2);
    e_cvtsi2sd(T, XMM2, RAX);
    e_mulsd(T, XMM2, XMM1);
    e_subsd(T, XMM0, XMM2);
    e_movq_r_x(T, RAX, XMM0);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_FPOW
   ----------------------------------------------------------------
   An exponent that happens to be a whole number - which covers nearly every
   `**` a SUB program writes, including the negative ones - is done by
   repeated multiplication, so `2 ** 10` is exactly 1024 and a negative base
   keeps its sign. Anything else falls back to the x87 identity
   2^(y*log2(x)), which is how pow() has classically been computed and needs
   no library. */

static void emit_fpow(NCtx *c) {
    e_push(T, RBP);
    e_mov_r_r(T, RBP, RSP);
    e_sub_r_imm(T, RSP, 32);

    e_movq_x_r(T, XMM0, RDI);            /* base */
    e_movq_x_r(T, XMM1, RSI);            /* exponent */

    e_cvttsd2si(T, RAX, XMM1);
    e_cvtsi2sd(T, XMM2, RAX);
    e_ucomisd(T, XMM2, XMM1);
    size_t g1 = e_jcc(T, CC_NE);
    size_t g2 = e_jcc(T, CC_P);

    e_mov_r_r(T, RCX, RAX);
    e_test_r_r(T, RCX, RCX);
    size_t absok = e_jcc(T, CC_NS);
    e_neg_r(T, RCX);
    here(c, absok);
    e_cmp_r_imm(T, RCX, 4096);
    size_t g3 = e_jcc(T, CC_G);

    e_mov_r_imm64(T, RDX, 1);
    e_cvtsi2sd(T, XMM3, RDX);            /* 1.0 */
    e_test_r_r(T, RCX, RCX);
    size_t intdone = e_jcc(T, CC_E);
    size_t iloop = T->len;
    e_mulsd(T, XMM3, XMM0);
    e_sub_r_imm(T, RCX, 1);
    size_t iagain = e_jcc(T, CC_NE);
    e_patch_rel32(T, iagain, iloop);
    here(c, intdone);

    e_test_r_r(T, RAX, RAX);
    size_t nonneg = e_jcc(T, CC_NS);
    e_mov_r_imm64(T, RDX, 1);
    e_cvtsi2sd(T, XMM4, RDX);
    e_divsd(T, XMM4, XMM3);
    e_movsd_x_x(T, XMM3, XMM4);
    here(c, nonneg);
    e_movq_r_x(T, RAX, XMM3);
    e_leave(T);
    e_ret(T);

    here(c, g1); here(c, g2); here(c, g3);
    e_movsd_mem_x(T, RBP, -8,  XMM0);
    e_movsd_mem_x(T, RBP, -16, XMM1);
    e_fld_mem64(T, RBP, -16);            /* y            */
    e_fld_mem64(T, RBP, -8);             /* x            */
    e_f_op(T, 0xD9, 0xF1);               /* fyl2x        -> t = y*log2(x) */
    e_f_op(T, 0xD9, 0xC0);               /* fld st0      */
    e_f_op(T, 0xD9, 0xFC);               /* frndint      -> i */
    e_f_op(T, 0xDC, 0xE9);               /* fsub st1,st0 -> st1 = frac */
    e_f_op(T, 0xD9, 0xC9);               /* fxch st1     */
    e_f_op(T, 0xD9, 0xF0);               /* f2xm1        */
    e_f_op(T, 0xD9, 0xE8);               /* fld1         */
    e_f_op(T, 0xDE, 0xC1);               /* faddp st1,st0 -> 2^frac */
    e_f_op(T, 0xD9, 0xFD);               /* fscale       -> 2^t */
    e_f_op(T, 0xDD, 0xD9);               /* fstp st1     */
    e_fstp_mem64(T, RBP, -8);
    e_mov_r_mem(T, RAX, RBP, -8);
    e_leave(T);
    e_ret(T);
}

/* ----------------------------------------------------------------
   RT_F2S - the %g formatter
   ----------------------------------------------------------------
   The interpreter prints floats with printf's %g, so every other backend
   does too and this one has to as well: six significant digits, trailing
   zeros removed, and a switch to exponent form outside 1e-4 .. 1e+6.

   The value is scaled into [1, 10) to recover its decimal exponent, the six
   digits are read out with integer division, and the result is laid out
   according to that exponent. Repeated scaling by ten is not exact for
   extreme magnitudes, so the last of the six digits can differ from glibc
   near the edges of the double range; within the range programs actually
   print, the output matches.
   ---------------------------------------------------------------- */

/* Copy `count` (RDI) bytes from digits[index] (RSI) to the cursor in R12.
   Digit array base is in R9. */
static void copy_digits(NCtx *c) {
    size_t top = T->len;
    e_test_r_r(T, RDI, RDI);
    size_t out = e_jcc(T, CC_E);
    e_mov_r_r(T, R8, R9);
    e_add_r_r(T, R8, RSI);
    e_movzx_r_mem8(T, RDX, R8, 0);
    e_mov_mem8_r(T, R12, 0, RDX);
    e_add_r_imm(T, R12, 1);
    e_add_r_imm(T, RSI, 1);
    e_sub_r_imm(T, RDI, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);
    here(c, out);
}

/* Emit `count` (RDI) '0' characters at the cursor in R12. */
static void pad_zeros(NCtx *c) {
    size_t top = T->len;
    e_test_r_r(T, RDI, RDI);
    size_t out = e_jcc(T, CC_E);
    e_mov_r_imm64(T, RDX, '0');
    e_mov_mem8_r(T, R12, 0, RDX);
    e_add_r_imm(T, R12, 1);
    e_sub_r_imm(T, RDI, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, top);
    here(c, out);
}

static void emit_f2s(NCtx *c) {
    e_push(T, RBP);
    e_mov_r_r(T, RBP, RSP);
    e_sub_r_imm(T, RSP, 64);
    e_push(T, RBX); e_push(T, R12); e_push(T, R13);
    e_push(T, R14); e_push(T, R15);

    /* sign from bit 63, so -0.0 still prints as "-0" like printf does */
    e_mov_r_r(T, R13, RDI);
    e_sar_r_imm8(T, R13, 63);
    e_and_r_imm(T, R13, 1);

    e_mov_r_r(T, RCX, RDI);
    e_mov_r_imm64(T, RDX, 0x7FFFFFFFFFFFFFFFULL);
    e_and_r_r(T, RCX, RDX);              /* |bits| */

    e_mov_r_imm64(T, RDX, 0x7FF0000000000000ULL);
    e_cmp_r_r(T, RCX, RDX);
    size_t is_nan = e_jcc(T, CC_A);
    size_t is_inf = e_jcc(T, CC_E);

    e_push(T, RCX);
    e_mov_r_imm64(T, RDI, 48);
    nc_call_rt(c, RT_ALLOC);
    e_pop(T, RCX);
    e_mov_r_r(T, RBX, RAX);
    e_mov_r_r(T, R12, RAX);              /* cursor */

    e_test_r_r(T, R13, R13);
    size_t no_sign = e_jcc(T, CC_E);
    put_char(c, R12, '-');
    here(c, no_sign);

    e_test_r_r(T, RCX, RCX);
    size_t nonzero = e_jcc(T, CC_NE);
    put_char(c, R12, '0');
    size_t zero_done = e_jmp(T);
    here(c, nonzero);

    e_movq_x_r(T, XMM0, RCX);            /* |x| */

    /* Estimate the decimal exponent by scaling a copy into [1, 10). */
    e_xor_r_r(T, R14, R14);
    e_mov_r_imm64(T, RAX, 10);
    e_cvtsi2sd(T, XMM2, RAX);            /* 10.0 */
    e_mov_r_imm64(T, RAX, 1);
    e_cvtsi2sd(T, XMM3, RAX);            /* 1.0  */
    e_movsd_x_x(T, XMM1, XMM0);

    size_t up = T->len;
    e_ucomisd(T, XMM1, XMM2);
    size_t up_done = e_jcc(T, CC_B);
    e_divsd(T, XMM1, XMM2);
    e_add_r_imm(T, R14, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, up);
    here(c, up_done);

    size_t down = T->len;
    e_ucomisd(T, XMM1, XMM3);
    size_t down_done = e_jcc(T, CC_AE);
    e_mulsd(T, XMM1, XMM2);
    e_sub_r_imm(T, R14, 1);
    back = e_jmp(T); e_patch_rel32(T, back, down);
    here(c, down_done);

    /* The digits themselves come from the original value scaled by a single
       power of ten, not from the repeatedly-divided copy above. 10^k is
       exact through k = 22, so a value sitting exactly on a rounding
       boundary - 999999.5, which printf renders as 1e+06 - lands on the same
       side as printf instead of drifting down with each division. */
    e_xor_r_r(T, RAX, RAX);
    e_mov_mem_r(T, RBP, -24, RAX);       /* exponent-adjustment counter */
    size_t redo = T->len;

    e_mov_r_imm64(T, R10, 5);
    e_sub_r_r(T, R10, R14);              /* k = 5 - e10 */

    /* 10^|k| is only exact through k = 22, and beyond k = 308 it is simply
       infinity, so the scaling is applied in chunks of 10^22 first. Each
       chunk is an exact power of two times an exact power of five, so a
       value in the ordinary range is scaled without any rounding at all. */
    e_movsd_x_x(T, XMM5, XMM0);
    e_mov_r_imm64(T, RAX, 1);
    e_cvtsi2sd(T, XMM7, RAX);
    e_mov_r_imm64(T, RCX, 22);
    size_t bigloop = T->len;
    e_mulsd(T, XMM7, XMM2);
    e_sub_r_imm(T, RCX, 1);
    size_t bigagain = e_jcc(T, CC_NE);
    e_patch_rel32(T, bigagain, bigloop);   /* xmm7 = 1e22 */

    size_t up22 = T->len;
    e_cmp_r_imm(T, R10, 22);
    size_t up22_done = e_jcc(T, CC_LE);
    e_mulsd(T, XMM5, XMM7);
    e_sub_r_imm(T, R10, 22);
    back = e_jmp(T); e_patch_rel32(T, back, up22);
    here(c, up22_done);

    size_t dn22 = T->len;
    e_cmp_r_imm(T, R10, -22);
    size_t dn22_done = e_jcc(T, CC_GE);
    e_divsd(T, XMM5, XMM7);
    e_add_r_imm(T, R10, 22);
    back = e_jmp(T); e_patch_rel32(T, back, dn22);
    here(c, dn22_done);

    e_mov_r_r(T, RCX, R10);
    e_test_r_r(T, RCX, RCX);
    size_t kpos2 = e_jcc(T, CC_NS);
    e_neg_r(T, RCX);
    here(c, kpos2);
    e_mov_r_imm64(T, RAX, 1);
    e_cvtsi2sd(T, XMM4, RAX);
    size_t sloop2 = T->len;
    e_test_r_r(T, RCX, RCX);
    size_t sdone2 = e_jcc(T, CC_E);
    e_mulsd(T, XMM4, XMM2);
    e_sub_r_imm(T, RCX, 1);
    back = e_jmp(T); e_patch_rel32(T, back, sloop2);
    here(c, sdone2);

    /* The final scaling and rounding happen on the x87 stack rather than in
       SSE. Both round to nearest with ties to even, which is what printf
       does - but x87 carries a 64-bit mantissa where a double carries 53,
       and those eleven bits decide the cases that sit on a boundary. In SSE,
       2.305 * 25.010 scaled by 10^4 rounds to exactly 576480.5 and the tie
       breaks to even, printing 57.648; the wider intermediate keeps enough
       of the product to see that it is above the halfway point and print
       57.6481, as printf does. */
    e_movsd_mem_x(T, RBP, -32, XMM5);
    e_movsd_mem_x(T, RBP, -40, XMM4);
    e_fld_mem64(T, RBP, -32);            /* value */
    e_fld_mem64(T, RBP, -40);            /* scale */
    e_test_r_r(T, R10, R10);
    size_t kneg = e_jcc(T, CC_S);
    e_f_op(T, 0xDE, 0xC9);               /* fmulp st1, st0 */
    size_t scaled = e_jmp(T);
    here(c, kneg);
    e_f_op(T, 0xDE, 0xF9);               /* fdivp st1, st0 */
    here(c, scaled);
    e_fistp_mem64(T, RBP, -32);
    e_mov_r_mem(T, R15, RBP, -32);

    /* Rounding can push the result out of [100000, 1000000); nudge the
       exponent and rescale. One step in each direction is always enough -
       the counter is only there so no input can spin here forever. */
    e_mov_r_mem(T, RAX, RBP, -24);
    e_cmp_r_imm(T, RAX, 4);
    size_t give_up = e_jcc(T, CC_GE);
    e_add_r_imm(T, RAX, 1);
    e_mov_mem_r(T, RBP, -24, RAX);

    e_mov_r_imm64(T, RAX, 1000000);
    e_cmp_r_r(T, R15, RAX);
    size_t not_high = e_jcc(T, CC_L);
    e_add_r_imm(T, R14, 1);
    back = e_jmp(T); e_patch_rel32(T, back, redo);
    here(c, not_high);

    e_mov_r_imm64(T, RAX, 100000);
    e_cmp_r_r(T, R15, RAX);
    size_t not_low = e_jcc(T, CC_GE);
    e_sub_r_imm(T, R14, 1);
    back = e_jmp(T); e_patch_rel32(T, back, redo);
    here(c, not_low);

    /* Final clamp. In range this is a no-op; it exists so the digit loop
       below can rely on having exactly six digits to read. */
    here(c, give_up);
    e_mov_r_imm64(T, RAX, 1000000);
    e_cmp_r_r(T, R15, RAX);
    size_t clamp1 = e_jcc(T, CC_L);
    e_mov_r_r(T, RAX, R15);
    e_xor_r_r(T, RDX, RDX);
    e_mov_r_imm64(T, R10, 10);
    e_div_r(T, R10);
    e_mov_r_r(T, R15, RAX);
    e_add_r_imm(T, R14, 1);
    here(c, clamp1);
    e_mov_r_imm64(T, RAX, 100000);
    e_cmp_r_r(T, R15, RAX);
    size_t clamp2 = e_jcc(T, CC_GE);
    e_mov_r_r(T, R15, RAX);
    here(c, clamp2);

    /* write the six digits, most significant first, at [rbp-16 .. rbp-11] */
    e_mov_r_r(T, R9, RBP);
    e_sub_r_imm(T, R9, 16);
    e_mov_r_r(T, R8, R9);
    e_add_r_imm(T, R8, 6);
    e_mov_r_r(T, RAX, R15);
    e_mov_r_imm64(T, RSI, 6);
    size_t dloop = T->len;
    e_xor_r_r(T, RDX, RDX);
    e_mov_r_imm64(T, R10, 10);
    e_div_r(T, R10);
    e_add_r_imm(T, RDX, '0');
    e_sub_r_imm(T, R8, 1);
    e_mov_mem8_r(T, R8, 0, RDX);
    e_sub_r_imm(T, RSI, 1);
    size_t dagain = e_jcc(T, CC_NE);
    e_patch_rel32(T, dagain, dloop);

    /* drop trailing zeros; RCX becomes the significant-digit count */
    e_mov_r_imm64(T, RCX, 6);
    size_t strip = T->len;
    e_cmp_r_imm(T, RCX, 1);
    size_t strip_done = e_jcc(T, CC_LE);
    e_mov_r_r(T, R8, R9);
    e_add_r_r(T, R8, RCX);
    e_sub_r_imm(T, R8, 1);
    e_movzx_r_mem8(T, RDX, R8, 0);
    e_cmp_r_imm(T, RDX, '0');
    size_t strip_done2 = e_jcc(T, CC_NE);
    e_sub_r_imm(T, RCX, 1);
    back = e_jmp(T); e_patch_rel32(T, back, strip);
    here(c, strip_done); here(c, strip_done2);

    /* fixed notation for exponents in [-4, 6), scientific otherwise */
    e_cmp_r_imm(T, R14, -4);
    size_t sci1 = e_jcc(T, CC_L);
    e_cmp_r_imm(T, R14, 6);
    size_t sci2 = e_jcc(T, CC_GE);

    e_cmp_r_imm(T, R14, 0);
    size_t frac_only = e_jcc(T, CC_L);

    /* ---- fixed, exponent >= 0 ---- */
    e_mov_r_r(T, R10, R14);
    e_add_r_imm(T, R10, 1);              /* digits before the point */
    e_cmp_r_r(T, RCX, R10);
    size_t has_frac = e_jcc(T, CC_G);
    /* all significant digits are integral; pad out to the exponent */
    e_push(T, R10); e_push(T, RCX);
    e_xor_r_r(T, RSI, RSI);
    e_mov_r_r(T, RDI, RCX);
    copy_digits(c);
    e_pop(T, RCX); e_pop(T, R10);
    e_mov_r_r(T, RDI, R10);
    e_sub_r_r(T, RDI, RCX);
    pad_zeros(c);
    size_t fixed_done = e_jmp(T);

    here(c, has_frac);
    e_push(T, R10); e_push(T, RCX);
    e_xor_r_r(T, RSI, RSI);
    e_mov_r_r(T, RDI, R10);
    copy_digits(c);
    e_pop(T, RCX); e_pop(T, R10);
    put_char(c, R12, '.');
    e_mov_r_r(T, RSI, R10);
    e_mov_r_r(T, RDI, RCX);
    e_sub_r_r(T, RDI, R10);
    copy_digits(c);
    size_t fixed_done2 = e_jmp(T);

    /* ---- fixed, exponent < 0: 0.000ddd ---- */
    here(c, frac_only);
    put_char(c, R12, '0');
    put_char(c, R12, '.');
    e_push(T, RCX);
    e_xor_r_r(T, RDI, RDI);
    e_sub_r_r(T, RDI, R14);
    e_sub_r_imm(T, RDI, 1);
    pad_zeros(c);
    e_pop(T, RCX);
    e_xor_r_r(T, RSI, RSI);
    e_mov_r_r(T, RDI, RCX);
    copy_digits(c);
    size_t fixed_done3 = e_jmp(T);

    /* ---- scientific ---- */
    here(c, sci1); here(c, sci2);
    e_push(T, RCX);
    e_xor_r_r(T, RSI, RSI);
    e_mov_r_imm64(T, RDI, 1);
    copy_digits(c);
    e_pop(T, RCX);
    e_cmp_r_imm(T, RCX, 1);
    size_t no_point = e_jcc(T, CC_LE);
    put_char(c, R12, '.');
    e_push(T, RCX);
    e_mov_r_imm64(T, RSI, 1);
    e_mov_r_r(T, RDI, RCX);
    e_sub_r_imm(T, RDI, 1);
    copy_digits(c);
    e_pop(T, RCX);
    here(c, no_point);
    put_char(c, R12, 'e');
    e_cmp_r_imm(T, R14, 0);
    size_t exp_pos = e_jcc(T, CC_GE);
    put_char(c, R12, '-');
    e_neg_r(T, R14);
    size_t exp_sign_done = e_jmp(T);
    here(c, exp_pos);
    put_char(c, R12, '+');
    here(c, exp_sign_done);

    /* at least two exponent digits, three when the value needs it */
    e_cmp_r_imm(T, R14, 100);
    size_t exp_two = e_jcc(T, CC_L);
    e_mov_r_r(T, RAX, R14);
    e_xor_r_r(T, RDX, RDX);
    e_mov_r_imm64(T, R10, 100);
    e_div_r(T, R10);
    e_add_r_imm(T, RAX, '0');
    e_mov_mem8_r(T, R12, 0, RAX);
    e_add_r_imm(T, R12, 1);
    e_mov_r_r(T, R14, RDX);
    here(c, exp_two);
    e_mov_r_r(T, RAX, R14);
    e_xor_r_r(T, RDX, RDX);
    e_mov_r_imm64(T, R10, 10);
    e_div_r(T, R10);
    e_add_r_imm(T, RAX, '0');
    e_mov_mem8_r(T, R12, 0, RAX);
    e_add_r_imm(T, R12, 1);
    e_add_r_imm(T, RDX, '0');
    e_mov_mem8_r(T, R12, 0, RDX);
    e_add_r_imm(T, R12, 1);

    here(c, fixed_done); here(c, fixed_done2); here(c, fixed_done3);
    here(c, zero_done);
    e_xor_r_r(T, RDX, RDX);
    e_mov_mem8_r(T, R12, 0, RDX);
    e_mov_r_r(T, RAX, RBX);
    size_t ret_site = e_jmp(T);

    here(c, is_nan);
    nc_load_cstr(c, RAX, "nan");
    size_t ret2 = e_jmp(T);

    here(c, is_inf);
    nc_load_cstr(c, RAX, "inf");
    nc_load_cstr(c, RCX, "-inf");
    e_test_r_r(T, R13, R13);
    e_cmovcc_r_r(T, CC_NE, RAX, RCX);

    here(c, ret_site); here(c, ret2);
    e_pop(T, R15); e_pop(T, R14); e_pop(T, R13);
    e_pop(T, R12); e_pop(T, RBX);
    e_leave(T);
    e_ret(T);
}


/* ----------------------------------------------------------------
   Arrays
   ----------------------------------------------------------------
   The header holds count, capacity, element kind and a pointer to the
   elements. Keeping the elements in a separate block means growing an array
   never moves its header, so a variable that holds one still points at it
   after a push - which is what `push(a, x); println(a)` requires.
   ---------------------------------------------------------------- */

static void emit_arr_new(NCtx *c) {
    e_push(T, R12); e_push(T, R13);
    e_mov_r_r(T, R12, RDI);              /* capacity */
    e_mov_r_r(T, R13, RSI);              /* element kind */

    e_mov_r_imm64(T, RDI, ARR_HEADER);
    nc_call_rt(c, RT_ALLOC);
    e_push(T, RAX);                      /* header */

    /* Always allocate room for at least one element, so the first push does
       not have to special-case a null data pointer. */
    e_mov_r_r(T, RDI, R12);
    e_test_r_r(T, RDI, RDI);
    size_t have_cap = e_jcc(T, CC_NE);
    e_mov_r_imm64(T, RDI, 4);
    e_mov_r_r(T, R12, RDI);
    here(c, have_cap);
    e_shl_r_imm8(T, RDI, 3);             /* capacity * 8 bytes */
    nc_call_rt(c, RT_ALLOC);
    e_mov_r_r(T, RCX, RAX);              /* data block */
    e_pop(T, RAX);                       /* header */

    e_xor_r_r(T, RDX, RDX);
    e_mov_mem_r(T, RAX, ARR_COUNT, RDX);
    e_mov_mem_r(T, RAX, ARR_CAP,  R12);
    e_mov_mem_r(T, RAX, ARR_KIND, R13);
    e_mov_mem_r(T, RAX, ARR_DATA, RCX);

    e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* Shared bounds check: index in RSI against the count of the array in RDI.
   Out of range stops the program the way the interpreter does. */
static void emit_arr_bounds(NCtx *c) {
    e_mov_r_mem(T, RCX, RDI, ARR_COUNT);
    /* A negative index counts from the end, as it does in the interpreter:
       a[-1] is the last element. The reported index is the adjusted one. */
    e_test_r_r(T, RSI, RSI);
    size_t nonneg = e_jcc(T, CC_NS);
    e_add_r_r(T, RSI, RCX);
    here(c, nonneg);
    e_cmp_r_imm(T, RSI, 0);
    size_t low = e_jcc(T, CC_L);
    e_cmp_r_r(T, RSI, RCX);
    size_t high = e_jcc(T, CC_GE);
    size_t ok = e_jmp(T);
    here(c, low); here(c, high);
    /* Same wording as the interpreter: "array index 10 out of bounds [0, 3)".
       An index and a count say what went wrong; "out of bounds" alone does
       not. */
    e_push(T, R12);
    e_mov_r_r(T, R12, RCX);              /* count */
    e_mov_r_r(T, RDI, RSI);
    nc_call_rt(c, RT_I2S);
    e_mov_r_r(T, RSI, RAX);
    nc_load_cstr(c, RDI, "array index ");
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RDI, RAX);
    nc_load_cstr(c, RSI, " out of bounds [0, ");
    nc_call_rt(c, RT_CONCAT);
    e_push(T, RAX);
    e_mov_r_r(T, RDI, R12);
    nc_call_rt(c, RT_I2S);
    e_mov_r_r(T, RSI, RAX);
    e_pop(T, RDI);
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RDI, RAX);
    nc_load_cstr(c, RSI, ")");
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RDI, RAX);
    nc_call_rt(c, RT_DIE);
    e_pop(T, R12);
    here(c, ok);
}

static void emit_arr_get(NCtx *c) {
    emit_arr_bounds(c);
    e_mov_r_mem(T, RCX, RDI, ARR_DATA);
    e_mov_r_r(T, RDX, RSI);
    e_shl_r_imm8(T, RDX, 3);
    e_add_r_r(T, RCX, RDX);
    e_mov_r_mem(T, RAX, RCX, 0);
    e_ret(T);
}

static void emit_arr_set(NCtx *c) {
    e_push(T, RDX);
    emit_arr_bounds(c);
    e_pop(T, RDX);
    e_mov_r_mem(T, RCX, RDI, ARR_DATA);
    e_mov_r_r(T, RAX, RSI);
    e_shl_r_imm8(T, RAX, 3);
    e_add_r_r(T, RCX, RAX);
    e_mov_mem_r(T, RCX, 0, RDX);
    e_ret(T);
}

static void emit_arr_push(NCtx *c) {
    e_push(T, R12); e_push(T, R13); e_push(T, R14);
    e_mov_r_r(T, R12, RDI);              /* array */
    e_mov_r_r(T, R13, RSI);              /* value */

    e_mov_r_mem(T, RCX, R12, ARR_COUNT);
    e_mov_r_mem(T, RDX, R12, ARR_CAP);
    e_cmp_r_r(T, RCX, RDX);
    size_t fits = e_jcc(T, CC_L);

    /* Full: allocate a block of twice the capacity and copy into it. The
       header is left where it is, so every reference to this array stays
       valid. */
    e_mov_r_r(T, R14, RDX);
    e_shl_r_imm8(T, R14, 1);
    e_test_r_r(T, R14, R14);
    size_t nonzero = e_jcc(T, CC_NE);
    e_mov_r_imm64(T, R14, 4);
    here(c, nonzero);

    e_mov_r_r(T, RDI, R14);
    e_shl_r_imm8(T, RDI, 3);
    nc_call_rt(c, RT_ALLOC);

    e_mov_r_mem(T, RSI, R12, ARR_DATA);  /* old block */
    e_mov_r_r(T, RCX, RAX);              /* new block */
    e_mov_r_mem(T, RDX, R12, ARR_COUNT);
    size_t copy = T->len;
    e_test_r_r(T, RDX, RDX);
    size_t copied = e_jcc(T, CC_E);
    e_mov_r_mem(T, R8, RSI, 0);
    e_mov_mem_r(T, RCX, 0, R8);
    e_add_r_imm(T, RSI, 8);
    e_add_r_imm(T, RCX, 8);
    e_sub_r_imm(T, RDX, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, copy);
    here(c, copied);

    e_mov_mem_r(T, R12, ARR_DATA, RAX);
    e_mov_mem_r(T, R12, ARR_CAP, R14);
    here(c, fits);

    e_mov_r_mem(T, RCX, R12, ARR_DATA);
    e_mov_r_mem(T, RDX, R12, ARR_COUNT);
    e_mov_r_r(T, RAX, RDX);
    e_shl_r_imm8(T, RAX, 3);
    e_add_r_r(T, RCX, RAX);
    e_mov_mem_r(T, RCX, 0, R13);
    e_add_r_imm(T, RDX, 1);
    e_mov_mem_r(T, R12, ARR_COUNT, RDX);

    e_pop(T, R14); e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

static void emit_arr_pop(NCtx *c) {
    e_mov_r_mem(T, RCX, RDI, ARR_COUNT);
    e_test_r_r(T, RCX, RCX);
    size_t nonempty = e_jcc(T, CC_NE);
    e_push(T, RDI);
    nc_load_cstr(c, RDI, "pop from empty array");
    nc_call_rt(c, RT_DIE);
    e_pop(T, RDI);
    here(c, nonempty);
    e_sub_r_imm(T, RCX, 1);
    e_mov_mem_r(T, RDI, ARR_COUNT, RCX);
    e_mov_r_mem(T, RDX, RDI, ARR_DATA);
    e_shl_r_imm8(T, RCX, 3);
    e_add_r_r(T, RDX, RCX);
    e_mov_r_mem(T, RAX, RDX, 0);
    e_ret(T);
}

/* A SUB array is a value: binding one copies it, so that `let b = a` leaves
   a alone when b is pushed to. The interpreter does the same (see val_copy);
   without this the native backend would share the header and diverge. */
static void emit_arr_copy(NCtx *c) {
    e_push(T, R12); e_push(T, R13);
    e_mov_r_r(T, R12, RDI);
    e_mov_r_mem(T, RDI, R12, ARR_COUNT);
    e_mov_r_mem(T, RSI, R12, ARR_KIND);
    nc_call_rt(c, RT_ARR_NEW);
    e_mov_r_r(T, R13, RAX);

    e_mov_r_mem(T, RSI, R12, ARR_DATA);
    e_mov_r_mem(T, RCX, R13, ARR_DATA);
    e_mov_r_mem(T, RDX, R12, ARR_COUNT);
    e_mov_mem_r(T, R13, ARR_COUNT, RDX);
    size_t loop = T->len;
    e_test_r_r(T, RDX, RDX);
    size_t done = e_jcc(T, CC_E);
    e_mov_r_mem(T, R8, RSI, 0);
    e_mov_mem_r(T, RCX, 0, R8);
    e_add_r_imm(T, RSI, 8);
    e_add_r_imm(T, RCX, 8);
    e_sub_r_imm(T, RDX, 1);
    size_t back = e_jmp(T); e_patch_rel32(T, back, loop);
    here(c, done);

    e_mov_r_r(T, RAX, R13);
    e_pop(T, R13); e_pop(T, R12);
    e_ret(T);
}

/* "[a, b, c]" - the interpreter's spelling, with no quotes around strings. */
static void emit_arr_str(NCtx *c) {
    e_push(T, RBX); e_push(T, R12); e_push(T, R13); e_push(T, R14);
    e_mov_r_r(T, R12, RDI);              /* array */

    nc_load_cstr(c, RDI, "[");
    e_mov_r_r(T, RBX, RDI);              /* accumulated string */

    e_xor_r_r(T, R13, R13);              /* index */
    size_t loop = T->len;
    e_mov_r_mem(T, RCX, R12, ARR_COUNT);
    e_cmp_r_r(T, R13, RCX);
    size_t done = e_jcc(T, CC_GE);

    e_test_r_r(T, R13, R13);
    size_t first = e_jcc(T, CC_E);
    e_mov_r_r(T, RDI, RBX);
    nc_load_cstr(c, RSI, ", ");
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RBX, RAX);
    here(c, first);

    /* element -> text, by the kind recorded in the header */
    e_mov_r_mem(T, RCX, R12, ARR_DATA);
    e_mov_r_r(T, RDX, R13);
    e_shl_r_imm8(T, RDX, 3);
    e_add_r_r(T, RCX, RDX);
    e_mov_r_mem(T, RDI, RCX, 0);
    e_mov_r_mem(T, R14, R12, ARR_KIND);

    e_cmp_r_imm(T, R14, AK_FLOAT);
    size_t not_f = e_jcc(T, CC_NE);
    nc_call_rt(c, RT_F2S);
    size_t have = e_jmp(T);
    here(c, not_f);
    e_cmp_r_imm(T, R14, AK_STRING);
    size_t not_s = e_jcc(T, CC_NE);
    e_mov_r_r(T, RAX, RDI);              /* already text */
    size_t have2 = e_jmp(T);
    here(c, not_s);
    e_cmp_r_imm(T, R14, AK_BOOL);
    size_t not_b = e_jcc(T, CC_NE);
    nc_call_rt(c, RT_B2S);
    size_t have3 = e_jmp(T);
    here(c, not_b);
    nc_call_rt(c, RT_I2S);
    here(c, have); here(c, have2); here(c, have3);

    e_mov_r_r(T, RSI, RAX);
    e_mov_r_r(T, RDI, RBX);
    nc_call_rt(c, RT_CONCAT);
    e_mov_r_r(T, RBX, RAX);

    e_add_r_imm(T, R13, 1);
    size_t again = e_jmp(T); e_patch_rel32(T, again, loop);
    here(c, done);

    e_mov_r_r(T, RDI, RBX);
    nc_load_cstr(c, RSI, "]");
    nc_call_rt(c, RT_CONCAT);

    e_pop(T, R14); e_pop(T, R13); e_pop(T, R12); e_pop(T, RBX);
    e_ret(T);
}

/* ----------------------------------------------------------------
   Driver
   ---------------------------------------------------------------- */

void nc_emit_runtime(NCtx *c) {
    struct { RtId id; void (*fn)(NCtx *); } routines[] = {
        { RT_ALLOC,   emit_alloc   },
        { RT_WRITE,   emit_write   },
        { RT_STRLEN,  emit_strlen  },
        { RT_PUTSN,   emit_putsn   },
        { RT_PUTS,    emit_puts    },
        { RT_I2S,     emit_i2s     },
        { RT_F2S,     emit_f2s     },
        { RT_B2S,     emit_b2s     },
        { RT_CONCAT,  emit_concat  },
        { RT_STRCMP,  emit_strcmp  },
        { RT_STRCASE, emit_strcase },
        { RT_TRIM,    emit_trim    },
        { RT_FIND,     emit_find     },
        { RT_SUBSTR,   emit_substr   },
        { RT_CHARAT,   emit_charat   },
        { RT_CONTAINS, emit_contains },
        { RT_REPLACE,  emit_replace  },
        { RT_SPLIT,    emit_split    },
        { RT_JOIN,     emit_join     },
        { RT_DIE,     emit_die     },
        { RT_IDIV,    emit_idiv    },
        { RT_IMOD,    emit_imod    },
        { RT_IPOW,    emit_ipow    },
        { RT_FPOW,    emit_fpow    },
        { RT_FMOD,    emit_fmod    },
        { RT_FDIV,    emit_fdiv    },
        { RT_ARR_NEW,  emit_arr_new  },
        { RT_ARR_GET,  emit_arr_get  },
        { RT_ARR_SET,  emit_arr_set  },
        { RT_ARR_PUSH, emit_arr_push },
        { RT_ARR_POP,  emit_arr_pop  },
        { RT_ARR_STR,  emit_arr_str  },
        { RT_ARR_COPY, emit_arr_copy },
    };
    int n = (int)(sizeof(routines) / sizeof(routines[0]));
    for (int i = 0; i < n; i++) {
        c->rt_off[routines[i].id] = c->text.len;
        routines[i].fn(c);
        /* keep routines from starting mid-word; costs a handful of bytes */
        while (c->text.len % 4) e_nop(T);
    }
}
