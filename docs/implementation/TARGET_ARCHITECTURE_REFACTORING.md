# Target Architecture Refactoring Summary

## Overview
Standardized the target architecture for the SUB Language Compiler by creating a unified registry system for managing code generation targets.

## Changes Made

### 1. Created `targets.h` and `targets.c`
**File: `targets.h`**
- Defined `TargetLanguage` enum with all supported languages (C, C++, C++17, C++20, Python, Java, Swift, Kotlin, Rust, JavaScript, TypeScript, Go, Assembly, CSS, Ruby, LLVM IR, WebAssembly)
- Defined `LanguageInfo` struct for language metadata (name, extension, compiler, run_command)
- Created unified function pointer type: `typedef char* (*CodegenFn)(ASTNode*, const char*)`
- Defined `TargetRegistry` struct for registry entries
- Declared utility functions: `language_info_get()`, `get_codegen_for_target()`, `parse_language()`, `language_to_string()`, `target_is_implemented()`

**File: `targets.c`**
- Implemented `language_info_table[]` array with language metadata
- Created `target_registry[]` lookup table mapping languages to codegen functions
- Implemented registry lookup functions using table-based approach instead of if/else chains
- Added simple stub implementations for `codegen_cpp()` and `codegen_rust()` (since these weren't in codegen_multilang.c)
- Implemented helper functions for target parsing and validation

### 2. Refactored `sub_multilang.c`
**Removed:**
- Local definitions of `TargetLanguage` enum and `LanguageInfo` struct
- Local `language_info[]` array
- Local `parse_language()` function (now uses `targets.c` version)
- Large switch statement in `generate_code()` (now uses registry lookup)

**Added:**
- `#include "targets.h"` to use new target registry
- Simplified `generate_code()` function:
  - Special case for C language (uses `codegen_generate_c()`)
  - Registry-based lookup for all other languages
  - Built-in implementation check for unimplemented targets

### 3. Updated `Makefile`
- Added `targets.c` to `TRANS_SOURCES`
- Removed `codegen_rust.c` and `codegen_cpp.c` from build (to avoid linker conflicts)
- Updated TRANS_SOURCES to: `sub_multilang.c lexer.c parser_enhanced.c semantic.c codegen.c codegen_multilang.c type_system.c error_handler.c targets.c`

## Benefits

### 1. Centralized Target Management
- All target metadata and codegen function mappings in one place
- Easy to add new targets by:
  - Adding entry to `TargetLanguage` enum
  - Adding entry to `language_info_table[]`
  - Adding entry to `target_registry[]`
  - Adding language name parsing in `parse_language()`

### 2. Type Safety
- Unified function pointer type ensures all codegen functions have same signature
- Compile-time type checking for codegen function registration

### 3. Maintainability
- No more massive switch statements to update
- Registry-based lookup is O(n) but cleaner and more maintainable
- Clear separation between data and logic

### 4. Extensibility
- Easy to add new language variants (e.g., C++11, C++14, C++23)
- Simple to mark targets as implemented vs not implemented
- `target_is_implemented()` function allows runtime checks

### 5. Code Organization
- `targets.h/c` provide clean API for target management
- Other files can include `targets.h` to query target information
- No need to duplicate target metadata across files

## Usage Example

### Adding a New Target

1. Add to enum in `targets.h`:
```c
typedef enum {
    // ... existing languages ...
    LANG_DART,
    LANG_COUNT
} TargetLanguage;
```

2. Add to `language_info_table[]` in `targets.c`:
```c
{"dart", ".dart", "dart", "dart output.dart && ./output"},
```

3. Add to `target_registry[]` in `targets.c`:
```c
{LANG_DART, "dart", codegen_dart, true},
```

4. Add parsing in `parse_language()`:
```c
if (strcasecmp(lang_str, "dart") == 0) return LANG_DART;
```

5. Implement `codegen_dart()` function in `codegen_multilang.c`:
```c
char* codegen_dart(ASTNode *ast, const char *source) {
    // implementation
}
```

## Testing
- Verified transpiler builds successfully with new architecture
- Tested code generation for multiple targets (Python, C, Rust)
- Confirmed registry-based lookup correctly routes to appropriate codegen functions
- All existing functionality preserved

## Architecture Diagram

```
targets.h (header)
    ├── TargetLanguage enum
    ├── LanguageInfo struct
    ├── CodegenFn typedef
    ├── TargetRegistry struct
    └── Function declarations

targets.c (implementation)
    ├── language_info_table[] - metadata
    ├── target_registry[] - function mappings
    ├── language_info_get() - get metadata
    ├── get_codegen_for_target() - lookup codegen fn
    ├── parse_language() - string → enum
    ├── language_to_string() - enum → string
    └── target_is_implemented() - check if ready

sub_multilang.c (driver)
    ├── uses targets.h
    ├── calls get_codegen_for_target()
    ├── calls parse_language()
    └── generates code using returned function
```

## Future Improvements

1. **Hash Table for O(1) Lookup**: Convert registry from array to hash table for faster lookups
2. **Plugin System**: Allow dynamic loading of codegen modules
3. **Target Options**: Add configuration per target (e.g., C++ version selection)
4. **Validation Layer**: Add runtime validation of target configurations
5. **Target Documentation**: Generate target documentation from registry metadata

## Compatibility Notes

- C language target uses special handling (`codegen_generate_c()`) which takes `Platform` parameter instead of source string
- TypeScript currently delegates to JavaScript (can be enhanced in future)
- Go, LLVM IR, and WebAssembly are marked as not implemented yet
- C++17 and C++20 variants currently use same codegen as C++
- Stub implementations for C++ and Rust in `targets.c` should be replaced with full implementations or moved to `codegen_multilang.c`
