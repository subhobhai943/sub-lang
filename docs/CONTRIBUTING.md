# Contributing to SUB Language

Thank you for your interest in contributing to SUB Language! This guide will help you contribute to the project step by step.

## 🌍 Language Integration

**Any contributor can integrate their favorite language into SUB!** Whether it's Python, JavaScript, Java, or any other language, you can help expand SUB's cross-platform capabilities. Follow the steps below to get started.

## 📋 Step-by-Step Contribution Guide

### Step 1: Fork and Clone
```bash
# Fork the repository on GitHub, then clone your fork
git clone https://github.com/YOUR_USERNAME/sub-lang.git
cd sub-lang
```

### Step 2: Create a Feature Branch
```bash
git checkout -b feature/your-feature-name
# Example: git checkout -b feature/rust-compiler-backend
```

### Step 3: Set Up Development Environment
```bash
# Install prerequisites (GCC/Clang, Make)
make clean
make
```

### Step 4: Make Your Changes
- Add your language backend in appropriate files
- Follow the coding standards (see below)
- Test thoroughly on your platform

### Step 5: Test Your Changes
```bash
make clean
make
./sub example.sb [your-target]
```

### Step 6: Commit and Push
```bash
git add .
git commit -m "Add: Brief description of your changes"
git push origin feature/your-feature-name
```

### Step 7: Create Pull Request
- Go to your fork on GitHub
- Click "New Pull Request"
- Describe your changes in detail
- Reference any related issues
- Wait for review and address feedback

## ⚠️ Known Issues

### Windows Compatibility (v1.0.3)

**Current Status:** v1.0.3 does not support Windows due to POSIX-specific functions.

#### Issues:

| Issue        | Location          | Problem                  |
|--------------|-------------------|--------------------------|  
| `strdup()`   | 20+ files         | MSVC wants `_strdup()`   |
| `<strings.h>`| sub_multilang.c:9 | Doesn't exist on Windows |
| `strcasecmp()`| sub_multilang.c  | Windows uses `_stricmp()`|

#### The Fix for v1.0.4:

**Contributors can fix this in v1.0.4!** Add this at the top of affected files:

```c
#ifdef _WIN32
#define strdup _strdup
#define strcasecmp _stricmp
#endif
```

And replace `<strings.h>` include:

```c
// From:
#include <strings.h>

// To:
#ifndef _WIN32
#include <strings.h>
#endif
```

**Affected Files:** Check all C files using these functions (20+ files need updates).

## 🎯 Roadmap & Future Targets

Our next major goals are:

1. **Rust Compiler Architecture Integration** - Add Rust as a compilation target
2. **C++ Compiler Architecture Integration** - Enhanced C++ backend support  
3. **C Compiler Architecture Integration** - Improved C code generation
4. **Full Windows Support (v1.0.4)** - Fix POSIX compatibility issues

Want to work on these? See detailed guides below!

## 🦀 Rust Compiler Architecture Integration

### Overview
Integrate Rust as a compilation target for SUB Language, allowing `.sb` files to compile to native Rust code with performance and safety benefits.

### Step-by-Step Integration:

#### Step 1: Create Rust Backend Module
```bash
# Create new file: src/codegen/rust_backend.c
touch src/codegen/rust_backend.c
touch src/codegen/rust_backend.h
```

#### Step 2: Implement Rust Code Generator
Create functions to translate SUB AST to Rust code:

```c
// In rust_backend.c
void generate_rust_code(ASTNode* node, FILE* output) {
    // Translate SUB syntax to Rust
    // Handle:
    // - #variable declarations -> let mut / let
    // - #function definitions -> fn
    // - #loop / #if -> loop / if statements
    // - Type inference and ownership rules
}
```

#### Step 3: Add Rust Target to Compiler
Modify `sub_multilang.c` to recognize "rust" target:

```c
if (strcasecmp(target, "rust") == 0) {
    compile_to_rust(ast, output_file);
}
```

