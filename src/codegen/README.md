# Code Generation Backends

This directory contains all code generation backends for SUB language.

## Files

### Main Code Generation
- codegen.c - C backend (used by `subc`, the native compiler)
- codegen_infer.c/h - shared signature inference; annotates the AST once for every statically typed backend
- codegen_switch.c/h - switch lowering shared by the backends
- codegen_globals.c/h - top-level variable collection shared by the backends

The target registry (name -> codegen function, output extension, run hint)
lives in `src/compilers/sub.c`, not in this directory.

### Language-Specific Backends
- codegen_multilang.c - Python, Java, Swift, Kotlin, JavaScript, Go, Assembly, CSS, Ruby
- codegen_rust.c/h - Rust code generation
- codegen_cpp.c/h - C++ code generation (C++, C++17, C++20 targets)
