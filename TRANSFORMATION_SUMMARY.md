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
  - **NEW**: String support - variables can hold string pointers (char*), with runtime concatenation support.
- **`ir.c` & `ir.h`**: A new Intermediate Representation layer.
  - Decouples parsing from code generation.
  - Converts AST into a linear sequence of instructions (`IR_LOAD`, `IR_STORE`, `IR_ADD`, `IR_JUMP_IF_NOT`, etc.).
  - Handles variable resolution and scope management.
  - **NEW**: Supports string type handling (`IR_TYPE_STRING`, `IR_CONST_STRING`) and string concatenation.
- **`sub_native_compiler.c`**: The driver program for the native compiler.
  - Links `lexer`, `parser_enhanced`, `semantic`, `ir`, and `codegen_x64`.

## 2. String Support Implementation

String variables are now first-class citizens in the native compiler.

### IR Layer (ir.c):
- **String Literals**: `IR_CONST_STRING` instruction creates string constant references (ir.c:319-331)
- **Variable Declarations**: `AST_VAR_DECL` now handles `TYPE_STRING` with proper `IR_TYPE_STRING` typing (ir.c:174-193)
- **String Concatenation**: `AST_BINARY_EXPR` detects `+` operator with string operands and generates `IR_CALL` to `str_concat` runtime function (ir.c:288-310)
- **Variable Loading**: `AST_IDENTIFIER` and assignment handlers now preserve string type information (ir.c:480-503, 257-283)

### Code Generation (codegen_x64.c):
- **String Storage**: String variables stored as pointers (char*) on stack (8 bytes each)
- **String Literals**: Generated as .rodata labels, loaded via `leaq` instruction (codegen_x64.c:156-162)
- **String Printing**: `IR_PRINT` detects string types and uses `%s\n` format specifier (codegen_x64.c:271-286)
- **Runtime Helper**: `str_concat()` function provides string concatenation using `strlen`, `malloc`, and `strcpy` (codegen_x64.c:380-405)

### Example Usage:
```sub
var greeting = "Hello"
var name = "World"
print(greeting + " " + name)  # Output: Hello World
```

## 3. Parser Enhancements (`parser_enhanced.c`)

The parser was significantly refactored to support complex control flow and fix critical bugs.

### Fixes & Features:
- **Statement Parsing**: Fixed the main loop to parse statements correctly without requiring a `#` prefix.
- **Operator Precedence**: Implemented strict precedence climbing (PEMDAS) to fix logic errors (e.g., `x = x - 1` previously parsed incorrectly).
- **Block Handling**: Updated `parse_block` to correctly handle brace-delimited blocks (`{ ... }`) for `if`, `while`, and functions.
- **Expression Statements**: Added support for generic expression statements (assignments, function calls) as top-level statements.

## 4. Testing Infrastructure

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

## 5. Build System

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
