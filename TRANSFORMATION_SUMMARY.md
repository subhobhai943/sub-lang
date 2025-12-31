# SUB Language Transformation Summary

This document summarizes the major changes and enhancements made to the SUB language compiler, focusing on the implementation of a native verification pipeline and robust x86-64 code generation.

## 1. Native Compiler Implementation (x86-64)

We have successfully implemented a functional native compiler capable of generating Linux x86-64 assembly.

### Key Components:
- **`codegen_x64.c`**: A new backend that translates Intermediate Representation (IR) into x86-64 assembly.
  - Implements stack-based variable storage (variables are stored at `rbp - offset`).
  - Supports arithmetic operations (`+`, `-`, `*`, `/`) using stack logic to ensure correct register usage.
  - Implements control flow: `if`, `else`, `while` loops using labels and conditional jumps (`cmp`, `je`, `jmp`).
  - Implements `print(...)` using `printf` from libc.
- **`ir.c` & `ir.h`**: A new Intermediate Representation layer.
  - Decouples parsing from code generation.
  - Converts AST into a linear sequence of instructions (`IR_LOAD`, `IR_STORE`, `IR_ADD`, `IR_JUMP_IF_NOT`, etc.).
  - Handles variable resolution and scope management.
- **`sub_native_compiler.c`**: The driver program for the native compiler.
  - Links `lexer`, `parser_enhanced`, `semantic`, `ir`, and `codegen_x64`.

## 2. Parser Enhancements (`parser_enhanced.c`)

The parser was significantly refactored to support complex control flow and fix critical bugs.

### Fixes & Features:
- **Statement Parsing**: Fixed the main loop to parse statements correctly without requiring a `#` prefix.
- **Operator Precedence**: Implemented strict precedence climbing (PEMDAS) to fix logic errors (e.g., `x = x - 1` previously parsed incorrectly).
- **Block Handling**: Updated `parse_block` to correctly handle brace-delimited blocks (`{ ... }`) for `if`, `while`, and functions.
- **Expression Statements**: Added support for generic expression statements (assignments, function calls) as top-level statements.

## 3. Testing Infrastructure

We introduced a comprehensive verification suite.

- **`examples/universal_test.sb`**: A "Rosetta Stone" test file that verifies:
  - Integer arithmetic and printing.
  - Variable assignment and state updates.
  - Conditional logic (`if/else`).
  - Iterative logic (`while` loops).
- **`tests/run_tests.py`**: A validation script that:
  - Compiles `subc-native`.
  - Transpiles to Python, JS, etc. (regression testing).
  - Runs the native binary and verifies output matches expected results.

## 4. Build System

- **`Makefile`**: Updated to build both:
  - `subc-native`: The native compiler.
  - `sublang`: The multi-language transpiler.
  - Uses `parser_enhanced.c` for both targets to ensure consistency.

## Usage

### Native Compilation
```bash
make native
./subc-native examples/universal_test.sb my_program
./my_program
```

### Transpilation
```bash
make transpiler
./sublang examples/universal_test.sb python
python3 examples/universal_test.py
```
