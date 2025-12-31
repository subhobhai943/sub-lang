# Source Code Reorganization Plan

## Overview

Reorganizing all source files from root directory into a clean `src/` structure for better maintainability and professionalism.

## New Directory Structure

```
sub-lang/
├── src/
│   ├── core/              # Core compiler components
│   │   ├── lexer.c
│   │   ├── parser.c
│   │   ├── parser_enhanced.c
│   │   ├── semantic.c
│   │   ├── type_system.c
│   │   ├── type_system.h
│   │   ├── error_handler.c
│   │   ├── error_handler.h
│   │   └── utils.c
│   │
│   ├── codegen/           # Code generation backends
│   │   ├── codegen.c              # Main codegen
│   │   ├── codegen_cpp.c          # C++ backend
│   │   ├── codegen_cpp.h
│   │   ├── codegen_multilang.c    # Multi-language support
│   │   ├── codegen_native.c       # Native compilation
│   │   ├── codegen_native.h
│   │   ├── codegen_rust.c         # Rust backend
│   │   ├── codegen_rust.h
│   │   ├── codegen_x64.c          # x86-64 backend
│   │   ├── codegen_x64.h
│   │   ├── targets.c              # Target management
│   │   └── targets.h
│   │
│   ├── ir/                # Intermediate representation
│   │   ├── ir.c
│   │   └── ir.h
│   │
│   ├── compilers/         # Main compiler executables
│   │   ├── sub.c                  # Standard compiler
│   │   ├── sub_multilang.c        # Multi-language compiler
│   │   ├── sub_native.c           # Native compiler (old)
│   │   └── sub_native_compiler.c  # Native compiler (new)
│   │
│   └── include/           # Public headers
│       ├── sub_compiler.h         # Main compiler header
│       └── windows_compat.h       # Windows compatibility
│
├── examples/              # Example programs (existing)
├── tests/                 # Test files (existing)
├── stdlib/                # Standard library (existing)
├── docs/                  # Documentation (existing)
├── build/                 # Build artifacts (git ignored)
├── Makefile               # Build system
├── CMakeLists.txt         # CMake build
├── build.sh               # Build script
└── README.md              # Main readme
```

## Files to Move (35 files)

### Batch 1: Core Compiler (9 files)
- lexer.c → src/core/
- parser.c → src/core/
- parser_enhanced.c → src/core/
- semantic.c → src/core/
- type_system.c → src/core/
- type_system.h → src/core/
- error_handler.c → src/core/
- error_handler.h → src/core/
- utils.c → src/core/

### Batch 2: Code Generation (12 files)
- codegen.c → src/codegen/
- codegen_cpp.c → src/codegen/
- codegen_cpp.h → src/codegen/
- codegen_multilang.c → src/codegen/
- codegen_native.c → src/codegen/
- codegen_native.h → src/codegen/
- codegen_rust.c → src/codegen/
- codegen_rust.h → src/codegen/
- codegen_x64.c → src/codegen/
- codegen_x64.h → src/codegen/
- targets.c → src/codegen/
- targets.h → src/codegen/

### Batch 3: IR & Headers (4 files)
- ir.c → src/ir/
- ir.h → src/ir/
- sub_compiler.h → src/include/
- windows_compat.h → src/include/

### Batch 4: Main Compilers (4 files)
- sub.c → src/compilers/
- sub_multilang.c → src/compilers/
- sub_native.c → src/compilers/
- sub_native_compiler.c → src/compilers/

## Test Files to Move to tests/

Move all test_*.sb files from root to tests/:
- test_add.sb
- test_array.sb
- test_call.sb
- test_class.sb
- test_correct.sb
- test_functions.sb
- test_int.sb
- test_lexer.sb
- test_minimal.sb
- test_simple.sb
- test_simple_function.sb
- test_string.sb
- test_string_simple.sb
- test_tokens.sb
- test_type_error.sb
- test_type_errors.sb
- test_types.sb
- test_types2.sb
- test_types3.sb
- test_very_simple.sb
- simple_test.sb

## Build System Updates Required

### Makefile
Update all source paths:
- Change `lexer.c` to `src/core/lexer.c`
- Change `codegen.c` to `src/codegen/codegen.c`
- Add include path: `-Isrc/include`
- Update all object file paths

### CMakeLists.txt
Update:
```cmake
include_directories(src/include)
add_subdirectory(src/core)
add_subdirectory(src/codegen)
add_subdirectory(src/ir)
add_subdirectory(src/compilers)
```

### build.sh
Update all paths to reference `src/` directories

## Benefits

1. **Clean Root Directory**: Only essential files (README, LICENSE, Makefile, etc.)
2. **Logical Organization**: Related files grouped together
3. **Scalability**: Easy to add new modules
4. **Professional**: Follows industry best practices
5. **Maintainability**: Easier to navigate and understand
6. **Build Clarity**: Clear separation of concerns

## Implementation Steps

1. ✅ Create docs/implementation/SRC_REORGANIZATION_PLAN.md
2. ⏳ Create src/ directory structure
3. ⏳ Move files in batches (preserve git history)
4. ⏳ Update Makefile
5. ⏳ Update CMakeLists.txt
6. ⏳ Update build.sh
7. ⏳ Update #include paths in source files
8. ⏳ Test compilation
9. ⏳ Update documentation
10. ⏳ Remove old files from root

## Testing Checklist

After reorganization, verify:
- [ ] `make clean && make` compiles successfully
- [ ] `cmake . && make` compiles successfully
- [ ] `./build.sh` executes without errors
- [ ] All compilers (sub, sublang, subc-native) work
- [ ] Test programs compile and run
- [ ] Examples compile correctly

---

**Status**: Planning Complete
**Next Step**: Execute file moves in batches
**Estimated Time**: 15-20 minutes
**Risk**: Low (can revert if needed)
