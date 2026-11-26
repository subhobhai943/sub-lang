/* AArch64 codegen + freestanding runtime. Milestone 2: integers, + - * / %,
   comparisons, unary minus, if/elif/else, while, for-range, break/continue,
   functions + recursion + return, top-level globals and frame locals, print.
   Unsupported constructs decline by name -> C fallback (never miscompiles). */
#include "a64.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXSLOTS 32
#define FRAME_BYTES (8*MAXSLOTS)

typedef struct { const char *name; uint64_t addr; } Glob;
typedef struct { const char *name; int label; ASTNode *decl; } Func;
typedef struct { size_t off; int label; int kind; int aux; } Fixup;
typedef struct { size_t off; int bound; } Label;
/* kind: 0=B 1=BL 2=Bcond 3=CBZ 5=ADDR(4 words) */

typedef struct {
    Buf *text;
    Glob globs[256]; int nglob;
    Func funcs[256]; int nfunc;
    Fixup fix[4096]; int nfix;
    Label lab[4096]; int nlab;
    const char *locals[MAXSLOTS]; int nloc;
    int is_main, depth;
    int ret_label;
    int loop_brk[64], loop_cont[64], loop_n;
    int rt_print, div0;
    int failed; char *err; size_t errsize;
} Ctx;

static void fail(Ctx*c,const char*m){ if(!c->failed){c->failed=1; if(c->err&&c->errsize) snprintf(c->err,c->errsize,"%s",m);} }
static int new_label(Ctx*c){ if(c->nlab>=4096){fail(c,"too many labels");return 0;} c->lab[c->nlab].bound=0; return c->nlab++; }
static void bind(Ctx*c,int l){ c->lab[l].off=c->text->len; c->lab[l].bound=1; }
static void fix(Ctx*c,int kind,int label,int aux){ if(c->nfix>=4096){fail(c,"too many fixups");return;} c->fix[c->nfix++]=(Fixup){c->text->len-4,label,kind,aux}; }
static void emit_b(Ctx*c,int l){ buf_u32(c->text,0x14000000); fix(c,0,l,0); }
static void emit_bl(Ctx*c,int l){ buf_u32(c->text,0x94000000); fix(c,1,l,0); }
static void emit_bcond(Ctx*c,int cond,int l){ buf_u32(c->text,0x54000000u|(uint32_t)cond); fix(c,2,l,cond); }
static void emit_cbz(Ctx*c,int reg,int l){ buf_u32(c->text,0xB4000000u|(uint32_t)reg); fix(c,3,l,reg); }
static void emit_addr_label(Ctx*c,int reg,int l){
    a64_movz(c->text,reg,0,0); a64_movk(c->text,reg,0,1); a64_movk(c->text,reg,0,2); a64_movk(c->text,reg,0,3);
    if(c->nfix>=4096){fail(c,"too many fixups");return;}
    c->fix[c->nfix++]=(Fixup){c->text->len-16,l,5,reg};
}
static uint64_t glob_addr(Ctx*c,const char*n){ for(int i=0;i<c->nglob;i++) if(!strcmp(c->globs[i].name,n)) return c->globs[i].addr; return 0; }
static int local_find(Ctx*c,const char*n){ for(int i=0;i<c->nloc;i++) if(c->locals[i]&&!strcmp(c->locals[i],n)) return i; return -1; }
static int add_local(Ctx*c,const char*n){ if(c->nloc>=MAXSLOTS){fail(c,"too many locals for the AArch64 backend");return 0;} c->locals[c->nloc]=n; return c->nloc++; }
static int slot_disp(int slot){ return -8*(slot+1); }
static Func *find_func(Ctx*c,const char*n){ for(int i=0;i<c->nfunc;i++) if(!strcmp(c->funcs[i].name,n)) return &c->funcs[i]; return NULL; }
static ASTNode *block_first(ASTNode*n){ if(!n)return NULL; if(n->body)return n->body; if(n->children&&n->child_count>0)return n->children[0]; if(n->left)return n->left; return NULL; }
static int cmp_cond(const char*op){ if(!strcmp(op,"=="))return 0; if(!strcmp(op,"!="))return 1; if(!strcmp(op,"<"))return 11; if(!strcmp(op,">"))return 12; if(!strcmp(op,"<="))return 13; return 10; }
static int is_cmp(const char*op){ return !strcmp(op,"==")||!strcmp(op,"!=")||!strcmp(op,"<")||!strcmp(op,">")||!strcmp(op,"<=")||!strcmp(op,">="); }

