# SUB Programming Language

**The World's Easiest Programming Language with Blockchain-Inspired Syntax**

SUB is a modern programming language that uses intuitive `#` (hash) symbols to create readable, blockchain-style code. Currently, SUB works as a powerful **source-to-source compiler (transpiler)** that converts your SUB code to multiple target languages.

---

## 🎯 Current Status: Multi-Language Transpiler (v1.0.4)

### What SUB Does Now

SUB is a **transpiler** that converts SUB source code to:
- Python, JavaScript, TypeScript
- Java, Swift, Kotlin
- C, C++, Rust
- Ruby, Go
- Assembly, CSS

```
┌─────────────────┐
│  SUB Code (.sb) │
└────────┬────────┘
         │
         ↓
┌────────┴────────────────────────┐
│  SUB Compiler (Pure C)          │
│  • Lexer → Tokens               │
│  • Parser → AST                 │
│  • Semantic Analysis            │
│  • Multi-Language Code Gen      │
└────────┬────────────────────────┘
         │
         ↓
┌────────┴────────┐
│  Target Language │  Python, Java, Ruby,
│  Source Code     │  JavaScript, Rust, etc.
└─────────────────┘
```

### ⚠️ Current Limitation

SUB currently **transpiles** to other languages - meaning:
- ✅ You can write SUB and get Python/Java/Ruby code
- ✅ Cross-platform through target language runtimes
- ❌ No direct machine code compilation (yet)
- ❌ Requires target language runtime (Python interpreter, JVM, etc.)

**Why this matters**: Users might ask "Why use SUB if I need Python anyway?"

### 🚀 Future: Native Compilation (Planned)

We're building **native code generation** so SUB will:
- Generate standalone binaries (ELF, PE, Mach-O)
- Run without any interpreter or runtime
- Compete with C/Rust for performance
- Still support transpilation as an option

---

## ✨ Key Features

🔗 **Blockchain-Inspired Syntax**
- Uses `#` symbols for method chaining
- Clean, readable code structure
- Natural flow from top to bottom

⚡ **Multi-Language Output**
- Compile to 10+ languages from one source
- Choose the best target for your platform
- Leverage existing ecosystems

🌍 **Cross-Platform**
- Write once, compile to any language
- Android, iOS, Web, Desktop
- Platform-specific optimizations

🛡️ **Windows + Linux + macOS**
- Full MSVC/Visual Studio support
- GCC/Clang compatible
- One codebase, all platforms

---

## 💻 Quick Start

### Installation

#### Build from Source

**Windows (MSVC):**
```batch
REM Open Visual Studio Developer Command Prompt
cl /I. sub_multilang.c parser.c lexer.c semantic.c codegen.c codegen_multilang.c /Fe:subc.exe
```

**Linux/macOS:**
```bash
gcc -o subc sub_multilang.c parser.c lexer.c semantic.c codegen.c codegen_multilang.c
```

**Using Makefile:**
```bash
make
./sublang example.sb python
```

### Your First Program

```sub
#var name = "World"
#var count = 10

#function greet()
    #print("Hello, " + name)
#end

#for i in range(count)
    #print(i)
#end
```

### Compile to Any Language

```bash
# Compile to Python
./subc program.sb python
python3 output.py

# Compile to JavaScript
./subc program.sb javascript
node output.js

# Compile to Ruby
./subc program.sb ruby
ruby output.rb

# Compile to Java
./subc program.sb java
javac SubProgram.java && java SubProgram

# Compile to Rust
./subc program.sb rust
rustc output.rs && ./output
```

---

## 📝 Syntax Guide

### Variables
```sub
#var name = "John"        // String
#var age = 25             // Integer
#var price = 19.99        // Float
#var isActive = true      // Boolean
```

### Functions
```sub
#function greet(name)
    #return "Hello, " + name
#end

#var message = greet("Alice")
#print(message)
```

### Control Flow
```sub
#if age > 18
    #print("Adult")
#elif age > 13
    #print("Teen")
#else
    #print("Child")
#end
```

### Loops
```sub
#for i in range(10)
    #print(i)
#end

#while count > 0
    #print(count)
    count = count - 1
#end
```

### Embedded Code (Language Interop)
```sub
#embed python
import math
result = math.sqrt(16)
print(f"Square root: {result}")
#endembed

#embed javascript
console.log("Direct JavaScript!");
const data = [1, 2, 3].map(x => x * 2);
#endembed
```

---

## 🎯 Supported Target Languages

