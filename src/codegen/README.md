# Code Generation Backends

This directory contains all code generation backends for SUB language.

## Files

### Main Code Generation
- codegen.c - Main code generation logic
- targets.c - Target platform management
- targets.h - Target platform definitions

### Language-Specific Backends
- codegen_cpp.c/h - C++ code generation
- codegen_rust.c/h - Rust code generation
- codegen_x64.c/h - x86-64 assembly generation
- codegen_native.c/h - Native compilation support
- codegen_multilang.c - Multi-language transpilation
