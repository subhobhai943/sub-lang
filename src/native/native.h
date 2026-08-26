/* ========================================
   SUB Language - native backend entry point
   ======================================== */

#ifndef SUB_NATIVE_H
#define SUB_NATIVE_H

#include "sub_compiler.h"
#include <stddef.h>

/* True when this build can emit machine code itself. The backend targets
   x86-64 Linux; everywhere else `subc` still goes through the C backend and
   a host C compiler. */
#if defined(__x86_64__) && defined(__linux__)
#  define SUB_NATIVE_BACKEND 1
#else
#  define SUB_NATIVE_BACKEND 0
#endif

/* Compile `program` straight to a static executable at `out_path`, with no
   assembler, linker or C compiler involved. Returns 0 on success; on failure
   returns non-zero and writes the reason into `err`. A failure here means
   the program uses something the backend does not implement yet, not that
   the program is wrong - the caller can fall back to the C backend. */
int native_compile(ASTNode *program, const char *out_path,
                   char *err, size_t err_size);

#endif /* SUB_NATIVE_H */
