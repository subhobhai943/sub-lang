/* ========================================
   SUB Language - Windows Compatibility Header
   Handles MSVC/Windows-specific function mappings
   Include this at the top of files using POSIX functions
   ======================================== */

#ifndef WINDOWS_COMPAT_H
#define WINDOWS_COMPAT_H

#ifdef _WIN32
    #include <string.h>
    #include <stdlib.h>
    
    // MSVC uses _strdup instead of strdup
    #define strdup _strdup
    
    // MSVC uses _stricmp instead of strcasecmp
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
#endif

#endif /* WINDOWS_COMPAT_H */
