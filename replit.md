# SUB Programming Language

## Overview
SUB is a compiler for a programming language with blockchain-inspired syntax that compiles directly to native machine code (x86-64). It also supports transpilation to 10+ languages including Python, JavaScript, Java, Rust, and more.

## Project Structure
- **Compilers**: Two compiler executables
  - `subc-native` - Native x86-64 machine code compiler
  - `sublang` - Multi-language transpiler
- **Source Files**:
  - `lexer.c` - Tokenization
  - `parser.c` - AST construction
  - `semantic.c` - Type checking
  - `ir.c` - Intermediate representation
  - `codegen_x64.c` - x86-64 code generation
  - `codegen_multilang.c` - Multi-language transpiler
  - `sub_native_compiler.c` - Native compiler driver
  - `sub_multilang.c` - Transpiler driver
- **Standard Library**: `stdlib/` - Collection of standard library modules
- **Examples**: `examples/` and various `.sb` test files

## Build Commands
```bash
make all          # Build both compilers
make native       # Build native compiler only
make transpiler   # Build transpiler only
make test         # Run test suite
make clean        # Clean build artifacts
```

## Usage
### Native Compilation
```bash
./subc-native program.sb output_name
./output_name   # Run the compiled binary
```

### Transpilation
```bash
./sublang program.sb python      # Generate Python
./sublang program.sb javascript  # Generate JavaScript
./sublang program.sb rust        # Generate Rust
```

## SUB Syntax
- Variables: `#var name = "value"`
- Functions: `#function name(args) ... #end`
- Print: `#print(expression)`
- Control flow: `#if`, `#elif`, `#else`, `#end`
- Loops: `#for`, `#while`, `#end`

## Development Notes
- Built with C11 standard
- Uses GCC for compilation
- Platform detection for Linux/macOS/Windows
