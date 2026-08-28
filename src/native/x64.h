/* ========================================
   SUB Language - Native x86-64 Backend
   ----------------------------------------
   `subc` used to be a native compiler in name only: it emitted C and shelled
   out to gcc. That made a "compiled" SUB program depend on a C toolchain
   being installed, which is a strange thing for a language to require of its
   users.

   This backend emits x86-64 machine code directly and writes a static ELF64
   executable itself. There is no assembler, no linker, and no libc in the
   pipeline - the produced binary talks to the kernel through syscalls and
   carries its own runtime.

   Layout of a produced binary:

     0x400000  ELF header + program headers
               entry stub          (sets up the heap, calls main, exits)
               runtime routines    (see x64_runtime.c)
               user functions      (see x64_codegen.c)
               string literals
     0x500000  BSS: heap pointer, scratch buffer, then the bump heap

   Everything is addressed with absolute 64-bit immediates rather than
   RIP-relative displacements. Addresses inside the text segment are not known
   until the whole thing is laid out, so those immediates are recorded and
   patched at the end; absolute addressing keeps that patching to "store 8
   bytes here" instead of recomputing a displacement against the end of a
   variable-length instruction.
   ======================================== */

#ifndef SUB_X64_H
#define SUB_X64_H

#include <stddef.h>
#include <stdint.h>

/* ---------------- Memory layout ---------------- */

#define X64_TEXT_BASE   0x400000UL
#define X64_BSS_BASE    0x500000UL

/* Fixed BSS slots. The heap pointer is the only mutable global the runtime
   needs; the scratch buffer is where number formatting builds its digits. */
#define X64_G_HEAPPTR   (X64_BSS_BASE + 0)
#define X64_G_SCRATCH   (X64_BSS_BASE + 64)      /* 256 bytes */
/* One 8-byte cell per top-level SUB variable. A frame slot is gone when its
   function returns, so a variable a function both reads and outlives has to
   live somewhere fixed and writable; this is the room between the scratch
   buffer and the start of the heap. */
#define X64_G_GLOBALS   (X64_BSS_BASE + 0x400)
#define X64_G_GLOBAL_N  256                      /* 2 KB, ends well below the heap */
#define X64_HEAP_START  (X64_BSS_BASE + 0x1000)
#define X64_BSS_SIZE    (0x1000 + (64UL << 20))  /* 64 MB of bump heap */

/* ---------------- Byte buffer ---------------- */

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_u8(Buf *b, uint8_t v);
void buf_u16(Buf *b, uint16_t v);
void buf_u32(Buf *b, uint32_t v);
void buf_u64(Buf *b, uint64_t v);
void buf_bytes(Buf *b, const void *p, size_t n);
void buf_zero(Buf *b, size_t n);

/* ---------------- Registers ---------------- */

enum {
    RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15
};

enum { XMM0 = 0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7 };

/* Condition codes, as used by jcc and setcc. */
enum {
    CC_O = 0x0, CC_NO = 0x1, CC_B  = 0x2, CC_AE = 0x3,
    CC_E = 0x4, CC_NE = 0x5, CC_BE = 0x6, CC_A  = 0x7,
    CC_S = 0x8, CC_NS = 0x9, CC_P  = 0xA, CC_NP = 0xB,
    CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G  = 0xF
};

/* ---------------- Instruction emitters ----------------
   Every helper appends to `b`. `disp` operands are signed byte offsets from
   the named base register; the encoder picks the shortest form. */

void e_mov_r_imm64(Buf *b, int dst, uint64_t imm);
void e_mov_r_r(Buf *b, int dst, int src);
void e_mov_r_mem(Buf *b, int dst, int base, int32_t disp);
void e_mov_mem_r(Buf *b, int base, int32_t disp, int src);
void e_movzx_r_mem8(Buf *b, int dst, int base, int32_t disp);
void e_mov_mem8_r(Buf *b, int base, int32_t disp, int src);
void e_movzx_r_r8(Buf *b, int dst, int src);

void e_push(Buf *b, int r);
void e_pop(Buf *b, int r);

