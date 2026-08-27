# SUB Language — Installation Guide

This guide covers how to **download and use pre-built binaries** from the latest release. If you want to build from source instead, see the [Build Guide](BUILD_GUIDE.md).

> **Downloads:** [latest release](https://github.com/subhobhai943/sub-lang/releases/latest)
>
> Every command below points at `releases/latest`, so it keeps working
> across releases rather than pinning a version that will go stale.

---

## 📦 What's in a Release?

Every download contains the **whole toolchain** — `sub`, `subc` and `subi`
— rather than a binary per tool:

| File | Platform | Format |
|------|----------|--------|
| `sub-linux-x86_64.tar.gz` | Linux (x86-64) | archive |
| `sub-linux-arm64.tar.gz` | Linux (arm64 / aarch64) | archive |
| `sub-lang_<version>_amd64.deb` | Debian, Ubuntu and derivatives | package |
| `sub-lang_<version>_arm64.deb` | Debian, Ubuntu (arm64) | package |
| `sub-macos-arm64.tar.gz` | macOS Apple Silicon (M1/M2/M3) | archive |
| `sub-lang-<version>-windows-x86_64.msi` | Windows (x86-64) | installer |
| `checksums-sha256.txt` | — | checksums for all of the above |

**`sub`** — transpiles `.sb` files to Python, JavaScript, C, C++, Rust, Go,
Java, Kotlin, Swift, Ruby and more.
**`subc`** — compiles `.sb` files to a standalone executable. On x86-64
Linux it emits machine code itself and needs no C compiler installed.
**`subi`** — runs `.sb` files directly, and gives you a REPL.

### Checking what you downloaded

```bash
sha256sum -c checksums-sha256.txt
```

---

## 🐧 Linux (x86-64 and arm64)

### Debian, Ubuntu and derivatives

```bash
ARCH=$(dpkg --print-architecture)          # amd64 or arm64
curl -sL https://api.github.com/repos/subhobhai943/sub-lang/releases/latest \
  | grep -o "https://[^\"]*_${ARCH}\.deb" | head -1 \
  | xargs curl -Lo sub-lang.deb
sudo dpkg -i sub-lang.deb
```

That puts all three tools in `/usr/local/bin`, so they are on your `PATH`
already. Remove it with `sudo dpkg -r sub-lang`.

### Any other distribution

```bash
ARCH=$(uname -m); [ "$ARCH" = "aarch64" ] && ARCH=arm64
curl -sL https://api.github.com/repos/subhobhai943/sub-lang/releases/latest \
  | grep -o "https://[^\"]*sub-linux-${ARCH}\.tar\.gz" | head -1 \
  | xargs curl -Lo sub.tar.gz
tar xzf sub.tar.gz
sudo install -m755 sub-linux-*/sub sub-linux-*/subc sub-linux-*/subi /usr/local/bin/
```

### Verify

```bash
sub --version
subc --version
subi --version
```

### Run your first program

Create `hello.sb`:

```sub
let name = "World"
println("Hello, " + name)
```

```bash
subi hello.sb              # run it
subc hello.sb -o hello     # compile it
./hello
sub hello.sb python        # or transpile it
python3 hello.py
```

On x86-64 Linux, `subc` produces a static binary with no library
dependencies and does not need a C compiler on the machine.

---

## 🍎 macOS (Apple Silicon — M1/M2/M3)

### Step 1 — Download and install

```bash
curl -sL https://api.github.com/repos/subhobhai943/sub-lang/releases/latest \
  | grep -o "https://[^\"]*sub-macos-arm64\.tar\.gz" | head -1 \
  | xargs curl -Lo sub.tar.gz
tar xzf sub.tar.gz
sudo install -m755 sub-macos-arm64/sub sub-macos-arm64/subc sub-macos-arm64/subi /usr/local/bin/
```

### Step 2 — Clear the quarantine flag

The binaries are not notarized by Apple, so Gatekeeper blocks them the first
time:

```bash
sudo xattr -d com.apple.quarantine /usr/local/bin/sub /usr/local/bin/subc /usr/local/bin/subi
```

> **Alternative:** right-click each binary in Finder → Open → Open, once.

### Step 3 — Verify

```bash
sub --version
subc --version
subi --version
```

### Step 4 — Run your first program

```sub
let name = "World"
println("Hello, " + name)
```

```bash
subi hello.sb
subc hello.sb -o hello && ./hello
sub hello.sb go
```

On macOS `subc` generates C and builds it with the host compiler, so Xcode
command line tools (`xcode-select --install`) need to be installed. `sub`
and `subi` have no such requirement.

> **Intel Mac (x86-64)?** No Intel build is published — GitHub no longer
> offers free Intel macOS runners. [Build from source](BUILD_GUIDE.md) with
> `make all`, or run the arm64 binaries under
> [Rosetta 2](https://support.apple.com/en-us/HT211861).

---

## 🪟 Windows (x86-64)

### Step 1 — Run the installer

1. Go to the [latest release](https://github.com/subhobhai943/sub-lang/releases/latest)
2. Download `sub-lang-<version>-windows-x86_64.msi`
3. Double-click it and follow the prompts

The installer puts all three tools in `C:\Program Files\SUB Language\`,
offers to add that folder to your system `PATH`, and associates `.sb` files
with the interpreter so you can double-click a program to run it. Both the
`PATH` entry and the Start Menu shortcuts are optional — clear them on the
feature page if you would rather manage those yourself.

Installing per-machine needs administrator rights, so Windows will ask.

Or from **PowerShell** (run as Administrator):

```powershell
$url = (Invoke-RestMethod https://api.github.com/repos/subhobhai943/sub-lang/releases/latest).
        assets | Where-Object { $_.name -like "*.msi" } | Select-Object -First 1 -Expand browser_download_url
Invoke-WebRequest -Uri $url -OutFile sub-lang.msi
msiexec /i sub-lang.msi
```

Add `/quiet` to `msiexec` for an unattended install.

### Step 2 — Verify

Open a **new** Command Prompt or PowerShell — an existing one will not have
the updated `PATH`:

```cmd
sub --version
subc --version
subi --version
```

### Step 3 — Run your first program

Create `hello.sb`:

```sub
let name = "World"
println("Hello, " + name)
```

```cmd
:: Interpret it
subi hello.sb

:: Compile it
subc hello.sb -o hello
hello.exe

:: Or transpile to JavaScript
sub hello.sb javascript
node hello.js
```

Double-clicking `hello.sb` runs it under the interpreter as well.

### Uninstalling

**Settings → Apps → Installed apps → SUB Language → Uninstall**, or
`msiexec /x` with the same `.msi`. The `PATH` entry and the file
association are removed with it.

> **Note:** the installer is not code-signed, so SmartScreen may warn you
> the first time. Click **More info → Run anyway**. The published checksums
> (`checksums-sha256.txt` on the release page) let you confirm you have the
> file the build produced.

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

Copy this in and run it to confirm your installation works.

**fibonacci.sb**

```sub
let a = 0
let b = 1

println("Fibonacci sequence:")

for i in range(10) {
    println(a)
    let temp = a + b
    a = b
    b = temp
}
```

```bash
subi fibonacci.sb            # interpret
subc fibonacci.sb -o fib     # or compile
./fib
```

Expected output:

```
Fibonacci sequence:
0
1
1
2
3
5
8
13
21
34
```

All three tools should agree — that is what the conformance suite checks on
every push, across eleven implementations of the language.

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