static void emit_print_int(Buf*b){
    a64_stp_fp48(b); a64_addi(b,A64_X29,A64_SP,0);
    a64_addi(b,A64_X1,A64_SP,16); a64_addi(b,A64_X2,A64_X1,32); a64_addi(b,A64_X11,A64_X1,32);
    a64_asr63(b,A64_X9,A64_X0); a64_neg(b,A64_X3,A64_X0); a64_cmpi(b,A64_X0,0);
    a64_csel(b,A64_X0,A64_X0,A64_X3,10); a64_movz(b,A64_X10,10,0);
    size_t loop=b->len;
    a64_udiv(b,A64_X4,A64_X0,A64_X10); a64_msub(b,A64_X5,A64_X4,A64_X10,A64_X0);
    a64_addi(b,A64_X5,A64_X5,48); a64_strb_pre(b,A64_X5,A64_X2,-1); a64_mov(b,A64_X0,A64_X4);
    a64_cbnz_back(b,A64_X0,(int)(loop-b->len));
    a64_movz(b,A64_X6,45,0); a64_strb_pre(b,A64_X6,A64_X2,-1); a64_addi(b,A64_X7,A64_X2,1);
    a64_cmpi(b,A64_X9,0); a64_csel(b,A64_X1,A64_X2,A64_X7,1); a64_sub(b,A64_X2,A64_X11,A64_X1);
    a64_movz(b,A64_X0,1,0); a64_movz(b,A64_X8,64,0); a64_svc(b);
    a64_movz(b,A64_X5,10,0); a64_strb(b,A64_X5,A64_X1);
    a64_movz(b,A64_X0,1,0); a64_movz(b,A64_X2,1,0); a64_movz(b,A64_X8,64,0); a64_svc(b);
    a64_ldp_fp48(b); a64_ret(b);
}

static void gen_expr(Ctx*,ASTNode*);
static void gen_stmt(Ctx*,ASTNode*);
static void gen_block(Ctx*c,ASTNode*n){ c->depth++; for(ASTNode*s=block_first(n);s;s=s->next){ if(c->failed)break; gen_stmt(c,s);} c->depth--; }

static void gen_user_call(Ctx*c,ASTNode*n){
    Func*f=find_func(c,n->value?n->value:"");
    if(!f){ fail(c,"call to an unknown function in the AArch64 backend"); return; }
    int argc=n->child_count; if(argc>8){fail(c,"the AArch64 backend supports at most 8 arguments");return;}
    for(int i=0;i<argc;i++){ gen_expr(c,n->children[i]); if(c->failed)return; a64_push(c->text,A64_X0); }
    for(int i=argc-1;i>=0;i--) a64_pop(c->text,i);
    emit_bl(c,f->label);
}

