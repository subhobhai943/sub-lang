/* ========================================
   SUB Language - Windows Compatibility Header
   Handles MSVC/Windows-specific function mappings
   Include this at the top of files using POSIX functions
   ======================================== */

#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

#ifdef _WIN32
    #ifdef _MSC_VER
        // MSVC-specific: include <string.h> instead of <strings.h>
        #include <string.h>
        #include <stdlib.h>
        #include <inttypes.h>

        // MSVC doesn't support __attribute__
        #define UNUSED

        // MSVC doesn't have __builtin_expect, provide no-op shim
        #define __builtin_expect(expr, c) (expr)
        
        // MSVC uses _atoi64 for 64-bit string to int
        #define atoll _atoi64
    #else
        // MinGW and other Windows compilers
        #include <string.h>
        #include <stdlib.h>
        #include <inttypes.h>

        // GCC/Clang-style attributes work in MinGW
        #define UNUSED __attribute__((unused))

        // MinGW has __builtin_expect as a compiler built-in — do not redefine.
    #endif

    // Only MSVC needs the POSIX name shims below.
    #ifdef _MSC_VER
        #define strdup _strdup
        #define strcasecmp _stricmp
        #define strncasecmp _strnicmp
    #else
        static inline char* sub_strdup_compat(const char* s) {
            if (!s) return NULL;

            size_t len = strlen(s);
            char* result = (char*)malloc(len + 1);
            if (!result) return NULL;

            memcpy(result, s, len + 1);
            return result;
        }

        static inline char* sub_strndup_compat(const char* s, size_t n) {
            if (!s) return NULL;

            size_t len = strlen(s);
            if (n < len) len = n;

            char* result = (char*)malloc(len + 1);
            if (!result) return NULL;

            memcpy(result, s, len);
            result[len] = '\0';
            return result;
        }

        #define strdup sub_strdup_compat
        #define strndup sub_strndup_compat
    #endif

    // Windows doesn't have <strings.h>, functions are in <string.h>
    #ifndef _STRINGS_H_INCLUDED
        #define _STRINGS_H_INCLUDED
    #endif

    // MSVC still lacks strndup - provide a local implementation there only.
    #if defined(_WIN32) && defined(_MSC_VER)
        #ifndef HAVE_STRNDUP
            #define HAVE_STRNDUP

            static inline char* strndup(const char* s, size_t n) {
                if (!s) return NULL;

                size_t len = strlen(s);
                if (n < len) len = n;

                char* result = (char*)malloc(len + 1);
                if (!result) return NULL;

                memcpy(result, s, len);
                result[len] = '\0';
                return result;
            }
        #endif
    #endif
#else
    // On Unix-like systems, strings.h is available
    #include <strings.h>
    #include <inttypes.h>

    #define UNUSED __attribute__((unused))

    // GCC/Clang have __builtin_expect as a compiler built-in.
    // Do NOT redefine it here — the #ifndef check would succeed
    // (because it's a built-in, not a macro) and clobber it.
#endif

#endif /* WINDOWS_COMPAT_H */
