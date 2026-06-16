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
- **Transpiles** to 10+ languages: Python, C, C++, JavaScript, TypeScript, Rust, Go, Java, Kotlin, Swift, Lua, Ruby
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

| Tool    | Description                                    |
|---------|------------------------------------------------|
| `subc`  | The **compiler** — produces a native binary    |
| `subcc` | The **transpiler** — converts to another language |
| `subi`  | The **interpreter** — runs `.sb` files directly |

---

## Usage

### Compile to native binary

```bash
./subc hello.sb              # produces ./hello
./subc hello.sb myapp        # produces ./myapp
```

### Transpile to another language

```bash
./subcc hello.sb python            # produces hello.py
./subcc hello.sb c                 # produces hello.c
./subcc hello.sb js                # produces hello.js
./subcc hello.sb rust              # produces hello.rs
./subcc hello.sb go                # produces hello.go
./subcc hello.sb ts                # produces hello.ts
./subcc hello.sb java              # produces hello.java
./subcc hello.sb kotlin            # produces hello.kt
./subcc hello.sb swift             # produces hello.swift
./subcc hello.sb lua               # produces hello.lua
./subcc hello.sb ruby              # produces hello.rb
./subcc hello.sb cpp               # produces hello.cpp

# Custom output name:
./subcc first.sb python first      # produces first.py
./subcc first.sb c    mylib        # produces mylib.c
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
    ├─ Parser      (src/core/parser.c)       ← recursion depth guard
    ├─ Semantic    (src/core/semantic.c)
    │
    ├─ Compiler    (src/compilers/sub_native_compiler.c)
    │       └─ C backend → gcc/clang → native binary
    │
    ├─ Transpiler  (src/compilers/sub_multilang.c)
    │       └─ generates <input-stem>.<ext> automatically
    │
    └─ Interpreter (src/core/interpreter.c + src/compilers/subi.c)
            └─ tree-walk evaluator with all built-ins
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
| Lua        | `lua`      | `.lua`          |
| Ruby       | `ruby`     | `.rb`           |

---

## Requirements

- GCC or Clang (for native compilation)
- GNU Make
- Linux / macOS / Windows (WSL recommended on Windows)

---

## Contributing

Contributions, bug reports, and feature requests are welcome!  
Please open an issue or pull request at [github.com/subhobhai943/sub-lang](https://github.com/subhobhai943/sub-lang).

---

## License

MIT © [Subhadip Sarkar](https://github.com/subhobhai943)
