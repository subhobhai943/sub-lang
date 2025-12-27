# SUB-Lang Build Guide 🔨

## Quick Start

```bash
# Build everything (both compilers)
make

# Build specific compiler
make basic    # Platform-specific compiler (sub)
make multi    # Multi-language compiler (sublang)

# Clean build files
make clean

# Install compilers
make install
```

---

## Two Compilers Explained

SUB-Lang now has **TWO different compilers** for different use cases:

### 1. Basic Compiler (`sub`) - Platform-Specific

**Purpose:** Compile SUB code for different **platforms** (operating systems/devices)

**Usage:**
```bash
./sub program.sb [platform]
```

**Supported Platforms:**
- `linux` - Linux systems (default)
- `windows` - Windows desktop
- `macos` - macOS
- `android` - Android apps (Java)
- `ios` - iOS apps (Swift)
- `web` - Web browsers (HTML/JS)

**Output:** Platform-specific code files
- Linux/Windows/macOS → `.c` files
- Android → `.java` files  
- iOS → `.swift` files
- Web → `.html` files

**Example:**
```bash
./sub myapp.sb android
# Creates: output_android.java
javac output_android.java
```

---

### 2. Multi-Language Compiler (`sublang`) - Language-Specific

**Purpose:** Compile SUB code to different **programming languages**

**Usage:**
```bash
./sublang program.sb [language]
```

**Supported Languages:**
- `c` - C programming language
- `cpp` / `c++` - C++
- `python` / `py` - Python 3
- `java` - Java
- `swift` - Swift
- `kotlin` / `kt` - Kotlin
- `rust` / `rs` - Rust
- `javascript` / `js` - JavaScript
- `typescript` / `ts` - TypeScript
- `assembly` / `asm` - x86-64 Assembly
- `css` - CSS stylesheets
- `go` - Go (coming soon)
- `llvm` - LLVM IR (coming soon)
- `wasm` - WebAssembly (coming soon)

**Output:** Source code in target language
- Python → `output.py`
- Java → `SubProgram.java`
- Rust → `output.rs`
- JavaScript → `output.js`
- etc.

**Example:**
```bash
./sublang myapp.sb python
# Creates: output.py
python3 output.py

./sublang myapp.sb rust
# Creates: output.rs
rustc output.rs && ./output
```

---

## When to Use Which Compiler?

### Use `sub` (Basic) when:
- ✅ Building apps for specific platforms (Android, iOS, Web)
- ✅ Need platform-specific features (UI components)
- ✅ Cross-platform mobile/desktop development
- ✅ Want automatic platform setup (Java for Android, Swift for iOS)

### Use `sublang` (Multi-Language) when:
- ✅ Learning different programming languages
- ✅ Need to integrate with existing projects in specific languages
- ✅ Want maximum language choice and flexibility
- ✅ Comparing performance across languages
- ✅ Polyglot development (one codebase, many languages)

---

## Build System Reference

### Build Targets

```bash
make              # Build both compilers
make basic        # Build platform compiler (sub)
make multi        # Build language compiler (sublang)
```

### Clean Targets

```bash
make clean        # Remove all build files and generated code
```

### Install Targets

```bash
make install         # Install both compilers to /usr/local/bin
make install-basic   # Install only sub
make install-multi   # Install only sublang
```

### Test Targets

```bash
make test                 # Test both compilers
make test-basic           # Test platform compiler
make test-multi           # Test multi-language compiler
make test-all-languages   # Compile test to ALL supported languages
```

### Help

```bash
make help         # Show detailed build system help
```

---

## Manual Build Commands

If you don't want to use `make`:

### Build Basic Compiler
```bash
gcc -Wall -Wextra -O2 -std=c11 \
    -o sub \
    sub.c lexer.c parser.c semantic.c codegen.c utils.c
```

### Build Multi-Language Compiler
```bash
gcc -Wall -Wextra -O2 -std=c11 \
    -o sublang \
    sub_multilang.c lexer.c parser.c semantic.c \
    codegen.c codegen_multilang.c utils.c
```

---

## File Structure