| Language | Status | Extension | Command |
|----------|--------|-----------|----------|
| **Python** | ✅ Ready | `.py` | `subc file.sb python` |
| **JavaScript** | ✅ Ready | `.js` | `subc file.sb javascript` |
| **TypeScript** | ✅ Ready | `.ts` | `subc file.sb typescript` |
| **Java** | ✅ Ready | `.java` | `subc file.sb java` |
| **Ruby** | ✅ Ready | `.rb` | `subc file.sb ruby` |
| **C** | ✅ Ready | `.c` | `subc file.sb c` |
| **C++** | ✅ Ready | `.cpp` | `subc file.sb cpp` |
| **Swift** | ✅ Ready | `.swift` | `subc file.sb swift` |
| **Kotlin** | ✅ Ready | `.kt` | `subc file.sb kotlin` |
| **Rust** | ✅ Ready | `.rs` | `subc file.sb rust` |
| **Assembly** | ✅ Ready | `.asm` | `subc file.sb assembly` |
| **CSS** | ✅ Ready | `.css` | `subc file.sb css` |
| **Go** | 🚧 Coming | `.go` | `subc file.sb go` |
| **Native** | 🚧 Planned | `.exe/.elf` | `subc file.sb --native` |

---

## 🏗️ Architecture

### Current Implementation (Pure C)

```
Source Files (C):
├── sub_multilang.c      # Main compiler driver
├── lexer.c              # Tokenization
├── parser.c             # AST construction
├── parser_enhanced.c    # Advanced parsing
├── semantic.c           # Type checking & validation
├── codegen.c            # C code generation
├── codegen_multilang.c  # Multi-language generators
├── sub_compiler.h       # Shared definitions
└── windows_compat.h     # Windows/MSVC compatibility

Build Output:
├── sublang (Linux/macOS)
└── subc.exe (Windows)
```

### Compilation Pipeline

```
1. Lexer:     Source → Tokens
2. Parser:    Tokens → AST (Abstract Syntax Tree)
3. Semantic:  AST → Validated AST
4. CodeGen:   AST → Target Language Code
```

---

## 🛠️ Compiler Options

```bash
subc <input.sb> <target_language> [options]

Target Languages:
  python, javascript/js, typescript/ts
  java, kotlin, swift
  c, cpp/c++, rust
  ruby, go, assembly

Examples:
  subc program.sb python      # Generate Python
  subc program.sb java        # Generate Java
  subc program.sb rust        # Generate Rust
```

---

## 📊 Project Status

### ✅ Completed (v1.0.4)
- [x] Full C compiler implementation
- [x] Lexer with comprehensive token support
- [x] Parser with AST generation
- [x] Semantic analysis
- [x] Multi-language code generation (10+ languages)
- [x] Windows/MSVC compatibility
- [x] Linux/macOS support
- [x] Embedded code support
- [x] Error handling and reporting

### 🚧 In Progress
- [ ] Native machine code generation
- [ ] LLVM IR backend
- [ ] Go language target
- [ ] Optimization passes
- [ ] Standard library
- [ ] Package manager

### 📝 Planned Features
- [ ] Direct binary compilation (no transpiler)
- [ ] Garbage collection
- [ ] Async/await support
- [ ] Generic types
- [ ] Module system
- [ ] Debugger integration

---

## 📚 Documentation

- [Language Specification](LANGUAGE_SPEC.md)
- [Multi-Language Guide](MULTILANG_GUIDE.md)
- [Build Guide](BUILD_GUIDE.md)
- [Contributing Guidelines](CONTRIBUTING.md)
- [Native Compilation Plan](NATIVE_COMPILATION.md)

---

## 🤝 Contributing

Contributions are welcome! See [CONTRIBUTING.md](CONTRIBUTING.md).

### Development

```bash
# Clone repository
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang

# Build
make clean && make

# Test
./sublang test_ruby.sb ruby
ruby output.rb
```

---

## 🎯 Roadmap

### Phase 1: Transpiler (Current) ✅
- Multi-language source code generation
- Cross-platform compilation
- Language interoperability

### Phase 2: Native Compilation (Next) 🚧
- Direct machine code generation
- Standalone binary output
- No runtime dependencies
- Competitive with C/Rust performance

### Phase 3: Ecosystem
- Standard library
- Package manager
- IDE integrations
- Community tools

---

## ⚖️ License

MIT License - See [LICENSE](LICENSE)

---

## 📧 Contact

- **GitHub**: https://github.com/subhobhai943/sub-lang
- **Issues**: https://github.com/subhobhai943/sub-lang/issues

---

## ⭐ Why SUB?

### Current Value (Transpiler)
1. **Unified Syntax** - Learn one language, output many
2. **Easy to Learn** - Blockchain `#` syntax is intuitive
3. **Polyglot** - Embed any language directly
4. **Cross-Platform** - Leverage existing runtimes

### Future Value (Native Compiler)
1. **True Independence** - No runtime dependencies
2. **High Performance** - Native machine code
3. **Small Binaries** - Standalone executables
4. **System Programming** - Compete with C/Rust

---

## 🎬 Current State vs. Future Goal

| Aspect | Now (Transpiler) | Future (Native) |
|--------|------------------|------------------|
| **Output** | Python/Java/etc code | Machine code binary |
| **Runtime** | Requires interpreter | Standalone |
| **Speed** | Interpreter speed | Native C/Rust speed |
| **Size** | Script + runtime | Small binary |
| **Deployment** | Complex | Single file |
| **Value Prop** | Unified syntax | Real compiler |

---

Built with ❤️ by the SUB community

**Powered by Pure C** 🔧
