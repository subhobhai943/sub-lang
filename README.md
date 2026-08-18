<p align="center">
  <img src="docs/logo.png" alt="SUB Language Logo" width="480" />
</p>

<h1 align="center">SUB Programming Language</h1>

<p align="center">
  <em>Simple Universal Builder — A modern, unified multi-paradigm compiled & transpiled language</em>
</p>

<p align="center">
  <a href="https://github.com/subhobhai943/sub-lang/actions/workflows/ci.yml">
    <img src="https://github.com/subhobhai943/sub-lang/actions/workflows/build.yml/badge.svg" alt="Build" />
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" />
  </a>
  <img src="https://img.shields.io/badge/version-2.0.0-brightgreen" alt="Version 2.0.0" />
  <img src="https://img.shields.io/badge/transpiles%20to-13%20languages-orange" alt="Transpiles to 13 languages" />
</p>

---

> **v2.0.0** — Native compiler via optimized C backend, direct AST interpreter (`subi`), multi-target transpiler (`sub`), classes, UI component declarations, and embed blocks.

---

## Table of Contents

- [What is SUB?](#what-is-sub)
- [Key Highlights](#key-highlights)
- [Quick Start](#quick-start)
- [The Three Tools](#the-three-tools)
- [Language Syntax Guide](#language-syntax-guide)
  - [Variables & Types](#variables--types)
  - [Functions](#functions)
  - [Classes & Objects](#classes--objects)
  - [Control Flow](#control-flow)
  - [Loops & Iteration](#loops--iteration)
  - [Pattern Matching](#pattern-matching)
  - [Exception Handling](#exception-handling)
  - [String & Array Methods](#string--array-methods)
  - [Embedded Code Blocks](#embedded-code-blocks)
  - [Cross-Platform UI Declarations](#cross-platform-ui-declarations)
- [Built-in Functions](#built-in-functions)
- [Supported Transpilation Targets](#supported-transpilation-targets)
- [Testing](#testing)
- [Architecture](#architecture)
- [Contributing & License](#license)

---

## What is SUB?

**SUB (Simple Universal Builder)** is a clean, versatile programming language designed for developer productivity and maximum portability:

- 🚀 **Native Compilation**: Compiles `.sb` code directly to native machine binaries via an optimized C backend.
- 🔄 **Multi-Target Transpilation**: Generates idiomatic code in Python, JavaScript, TypeScript, C, C++, Rust, Go, Java, Kotlin, Swift, Ruby, x86-64 Assembly, and CSS.
- ⚡ **Direct AST Interpretation**: Run `.sb` programs instantly or interactively in REPL mode via `subi`.
- 🎨 **Minimal, Expressive Syntax**: Clean syntax with optional semicolons, supporting both brace-delimited `{}` and keyword-delimited `end` blocks.

---

## Key Highlights

- **Dynamic & Static Flexibility**: Type inference with runtime safety.
- **Rich Standard Library**: 25+ built-in utility functions, string methods, math wrappers, and dynamic array operations.
- **Embedded Foreign Code**: Seamlessly mix foreign C, Go, or Python directly inside `.sb` files using `#embed` blocks.
- **Cross-Platform UI Tree**: First-class declarative syntax for UI windows, labels, buttons, and inputs.

---

## Quick Start

### Build from Source

```bash
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang
make
```

This builds the three primary CLI tools:
- `./subc` — Native compiler
- `./subi` — Direct AST interpreter & REPL
- `./sub` — Multi-language transpiler

---

## The Three Tools

### 1. Native Compiler (`subc`)

Compiles SUB source code directly to a standalone native machine executable:

```bash
# Compile and create executable (default: ./hello)
./subc examples/hello_native.sb

# Specify custom executable name
./subc calculator.sb -o calc

# Run native binary
./calc
```

### 2. Direct Interpreter (`subi`)

Executes SUB source code on the fly:

```bash
# Execute a script
./subi examples/fizzbuzz.sb

# Run the interactive REPL
./subi
```

### 3. Multi-Target Transpiler (`sub`)

Translates `.sb` code into the target programming language of your choice:

```bash
./sub examples/fibonacci.sb python      # -> fibonacci.py
./sub examples/fibonacci.sb js          # -> fibonacci.js
./sub examples/fibonacci.sb ts          # -> fibonacci.ts
./sub examples/fibonacci.sb c           # -> fibonacci.c
./sub examples/fibonacci.sb cpp         # -> fibonacci.cpp
./sub examples/fibonacci.sb rust        # -> fibonacci.rs
./sub examples/fibonacci.sb go          # -> fibonacci.go
./sub examples/fibonacci.sb java        # -> fibonacci.java
./sub examples/fibonacci.sb kotlin      # -> fibonacci.kt
./sub examples/fibonacci.sb swift       # -> fibonacci.swift
./sub examples/fibonacci.sb ruby        # -> fibonacci.rb
./sub examples/fibonacci.sb asm         # -> fibonacci.asm
./sub examples/fibonacci.sb css         # -> fibonacci.css
```

---

## Language Syntax Guide

### Variables & Types

```sub
var name  = "SUB"        # string
var age   = 25           # integer
var pi    = 3.14159      # float
var ok    = true         # boolean
var empty = null         # null / nil
const MAX = 100          # constant
```

### Functions

Functions can be declared using `function`, `fn`, or `def`:

```sub
function greet(name) {
    return "Hello, " + name + "!"
}

fn add(a, b) {
    return a + b
}

def multiply(a, b) {
    return a * b
}

println(greet("World"))   # Hello, World!
println(add(10, 20))      # 30
println(multiply(6, 7))   # 42
```

### Classes & Objects

```sub
class Point {
    var x = 0
    var y = 0
}

var p = Point.new
p.x = 10
p.y = 20

println(p.x)  # 10
println(p.y)  # 20
```

### Control Flow

Supports both `{}` brace syntax and `end` blocks:

```sub
var score = 85

# Brace style
if score >= 90 {
    println("Grade: A")
} elif score >= 80 {
    println("Grade: B")
} else {
    println("Grade: C")
}

# End-delimited style
if score >= 90
    println("Grade: A")
elif score >= 80
    println("Grade: B")
else
    println("Grade: C")
end
```

### Loops & Iteration

```sub
# Numeric range iteration
for i in range(1, 6) {
    println(i)          # 1, 2, 3, 4, 5
}

# Array iteration
var items = [10, 20, 30]
for item in items {
    println(item)
}

# String character iteration
for ch in "hello" {
    println(ch)         # h, e, l, l, o
}

# While loop
var n = 3
while n > 0 {
    println(n)
    n -= 1
}

# Break and Continue
for i in range(10) {
    if i % 2 == 1 { continue }
    if i == 6 { break }
    println(i)          # 0, 2, 4
}
```

### Pattern Matching

```sub
var choice = 2

switch choice {
    case 1:
        println("First choice")
    case 2:
        println("Second choice")
    default:
        println("Other choice")
}
```

### Exception Handling

```sub
try {
    throw "Fatal operation error"
} catch (err) {
    println("Caught exception:", err)
} finally {
    println("Cleanup completed")
}
```

### String & Array Methods

#### Strings
```sub
var s = "Hello World"
println(s.length)              # 11
println(s.upper())             # HELLO WORLD
println(s.lower())             # hello world
println(s.substring(0, 5))     # Hello
println(s.contains("World"))   # 1 (true)
println(s.replace("World", "SUB")) # Hello SUB
println(s.trim())              # trims whitespace
println(s.char_at(0))          # H

var parts = "a,b,c".split(",")
println(parts[0])              # a
```

#### Arrays
```sub
var list = [1, 2, 3]
list.push(4)
println(list.length)           # 4
println(list.pop())            # 4
println(list.join(" - "))      # 1 - 2 - 3
```

### Embedded Code Blocks

Embed foreign target languages directly in SUB source:

```sub
#embed c
int custom_c_add(int a, int b) {
    return a + b;
}
#endembed

println("Embedded C code compiled seamlessly!")
```

### Cross-Platform UI Declarations

```sub
function handleClick() {
    println("Button clicked!")
}

ui.window(title="SUB App", width=800, height=600)
    ui.label(text="Welcome to SUB!", size=24)
    ui.button(text="Click Me", onclick=handleClick)
    ui.input(placeholder="Enter name", id="nameInput")
end
```

---

## Built-in Functions

| Function | Description | Example |
|---|---|---|
| `print(x)` / `show(x)` | Prints expression with newline | `print("hi")` |
| `println(a, b...)` | Prints space-separated arguments | `println("val:", 42)` |
| `input(prompt)` | Reads string input from stdin | `var s = input("Enter: ")` |
| `str(x)` / `to_string(x)` | Converts value to string | `str(123)` |
| `int(x)` | Converts value to integer | `int("42")` |
| `float(x)` | Converts value to float | `float("3.14")` |
| `type(x)` | Returns type string (`"int"`, `"string"`, etc.) | `type(42)` -> `"int"` |
| `len(x)` | Length of string or array | `len([1, 2])` |
| `range(end)` / `range(start, end)` | Generates iteration range | `range(1, 5)` |
| `abs(x)` / `sqrt(x)` | Absolute value / square root | `sqrt(16)` -> `4.0` |
| `min(a, b)` / `max(a, b)` | Minimum / Maximum of two values | `max(10, 20)` |
| `floor(x)` / `ceil(x)` / `round(x)` | Rounding operations | `floor(3.7)` -> `3` |
| `push(arr, val)` / `pop(arr)` | Array operations | `push(arr, 10)` |
| `join(arr, sep)` | Join array to string | `join(arr, ", ")` |
| `trim(s)` / `char_at(s, i)` | String operations | `trim(" hi ")` |

---

## Supported Transpilation Targets

| Target | Command | Output File |
|---|---|---|
| **Python** | `./sub file.sb python` | `file.py` |
| **JavaScript** | `./sub file.sb js` | `file.js` |
| **TypeScript** | `./sub file.sb ts` | `file.ts` |
| **C** | `./sub file.sb c` | `file.c` |
| **C++** | `./sub file.sb cpp` | `file.cpp` |
| **Rust** | `./sub file.sb rust` | `file.rs` |
| **Go** | `./sub file.sb go` | `file.go` |
| **Java** | `./sub file.sb java` | `file.java` |
| **Kotlin** | `./sub file.sb kotlin` | `file.kt` |
| **Swift** | `./sub file.sb swift` | `file.swift` |
| **Ruby** | `./sub file.sb ruby` | `file.rb` |
| **Assembly (x86-64)** | `./sub file.sb asm` | `file.asm` |
| **CSS** | `./sub file.sb css` | `file.css` |

---

## Testing

Run the automated regression test suite:

```bash
python3 tests/run_tests.py
```

This validates `subi`, `subc` (native binary execution), `sub python`, `sub js`, and `sub cpp` (compiled via `g++`) across all standard test suites.

---

## Architecture

```
source.sb
    │
    ├── Lexer         (src/core/lexer.c)
    ├── Parser        (src/core/parser_enhanced.c)
    ├── Semantic      (src/core/semantic.c)
    └── Type System   (src/core/type_system.c)
         │
         ├── subc  ─ Native Compiler  (src/compilers/sub_native.c + src/codegen/codegen.c)
         │         └── Emits optimized C99 + runtime library -> GCC/Clang -> Native Binary
         │
         ├── subi  ─ Tree-Walk Interpreter (src/compilers/subi.c + src/core/interpreter.c)
         │         └── Direct AST evaluation & interactive REPL
         │
         └── sub   ─ Multi-Target Transpiler (src/compilers/sub.c + src/codegen/)
                   └── Codegen modules for Python, JS, TS, C, C++, Rust, Go, Java, Swift, Kotlin, Ruby, ASM, CSS
```

---

## License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for more information.

Copyright &copy; [Subhadip Sarkar](https://github.com/subhobhai943).
