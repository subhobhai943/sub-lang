# Building SUB Language Compiler

This guide provides detailed instructions for building the SUB compiler on Linux, macOS, and Windows.

## Architecture Overview

The SUB compiler uses a multi-language architecture:

- **Frontend**: Rust (memory-safe lexer/parser)
- **Middle-end**: C++ (semantic analysis and optimizations)
- **Backend**: C (portable code generation)

## Prerequisites

### All Platforms

1. **CMake** 3.20 or higher
2. **Rust** stable toolchain (rustc + cargo)
3. **C++17** compatible compiler
4. **C11** compatible compiler

### Platform-Specific Requirements

#### Linux
- GCC 7+ or Clang 6+
- Make (optional, for convenience)
```bash
sudo apt update
sudo apt install build-essential cmake
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

#### macOS
- Xcode Command Line Tools
- Homebrew (recommended)
```bash
xcode-select --install
brew install cmake
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

#### Windows
- Visual Studio 2019+ (with C++ desktop development)
- CMake for Windows
- Rust for Windows
```powershell
# Install Rust from https://rustup.rs/
# Install CMake from https://cmake.org/download/
# Install Visual Studio from https://visualstudio.microsoft.com/
```

## Building

### Using Make (Linux/macOS)

```bash
# Clone the repository
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang

# Build the compiler
make

# The compiled binary will be at: build/subc
```

### Using CMake Directly (All Platforms)

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

### Windows-Specific Build

```powershell
# Using Visual Studio Developer Command Prompt
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# The compiled binary will be at: build\Release\subc.exe
```

## Testing the Build

### Linux/macOS
```bash
./build/subc example.sb web
# Should generate: output_web.js

./build/subc example.sb android
# Should generate: output_android.java

./build/subc example.sb native
# Should generate: output_native.c
```

### Windows
```powershell
.\build\Release\subc.exe example.sb web
# Should generate: output_web.js
```

## Installation

### Linux/macOS
```bash
sudo make install
# Installs to /usr/local/bin/subc
```

### Windows
```powershell
# Run as Administrator
make install
# Installs to C:\Program Files\SUB\subc.exe
```

## Build Targets

The Makefile provides several convenient targets:

- `make all` (default) - Build the compiler
- `make build` - Explicitly build with CMake
- `make clean` - Remove all build artifacts
- `make install` - Install to system location
- `make test` - Build and run test compilation
- `make help` - Show available targets

## Troubleshooting

### Rust Frontend Build Fails

**Problem**: `cargo build --release` fails in frontend directory

**Solution**:
```bash
cd compiler/frontend
cargo clean
cargo build --release
```

### CMake Can't Find Rust Library

**Problem**: `libsub_frontend.a` or `sub_frontend.lib` not found

**Solution**: Ensure Rust frontend builds first
```bash
cd compiler/frontend
cargo build --release
ls target/release/  # Check for library files
```

### C++ Compilation Errors

**Problem**: Semantic analyzer fails to compile

**Solution**: Ensure C++17 support
```bash
# Linux/macOS
export CXXFLAGS="-std=c++17"
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Windows
cmake -B build -DCMAKE_CXX_STANDARD=17
```

### Windows: "Cannot open include file"

**Problem**: Missing Visual Studio C++ components

**Solution**: Install "Desktop development with C++" workload in Visual Studio Installer

### macOS: Linker Errors with pthread

**Problem**: Undefined symbols for pthread functions

**Solution**: Already handled in CMakeLists.txt, but if issues persist:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXE_LINKER_FLAGS="-pthread"
```

## Build Configuration

### Debug Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Custom Install Location
```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/custom/path
cmake --build build
cmake --install build
```

### Disable Optimizations (for debugging)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-O0 -g" -DCMAKE_CXX_FLAGS="-O0 -g"
```

## Continuous Integration

The project uses GitHub Actions for CI/CD. The workflow automatically:

1. Sets up Rust toolchain
2. Configures CMake
3. Builds on Linux, macOS, and Windows
4. Runs test compilations
5. Archives build artifacts

See `.github/workflows/ci.yml` for details.

## Clean Build

To perform a completely clean build:

```bash
make clean
make all
```

Or manually:

```bash
rm -rf build
cd compiler/frontend && cargo clean && cd ../..
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Cross-Compilation

For advanced users wanting to cross-compile:

```bash
# Example: Build for ARM64 on x86_64 Linux
rustup target add aarch64-unknown-linux-gnu
cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm64-toolchain.cmake
cmake --build build
```

## Getting Help

If you encounter build issues:

1. Check this troubleshooting guide
2. Review the [GitHub Issues](https://github.com/subhobhai943/sub-lang/issues)
3. Run `make help` for available build targets
4. Ensure all prerequisites are installed correctly

## Development Build Workflow

For active development:

```bash
# Initial setup
make build

# After code changes
cmake --build build --config Release

# Full rebuild if needed
make clean && make build
```
