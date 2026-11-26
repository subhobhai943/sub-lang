/* AArch64 codegen + freestanding itoa runtime. Milestone 1 subset only;
   anything unsupported sets the error and returns non-zero so the driver
   falls back to the C backend (never miscompiles). */
#include "a64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct { const char *name; uint64_t addr; } Glob;
typedef struct { Buf *text; uint64_t rt_print; Glob globs[256]; int nglob;
                 int failed; char *err; size_t errsize; } Ctx;
static void fail(Ctx *c, const char *msg){
    if(!c->failed){ c->failed=1; if(c->err&&c->errsize) snprintf(c->err,c->errsize,"%s",msg); }
}
static uint64_t glob_addr(Ctx *c, const char *n){
    for(int i=0;i<c->nglob;i++) if(!strcmp(c->globs[i].name,n)) return c->globs[i].addr;
    return 0;
}
static ASTNode *block_first(ASTNode *n){
    if(!n) return NULL;
    if(n->body) return n->body;
    if(n->children && n->child_count>0) return n->children[0];
    if(n->left) return n->left;
    return NULL;
}
static void call_abs(Buf *b, uint64_t addr){ a64_imm64(b,A64_X16,addr); a64_blr(b,A64_X16); }
static void emit_print_int(Buf *b){
    a64_stp_fp48(b);
    a64_addi(b,A64_X29,A64_SP,0);
    a64_addi(b,A64_X1,A64_SP,16);
    a64_addi(b,A64_X2,A64_X1,32);
    a64_addi(b,A64_X11,A64_X1,32);
    a64_asr63(b,A64_X9,A64_X0);
    a64_neg(b,A64_X3,A64_X0);
    a64_cmpi(b,A64_X0,0);
    a64_csel(b,A64_X0,A64_X0,A64_X3,10);
    a64_movz(b,A64_X10,10,0);
    size_t loop=b->len;
    a64_udiv(b,A64_X4,A64_X0,A64_X10);
    a64_msub(b,A64_X5,A64_X4,A64_X10,A64_X0);
    a64_addi(b,A64_X5,A64_X5,48);
    a64_strb_pre(b,A64_X5,A64_X2,-1);
    a64_mov(b,A64_X0,A64_X4);
    a64_cbnz_back(b,A64_X0,(int)(loop-b->len));
    a64_movz(b,A64_X6,45,0);
    a64_strb_pre(b,A64_X6,A64_X2,-1);
    a64_addi(b,A64_X7,A64_X2,1);
    a64_cmpi(b,A64_X9,0);
    a64_csel(b,A64_X1,A64_X2,A64_X7,1);
    a64_sub(b,A64_X2,A64_X11,A64_X1);
    a64_movz(b,A64_X0,1,0);
    a64_movz(b,A64_X8,64,0);
    a64_svc(b);
    a64_movz(b,A64_X5,10,0);
    a64_strb(b,A64_X5,A64_X1);
    a64_movz(b,A64_X0,1,0);
    a64_movz(b,A64_X2,1,0);
    a64_movz(b,A64_X8,64,0);
    a64_svc(b);
    a64_ldp_fp48(b);
    a64_ret(b);
}
static void gen_expr(Ctx *c, ASTNode *n){
    Buf *b=c->text;
    if(!n){ fail(c,"empty expression"); return; }
    switch(n->type){
    case AST_LITERAL:
        if(n->data_type!=TYPE_INT){ fail(c,"the AArch64 backend supports integer literals only (for now)"); return; }
        a64_imm64(b,A64_X0,(uint64_t)strtoll(n->value?n->value:"0",NULL,10));
        return;
    case AST_IDENTIFIER:{
        uint64_t a=glob_addr(c,n->value?n->value:"");
        if(!a){ fail(c,"the AArch64 backend supports top-level globals only (for now)"); return; }
        a64_imm64(b,A64_X1,a); a64_ldr(b,A64_X0,A64_X1); return; }
    case AST_UNARY_EXPR:{
        ASTNode *v=n->right?n->right:n->left;
        const char *op=n->value?n->value:"-";
        if(!strcmp(op,"+")){ gen_expr(c,v); return; }
        if(strcmp(op,"-")!=0){ fail(c,"unsupported unary operator in the AArch64 backend"); return; }
        gen_expr(c,v); if(c->failed) return; a64_neg(b,A64_X0,A64_X0); return; }
    case AST_BINARY_EXPR:{
        const char *op=n->value?n->value:"";
        gen_expr(c,n->left); if(c->failed) return;
        a64_push(b,A64_X0);
        gen_expr(c,n->right); if(c->failed) return;
        a64_mov(b,A64_X1,A64_X0);
        a64_pop(b,A64_X0);
        if(!strcmp(op,"+")) a64_add(b,A64_X0,A64_X0,A64_X1);
        else if(!strcmp(op,"-")) a64_sub(b,A64_X0,A64_X0,A64_X1);
        else if(!strcmp(op,"*")) a64_mul(b,A64_X0,A64_X0,A64_X1);
        else fail(c,"the AArch64 backend supports + - * only (for now)");
        return; }
    default: fail(c,"unsupported expression in the AArch64 backend"); return;
    }
}
static void gen_stmt(Ctx *c, ASTNode *s){
    Buf *b=c->text;
    if(s->type==AST_VAR_DECL||s->type==AST_CONST_DECL){
        gen_expr(c,s->right); if(c->failed) return;
        a64_imm64(b,A64_X1,glob_addr(c,s->value?s->value:"")); a64_str(b,A64_X0,A64_X1);
        return;
    }
    if(s->type==AST_CALL_EXPR && s->value &&
       (!strcmp(s->value,"print")||!strcmp(s->value,"println")||!strcmp(s->value,"show"))){
        if(s->child_count!=1){ fail(c,"the AArch64 backend supports print of one value (for now)"); return; }
        gen_expr(c,s->children[0]); if(c->failed) return;
        call_abs(b,c->rt_print);
        return;
    }
    fail(c,"unsupported statement in the AArch64 backend");
}
int a64_native_compile(ASTNode *program, const char *out_path, char *err, size_t err_size){
    Ctx c; memset(&c,0,sizeof(c)); c.err=err; c.errsize=err_size;
    for(ASTNode*s=block_first(program);s;s=s->next){
        if(s->type==AST_FUNCTION_DECL){ fail(&c,"functions are not yet supported by the AArch64 backend"); break; }
        if((s->type==AST_VAR_DECL||s->type==AST_CONST_DECL)&&s->value){
            if(c.nglob>=256){ fail(&c,"too many top-level names for the AArch64 backend"); break; }
            c.globs[c.nglob].name=s->value;
            c.globs[c.nglob].addr=A64_BSS_BASE+8*(uint64_t)c.nglob; c.nglob++;
        }
    }
    Buf text; buf_init(&text); c.text=&text;
    size_t patch=text.len;
    a64_movz(&text,A64_X16,0,0);
    a64_movk(&text,A64_X16,0,1);
    a64_movk(&text,A64_X16,0,2);
    a64_movk(&text,A64_X16,0,3);
    a64_blr(&text,A64_X16);
    a64_movz(&text,A64_X0,0,0); a64_movz(&text,A64_X8,93,0); a64_svc(&text);
    size_t rt_off=text.len; emit_print_int(&text);
    c.rt_print=A64_ADDR(rt_off);
    size_t main_off=text.len;
    a64_stp_fp16(&text); a64_addi(&text,A64_X29,A64_SP,0);
    for(ASTNode*s=block_first(program);s;s=s->next){
        if(c.failed) break;
        gen_stmt(&c,s);
    }
    a64_ldp_fp16(&text); a64_ret(&text);
    if(c.failed){ buf_free(&text); return 1; }
    uint64_t ma=A64_ADDR(main_off);
    uint32_t words[4]={
        0xD2800000u|(((uint32_t)ma&0xFFFF)<<5)|A64_X16,
        0xF2800000u|(1u<<21)|(((uint32_t)(ma>>16)&0xFFFF)<<5)|A64_X16,
        0xF2800000u|(2u<<21)|(((uint32_t)(ma>>32)&0xFFFF)<<5)|A64_X16,
        0xF2800000u|(3u<<21)|(((uint32_t)(ma>>48)&0xFFFF)<<5)|A64_X16 };
    memcpy(text.data+patch,words,16);
    int rc=elf64_write_target(out_path,&text,0,A64_EM,A64_TEXT_BASE,A64_BSS_BASE,A64_BSS_SIZE);
    buf_free(&text);
    if(rc!=0){ fail(&c,"could not write the output file"); return 1; }
    return 0;
}
