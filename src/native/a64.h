#ifndef SUB_A64_H
#define SUB_A64_H
#include "x64.h"
#include "sub_compiler.h"
#define A64_TEXT_BASE  0x400000ULL
#define A64_BSS_BASE   0x500000ULL
#define A64_BSS_SIZE   (1ULL<<20)
#define A64_HDR_TOTAL  176ULL
#define A64_EM         183
#define A64_ADDR(off)  (A64_TEXT_BASE + A64_HDR_TOTAL + (uint64_t)(off))
enum { A64_X0=0,A64_X1,A64_X2,A64_X3,A64_X4,A64_X5,A64_X6,A64_X7,A64_X8,
       A64_X9,A64_X10,A64_X11,A64_X16=16,A64_X29=29,A64_X30=30,A64_SP=31 };
void a64_movz(Buf*,int,uint16_t,int); void a64_movk(Buf*,int,uint16_t,int);
void a64_imm64(Buf*,int,uint64_t); void a64_mov(Buf*,int,int);
void a64_add(Buf*,int,int,int); void a64_sub(Buf*,int,int,int); void a64_mul(Buf*,int,int,int);
void a64_neg(Buf*,int,int); void a64_addi(Buf*,int,int,uint32_t); void a64_cmpi(Buf*,int,uint32_t);
void a64_asr63(Buf*,int,int); void a64_csel(Buf*,int,int,int,int);
void a64_udiv(Buf*,int,int,int); void a64_msub(Buf*,int,int,int,int);
void a64_ldr(Buf*,int,int); void a64_str(Buf*,int,int);
void a64_strb_pre(Buf*,int,int,int); void a64_strb(Buf*,int,int);
void a64_push(Buf*,int); void a64_pop(Buf*,int);
void a64_stp_fp48(Buf*); void a64_ldp_fp48(Buf*); void a64_stp_fp16(Buf*); void a64_ldp_fp16(Buf*);
void a64_blr(Buf*,int); void a64_ret(Buf*); void a64_svc(Buf*); void a64_cbnz_back(Buf*,int,int);
void a64_sdiv(Buf*,int,int,int); void a64_cmp(Buf*,int,int); void a64_cset(Buf*,int,int);
void a64_subi(Buf*,int,int,uint32_t); void a64_ldur(Buf*,int,int,int); void a64_stur(Buf*,int,int,int);
int a64_native_compile(ASTNode *program, const char *out_path, char *err, size_t err_size);
#endif
