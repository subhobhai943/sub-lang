#include "a64.h"
static void w(Buf *b, uint32_t i){ buf_u32(b, i); }
void a64_movz(Buf*b,int rd,uint16_t imm,int hw){ w(b,0xD2800000u|((uint32_t)hw<<21)|((uint32_t)imm<<5)|(uint32_t)rd); }
void a64_movk(Buf*b,int rd,uint16_t imm,int hw){ w(b,0xF2800000u|((uint32_t)hw<<21)|((uint32_t)imm<<5)|(uint32_t)rd); }
void a64_imm64(Buf*b,int rd,uint64_t v){
    a64_movz(b,rd,(uint16_t)(v&0xFFFF),0);
    if(v>>16) a64_movk(b,rd,(uint16_t)((v>>16)&0xFFFF),1);
    if(v>>32) a64_movk(b,rd,(uint16_t)((v>>32)&0xFFFF),2);
    if(v>>48) a64_movk(b,rd,(uint16_t)((v>>48)&0xFFFF),3);
}
void a64_mov(Buf*b,int rd,int rn){ w(b,0xAA000000u|((uint32_t)rn<<16)|(31u<<5)|(uint32_t)rd); }
void a64_add(Buf*b,int rd,int rn,int rm){ w(b,0x8B000000u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_sub(Buf*b,int rd,int rn,int rm){ w(b,0xCB000000u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_mul(Buf*b,int rd,int rn,int rm){ w(b,0x9B007C00u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_neg(Buf*b,int rd,int rn){ w(b,0xCB000000u|((uint32_t)rn<<16)|(31u<<5)|(uint32_t)rd); }
void a64_addi(Buf*b,int rd,int rn,uint32_t imm){ w(b,0x91000000u|(imm<<10)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_cmpi(Buf*b,int rn,uint32_t imm){ w(b,0xF1000000u|(imm<<10)|((uint32_t)rn<<5)|31u); }
void a64_asr63(Buf*b,int rd,int rn){ w(b,0x937FFC00u|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_csel(Buf*b,int rd,int rn,int rm,int cond){ w(b,0x9A800000u|((uint32_t)rm<<16)|((uint32_t)cond<<12)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_udiv(Buf*b,int rd,int rn,int rm){ w(b,0x9AC00800u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_msub(Buf*b,int rd,int rn,int rm,int ra){ w(b,0x9B008000u|((uint32_t)rm<<16)|((uint32_t)ra<<10)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_ldr(Buf*b,int rt,int rn){ w(b,0xF9400000u|((uint32_t)rn<<5)|(uint32_t)rt); }
void a64_str(Buf*b,int rt,int rn){ w(b,0xF9000000u|((uint32_t)rn<<5)|(uint32_t)rt); }
void a64_strb_pre(Buf*b,int rt,int rn,int imm9){ w(b,0x38000C00u|(((uint32_t)imm9&0x1FF)<<12)|((uint32_t)rn<<5)|(uint32_t)rt); }
void a64_strb(Buf*b,int rt,int rn){ w(b,0x39000000u|((uint32_t)rn<<5)|(uint32_t)rt); }
void a64_push(Buf*b,int rt){ w(b,0xF81F0FE0u|(uint32_t)rt); }
void a64_pop(Buf*b,int rt){ w(b,0xF84107E0u|(uint32_t)rt); }
void a64_stp_fp48(Buf*b){ w(b,0xA9BD7BFDu); }
void a64_ldp_fp48(Buf*b){ w(b,0xA8C37BFDu); }
void a64_stp_fp16(Buf*b){ w(b,0xA9BF7BFDu); }
void a64_ldp_fp16(Buf*b){ w(b,0xA8C17BFDu); }
void a64_blr(Buf*b,int rn){ w(b,0xD63F0000u|((uint32_t)rn<<5)); }
void a64_ret(Buf*b){ w(b,0xD65F03C0u); }
void a64_svc(Buf*b){ w(b,0xD4000001u); }
void a64_cbnz_back(Buf*b,int rn,int off_bytes){ uint32_t imm19=(uint32_t)(off_bytes>>2)&0x7FFFF; w(b,0xB5000000u|(imm19<<5)|(uint32_t)rn); }
void a64_sdiv(Buf*b,int rd,int rn,int rm){ w(b,0x9AC00C00u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_cmp(Buf*b,int rn,int rm){ w(b,0xEB000000u|((uint32_t)rm<<16)|((uint32_t)rn<<5)|31u); }
void a64_cset(Buf*b,int rd,int cond){ w(b,0x9A800400u|(31u<<16)|(((uint32_t)(cond^1))<<12)|(31u<<5)|(uint32_t)rd); }
void a64_subi(Buf*b,int rd,int rn,uint32_t imm){ w(b,0xD1000000u|(imm<<10)|((uint32_t)rn<<5)|(uint32_t)rd); }
void a64_ldur(Buf*b,int rt,int rn,int imm9){ w(b,0xF8400000u|(((uint32_t)imm9&0x1FF)<<12)|((uint32_t)rn<<5)|(uint32_t)rt); }
void a64_stur(Buf*b,int rt,int rn,int imm9){ w(b,0xF8000000u|(((uint32_t)imm9&0x1FF)<<12)|((uint32_t)rn<<5)|(uint32_t)rt); }
