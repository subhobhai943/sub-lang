/* ========================================
   SUB Language - native backend, shared state
   ----------------------------------------
   The runtime emitter and the code generator both append to one text buffer
   and both need to call things that have not been emitted yet, so every
   cross-reference goes through this context and is patched once the layout
   is final.
   ======================================== */

#ifndef SUB_X64_INTERNAL_H
#define SUB_X64_INTERNAL_H

#include "x64.h"

/* Runtime routines, all emitted into the text buffer ahead of user code.
   The SUB native ABI is: integer, pointer and double arguments alike travel
   in RDI, RSI, RDX, RCX, R8, R9 as raw 64-bit values (a double is passed as
   its bit pattern), the result comes back in RAX, and RBX/R12-R15 are
   callee-saved. Doubles never touch the SSE argument registers, which keeps
   call sites uniform whatever the static types happen to be. */
typedef enum {
    RT_ALLOC,      /* rdi=bytes                  -> rax=ptr        */
    RT_WRITE,      /* rdi=ptr, rsi=len                             */
    RT_STRLEN,     /* rdi=ptr                    -> rax=len        */
    RT_PUTSN,      /* rdi=ptr : write, no newline                  */
    RT_PUTS,       /* rdi=ptr : write + '\n'                       */
    RT_I2S,        /* rdi=i64                    -> rax=ptr        */
    RT_F2S,        /* rdi=f64 bits               -> rax=ptr        */
    RT_B2S,        /* rdi=0/1                    -> rax=ptr        */
    RT_CONCAT,     /* rdi=a, rsi=b               -> rax=ptr        */
    RT_STRCMP,     /* rdi=a, rsi=b               -> rax=difference */
    RT_STRCASE,    /* rdi=ptr, rsi=1 upper/0 lower -> rax=ptr      */
    RT_TRIM,       /* rdi=ptr                    -> rax=ptr        */

    /* The character-level string builtins. RT_FIND is the search the other
       three are built on: contains asks whether it found anything, replace
       walks it to the end, and split cuts at each hit. */
    RT_FIND,       /* rdi=hay, rsi=needle, rdx=from -> rax=index or -1 */
    RT_SUBSTR,     /* rdi=ptr, rsi=start, rdx=end   -> rax=ptr         */
    RT_CHARAT,     /* rdi=ptr, rsi=index            -> rax=ptr         */
    RT_CONTAINS,   /* rdi=hay, rsi=needle           -> rax=0/1         */
    RT_REPLACE,    /* rdi=ptr, rsi=old, rdx=new     -> rax=ptr         */
    RT_SPLIT,      /* rdi=ptr, rsi=sep              -> rax=array       */
    RT_JOIN,       /* rdi=array, rsi=sep            -> rax=ptr         */
    RT_DIE,        /* rdi=message : print "RuntimeError: ..." and exit 70 */
    RT_IDIV,       /* rdi, rsi                   -> rax            */
    RT_IMOD,       /* rdi, rsi                   -> rax            */
    RT_IPOW,       /* rdi=base, rsi=exp          -> rax            */
    RT_FPOW,       /* rdi=a bits, rsi=b bits     -> rax=bits       */
    RT_FMOD,       /* rdi=a bits, rsi=b bits     -> rax=bits       */
    RT_FDIV,       /* rdi=a bits, rsi=b bits     -> rax=bits       */

    /* Arrays. The header is fixed-size and the elements live in a separate
       block, so growing an array never moves the header - a variable holding
       one keeps working after a push, and so does a second variable that was
       handed the same array. */
    RT_ARR_NEW,    /* rdi=capacity, rsi=elem kind -> rax=array      */
    RT_ARR_GET,    /* rdi=array, rsi=index        -> rax=element    */
    RT_ARR_SET,    /* rdi=array, rsi=index, rdx=value               */
    RT_ARR_PUSH,   /* rdi=array, rsi=value                          */
    RT_ARR_POP,    /* rdi=array                   -> rax=element    */
    RT_ARR_STR,    /* rdi=array                   -> rax=ptr        */
    RT_ARR_COPY,   /* rdi=array                   -> rax=new array  */
    RT_COUNT
} RtId;

/* Array header layout, in bytes from the array pointer. */
#define ARR_COUNT  0
#define ARR_CAP    8
#define ARR_KIND   16
#define ARR_DATA   24
#define ARR_HEADER 32

/* What an array's elements are, so the runtime can print them. Codegen
   knows the element type statically; the runtime needs it too because
   printing an array is the one operation that has to format them. */
enum { AK_INT = 0, AK_FLOAT = 1, AK_STRING = 2, AK_BOOL = 3 };

typedef struct { size_t site; int id; }      RtFix;
typedef struct { size_t site; char *name; }  FnFix;
typedef struct { char *name; size_t off; }   FnDef;
typedef struct { size_t site; size_t off; }  DataFix;

typedef struct {
    Buf     text;                 /* entry stub + runtime + user code */
    Buf     rodata;               /* string literals, appended to text at the end */
    size_t  rt_off[RT_COUNT];

    RtFix   *rtfix;   int rtfix_n,  rtfix_cap;
    FnFix   *fnfix;   int fnfix_n,  fnfix_cap;
    FnDef   *fns;     int fns_n,    fns_cap;
    DataFix *dfix;    int dfix_n,   dfix_cap;

    char    err[256];             /* first unsupported construct, if any */
    int     failed;
} NCtx;

/* Emit `call <runtime routine>`, recording a fixup. */
void nc_call_rt(NCtx *c, RtId id);

/* Emit `call <user function>` by name, recording a fixup. */
void nc_call_fn(NCtx *c, const char *name);

/* Record that a user function starts at the current end of the text. */
void nc_define_fn(NCtx *c, const char *name);

/* Add bytes to the literal pool, returning their offset within it.
   Identical literals are shared. */
size_t nc_intern(NCtx *c, const void *bytes, size_t n);
size_t nc_intern_str(NCtx *c, const char *s);   /* interns s including its NUL */

/* Emit `mov reg, <absolute address of literal-pool offset>`, recording a
   fixup for when the pool's final address is known. */
void nc_load_data(NCtx *c, int reg, size_t rodata_off);

/* Convenience: intern `s` and load its address into `reg`. */
void nc_load_cstr(NCtx *c, int reg, const char *s);

/* Record the first construct the backend cannot compile. Codegen keeps going
   afterwards so the caller gets one clear reason rather than a cascade. */
void nc_fail(NCtx *c, const char *fmt, ...);

/* Emit every runtime routine into c->text and fill in c->rt_off. */
void nc_emit_runtime(NCtx *c);

/* Resolve all recorded fixups. Call once, after the literal pool has been
   appended to the text buffer at `rodata_off`. */
void nc_resolve(NCtx *c, size_t rodata_off);

#endif /* SUB_X64_INTERNAL_H */
