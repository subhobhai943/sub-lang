<p align="center">
  <img src="docs/logo.png" alt="SUB Language Logo" width="480" />
</p>

<h1 align="center">SUB Programming Language</h1>
<p align="center"><em>Simple Universal Builder — A modern, easy-to-learn compiled language</em></p>

<p align="center">
  <a href="https://github.com/subhobhai943/sub-lang/actions/workflows/ci.yml">
    <img src="https://github.com/subhobhai943/sub-lang/actions/workflows/build.yml/badge.svg" alt="Build" />
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" />
  </a>
  <img src="https://img.shields.io/badge/version-2.0.0-brightgreen" alt="Version 2.0.0" />
  <img src="https://img.shields.io/badge/transpiles%20to-10%2B%20languages-orange" alt="Transpiles to 10+ languages" />
</p>

---

> **v2.0.0** — Native compiler via C-backend, working interpreter, fixed transpiler naming, simplified syntax

---

## What is SUB?

SUB is a modern, easy-to-learn compiled programming language that:

- **Compiles to native machine code** via a C backend (GCC/Clang)
- **Transpiles** to Python, C, C++, JavaScript, TypeScript, Rust, Go, Java, Kotlin, Swift, Assembly, CSS, and Ruby
- **Interprets** code directly with the built-in SUBI interpreter
- Has a **clean, minimal syntax** — no semicolons required, optional `{}` or `end` blocks

---

## Quick Install

```bash
git clone https://github.com/subhobhai943/sub-lang
cd sub-lang
make
```

This builds three tools:

| Tool   | Description                                        |
|--------|-----------------------------------------------------|
| `sub`  | The **transpiler** — converts to another language  |
| `subc` | The **native compiler** — produces a native binary |
| `subi` | The **interpreter** — runs `.sb` files directly     |

---

## Usage

### Compile to native binary

```bash
./subc hello.sb              # produces ./hello
./subc hello.sb myapp        # produces ./myapp
```

### Transpile to another language

```bash
./sub hello.sb python            # produces hello.py
./sub hello.sb c                 # produces hello.c
./sub hello.sb js                # produces hello.js
./sub hello.sb rust               # produces hello.rs
./sub hello.sb go                # produces hello.go
./sub hello.sb ts                 # produces hello.ts
./sub hello.sb java                # produces hello.java
./sub hello.sb kotlin              # produces hello.kt
./sub hello.sb swift               # produces hello.swift
./sub hello.sb ruby                # produces hello.rb

# Custom output name:
./sub first.sb python first      # produces first.py
./sub first.sb c    mylib        # produces mylib.c
```

### Interpret directly

```bash
./subi hello.sb
```

---

## Language Syntax

### Hello World

```sub
print("Hello, World!")
```

### Variables

```sub
var name  = "Subhadip"
var age   = 18
var pi    = 3.14
const MAX = 100
```

### Print / Output

```sub
print("Hello, World!")
show("Hello, World!")   # show() is identical to print()
```

### If / Else

```sub
var x = 10

# Brace style
if x > 5 {
    show("big")
} else {
    show("small")
}

# End-block style
if x > 5
    show("big")
else
    show("small")
end
```

### While Loop

```sub
var i = 0
while i < 5 {
    show(i)
    i = i + 1
}
```

### For Loop

```sub
for n in range(5) {
    show(n)
}

for n in range(1, 10) {
    show(n)
}
```

### Functions

```sub
function greet(name) {
    return "Hello, " + name
}

show(greet("Subhadip"))

# Short forms: fn and def are also accepted
fn add(a, b) {
    return a + b
}

def multiply(a, b) {
    return a * b
}
```

### String Operations

```sub
var s = "Hello"
show(len(s))           # 5
show(s + " World")     # Hello World
show(str(42))          # "42"
show(int("7"))         # 7
```

### User Input

```sub
var name = input("Enter name: ")
show("Hi, " + name)
```

### Comments

```sub
# This is a single-line comment
```

---

## Examples

**FizzBuzz:**

```sub
for i in range(1, 31) {
    if i % 15 == 0 {
        show("FizzBuzz")
    } elif i % 3 == 0 {
        show("Fizz")
    } elif i % 5 == 0 {
        show("Buzz")
    } else {
        show(i)
    }
}
```

**Fibonacci:**

```sub
function fib(n) {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

for i in range(10) {
    show(fib(i))
}
```

---

## Built-in Functions

| Function       | Description                         |
|----------------|-------------------------------------|
| `print(x)`     | Print value with newline            |
| `show(x)`      | Alias for `print()`                 |
| `input(prompt)`| Read a line from stdin              |
| `str(x)`       | Convert to string                   |
| `int(x)`       | Convert to integer                  |
| `float(x)`     | Convert to float                    |
| `len(s)`       | String / array length               |
| `range(n)`     | Generate range `[0, n)`             |
| `range(a, b)`  | Generate range `[a, b)`             |

---

## Architecture

```
source.sb
    │
    ├─ Lexer       (src/core/lexer.c)
    ├─ Parser      (src/core/parser_enhanced.c)
    ├─ Semantic    (src/core/semantic.c)
    │
    ├─ subc — native compiler (src/compilers/sub_native.c)
    │       └─ C backend (src/codegen/codegen.c) → gcc/clang → native binary
    │
    ├─ sub — transpiler (src/compilers/sub.c)
    │       └─ src/codegen/codegen_multilang.c + codegen_rust.c
    │       └─ generates <input-stem>.<ext> automatically
    │
    └─ subi — interpreter (src/compilers/subi.c)
            └─ tree-walk evaluator (src/core/interpreter.c) with all built-ins
```

---

## Supported Transpilation Targets

| Target     | Flag       | Output extension |
|------------|------------|-----------------|
| Python     | `python`   | `.py`           |
| C          | `c`        | `.c`            |
| C++        | `cpp`      | `.cpp`          |
| JavaScript | `js`       | `.js`           |
| TypeScript | `ts`       | `.ts`           |
| Rust       | `rust`     | `.rs`           |
| Go         | `go`       | `.go`           |
| Java       | `java`     | `.java`         |
| Kotlin     | `kotlin`   | `.kt`           |
| Swift      | `swift`    | `.swift`        |
| Assembly   | `asm`      | `.asm`          |
| CSS        | `css`      | `.css`          |
| Ruby       | `ruby`     | `.rb`           |

---

## Requirements

- GCC or Clang (for native compilation)
- GNU Make
- Linux / macOS / Windows (via MSYS2/MinGW or Git Bash — see CI config in `.github/workflows/build.yml`; WSL also works)

---

## Contributing

Contributions, bug reports, and feature requests are welcome!  
Please open an issue or pull request at [github.com/subhobhai943/sub-lang](https://github.com/subhobhai943/sub-lang).

---

## License

MIT © [Subhadip Sarkar](https://github.com/subhobhai943)