static void gen_expr(Ctx*c,ASTNode*n){
    Buf*b=c->text;
    if(!n){fail(c,"empty expression");return;}
    switch(n->type){
    case AST_LITERAL:
        if(n->data_type!=TYPE_INT){fail(c,"the AArch64 backend supports integer literals only (for now)");return;}
        a64_imm64(b,A64_X0,(uint64_t)strtoll(n->value?n->value:"0",NULL,10)); return;
    case AST_IDENTIFIER:{
        int s=local_find(c,n->value?n->value:"");
        if(s>=0){ a64_ldur(b,A64_X0,A64_X29,slot_disp(s)); return; }
        uint64_t ga=glob_addr(c,n->value?n->value:"");
        if(ga){ a64_imm64(b,A64_X1,ga); a64_ldr(b,A64_X0,A64_X1); return; }
        fail(c,"unknown name in the AArch64 backend"); return; }
    case AST_UNARY_EXPR:{
        ASTNode*v=n->right?n->right:n->left; const char*op=n->value?n->value:"-";
        if(!strcmp(op,"+")){gen_expr(c,v);return;}
        if(strcmp(op,"-")!=0){fail(c,"unsupported unary operator in the AArch64 backend");return;}
        gen_expr(c,v); if(c->failed)return; a64_neg(b,A64_X0,A64_X0); return; }
    case AST_BINARY_EXPR:{
        const char*op=n->value?n->value:"";
        gen_expr(c,n->left); if(c->failed)return; a64_push(b,A64_X0);
        gen_expr(c,n->right); if(c->failed)return; a64_mov(b,A64_X1,A64_X0); a64_pop(b,A64_X0);
        if(is_cmp(op)){ a64_cmp(b,A64_X0,A64_X1); a64_cset(b,A64_X0,cmp_cond(op)); return; }
        if(!strcmp(op,"+")) a64_add(b,A64_X0,A64_X0,A64_X1);
        else if(!strcmp(op,"-")) a64_sub(b,A64_X0,A64_X0,A64_X1);
        else if(!strcmp(op,"*")) a64_mul(b,A64_X0,A64_X0,A64_X1);
        else if(!strcmp(op,"/")){ emit_cbz(c,A64_X1,c->div0); a64_sdiv(b,A64_X0,A64_X0,A64_X1); }
        else if(!strcmp(op,"%")){ emit_cbz(c,A64_X1,c->div0); a64_sdiv(b,A64_X2,A64_X0,A64_X1); a64_msub(b,A64_X0,A64_X2,A64_X1,A64_X0); }
        else fail(c,"unsupported binary operator in the AArch64 backend");
        return; }
    case AST_CALL_EXPR:
        gen_user_call(c,n); return;
    default: fail(c,"unsupported expression in the AArch64 backend"); return;
    }
}