#### Step 4: Handle SUB to Rust Syntax Mapping
| SUB Syntax | Rust Equivalent |
|------------|----------------|
| `#variable x = 10;` | `let mut x = 10;` |
| `#function add(a, b)` | `fn add(a: i32, b: i32) -> i32` |
| `#loop(i, 0, 10)` | `for i in 0..10` |
| `#if (condition)` | `if condition` |

#### Step 5: Test Rust Compilation
```bash
./sub example.sb rust
rustc output.rs -o program
./program
```

#### Step 6: Documentation
- Add Rust examples to `examples/` directory
- Update README.md with Rust compilation instructions
- Document Rust-specific features and limitations

## ⚙️ C++ Compiler Architecture Integration

### Overview
Enhance C++ backend support with modern C++ features (C++11/14/17/20) for better performance and object-oriented programming support.

### Step-by-Step Integration:

#### Step 1: Create Enhanced C++ Backend
```bash
# Create/update: src/codegen/cpp_backend.c
touch src/codegen/cpp_backend.h
```

#### Step 2: Implement Modern C++ Features

```c
// In cpp_backend.c
void generate_cpp_code(ASTNode* node, FILE* output) {
    // Support:
    // - Smart pointers (unique_ptr, shared_ptr)
    // - Lambda functions
    // - Range-based for loops
    // - Auto type inference
    // - STL containers
}
```

#### Step 3: SUB to C++ Syntax Mapping
| SUB Syntax | C++ Equivalent |
|------------|----------------|
| `#variable x = 10;` | `auto x = 10;` |
| `#function add(a, b)` | `auto add(int a, int b) -> int` |
| `#array nums = [1,2,3];` | `std::vector<int> nums = {1,2,3};` |
| `#loop(i, 0, 10)` | `for(int i = 0; i < 10; i++)` |

#### Step 4: Add C++ Standard Support
```c
// Allow users to specify C++ version
if (strcasecmp(target, "cpp17") == 0) {
    compile_to_cpp(ast, output_file, CPP_17);
} else if (strcasecmp(target, "cpp20") == 0) {
    compile_to_cpp(ast, output_file, CPP_20);
}
```

#### Step 5: Test C++ Compilation
```bash
./sub example.sb cpp
g++ -std=c++17 output.cpp -o program
./program
```

#### Step 6: Add OOP Support
- Implement class/struct generation from SUB
- Support inheritance and polymorphism
- Add namespace management

## 🔧 C Compiler Architecture Integration

### Overview
Improve C code generation with optimized output, better memory management, and support for different C standards (C99, C11, C17).

### Step-by-Step Integration:

#### Step 1: Enhance Existing C Backend
```bash
# Update: src/codegen/c_backend.c
# Create: src/codegen/c_optimizer.c
```

#### Step 2: Implement C Standard Support

```c
// In c_backend.c
void generate_c_code(ASTNode* node, FILE* output, CStandard standard) {
    // Generate code for:
    // - C99: Variable-length arrays, inline functions
    // - C11: Anonymous structs, static assertions
    // - C17: Bug fixes and clarifications
}
```

#### Step 3: Optimize Generated C Code

```c
void optimize_c_output(ASTNode* node) {
    // Apply optimizations:
    // - Constant folding
    // - Dead code elimination
    // - Inline small functions
    // - Reduce memory allocations
}
```

#### Step 4: SUB to C Syntax Mapping
| SUB Syntax | C Equivalent |
|------------|----------------|
| `#variable x = 10;` | `int x = 10;` |
| `#function add(a, b)` | `int add(int a, int b)` |
| `#array nums = [1,2,3];` | `int nums[] = {1,2,3};` |
| `#loop(i, 0, 10)` | `for(int i = 0; i < 10; i++)` |

#### Step 5: Add Multiple C Standard Targets
```c
// Support different C versions
if (strcasecmp(target, "c99") == 0) {
    compile_to_c(ast, output_file, C99);
} else if (strcasecmp(target, "c11") == 0) {
    compile_to_c(ast, output_file, C11);
}
```

