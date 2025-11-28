/**
 * SUB Language Backend Header
 */

#ifndef SUB_BACKEND_CODEGEN_H
#define SUB_BACKEND_CODEGEN_H

#ifdef __cplusplus
extern "C" {
#endif

char* sub_backend_generate_web(void* ast_root);
char* sub_backend_generate_android(void* ast_root);
char* sub_backend_generate_native(void* ast_root);
void sub_backend_free_code(char* code);

#ifdef __cplusplus
}
#endif

#endif // SUB_BACKEND_CODEGEN_H