static void gen_stmt(Ctx*c,ASTNode*s){
    Buf*b=c->text;
    switch(s->type){
    case AST_VAR_DECL: case AST_CONST_DECL:{
        gen_expr(c,s->right); if(c->failed)return;
        if(c->is_main&&c->depth==0){ a64_imm64(b,A64_X1,glob_addr(c,s->value?s->value:"")); a64_str(b,A64_X0,A64_X1); }
        else { int slot=add_local(c,s->value?s->value:""); a64_stur(b,A64_X0,A64_X29,slot_disp(slot)); }
        return; }
    case AST_ASSIGN_STMT:{
        if(!s->left||s->left->type!=AST_IDENTIFIER){fail(c,"the AArch64 backend supports simple assignment only (for now)");return;}
        gen_expr(c,s->right); if(c->failed)return;
        const char*nm=s->left->value?s->left->value:"";
        int slot=local_find(c,nm);
        if(slot>=0){ a64_stur(b,A64_X0,A64_X29,slot_disp(slot)); return; }
        uint64_t ga=glob_addr(c,nm);
        if(ga){ a64_imm64(b,A64_X1,ga); a64_str(b,A64_X0,A64_X1); return; }
        fail(c,"assignment to an unknown name"); return; }
    case AST_IF_STMT:{
        gen_expr(c,s->condition); if(c->failed)return;
        int els=new_label(c), done=new_label(c);
        emit_cbz(c,A64_X0,els);
        gen_block(c,s->body);
        if(s->right){ emit_b(c,done); bind(c,els);
            if(s->right->type==AST_IF_STMT) gen_stmt(c,s->right); else gen_block(c,s->right);
            bind(c,done);
        } else bind(c,els);
        return; }
    case AST_WHILE_STMT:{
        int cont=new_label(c), brk=new_label(c);
        c->loop_brk[c->loop_n]=brk; c->loop_cont[c->loop_n]=cont; c->loop_n++;
        bind(c,cont);
        gen_expr(c,s->condition); if(c->failed)return;
        emit_cbz(c,A64_X0,brk);
        gen_block(c,s->body);
        emit_b(c,cont); bind(c,brk);
        c->loop_n--;
        return; }
    case AST_FOR_STMT:{
        ASTNode*range=(s->child_count>0&&s->children[0]&&s->children[0]->type==AST_RANGE_EXPR)?s->children[0]:NULL;
        if(!range){fail(c,"the AArch64 backend supports for x in range(...) (for now)");return;}
        ASTNode*start=range->right?range->left:NULL;
        ASTNode*end=range->right?range->right:range->left;
        int slot=add_local(c,s->value?s->value:"i");
        if(start){gen_expr(c,start);if(c->failed)return;} else a64_imm64(b,A64_X0,0);
        a64_stur(b,A64_X0,A64_X29,slot_disp(slot));
        int top=new_label(c), cont=new_label(c), brk=new_label(c);
        c->loop_brk[c->loop_n]=brk; c->loop_cont[c->loop_n]=cont; c->loop_n++;
        bind(c,top);
        if(end){gen_expr(c,end);if(c->failed)return;} else a64_imm64(b,A64_X0,10);
        a64_mov(b,A64_X1,A64_X0); a64_ldur(b,A64_X0,A64_X29,slot_disp(slot));
        a64_cmp(b,A64_X0,A64_X1); emit_bcond(c,10,brk);
        gen_block(c,s->body);
        bind(c,cont);
        a64_ldur(b,A64_X0,A64_X29,slot_disp(slot)); a64_addi(b,A64_X0,A64_X0,1); a64_stur(b,A64_X0,A64_X29,slot_disp(slot));
        emit_b(c,top); bind(c,brk);
        c->loop_n--;
        return; }
    case AST_RETURN_STMT:{
        ASTNode*v=s->right?s->right:(s->left?s->left:s->body);
        if(v){gen_expr(c,v);if(c->failed)return;} else a64_imm64(b,A64_X0,0);
        emit_b(c,c->ret_label); return; }
    case AST_BREAK_STMT:
        if(c->loop_n==0){fail(c,"break outside a loop");return;}
        emit_b(c,c->loop_brk[c->loop_n-1]); return;
    case AST_CONTINUE_STMT:
        if(c->loop_n==0){fail(c,"continue outside a loop");return;}
        emit_b(c,c->loop_cont[c->loop_n-1]); return;
    case AST_CALL_EXPR:{
        const char*fn=s->value?s->value:"";
        if(!strcmp(fn,"print")||!strcmp(fn,"println")||!strcmp(fn,"show")){
            if(s->child_count!=1){fail(c,"the AArch64 backend supports print of one value (for now)");return;}
            gen_expr(c,s->children[0]); if(c->failed)return; emit_bl(c,c->rt_print); return;
        }
        gen_user_call(c,s); return; }
    case AST_BLOCK: gen_block(c,s); return;
    default: fail(c,"unsupported statement in the AArch64 backend"); return;
    }
}

static void emit_function(Ctx*c,ASTNode*fn,int label){
    Buf*b=c->text;
    bind(c,label);
    c->nloc=0; c->is_main=0; c->depth=0; c->loop_n=0;
    c->ret_label=new_label(c);
    a64_stp_fp16(b); a64_addi(b,A64_X29,A64_SP,0); a64_subi(b,A64_SP,A64_SP,FRAME_BYTES);
    int argc=fn->child_count; if(argc>8){fail(c,"the AArch64 backend supports at most 8 parameters");return;}
    for(int i=0;i<argc;i++){ ASTNode*p=fn->children[i]; int slot=add_local(c,p->value?p->value:""); a64_stur(b,i,A64_X29,slot_disp(slot)); }
    gen_block(c,fn->body);
    a64_imm64(b,A64_X0,0);
    bind(c,c->ret_label);
    a64_addi(b,A64_SP,A64_X29,0); a64_ldp_fp16(b); a64_ret(b);
}

