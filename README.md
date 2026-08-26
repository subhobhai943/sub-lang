<p align="center">
  <img src="docs/logo.png" alt="SUB Language Logo" width="480" />
</p>

<h1 align="center">SUB Programming Language</h1>

<p align="center">
  <em>Simple Universal Builder — A modern, unified multi-paradigm compiled & transpiled language</em>
</p>

<p align="center">
  <a href="https://github.com/subhobhai943/sub-lang/actions/workflows/ci.yml">
    <img src="https://github.com/subhobhai943/sub-lang/actions/workflows/ci.yml/badge.svg" alt="Build" />
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
- 🎨 **Minimal, Expressive Syntax**: One canonical spelling per idea - `let`, `fn`, `null`, and brace-delimited `{}` blocks. Older spellings still compile, with a deprecation note.

---

## Key Highlights

- **Dynamic & Static Flexibility**: SUB is dynamically typed, but a shared inference pass gives every function a concrete signature when targeting a statically typed language - so `fn add(a, b)` becomes `long add(long, long)` in C and `fn add(a: i64, b: i64) -> i64` in Rust without annotations.
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

### Platform notes

The toolchain builds and runs on Linux, macOS and Windows (MinGW/MSYS2 or
MSVC). Platform handling is resolved at build time rather than assumed:

- `subc` generates code for the **host** platform and appends `.exe`
  automatically on Windows.
- The C compiler `subc` shells out to defaults to `clang` on macOS and `gcc`
  elsewhere. Set `CC` to override it:

  ```bash
  CC=clang ./subc hello.sb -o hello
  ```

- Console output is switched to UTF-8 on Windows so the banner and any
  box-drawing characters render correctly.

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

All three tools accept `--help` and `--version`:

```bash
./sub --version     # sub 2.0.0
./subc --help
./subi --help
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

SUB has **one canonical spelling for each idea**. Older spellings still
compile, so existing programs keep working, but the compiler prints a
deprecation note and the docs below teach only the canonical form.

| Idea | Use this | Still accepted (deprecated) |
|------|----------|-----------------------------|
| Variable | `let` | `var` |
| Function | `fn` | `function`, `def` |
| Empty value | `null` | `nil` |
| Block | `{ ... }` | `end`-delimited blocks |

`const` is **not** an alias for `let` - it declares a value that cannot be
reassigned, so it stays.

### Variables & Types

```sub
let name  = "SUB"        # string
let age   = 25           # integer
let pi    = 3.14159      # float
let ok    = true         # boolean
let empty = null         # empty value
const MAX = 100          # constant (cannot be reassigned)
```

### Functions

Declare functions with `fn`:

```sub
fn greet(name) {
    return "Hello, " + name + "!"
}

fn add(a, b) {
    return a + b
}

println(greet("World"))   # Hello, World!
println(add(10, 20))      # 30
```

Parameter and return types are inferred, including when compiling to a
statically typed target - `add` above becomes `long add(long, long)` in C and
`fn add(a: i64, b: i64) -> i64` in Rust. You can annotate explicitly when you
want to; an explicit type always wins over inference.

### Classes & Objects

```sub
class Point {
    let x = 0
    let y = 0
}

let p = Point.new
p.x = 10
p.y = 20

println(p.x)  # 10
println(p.y)  # 20
```

### Control Flow

Blocks use braces:

```sub
let score = 85

if score >= 90 {
    println("Grade: A")
} elif score >= 80 {
    println("Grade: B")
} else {
    println("Grade: C")
}
```

The older `end`-delimited style still parses, but braces are the documented
form and the one every example uses.

### Loops & Iteration

```sub
# Numeric range iteration
for i in range(1, 6) {
    println(i)          # 1, 2, 3, 4, 5
}

# Array iteration
let items = [10, 20, 30]
for item in items {
    println(item)
}

# String character iteration
for ch in "hello" {
    println(ch)         # h, e, l, l, o
}

# While loop
let n = 3
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
let choice = 2

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
let s = "Hello World"
println(s.length)              # 11
println(s.upper())             # HELLO WORLD
println(s.lower())             # hello world
println(s.substring(0, 5))     # Hello
println(s.contains("World"))   # 1 (true)
println(s.replace("World", "SUB")) # Hello SUB
println(s.trim())              # trims whitespace
println(s.char_at(0))          # H

let parts = "a,b,c".split(",")
println(parts[0])              # a
```

#### Arrays
```sub
let list = [1, 2, 3]
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
fn handleClick() {
    println("Button clicked!")
}

