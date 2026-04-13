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

    // All Windows compilers (MSVC and MinGW) use these mappings
    #define strdup _strdup
    #define strcasecmp _stricmp
    #define strncasecmp _strnicmp

    // Windows doesn't have <strings.h>, functions are in <string.h>
    #ifndef _STRINGS_H_INCLUDED
        #define _STRINGS_H_INCLUDED
    #endif

    // Windows doesn't have strndup - provide implementation
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
