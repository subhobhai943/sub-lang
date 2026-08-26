/* ========================================
   SUB Language - x86-64 instruction encoder
   See x64.h for how this fits together.

   This is deliberately a plain encoder rather than a full assembler: it knows
   how to lay down the roughly forty instructions the backend actually emits,
   and nothing else. Each helper writes one instruction and returns; the only
   state is the byte buffer.
   ======================================== */

#include "x64.h"
#include <stdlib.h>
#include <string.h>

/* ---------------- Byte buffer ---------------- */

void buf_init(Buf *b) { b->data = NULL; b->len = 0; b->cap = 0; }
void buf_free(Buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap * 2 : 4096;
    while (cap < b->len + extra) cap *= 2;
    uint8_t *p = realloc(b->data, cap);
    if (!p) { /* out of memory: nothing sensible to do mid-encode */ abort(); }
    b->data = p;
    b->cap  = cap;
}

void buf_u8(Buf *b, uint8_t v)  { buf_reserve(b, 1); b->data[b->len++] = v; }
void buf_u16(Buf *b, uint16_t v){ buf_bytes(b, &v, 2); }
void buf_u32(Buf *b, uint32_t v){ buf_bytes(b, &v, 4); }
void buf_u64(Buf *b, uint64_t v){ buf_bytes(b, &v, 8); }

