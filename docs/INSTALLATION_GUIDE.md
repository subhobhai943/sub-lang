# SUB Language — Installation Guide

This guide covers how to **download and use pre-built binaries** from the latest release. If you want to build from source instead, see the [Build Guide](BUILD_GUIDE.md).

> **Current Release:** [v1.0.7-beta](https://github.com/subhobhai943/sub-lang/releases/tag/v1.0.7-beta)

---

## 📦 What's in a Release?

Each release ships **6 binary files** for 3 platforms:

| File | Platform | Compiler Type |
|------|----------|---------------|
| `sub-linux-x86_64` | Linux (x86-64) | Transpiler (`sub`) |
| `sub-linux-x86_64-native` | Linux (x86-64) | Native compiler (`subc`) |
| `sub-macos-arm64` | macOS Apple Silicon (M1/M2/M3) | Transpiler (`sub`) |
| `sub-macos-arm64-native` | macOS Apple Silicon | Native compiler (`subc`) |
| `sub-windows-x86_64.exe` | Windows (x86-64) | Transpiler (`sub`) |
| `sub-windows-x86_64-native.exe` | Windows (x86-64) | Native compiler (`subc`) |

**Transpiler (`sub`)** — converts `.sb` files to Python, JavaScript, Go, Rust, and 8 other languages.  
**Native compiler (`subc`)** — compiles `.sb` files directly to a standalone executable binary.

---

## 🐧 Linux (x86-64)

### Step 1 — Download

```bash
# Transpiler
wget https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-linux-x86_64 -O sub

# Native compiler
wget https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-linux-x86_64-native -O subc
```

Or use `curl`:

```bash
curl -L https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-linux-x86_64 -o sub
curl -L https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-linux-x86_64-native -o subc
```

### Step 2 — Make Executable

```bash
chmod +x sub subc
```

### Step 3 — (Optional) Install System-Wide

```bash
sudo mv sub subc /usr/local/bin/
```

After this you can run `sub` and `subc` from any directory.

### Step 4 — Verify

```bash
./sub --version
./subc --version
```

### Step 5 — Run Your First Program

Create a file `hello.sb`:

```sub
#var name = "World"
#print("Hello, " + name)
```

**Compile to native binary:**

```bash
./subc hello.sb hello
./hello
# Output: Hello, World
```

**Transpile to Python:**

```bash
./sub hello.sb python
# Outputs: hello.py
python3 hello.py
```

---

## 🍎 macOS (Apple Silicon — M1/M2/M3)

### Step 1 — Download

```bash
# Transpiler
curl -L https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-macos-arm64 -o sub

# Native compiler
curl -L https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-macos-arm64-native -o subc
```

### Step 2 — Make Executable

```bash
chmod +x sub subc
```

### Step 3 — Remove macOS Quarantine (Gatekeeper)

Because these binaries are downloaded from the internet and not notarized by Apple, macOS will block them the first time. Remove the quarantine flag:

```bash
xattr -d com.apple.quarantine sub subc
```

> **Alternative:** Right-click each binary in Finder → Open → Open (to approve it once).

### Step 4 — (Optional) Install System-Wide

```bash
sudo mv sub subc /usr/local/bin/
```

### Step 5 — Verify

```bash
./sub --version
./subc --version
```

### Step 6 — Run Your First Program

Create `hello.sb`:

```sub
#var name = "World"
#print("Hello, " + name)
```

```bash
# Native binary
./subc hello.sb hello
./hello

# Transpile to Go
./sub hello.sb go
cat hello.go
```

> **Intel Mac (x86-64)?** The macOS Intel build is not included in v1.0.7-beta. Please [build from source](BUILD_GUIDE.md) using `make all`, or use [Rosetta 2](https://support.apple.com/en-us/HT211861) to run the ARM64 binary on Intel Macs.

---

## 🪟 Windows (x86-64)

### Step 1 — Download

1. Go to the [v1.0.7-beta release page](https://github.com/subhobhai943/sub-lang/releases/tag/v1.0.7-beta)
2. Download:
   - `sub-windows-x86_64.exe` → Transpiler
   - `sub-windows-x86_64-native.exe` → Native compiler
3. Rename them to `sub.exe` and `subc.exe` for convenience

Or use **PowerShell**:

```powershell
# Transpiler
Invoke-WebRequest -Uri "https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-windows-x86_64.exe" -OutFile "sub.exe"

# Native compiler
Invoke-WebRequest -Uri "https://github.com/subhobhai943/sub-lang/releases/download/v1.0.7-beta/sub-windows-x86_64-native.exe" -OutFile "subc.exe"
```

### Step 2 — (Optional) Add to PATH

To use `sub` and `subc` from any folder:

1. Move both `.exe` files to a folder, e.g. `C:\SUBLang\`
2. Open **Start Menu** → search **Environment Variables**
3. Click **Edit the system environment variables** → **Environment Variables**
4. Under **System Variables**, select **Path** → **Edit** → **New**
5. Add: `C:\SUBLang\`
6. Click **OK** and restart your terminal

### Step 3 — Verify

Open **Command Prompt** or **PowerShell**:

```cmd
sub.exe --version
subc.exe --version
```

### Step 4 — Run Your First Program

Create `hello.sb`:

```sub
#var name = "World"
#print("Hello, " + name)
```

```cmd
:: Native binary
subc.exe hello.sb hello
hello.exe

:: Transpile to JavaScript
sub.exe hello.sb javascript
node hello.js
```

> **Note:** Windows Defender may flag unknown executables. Click **More info** → **Run anyway** if prompted. You can also whitelist the binaries in your antivirus settings.

---

## 🔧 Using the Compilers

### Native Compiler (`subc`)

Compiles `.sb` source directly to a standalone executable.

```bash
# Syntax
subc <source.sb> <output-name>

# Example
subc myapp.sb myapp
./myapp          # Linux/macOS
myapp.exe        # Windows
```

The output binary has **zero runtime dependencies** — distribute it anywhere.

### Transpiler (`sub`)

Converts `.sb` source to code in a target language.

```bash
# Syntax
sub <source.sb> <target-language>

# Examples
sub myapp.sb python       # → myapp.py
sub myapp.sb javascript   # → myapp.js
sub myapp.sb typescript   # → myapp.ts
sub myapp.sb java         # → myapp.java
sub myapp.sb rust         # → myapp.rs
sub myapp.sb go           # → myapp.go
sub myapp.sb cpp          # → myapp.cpp
sub myapp.sb c            # → myapp.c
sub myapp.sb swift        # → myapp.swift
sub myapp.sb kotlin       # → myapp.kt
sub myapp.sb ruby         # → myapp.rb
```

---

## 🧪 Quick Test

Copy and run this test program to confirm your installation works:

**fibonacci.sb**

```sub
#var a = 0
#var b = 1
#var n = 10

#print("Fibonacci sequence:")

#for i in range(n)
    #print(a)
    #var temp = a + b
    a = b
    b = temp
#end
```

```bash
# Native
./subc fibonacci.sb fib
./fib

# Expected output:
# Fibonacci sequence:
# 0
# 1
# 1
# 2
# 3
# 5
# 8
# 13
# 21
# 34
```

---

## 🆚 VS Code Syntax Highlighting

For a better editing experience, install the official VS Code extension:

1. Open VS Code
2. Go to **Extensions** (`Ctrl+Shift+X`)
3. Search for **SUB Language**
4. Install [vscode-sub-language](https://github.com/subhobhai943/vscode-sub-language)

This adds syntax highlighting and snippets for `.sb` files.

---

## ❓ Troubleshooting

| Problem | Solution |
|---------|----------|
| `Permission denied` on Linux/macOS | Run `chmod +x sub subc` |
| macOS: *"cannot be opened because it is from an unidentified developer"* | Run `xattr -d com.apple.quarantine sub subc` |
| Windows Defender blocks the `.exe` | Click **More info → Run anyway**, or add to antivirus exclusions |
| `command not found` | Move binaries to `/usr/local/bin/` (Linux/macOS) or add folder to PATH (Windows) |
| Build errors when compiling from source | See the [Build Guide](BUILD_GUIDE.md) for dependencies |
| macOS Intel (x86-64) not supported | [Build from source](BUILD_GUIDE.md) on your machine |

---

## 📥 All Releases

View all past and current releases on the [Releases page](https://github.com/subhobhai943/sub-lang/releases).

---

## 📚 Next Steps

- [Language Specification](LANGUAGE_SPEC.md) — Learn the full SUB syntax
- [Build Guide](BUILD_GUIDE.md) — Compile from source
- [Contributing](CONTRIBUTING.md) — Help improve SUB
- [Release Notes](RELEASE_NOTES.md) — What changed in each version