int a64_native_compile(ASTNode*program,const char*out_path,char*err,size_t err_size){
    Ctx c; memset(&c,0,sizeof(c)); c.err=err; c.errsize=err_size;
    Buf text; buf_init(&text); c.text=&text;
    for(ASTNode*s=block_first(program);s;s=s->next){
        if(s->type==AST_FUNCTION_DECL){
            if(c.nfunc>=256){fail(&c,"too many functions");break;}
            c.funcs[c.nfunc].name=s->value; c.funcs[c.nfunc].decl=s; c.funcs[c.nfunc].label=new_label(&c); c.nfunc++;
        } else if((s->type==AST_VAR_DECL||s->type==AST_CONST_DECL)&&s->value){
            if(c.nglob>=256){fail(&c,"too many globals");break;}
            c.globs[c.nglob].name=s->value; c.globs[c.nglob].addr=A64_BSS_BASE+8*(uint64_t)c.nglob; c.nglob++;
        }
    }
    int main_label=new_label(&c);
    c.rt_print=new_label(&c); c.div0=new_label(&c); int msg_label=new_label(&c);
    emit_bl(&c,main_label);
    a64_movz(&text,A64_X0,0,0); a64_movz(&text,A64_X8,93,0); a64_svc(&text);
    bind(&c,c.rt_print); emit_print_int(&text);
    bind(&c,c.div0);
    emit_addr_label(&c,A64_X1,msg_label);
    a64_movz(&text,A64_X0,2,0); a64_movz(&text,A64_X2,31,0); a64_movz(&text,A64_X8,64,0); a64_svc(&text);
    a64_movz(&text,A64_X0,70,0); a64_movz(&text,A64_X8,93,0); a64_svc(&text);
    for(int i=0;i<c.nfunc&&!c.failed;i++) emit_function(&c,c.funcs[i].decl,c.funcs[i].label);
    bind(&c,main_label);
    c.nloc=0; c.is_main=1; c.depth=0; c.loop_n=0; c.ret_label=new_label(&c);
    a64_stp_fp16(&text); a64_addi(&text,A64_X29,A64_SP,0); a64_subi(&text,A64_SP,A64_SP,FRAME_BYTES);
    for(ASTNode*s=block_first(program);s;s=s->next){
        if(c.failed)break;
        if(s->type==AST_FUNCTION_DECL)continue;
        gen_stmt(&c,s);
    }
    a64_imm64(&text,A64_X0,0);
    bind(&c,c.ret_label);
    a64_addi(&text,A64_SP,A64_X29,0); a64_ldp_fp16(&text); a64_ret(&text);
    bind(&c,msg_label);
    buf_bytes(&text,"RuntimeError: division by zero\n",31);
    if(c.failed){ buf_free(&text); return 1; }
    for(int i=0;i<c.nfix;i++){
        Fixup*f=&c.fix[i];
        if(!c.lab[f->label].bound){ fail(&c,"unbound label"); break; }
        size_t tgt=c.lab[f->label].off;
        if(f->kind==5){
            uint64_t a=A64_ADDR(tgt); uint32_t*r=(uint32_t*)(text.data+f->off);
            r[0]=0xD2800000u|(((uint32_t)a&0xFFFF)<<5)|f->aux;
            r[1]=0xF2800000u|(1u<<21)|(((uint32_t)(a>>16)&0xFFFF)<<5)|f->aux;
            r[2]=0xF2800000u|(2u<<21)|(((uint32_t)(a>>32)&0xFFFF)<<5)|f->aux;
            r[3]=0xF2800000u|(3u<<21)|(((uint32_t)(a>>48)&0xFFFF)<<5)|f->aux;
        } else {
            int64_t disp=(int64_t)tgt-(int64_t)f->off; int32_t imm=(int32_t)(disp>>2);
            uint32_t*r=(uint32_t*)(text.data+f->off); uint32_t w=*r;
            if(f->kind==0) w=0x14000000u|((uint32_t)imm&0x3FFFFFFu);
            else if(f->kind==1) w=0x94000000u|((uint32_t)imm&0x3FFFFFFu);
            else if(f->kind==2) w=0x54000000u|(((uint32_t)imm&0x7FFFFu)<<5)|(uint32_t)f->aux;
            else if(f->kind==3) w=0xB4000000u|(((uint32_t)imm&0x7FFFFu)<<5)|(uint32_t)f->aux;
            *r=w;
        }
    }
    if(c.failed){ buf_free(&text); return 1; }
    int rc=elf64_write_target(out_path,&text,0,A64_EM,A64_TEXT_BASE,A64_BSS_BASE,A64_BSS_SIZE);
    buf_free(&text);
    if(rc!=0){ fail(&c,"could not write the output file"); return 1; }
    return 0;
}
