/* ============================================================
   SUB Language - Common Utilities
   Shared StringBuilder, file I/O, and helper functions used
   across all compiler components.
   ============================================================ */

#ifndef SUB_COMMON_H
#define SUB_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   StringBuilder - Dynamic string construction
   ============================================================ */

typedef struct {
    char  *buffer;
    size_t size;
    size_t capacity;
} SbBuilder;

/* Create a new string builder. Returns NULL on allocation failure. */
static inline SbBuilder* sb_create(void) {
    SbBuilder *sb = malloc(sizeof(SbBuilder));
    if (!sb) return NULL;
    sb->capacity = 4096;
    sb->size     = 0;
    sb->buffer   = malloc(sb->capacity);
    if (!sb->buffer) { free(sb); return NULL; }
    sb->buffer[0] = '\0';
    return sb;
}

/* Append formatted text to the builder. */
static inline void sb_append(SbBuilder *sb, const char *fmt, ...) {
    if (!sb || !fmt) return;
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    if (needed < 0) { va_end(args); return; }
    while (sb->size + (size_t)needed + 1 > sb->capacity) {
        size_t new_cap = sb->capacity * 2;
        if (new_cap <= sb->capacity) { va_end(args); return; } /* overflow */
        char *new_buf = realloc(sb->buffer, new_cap);
        if (!new_buf) { va_end(args); return; }
        sb->buffer   = new_buf;
        sb->capacity = new_cap;
    }
    vsnprintf(sb->buffer + sb->size, (size_t)needed + 1, fmt, args);
    sb->size += (size_t)needed;
    va_end(args);
}

/* Convert builder to a heap-allocated C string. Frees the builder. */
static inline char* sb_to_string(SbBuilder *sb) {
    if (!sb) return NULL;
    char *result = strdup(sb->buffer);
    free(sb->buffer);
    free(sb);
    return result;
}

/* Free the builder without extracting the string. */
static inline void sb_free(SbBuilder *sb) {
    if (!sb) return;
    free(sb->buffer);
    free(sb);
}

/* ============================================================
   File I/O Utilities
   ============================================================ */

/* Read an entire file into a heap-allocated string. Returns NULL on failure. */
static inline char* sub_read_file(const char *filename) {
    if (!filename) return NULL;
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nread] = '\0';
    return buf;
}

/* Write a string to a file. Returns 0 on success, -1 on failure. */
static inline int sub_write_file(const char *filename, const char *content) {
    if (!filename || !content) return -1;
    FILE *f = fopen(filename, "w");
    if (!f) return -1;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

/* ============================================================
   String Utilities
   ============================================================ */

/* Concatenate two strings, returning a new heap-allocated string. */
static inline char* sub_str_concat(const char *a, const char *b) {
    if (!a && !b) return NULL;
    if (!a) return strdup(b);
    if (!b) return strdup(a);
    size_t la = strlen(a), lb = strlen(b);
    char *r = malloc(la + lb + 1);
    if (!r) return NULL;
    memcpy(r, a, la);
    memcpy(r + la, b, lb + 1);
    return r;
}

/* Escape a string for use in C string literals. */
static inline char* sub_cstr_escape(const char *s) {
    if (!s) return strdup("");
    size_t len = strlen(s);
    char *out = malloc(len * 2 + 1);
    if (!out) return strdup("");
    size_t j = 0;
    for (size_t i = 0; i < len && s[i]; i++) {
        switch (s[i]) {
            case '\n':  out[j++] = '\\'; out[j++] = 'n';  break;
            case '\r':  out[j++] = '\\'; out[j++] = 'r';  break;
            case '\t':  out[j++] = '\\'; out[j++] = 't';  break;
            case '\\':  out[j++] = '\\'; out[j++] = '\\';  break;
            case '"':  out[j++] = '\\'; out[j++] = '"';  break;
            case '\0':  out[j++] = '\\'; out[j++] = '0';  break;
            default:    out[j++] = s[i]; break;
        }
    }
    out[j] = '\0';
    return out;
}

/* Get the base name of a file path (without extension). */
static inline char* sub_basename(const char *path) {
    if (!path) return strdup("out");
    const char *slash = strrchr(path, '/');
    if (!slash) slash = strrchr(path, '\\');
    const char *start = slash ? slash + 1 : path;
    char *dup = strdup(start);
    if (!dup) return strdup("out");
    char *dot = strrchr(dup, '.');
    if (dot) *dot = '\0';
    return dup;
}

/* Resolve an import path relative to the current file's directory.
   e.g., resolve_import("/src/main.sb", "math") -> "/src/math.sb" */
static inline char* sub_resolve_import(const char *current_file, const char *module_name) {
    if (!current_file || !module_name) return NULL;
    /* Find directory of current file */
    const char *last_slash = strrchr(current_file, '/');
    size_t dir_len = last_slash ? (size_t)(last_slash - current_file + 1) : 0;
    size_t mod_len = strlen(module_name);
    /* Ensure .sb extension */
    const char *ext = ".sb";
    size_t ext_len = 3;
    int has_ext = (mod_len > 3 && strcmp(module_name + mod_len - 3, ".sb") == 0);
    size_t total = dir_len + mod_len + (has_ext ? 0 : ext_len) + 1;
    char *result = malloc(total);
    if (!result) return NULL;
    memcpy(result, current_file, dir_len);
    memcpy(result + dir_len, module_name, mod_len);
    if (!has_ext) memcpy(result + dir_len + mod_len, ext, ext_len + 1);
    else result[dir_len + mod_len] = '\0';
    return result;
}

/* Search for a module in standard library paths.
   Checks: <dir_of_current_file>/<module>.sb, <exe_dir>/../stdlib/<module>.sb,
   /usr/lib/sub-lang/<module>.sb */
static inline char* sub_find_module(const char *current_file, const char *module_name) {
    /* 1. Relative to current file */
    char *path = sub_resolve_import(current_file, module_name);
    if (path) {
        FILE *f = fopen(path, "r");
        if (f) { fclose(f); return path; }
        free(path);
    }
    /* 2. Try with stdlib/ prefix relative to current file's directory */
    const char *last_slash = strrchr(current_file, '/');
    size_t dir_len = last_slash ? (size_t)(last_slash - current_file + 1) : 0;
    const char *stdlib_rel = "stdlib/";
    size_t slen = 7;
    size_t mlen = strlen(module_name);
    int has_ext = (mlen > 3 && strcmp(module_name + mlen - 3, ".sb") == 0);
    const char *ext = ".sb";
    size_t total = dir_len + slen + mlen + (has_ext ? 0 : 3) + 1;
    char *sp = malloc(total);
    if (sp) {
        memcpy(sp, current_file, dir_len);
        memcpy(sp + dir_len, stdlib_rel, slen);
        memcpy(sp + dir_len + slen, module_name, mlen);
        if (!has_ext) { memcpy(sp + dir_len + slen + mlen, ext, 4); }
        else { sp[dir_len + slen + mlen] = '\0'; }
        FILE *f = fopen(sp, "r");
        if (f) { fclose(f); return sp; }
        free(sp);
    }
    return NULL;
}

#endif /* SUB_COMMON_H */