void e_add_r_r(Buf *b, int dst, int src);
void e_sub_r_r(Buf *b, int dst, int src);
void e_and_r_r(Buf *b, int dst, int src);
void e_or_r_r(Buf *b, int dst, int src);
void e_xor_r_r(Buf *b, int dst, int src);
void e_cmp_r_r(Buf *b, int a, int bb);
void e_test_r_r(Buf *b, int a, int bb);
void e_imul_r_r(Buf *b, int dst, int src);

void e_add_r_imm(Buf *b, int dst, int32_t imm);
void e_sub_r_imm(Buf *b, int dst, int32_t imm);
void e_cmp_r_imm(Buf *b, int dst, int32_t imm);
void e_and_r_imm(Buf *b, int dst, int32_t imm);

void e_shl_r_imm8(Buf *b, int dst, uint8_t n);
void e_sar_r_imm8(Buf *b, int dst, uint8_t n);
void e_shr_r_imm8(Buf *b, int dst, uint8_t n);

void e_neg_r(Buf *b, int r);
void e_not_r(Buf *b, int r);
void e_cqo(Buf *b);
void e_idiv_r(Buf *b, int r);
void e_div_r(Buf *b, int r);   /* unsigned: rdx:rax / r */

void e_setcc(Buf *b, int cc, int r8);
void e_cmovcc_r_r(Buf *b, int cc, int dst, int src);

void e_ret(Buf *b);
void e_leave(Buf *b);
void e_syscall(Buf *b);
void e_nop(Buf *b);

/* Jumps and calls emit a rel32 and return the file offset of that rel32 so
   the caller can patch it once the destination is known. */
size_t e_jmp(Buf *b);
size_t e_jcc(Buf *b, int cc);
size_t e_call(Buf *b);
void   e_patch_rel32(Buf *b, size_t site, size_t target);

/* ---------------- SSE (doubles) ---------------- */

void e_movq_x_r(Buf *b, int xmm, int r);     /* xmm <- r (raw bits) */
void e_movq_r_x(Buf *b, int r, int xmm);     /* r   <- xmm (raw bits) */
void e_movsd_x_x(Buf *b, int dst, int src);
void e_addsd(Buf *b, int dst, int src);
void e_subsd(Buf *b, int dst, int src);
void e_mulsd(Buf *b, int dst, int src);
void e_divsd(Buf *b, int dst, int src);
void e_sqrtsd(Buf *b, int dst, int src);
void e_ucomisd(Buf *b, int a, int bb);
void e_cvtsi2sd(Buf *b, int xmm, int r);
void e_cvttsd2si(Buf *b, int r, int xmm);  /* truncate toward zero */
void e_cvtsd2si(Buf *b, int r, int xmm);   /* round to nearest even, like printf */
void e_xorpd(Buf *b, int dst, int src);
void e_movsd_x_mem(Buf *b, int xmm, int base, int32_t disp);
void e_movsd_mem_x(Buf *b, int base, int32_t disp, int xmm);
void e_roundsd(Buf *b, int dst, int src, uint8_t mode); /* SSE4.1 */

/* ---------------- x87 (only used for the general pow) ---------------- */

void e_fld_mem64(Buf *b, int base, int32_t disp);
void e_fstp_mem64(Buf *b, int base, int32_t disp);
void e_fistp_mem64(Buf *b, int base, int32_t disp);  /* round to int64 per the control word */
void e_f_op(Buf *b, uint8_t b0, uint8_t b1);   /* two-byte x87 opcode */

/* ---------------- ELF writer ---------------- */

/* Writes a static, position-dependent ELF64 executable for x86-64 Linux.
   `text` is placed at X64_TEXT_BASE + header size and entered at
   `entry_off` bytes into it. Returns 0 on success. */
int elf64_write(const char *path, const Buf *text, size_t entry_off);

/* Offset from the start of `text` to its virtual address. Codegen needs this
   to turn "byte 37 of the text buffer" into an absolute address. */
size_t elf64_text_offset(void);

#endif /* SUB_X64_H */