```
sub-lang/
├── Makefile                    # Build system
│
├── Basic Compiler (Platform-Specific)
│   ├── sub.c                   # Main driver
│   ├── codegen.c               # Platform code generation
│   └── output_*.c/java/swift   # Generated platform code
│
├── Multi-Language Compiler
│   ├── sub_multilang.c         # Main driver
│   ├── codegen_multilang.c     # Multi-language code generation
│   └── output.*                # Generated language code
│
├── Shared Components
│   ├── lexer.c                 # Tokenization
│   ├── parser.c                # AST construction
│   ├── semantic.c              # Type checking
│   ├── utils.c                 # Utilities
│   ├── sub_compiler.h          # Main header
│   └── error_handler.h         # Error handling
│
└── Documentation
    ├── README.md               # Main readme
    ├── FIXES_APPLIED.md        # What was fixed
    ├── MULTILANG_GUIDE.md      # Multi-language details
    └── BUILD_GUIDE.md          # This file
```

---

## Complete Workflow Examples

### Example 1: Build and Test Everything

```bash
# Clean previous builds
make clean

# Build both compilers
make

# Test platform compiler
./sub test_real.sb linux
gcc output_linux.c -o test_platform
./test_platform

# Test multi-language compiler
./sublang test_real.sb python
python3 output.py

./sublang test_real.sb rust
rustc output.rs && ./output
```

### Example 2: Android App Development

```bash
# Build platform compiler
make basic

# Compile for Android
./sub myapp.sb android

# Compile generated Java
javac output_android.java

# Run on Android device/emulator
adb push output_android.class /sdcard/
```

### Example 3: Polyglot Development

```bash
# Build multi-language compiler
make multi

# Generate same program in multiple languages
./sublang myapp.sb python
./sublang myapp.sb java
./sublang myapp.sb rust
./sublang myapp.sb javascript

# Now you have:
# - output.py
# - SubProgram.java
# - output.rs
# - output.js
```

### Example 4: Full Test Suite

```bash
# Build everything
make

# Run all tests
make test

# Test all language targets
make test-all-languages

# You'll see compilation to:
# C, C++, Python, Java, Swift, Kotlin, Rust, 
# JavaScript, CSS, Assembly
```

---

## Optimization Flags

The Makefile uses `-O2` by default. You can customize:

### Debug Build
```bash
make CFLAGS="-Wall -Wextra -g -O0"
```

### Maximum Optimization
```bash
make CFLAGS="-Wall -Wextra -O3 -march=native"
```

### Size Optimization
```bash
make CFLAGS="-Wall -Wextra -Os"
```

---

## Troubleshooting

### "Command not found" after `make`

Make sure you're running from the project directory:
```bash
./sub --help
./sublang --help
```

Or install globally:
```bash
sudo make install
sub --help
sublang --help
```

### Compilation errors

Check you have GCC installed:
```bash
gcc --version
```

If not:
```bash
# Ubuntu/Debian
sudo apt install build-essential

# macOS
xcode-select --install

# Fedora/RHEL
sudo dnf install gcc
```

### Generated code won't compile

Make sure target language compiler is installed:
```bash
python3 --version
javac --version
rustc --version
node --version
```

---

## Advanced: Using Both Compilers Together

You can use both compilers in the same project:

```bash
# 1. Develop in SUB
vim myapp.sb

# 2. Test quickly with Python (fast iteration)
./sublang myapp.sb python
python3 output.py

# 3. Once working, compile to production languages
./sublang myapp.sb rust        # Backend API
./sublang myapp.sb javascript   # Frontend web
./sub myapp.sb android          # Mobile app

# 4. Now you have the same logic in 3 different targets!
```

---

## Summary

| Compiler | Binary | Purpose | Output |
|----------|--------|---------|--------|
| Basic | `sub` | Platform-specific apps | `.c`, `.java`, `.swift`, `.html` |
| Multi-Language | `sublang` | Polyglot compilation | `.py`, `.rs`, `.js`, `.cpp`, etc. |

**Build command:** `make` (builds both)

**One codebase, unlimited possibilities!** 🚀