ui.window(title="SUB App", width=800, height=600) {
    ui.label(text="Welcome to SUB!", size=24)
    ui.button(text="Click Me", onclick=handleClick)
    ui.input(placeholder="Enter name", id="nameInput")
}
```

---

## Built-in Functions

| Function | Description | Example |
|---|---|---|
| `print(x)` / `show(x)` | Prints expression with newline | `print("hi")` |
| `println(a, b...)` | Prints space-separated arguments | `println("val:", 42)` |
| `input(prompt)` | Reads string input from stdin | `let s = input("Enter: ")` |
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

| Target | Command | Output File | Verified |
|---|---|---|---|
| **Python** | `./sub file.sb python` | `file.py` | ✅ executed in CI |
| **JavaScript** | `./sub file.sb js` | `file.js` | ✅ executed in CI |
| **C** | `./sub file.sb c` | `file.c` | ✅ executed in CI |
| **C++** | `./sub file.sb cpp` | `file.cpp` | ✅ executed in CI |
| **Rust** | `./sub file.sb rust` | `file.rs` | ✅ executed in CI |
| **Go** | `./sub file.sb go` | `file.go` | ✅ executed in CI |
| **Java** | `./sub file.sb java` | `SubProgram.java` | ✅ executed in CI |
| **Ruby** | `./sub file.sb ruby` | `file.rb` | ✅ executed in CI |
| **Kotlin** | `./sub file.sb kotlin` | `file.kt` | ⚠️ reviewed, not executed |
| **Swift** | `./sub file.sb swift` | `file.swift` | ⚠️ reviewed, not executed |
| **TypeScript** | `./sub file.sb ts` | `file.ts` | ⚠️ shares the JS backend |
| **Assembly (x86-64)** | `./sub file.sb asm` | `file.asm` | ⚠️ incomplete - see below |
| **CSS** | `./sub file.sb css` | `file.css` | ⚠️ UI declarations only |

"Executed in CI" means the conformance suite compiles and runs the generated
code and diffs its output against the interpreter. The rest are generated but
not run automatically - install `swiftc` / `kotlinc` and re-run the suite to
cover those two.

Java is the one target whose filename is fixed rather than derived: the class
must be named `SubProgram`, so the file must be `SubProgram.java`.

Two targets are narrower than the table suggests, so treat them accordingly:

- **Assembly** emits top-level arithmetic, variables and `print` correctly,
  but silently drops function declarations and loops - compiling
  `examples/fibonacci.sb` yields an empty `main`. Do not rely on it for
  anything with a function in it.
- **CSS** does not translate program logic. It emits a base stylesheet plus a
  class per `ui.*` declaration, so it is only meaningful alongside the UI
  syntax.

---

## Testing

There are two suites, and they answer different questions.

**Regression suite** - does each tool run without crashing?

```bash
python3 tests/run_tests.py
```

Validates `subi`, `subc` (native binary execution), `sub python`, `sub js`,
and `sub cpp` (compiled via `g++`).

**Conformance suite** - does a program *mean the same thing* on every backend?

```bash
make                                     # build first
python3 tests/conformance.py             # every installed target
python3 tests/conformance.py python c    # just these
```

The interpreter is the reference. For each program in `tests/conformance/`,
the harness runs it under `subi`, then compiles it with `subc` and transpiles
it to every target whose toolchain is on `PATH`, builds and runs each result,
and diffs the output against the reference. That is what catches the class of
bug where the same source silently produces different answers depending on
how you ran it.

Targets whose toolchain is missing are reported and skipped, and a run in
which nothing actually executed is reported as a failure rather than a pass -
a missing toolchain can never look green.

Adding a case is just dropping a `.sb` file into `tests/conformance/`;
whatever the interpreter prints becomes the expected output.

> **What this cannot catch:** because the interpreter is the reference, a bug
> *in the interpreter* makes every backend agree on the wrong answer and the
> suite still passes. Interpreter behaviour has to be checked against
> independently known-correct values. Read a green run as "the
> implementations agree", not "the language is correct".

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
         ├── subi  ─ Tree-Walk Interpreter (src/compilers/subi.c + src/core/interpreter.c)
         │         └── Direct AST evaluation & interactive REPL
         │           (values are dynamic at runtime - no inference needed)
         │
         └── Signature Inference (src/codegen/codegen_infer.c)
             │   Runs before any codegen. Annotates every function, variable
             │   and identifier with a concrete type, so the statically typed
             │   backends emit consistent signatures instead of each guessing.
             │
             ├── subc  ─ Native Compiler (src/compilers/sub_native.c + src/codegen/codegen.c)
             │         └── Emits C99 + runtime library -> $CC -> native binary
             │
             └── sub   ─ Multi-Target Transpiler (src/compilers/sub.c + src/codegen/)
                       └── Codegen modules for Python, JS, TS, C, C++, Rust,
                           Go, Java, Swift, Kotlin, Ruby, ASM, CSS
```

Inference is the reason a single fix reaches every backend: it writes types
onto the AST once, and each codegen module reads them rather than re-deriving
them. The interpreter bypasses it entirely, since it carries real values at
runtime.

---

## License

Distributed under the **MIT License**. See [`LICENSE`](LICENSE) for more information.

Copyright &copy; [Subhadip Sarkar](https://github.com/subhobhai943).
