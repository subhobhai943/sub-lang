# Code Generation Backends

This directory contains all code generation backends for SUB language.

## Files

### Main Code Generation
- codegen.c - C backend (used by `subc`, the native compiler)
- targets.c / targets.h - Target registry: maps a target name to its codegen function

### Language-Specific Backends
- codegen_multilang.c - Python, Java, Swift, Kotlin, JavaScript, Go, Assembly, CSS, Ruby
- codegen_rust.c/h - Rust code generation
- codegen_cpp.c/h - C++ code generation (C++, C++17, C++20 targets)