#### Step 6: Memory Management Improvements
- Add automatic cleanup generation
- Implement buffer overflow protection
- Generate valgrind-clean code
- Add memory leak detection helpers

#### Step 7: Test C Compilation
```bash
./sub example.sb c99
gcc -std=c99 output.c -o program
./program

# Test with different standards
./sub example.sb c11
gcc -std=c11 output.c -o program
```

## 🔄 Common Integration Tasks (All Architectures)

### Required Files to Modify:
1. `src/codegen/[language]_backend.c` - Core code generation
2. `src/codegen/[language]_backend.h` - Header file
3. `sub_multilang.c` - Add target recognition
4. `Makefile` - Add compilation rules
5. `README.md` - Update documentation
6. `examples/` - Add example programs

### Testing Checklist:
- [ ] Basic syntax compilation (variables, functions)
- [ ] Control flow (if, loops, switch)
- [ ] Data structures (arrays, structs)
- [ ] Memory management (no leaks)
- [ ] Cross-platform compatibility
- [ ] Performance benchmarks
- [ ] Error handling
- [ ] Edge cases

### Integration Best Practices:
1. **Start Small** - Begin with basic syntax translation
2. **Test Incrementally** - Test each feature as you add it
3. **Follow Patterns** - Look at existing backends (web, android, ios)
4. **Document Everything** - Add comments and examples
5. **Ask for Help** - Open issues or discussions if stuck

## 🐛 Reporting Bugs

- Use GitHub Issues to report bugs
- Include:
  - SUB version (e.g., v1.0.3)
  - Operating System (Windows/macOS/Linux)
  - Steps to reproduce
  - Sample .sb code demonstrating the issue

## 💡 Suggesting Features

- Open a GitHub Issue with the "enhancement" label
- Describe the feature and its use case
- Explain how it improves SUB Language

## 📝 Coding Standards

### C Code Style
- **Indentation:** 4 spaces (no tabs)
- **Bracing:** K&R style
- **Comments:** Add for complex logic
- **Function Length:** Keep under 50 lines when possible

### Naming Conventions
- **Functions:** `snake_case` (e.g., `compile_to_rust`)
- **Structs:** `PascalCase` (e.g., `ASTNode`)
- **Constants:** `UPPER_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)
- **Variables:** `snake_case` (e.g., `token_count`)

### Cross-Platform Considerations
- Always test on multiple platforms when possible
- Use `#ifdef` for platform-specific code
- Avoid POSIX-only functions (see Windows compatibility above)
- Document platform limitations

### Documentation
- Update README.md for user-facing changes
- Add inline comments for complex algorithms
- Update language specification for syntax changes
- Add examples for new features

## 🏭 Development Setup

### Prerequisites
- **Compiler:** GCC or Clang
- **Build System:** Make
- **Version Control:** Git
- **OS:** Linux/macOS (Windows support coming in v1.0.4)
- **Optional:** Rust, C++ compiler for testing backends

### Building from Source
```bash
make clean
make
```

### Running Examples
```bash
# Compile to web
./sub example.sb web

# Compile to Android
./sub example.sb android

# Compile to iOS
./sub example.sb ios

# Future targets:
./sub example.sb rust
./sub example.sb cpp
./sub example.sb c99
```

## 🎨 Language Design Principles

When contributing to SUB, keep these principles in mind:

1. **Simplicity First** - Code should be intuitive and easy to read
2. **Blockchain Method** - Use `#` syntax for visual clarity
3. **Cross-Platform** - Support all target platforms equally
4. **Performance** - Optimize for speed without sacrificing readability
5. **Compatibility** - Maintain backward compatibility when possible
6. **Accessibility** - Make it the "world's easiest" language to use

## ❓ Questions or Help?

Feel free to:
- Open a GitHub Issue for questions
- Start a GitHub Discussion
- Contact the maintainers
- Ask about specific compiler architecture integration

---

**Thank you for contributing to SUB Language! Together we're building the world's easiest cross-platform programming language.** 🚀
