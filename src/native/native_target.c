#include "native.h"
#include <string.h>
#include <stdio.h>
int x64_native_compile(ASTNode*, const char*, char*, size_t);
int a64_native_compile(ASTNode*, const char*, char*, size_t);
static const char *host_target(void){
#if defined(__aarch64__) && defined(__linux__)
    return "aarch64-linux";
#elif defined(__x86_64__) && defined(__linux__)
    return "x86_64-linux";
#else
    return NULL;
#endif
}
int native_compile_target(ASTNode *p, const char *out, char *err, size_t n, const char *tname){
    const char *t = tname ? tname : host_target();
    if(!t){ if(err&&n) snprintf(err,n,"no native backend for this host"); return 1; }
    if(!strcmp(t,"aarch64-linux")) return a64_native_compile(p,out,err,n);
    if(!strcmp(t,"x86_64-linux"))  return x64_native_compile(p,out,err,n);
    if(err&&n) snprintf(err,n,"unknown native target '%s'",t);
    return 1;
}
int native_compile(ASTNode *p, const char *out, char *err, size_t n){
    return native_compile_target(p,out,err,n,NULL);
}