void buf_bytes(Buf *b, const void *p, size_t n) {
    buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void buf_zero(Buf *b, size_t n) {
    buf_reserve(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
}

/* ---------------- Encoding primitives ---------------- */

/* REX is only emitted when it carries information: a 64-bit operand size or
   a register number above 7. */
static void rex(Buf *b, int w, int reg, int rm) {
    uint8_t v = (uint8_t)(0x40 | (w ? 8 : 0) | ((reg >> 3) << 2) | (rm >> 3));
    if (v != 0x40) buf_u8(b, v);
}

/* Register-direct form: mod = 11. */
static void modrm_rr(Buf *b, int reg, int rm) {
    buf_u8(b, (uint8_t)(0xC0 | ((reg & 7) << 3) | (rm & 7)));
}

/* [base + disp]. RSP-based addressing needs a SIB byte, and RBP with a zero
   displacement is the RIP-relative escape, so it must use the disp8 form. */
static void modrm_mem(Buf *b, int reg, int base, int32_t disp) {
    int need_sib = ((base & 7) == RSP);
    int mod;
    if (disp == 0 && (base & 7) != RBP)      mod = 0;
    else if (disp >= -128 && disp <= 127)    mod = 1;
    else                                     mod = 2;

    buf_u8(b, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (base & 7)));
    if (need_sib) buf_u8(b, 0x24);           /* scale=0 index=none base=rsp */
    if (mod == 1) buf_u8(b, (uint8_t)disp);
    else if (mod == 2) buf_u32(b, (uint32_t)disp);
}

/* ---------------- Data movement ---------------- */

void e_mov_r_imm64(Buf *b, int dst, uint64_t imm) {
    rex(b, 1, 0, dst);
    buf_u8(b, (uint8_t)(0xB8 + (dst & 7)));
    buf_u64(b, imm);
}

void e_mov_r_r(Buf *b, int dst, int src) {
    if (dst == src) return;
    rex(b, 1, src, dst);
    buf_u8(b, 0x89);
    modrm_rr(b, src, dst);
}

void e_mov_r_mem(Buf *b, int dst, int base, int32_t disp) {
    rex(b, 1, dst, base);
    buf_u8(b, 0x8B);
    modrm_mem(b, dst, base, disp);
}

void e_mov_mem_r(Buf *b, int base, int32_t disp, int src) {
    rex(b, 1, src, base);
    buf_u8(b, 0x89);
    modrm_mem(b, src, base, disp);
}

void e_movzx_r_mem8(Buf *b, int dst, int base, int32_t disp) {
    rex(b, 1, dst, base);
    buf_u8(b, 0x0F); buf_u8(b, 0xB6);
    modrm_mem(b, dst, base, disp);
}

void e_mov_mem8_r(Buf *b, int base, int32_t disp, int src) {
    /* An explicit REX is required to reach the low byte of rsi/rdi/rsp/rbp;
       without it those encodings mean ah/ch/dh/bh instead. */
    uint8_t v = (uint8_t)(0x40 | ((src >> 3) << 2) | (base >> 3));
    if (v != 0x40 || src >= 4) buf_u8(b, v);
    buf_u8(b, 0x88);
    modrm_mem(b, src, base, disp);
}

void e_movzx_r_r8(Buf *b, int dst, int src) {
    rex(b, 1, dst, src);
    buf_u8(b, 0x0F); buf_u8(b, 0xB6);
    modrm_rr(b, dst, src);
}

void e_push(Buf *b, int r) {
    if (r >= 8) buf_u8(b, 0x41);
    buf_u8(b, (uint8_t)(0x50 + (r & 7)));
}

void e_pop(Buf *b, int r) {
    if (r >= 8) buf_u8(b, 0x41);
    buf_u8(b, (uint8_t)(0x58 + (r & 7)));
}

/* ---------------- ALU, register/register ---------------- */

static void alu_rr(Buf *b, uint8_t op, int dst, int src) {
    rex(b, 1, src, dst);
    buf_u8(b, op);
    modrm_rr(b, src, dst);
}

void e_add_r_r(Buf *b, int dst, int src) { alu_rr(b, 0x01, dst, src); }
void e_sub_r_r(Buf *b, int dst, int src) { alu_rr(b, 0x29, dst, src); }
void e_and_r_r(Buf *b, int dst, int src) { alu_rr(b, 0x21, dst, src); }
void e_or_r_r (Buf *b, int dst, int src) { alu_rr(b, 0x09, dst, src); }
void e_xor_r_r(Buf *b, int dst, int src) { alu_rr(b, 0x31, dst, src); }
void e_cmp_r_r(Buf *b, int a, int bb)    { alu_rr(b, 0x39, a, bb); }
void e_test_r_r(Buf *b, int a, int bb)   { alu_rr(b, 0x85, a, bb); }

void e_imul_r_r(Buf *b, int dst, int src) {
    rex(b, 1, dst, src);
    buf_u8(b, 0x0F); buf_u8(b, 0xAF);
    modrm_rr(b, dst, src);
}

/* ---------------- ALU, register/immediate ---------------- */

static void alu_ri(Buf *b, int ext, int dst, int32_t imm) {
    rex(b, 1, 0, dst);
    if (imm >= -128 && imm <= 127) {
        buf_u8(b, 0x83);
        modrm_rr(b, ext, dst);
        buf_u8(b, (uint8_t)imm);
    } else {
        buf_u8(b, 0x81);
        modrm_rr(b, ext, dst);
        buf_u32(b, (uint32_t)imm);
    }
}

void e_add_r_imm(Buf *b, int dst, int32_t imm) { if (imm) alu_ri(b, 0, dst, imm); }
void e_sub_r_imm(Buf *b, int dst, int32_t imm) { if (imm) alu_ri(b, 5, dst, imm); }
void e_and_r_imm(Buf *b, int dst, int32_t imm) { alu_ri(b, 4, dst, imm); }
void e_cmp_r_imm(Buf *b, int dst, int32_t imm) { alu_ri(b, 7, dst, imm); }

void e_shl_r_imm8(Buf *b, int dst, uint8_t n) {
    rex(b, 1, 0, dst);
    buf_u8(b, 0xC1); modrm_rr(b, 4, dst); buf_u8(b, n);
}

void e_sar_r_imm8(Buf *b, int dst, uint8_t n) {
    rex(b, 1, 0, dst);
    buf_u8(b, 0xC1); modrm_rr(b, 7, dst); buf_u8(b, n);
}

void e_shr_r_imm8(Buf *b, int dst, uint8_t n) {
    rex(b, 1, 0, dst);
    buf_u8(b, 0xC1); modrm_rr(b, 5, dst); buf_u8(b, n);
}

/* ---------------- Unary and division ---------------- */

static void grp3(Buf *b, int ext, int r) {
    rex(b, 1, 0, r);
    buf_u8(b, 0xF7);
    modrm_rr(b, ext, r);
}

void e_neg_r(Buf *b, int r)  { grp3(b, 3, r); }
void e_not_r(Buf *b, int r)  { grp3(b, 2, r); }
void e_idiv_r(Buf *b, int r) { grp3(b, 7, r); }
void e_div_r(Buf *b, int r)  { grp3(b, 6, r); }
void e_cqo(Buf *b)           { buf_u8(b, 0x48); buf_u8(b, 0x99); }

void e_setcc(Buf *b, int cc, int r8) {
    uint8_t v = (uint8_t)(0x40 | (r8 >> 3));
    if (v != 0x40 || r8 >= 4) buf_u8(b, v);
    buf_u8(b, 0x0F); buf_u8(b, (uint8_t)(0x90 + cc));
    modrm_rr(b, 0, r8);
}

void e_cmovcc_r_r(Buf *b, int cc, int dst, int src) {
    rex(b, 1, dst, src);
    buf_u8(b, 0x0F); buf_u8(b, (uint8_t)(0x40 + cc));
    modrm_rr(b, dst, src);
}

/* ---------------- Control flow ---------------- */

void e_ret(Buf *b)     { buf_u8(b, 0xC3); }
void e_leave(Buf *b)   { buf_u8(b, 0xC9); }
void e_syscall(Buf *b) { buf_u8(b, 0x0F); buf_u8(b, 0x05); }
void e_nop(Buf *b)     { buf_u8(b, 0x90); }

size_t e_jmp(Buf *b) {
    buf_u8(b, 0xE9);
    size_t site = b->len;
    buf_u32(b, 0);
    return site;
}

size_t e_jcc(Buf *b, int cc) {
    buf_u8(b, 0x0F); buf_u8(b, (uint8_t)(0x80 + cc));
    size_t site = b->len;
    buf_u32(b, 0);
    return site;
}

size_t e_call(Buf *b) {
    buf_u8(b, 0xE8);
    size_t site = b->len;
    buf_u32(b, 0);
    return site;
}

/* A rel32 is measured from the end of the instruction, which is exactly where
   the four displacement bytes stop. */
void e_patch_rel32(Buf *b, size_t site, size_t target) {
    int32_t rel = (int32_t)((long)target - (long)(site + 4));
    memcpy(b->data + site, &rel, 4);
}

/* ---------------- SSE scalar double ---------------- */

static void sse(Buf *b, uint8_t pfx, uint8_t op, int reg, int rm, int w) {
    if (pfx) buf_u8(b, pfx);
    rex(b, w, reg, rm);
    buf_u8(b, 0x0F); buf_u8(b, op);
    modrm_rr(b, reg, rm);
}

void e_movq_x_r(Buf *b, int xmm, int r)  { sse(b, 0x66, 0x6E, xmm, r, 1); }
void e_movq_r_x(Buf *b, int r, int xmm)  { sse(b, 0x66, 0x7E, xmm, r, 1); }
void e_movsd_x_x(Buf *b, int d, int s)   { sse(b, 0xF2, 0x10, d, s, 0); }
void e_addsd(Buf *b, int d, int s)       { sse(b, 0xF2, 0x58, d, s, 0); }
void e_mulsd(Buf *b, int d, int s)       { sse(b, 0xF2, 0x59, d, s, 0); }
void e_subsd(Buf *b, int d, int s)       { sse(b, 0xF2, 0x5C, d, s, 0); }
void e_divsd(Buf *b, int d, int s)       { sse(b, 0xF2, 0x5E, d, s, 0); }
void e_sqrtsd(Buf *b, int d, int s)      { sse(b, 0xF2, 0x51, d, s, 0); }
void e_ucomisd(Buf *b, int a, int bb)    { sse(b, 0x66, 0x2E, a, bb, 0); }
void e_cvtsi2sd(Buf *b, int xmm, int r)  { sse(b, 0xF2, 0x2A, xmm, r, 1); }
void e_cvttsd2si(Buf *b, int r, int xmm) { sse(b, 0xF2, 0x2C, r, xmm, 1); }
void e_cvtsd2si(Buf *b, int r, int xmm)  { sse(b, 0xF2, 0x2D, r, xmm, 1); }
void e_xorpd(Buf *b, int d, int s)       { sse(b, 0x66, 0x57, d, s, 0); }

void e_movsd_x_mem(Buf *b, int xmm, int base, int32_t disp) {
    buf_u8(b, 0xF2); rex(b, 0, xmm, base);
    buf_u8(b, 0x0F); buf_u8(b, 0x10);
    modrm_mem(b, xmm, base, disp);
}

void e_movsd_mem_x(Buf *b, int base, int32_t disp, int xmm) {
    buf_u8(b, 0xF2); rex(b, 0, xmm, base);
    buf_u8(b, 0x0F); buf_u8(b, 0x11);
    modrm_mem(b, xmm, base, disp);
}

void e_roundsd(Buf *b, int dst, int src, uint8_t mode) {
    buf_u8(b, 0x66); rex(b, 0, dst, src);
    buf_u8(b, 0x0F); buf_u8(b, 0x3A); buf_u8(b, 0x0B);
    modrm_rr(b, dst, src);
    buf_u8(b, mode);
}

/* ---------------- x87 ----------------
   Only the general `a ** b` path uses these; see rt_fpow in x64_runtime.c. */

void e_fld_mem64(Buf *b, int base, int32_t disp) {
    rex(b, 0, 0, base);
    buf_u8(b, 0xDD);
    modrm_mem(b, 0, base, disp);
}

void e_fstp_mem64(Buf *b, int base, int32_t disp) {
    rex(b, 0, 0, base);
    buf_u8(b, 0xDD);
    modrm_mem(b, 3, base, disp);
}

void e_fistp_mem64(Buf *b, int base, int32_t disp) {
    rex(b, 0, 0, base);
    buf_u8(b, 0xDF);
    modrm_mem(b, 7, base, disp);
}

void e_f_op(Buf *b, uint8_t b0, uint8_t b1) { buf_u8(b, b0); buf_u8(b, b1); }
