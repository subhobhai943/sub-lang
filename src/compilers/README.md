# Main Compiler Executables

This directory contains the main compiler driver programs.

## Files

- sub.c - `sub`, the transpiler (any target from `src/codegen/`)
- sub_native.c - `subc`, the native compiler (transpiles to C, then invokes gcc/clang)
- subi.c - `subi`, the interpreter (drives `src/core/interpreter.c` directly)
