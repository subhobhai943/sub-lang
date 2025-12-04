# Troubleshooting Guide

Quick solutions for common build issues with the SUB compiler.

## CMake Configuration Issues

### Error: "CMake 3.20 or higher is required"

**Solution:**
```bash
# Ubuntu/Debian
sudo apt remove cmake
wget https://github.com/Kitware/CMake/releases/download/v3.27.0/cmake-3.27.0-Linux-x86_64.sh
sudo sh cmake-3.27.0-Linux-x86_64.sh --prefix=/usr/local --skip-license

# macOS
brew upgrade cmake

# Windows
# Download from https://cmake.org/download/
```

### Error: "Could not find Rust compiler"

**Solution:**
```bash
# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source $HOME/.cargo/env

# Verify
rustc --version
cargo --version
```

### Error: "march=native is not supported"

**Fixed in latest version**. If you still see this:
```bash
git pull origin fix-compiler-build
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

## Rust Build Issues

### Error: "cargo: command not found"

**Solution:**
```bash
# Add cargo to PATH
echo 'export PATH="$HOME/.cargo/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Or for current session
export PATH="$HOME/.cargo/bin:$PATH"
```

### Error: "linker 'cc' not found"

**Solution:**
```bash
# Ubuntu/Debian
sudo apt install build-essential

# macOS
xcode-select --install

# Windows
# Install Visual Studio with C++ tools
```

### Error: "could not compile `sub_frontend`"

**Solution:**
```bash
# Clean and rebuild
cd compiler/frontend
cargo clean
cargo build --release
cd ../..
cmake --build build --config Release
```

## C++ Compilation Issues

### Error: "C++17 support required"

**Solution:**
```bash
# Linux - ensure GCC 7+ or Clang 6+
gcc --version  # Should be 7.0 or higher

# Update if needed
sudo apt install g++-9
export CXX=g++-9

# macOS - update Xcode
xcode-select --install

# Windows - use Visual Studio 2019+
```

### Error: "frontend_ffi.h: No such file or directory"

**Fixed in latest version**. Pull the latest changes:
```bash
git pull origin fix-compiler-build
```

## Linking Issues

### Error: "undefined reference to sub_frontend_tokenize"

**Solution:**
```bash
# Ensure Rust library was built
ls compiler/frontend/target/release/libsub_frontend.a  # Linux/macOS
dir compiler\frontend\target\release\sub_frontend.lib  # Windows

# If missing, rebuild Rust
cd compiler/frontend
cargo build --release
cd ../..
```

### Error: "pthread library not found" (Linux)

**Solution:**
```bash
sudo apt install libpthread-stubs0-dev
```

### Error: "ws2_32.lib not found" (Windows)

**Solution:**
Install Visual Studio with "Desktop development with C++" workload.

## Platform-Specific Issues

### Linux: "GLIBC version too old"

**Solution:**
```bash
# Build with older C++ standard
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=14
cmake --build build
```

### macOS: "library not found for -lSystem"

**Solution:**
```bash
# Reinstall command line tools
sudo rm -rf /Library/Developer/CommandLineTools
xcode-select --install
```

### Windows: "LINK : fatal error LNK1104"

**Solution:**
```powershell
# Run from Visual Studio Developer Command Prompt
# Or add VS to PATH
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## GitHub Actions CI Issues

### CI Build Fails: "No space left on device"

**Solution:** Already optimized in CI workflow. If custom workflow:
```yaml
- name: Free Disk Space
  run: |
    sudo rm -rf /usr/share/dotnet
    sudo rm -rf /opt/ghc
    sudo rm -rf /usr/local/share/boost
```

### CI Build Fails: "Rust toolchain not found"

**Solution:** Ensure workflow includes:
```yaml
- name: Setup Rust
  uses: actions-rust-lang/setup-rust-toolchain@v1
  with:
    toolchain: stable
```

## Clean Build

When all else fails, perform a clean build:

```bash
# Remove all build artifacts
make clean
# Or manually
rm -rf build
cd compiler/frontend && cargo clean && cd ../..

# Rebuild from scratch
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Getting More Help

1. **Check logs:** CMake generates detailed logs in `build/CMakeFiles/`
2. **Verbose build:** `cmake --build build --verbose`
3. **GitHub Issues:** [Open an issue](https://github.com/subhobhai943/sub-lang/issues)
4. **Build documentation:** See [BUILD.md](BUILD.md)

## Common Questions

### Q: Why does the build take so long?

**A:** First build compiles Rust dependencies. Subsequent builds are much faster.

### Q: Can I use system libraries instead of building Rust?

**A:** No, the Rust frontend is essential for the compiler architecture.

### Q: Do I need all three languages (Rust/C++/C)?

**A:** Yes, the multi-language architecture is core to SUB's design:
- Rust: Memory-safe frontend
- C++: Optimization engine  
- C: Portable code generation

### Q: Can I build without CMake?

**A:** Not recommended. CMake handles the complex multi-language build properly.

## Quick Diagnostic

Run this to check your environment:

```bash
echo "=== SUB Compiler Build Diagnostic ==="
echo ""
echo "CMake version:"
cmake --version | head -1
echo ""
echo "Rust toolchain:"
rustc --version
cargo --version
echo ""
echo "C++ compiler:"
g++ --version 2>/dev/null | head -1 || clang++ --version 2>/dev/null | head -1
echo ""
echo "C compiler:"
gcc --version 2>/dev/null | head -1 || clang --version 2>/dev/null | head -1
echo ""
echo "=== All tools present ==="
```

All commands should show version numbers. If any fail, that tool needs installation.
