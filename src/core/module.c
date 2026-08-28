#define _GNU_SOURCE
#include "module.h"
#include "windows_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  define PATH_SEP      '\\'
#  define PATH_LIST_SEP ';'
#else
#  include <unistd.h>
#  define PATH_SEP      '/'
#  define PATH_LIST_SEP ':'
#endif
#ifdef __APPLE__
#  include <mach-o/dyld.h>
#endif

#define MODULE_MAX 256

static char *g_source = NULL;                  /* file currently being parsed */
static char *g_included[MODULE_MAX]; static int g_nincluded = 0;
static char *g_stack[MODULE_MAX];    static int g_nstack = 0;

/* ---------------------------------------------------------------- */

void module_set_source(const char *path) {
    free(g_source);
    g_source = path ? strdup(path) : NULL;
}

const char *module_current_source(void) { return g_source; }

void module_reset(void) {
    for (int i = 0; i < g_nincluded; i++) free(g_included[i]);
    for (int i = 0; i < g_nstack; i++) free(g_stack[i]);
    g_nincluded = g_nstack = 0;
}

/* ---------------------------------------------------------------- */

static int is_sep(char c) {
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

/* The directory part of a path, or "." when there is none. Caller frees. */
static char *dir_of(const char *path) {
    if (!path) return strdup(".");
    const char *last = NULL;
    for (const char *p = path; *p; p++)
        if (is_sep(*p)) last = p;
    if (!last) return strdup(".");
    size_t n = (size_t)(last - path);
    if (n == 0) n = 1;                          /* "/foo" -> "/" */
    char *d = malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, path, n);
    d[n] = '\0';
    return d;
}

static char *join_path(const char *dir, const char *name) {
    if (!dir || !name) return NULL;
    size_t dn = strlen(dir);
    int need = dn > 0 && !is_sep(dir[dn - 1]);
    char *out = malloc(dn + (size_t)need + strlen(name) + 1);
    if (!out) return NULL;
    memcpy(out, dir, dn);
    if (need) out[dn] = PATH_SEP;
    strcpy(out + dn + need, name);
    return out;
}

static int readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Full path of the running executable, or NULL. Used to find the stdlib that
   shipped alongside it, so an unpacked release tarball works with no setup. */
static char *exe_path(void) {
    char buf[4096];
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)sizeof buf);
    if (n == 0 || n >= sizeof buf) return NULL;
    return strdup(buf);
#elif defined(__APPLE__)
    uint32_t n = (uint32_t)sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return NULL;
    return strdup(buf);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL;
    buf[n] = '\0';
    return strdup(buf);
#endif
}

char *module_read(const char *path) {
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* ---------------------------------------------------------------- */

/* Record one candidate in the "tried" list, so a failed import can say where
   it looked instead of only that it failed. */
static void note(char **tried, const char *path) {
    if (!tried) return;
    size_t have = *tried ? strlen(*tried) : 0;
    size_t need = have + strlen(path) + 8;
    char *grown = realloc(*tried, need);
    if (!grown) return;
    if (!have) grown[0] = '\0';
    strcat(grown, "  ");
    strcat(grown, path);
    strcat(grown, "\n");
    *tried = grown;
}

/* Try `dir/name` and `dir/name.sb`. Returns a match or NULL. */
static char *try_dir(const char *dir, const char *name, char **tried) {
    if (!dir) return NULL;

    char *direct = join_path(dir, name);
    if (direct) {
        note(tried, direct);
        if (readable(direct)) return direct;
        free(direct);
    }

    size_t n = strlen(name);
    if (n > 3 && strcmp(name + n - 3, ".sb") == 0) return NULL;  /* already had one */

    char *with_ext = malloc(n + 4);
    if (!with_ext) return NULL;
    memcpy(with_ext, name, n);
    strcpy(with_ext + n, ".sb");
    char *cand = join_path(dir, with_ext);
    free(with_ext);
    if (cand) {
        note(tried, cand);
        if (readable(cand)) return cand;
        free(cand);
    }
    return NULL;
}

char *module_resolve(const char *name, const char *importing_file, char **tried) {
    if (!name || !*name) return NULL;
    if (tried) *tried = NULL;

    /* An absolute path, or one written relative to the importing file with an
       explicit ./ or ../, means exactly what it says. */
    int anchored = is_sep(name[0]) || name[0] == '.'
#ifdef _WIN32
                   || (name[0] && name[1] == ':')
#endif
                   ;

    char *from = dir_of(importing_file);
    if (anchored && (is_sep(name[0])
#ifdef _WIN32
                     || (name[0] && name[1] == ':')
#endif
                     )) {
        char *hit = try_dir("", name, tried);
        free(from);
        return hit;
    }

    /* 1. beside the importing file */
    char *hit = try_dir(from, name, tried);
    free(from);
    if (hit) return hit;

    /* 2. $SUB_PATH */
    const char *env = getenv("SUB_PATH");
    if (env && *env) {
        char *copy = strdup(env);
        if (copy) {
            char *p = copy;
            while (p && *p) {
                char *sep = strchr(p, PATH_LIST_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    hit = try_dir(p, name, tried);
                    if (hit) { free(copy); return hit; }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }

    /* 3 and 4. the stdlib shipped with the binary */
    char *exe = exe_path();
    if (exe) {
        char *bindir = dir_of(exe);
        free(exe);
        if (bindir) {
            char *lib = join_path(bindir, "stdlib");
            if (lib) { hit = try_dir(lib, name, tried); free(lib); }
            if (!hit) {
                char *up = join_path(bindir, ".." );
                if (up) {
                    char *libsub = join_path(up, "lib" );
                    free(up);
                    if (libsub) {
                        char *l2 = join_path(libsub, "sub");
                        free(libsub);
                        if (l2) {
                            char *l3 = join_path(l2, "stdlib");
                            free(l2);
                            if (l3) { hit = try_dir(l3, name, tried); free(l3); }
                        }
                    }
                }
            }
            free(bindir);
            if (hit) return hit;
        }
    }

    return NULL;
}

/* ---------------------------------------------------------------- */

/* Two spellings of one file -- "math" and "./math.sb" -- must count as the
   same module or it gets spliced twice. Comparing the resolved paths is
   enough for the shapes module_resolve() produces. */
static int same_file(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

int module_already_included(const char *resolved_path) {
    if (!resolved_path) return 0;
    for (int i = 0; i < g_nincluded; i++)
        if (same_file(g_included[i], resolved_path)) return 1;
    if (g_nincluded < MODULE_MAX)
        g_included[g_nincluded++] = strdup(resolved_path);
    return 0;
}

int module_in_progress(const char *resolved_path) {
    if (!resolved_path) return 0;
    for (int i = 0; i < g_nstack; i++)
        if (same_file(g_stack[i], resolved_path)) return 1;
    return 0;
}

void module_push(const char *resolved_path) {
    if (resolved_path && g_nstack < MODULE_MAX)
        g_stack[g_nstack++] = strdup(resolved_path);
}

void module_pop(void) {
    if (g_nstack > 0) free(g_stack[--g_nstack]);
}
