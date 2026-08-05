<p align="center">
  <img src="docs/logo.png" alt="SUB Language Logo" width="480" />
</p>

<h1 align="center">SUB Programming Language</h1>

<p align="center">
  <em>Simple Universal Builder — A modern, easy-to-learn compiled language</em>
</p>

<p align="center">
  <a href="https://github.com/subhobhai943/sub-lang/actions/workflows/ci.yml">
    <img src="https://github.com/subhobhai943/sub-lang/actions/workflows/build.yml/badge.svg" alt="Build" />
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License: MIT" />
  </a>
  <img src="https://img.shields.io/badge/version-2.0.0-brightgreen" alt="Version 2.0.0" />
  <img src="https://img.shields.io/badge/transpiles%20to-14%20languages-orange" alt="Transpiles to 14 languages" />
</p>

---

> **v2.0.0** — Native compiler via C-backend, working interpreter, fixed transpiler naming, simplified syntax, professional features

---

## What is SUB?

SUB (Simple Universal Builder) is a modern, easy-to-learn compiled programming language that:

- **Compiles to native machine code** via a C backend (GCC/Clang)
- **Transpiles** to Python, C, C++, JavaScript, TypeScript, Rust, Go, Java, Kotlin, Swift, Assembly, CSS, and Ruby
- **Interprets** code directly with the built-in `subi` interpreter
- Has a **clean, minimal syntax** — no semicolons required, optional `{}` or `end` blocks

---

## Features

### Core Language

- **Typed values** — integers, floats, strings, booleans, null, arrays, objects
- **Variables** — `var` (mutable), `const` (immutable), type inference
- **Functions** — `function` / `fn` / `def` with recursion and closures
- **Control flow** — `if` / `elif` / `else`, `for` / `while`, `do-while`
- **Pattern matching** — `switch` / `match` with `case` and `default`
- **Error handling** — `try` / `catch` / `finally` / `throw`
- **Loop control** — `break` and `continue` in all loop types

### Operators

- **Arithmetic** — `+`, `-`, `*`, `/`, `%`, `**` (power, right-associative)
- **Comparison** — `==`, `!=`, `<`, `>`, `<=`, `>=`
- **Logical** — `&&`, `||`, `!`
- **Bitwise** — `&`, `|`, `^`, `~`, `<<`, `>>`
- **Assignment** — `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- **Increment/Decrement** — `++`, `--` (prefix and postfix)
- **Ternary** — `condition ? then : else`
- **Float modulo** — `%` works on floats via `fmod()`

### String Methods

`.length`, `.upper()`, `.lower()`, `.substring(start, end)`, `.split(delimiter)`,
`.contains(substring)`, `.replace(old, new)`, `.trim()`, `.char_at(index)`

### Array Operations

Literal syntax `[1, 2, 3]`, indexing `arr[0]`, assignment `arr[1] = 99`,
`.length`, `.push(val)`, `.pop()`, `.join(sep)`, `for item in arr` iteration

### Other

- **For-in string iteration** — `for ch in "abc"` iterates characters
- **Object literals** — `{"key": "value"}`
- **Embed foreign code** — `embed c ... endembed`
- **`type()` function** — returns type name as string (`"int"`, `"float"`, `"string"`, `"bool"`, `"null"`, `"array"`)
- **22+ built-in functions** — `print`, `show`, `input`, `str`, `int`, `float`, `len`, `range`, `abs`, `min`, `max`, `floor`, `ceil`, `round`, `sqrt`, `trim`, `char_at`, `push`, `pop`, `join`, `to_string`, `println`
- **Dual block style** — brace-delimited `{ }` or `end`-delimited

---

## Quick Install

```bash
git clone https://github.com/subhobhai943/sub-lang
cd sub-lang
make
```

This builds three tools:

| Tool | Description | Usage |
|------|-------------|-------|
| `sub` | **Transpiler** — converts `.sb` to another language | `./sub hello.sb python` |
| `subc` | **Native compiler** — produces a native binary | `./subc hello.sb` |
| `subi` | **Interpreter** — runs `.sb` files directly | `./subi hello.sb` |

---

## Usage

### Compile to native binary

```bash
./subc hello.sb              # produces ./hello
./subc hello.sb -o myapp     # produces ./myapp
```

### Transpile to another language

```bash
./sub hello.sb python         # produces hello.py
./sub hello.sb c              # produces hello.c
./sub hello.sb cpp            # produces hello.cpp
./sub hello.sb js             # produces hello.js
./sub hello.sb ts             # produces hello.ts
./sub hello.sb rust           # produces hello.rs
./sub hello.sb go             # produces hello.go
./sub hello.sb java           # produces hello.java
./sub hello.sb kotlin         # produces hello.kt
./sub hello.sb swift          # produces hello.swift
./sub hello.sb ruby           # produces hello.rb
./sub hello.sb asm            # produces hello.asm
./sub hello.sb css            # produces hello.css

# Custom output name:
./sub first.sb python first   # produces first.py
```

### Interpret directly

```bash
./subi hello.sb
```

### REPL mode

```bash
./subi
# Interactive SUB interpreter — type expressions and statements line by line
```

---

## Language Syntax Guide

### Hello World

```sub
println("Hello, World!")
```

### Variables & Types

```sub
var name  = "SUB"       # string
var count = 42           # integer
var pi    = 3.14         # float
var ok    = true         # boolean
var empty = null         # null
const MAX = 100          # constant
```

### Functions

```sub
function greet(name) {
    return "Hello, " + name
}

fn add(a, b) {
    return a + b
}

def multiply(a, b) {
    return a * b
}

println(greet("World"))   # Hello, World
println(add(3, 4))         # 7
```

### If / Elif / Else

Both brace and `end`-delimited styles are supported:

```sub
var x = 10

# Brace style
if x > 5 {
    println("big")
} elif x > 0 {
    println("small positive")
} else {
    println("non-positive")
}

# End-delimited style
if x > 5
    println("big")
elif x > 0
    println("small positive")
else
    println("non-positive")
end
```

### For Loop

```sub
# Range iteration
for i in range(5) {
    println(i)           # 0, 1, 2, 3, 4
}

for i in range(1, 10) {
    println(i)           # 1 through 9
}

# Array iteration
var items = [10, 20, 30]
for item in items {
    println(item)
}

# String character iteration
for ch in "hello" {
    println(ch)          # h, e, l, l, o
}
```

### While Loop

```sub
var i = 0
while i < 5 {
    println(i)
    i += 1
}
```

### Do-While Loop

```sub
var x = 0

# Brace style
do {
    x += 1
} while (x < 3)

# End-delimited style
var y = 0
do
    y += 10
end while y < 50
```

### Break & Continue

```sub
# Break — stop loop early
for i in range(100) {
    if i == 5 {
        break
    }
}
# i is 5

# Continue — skip iteration
for i in range(10) {
    if i % 2 == 1 {
        continue
    }
    println(i)           # 0, 2, 4, 6, 8
}
```

### Switch / Match

```sub
var x = 2

# Brace style
switch x {
    case 1:
        println("one")
    case 2:
        println("two")
    case 3:
        println("three")
    default:
        println("other")
}

# End-delimited style
match x
    case 1
        println("one")
    case 2
        println("two")
    default
        println("other")
end
```

### Try / Catch / Throw

```sub
# With exception variable
try {
    throw "something went wrong"
} catch (e) {
    println("Caught:", e)
}

# Without exception variable
try {
    throw 42
} catch {
    println("An error occurred")
}

# With finally
try {
    println("trying")
} catch (err) {
    println("caught:", err)
} finally {
    println("cleanup")
}
```

### Ternary Expressions

```sub
var age = 20
var label = age >= 18 ? "adult" : "minor"
println(label)            # adult

var max = a > b ? a : b
```

### Operators

```sub
# Arithmetic
println(2 + 3)           # 5
println(10 - 4)           # 6
println(3 * 7)            # 21
println(10 / 3)           # 3
println(10 % 3)           # 1
println(2 ** 10)          # 1024  (power, right-associative)

# Comparison
println(1 < 2)            # true
println(2 > 1)            # true
println(1 == 1)           # true
println(1 != 2)           # true

# Logical
println(true && false)    # false
println(true || false)    # true
println(!true)            # false

# Bitwise
println(10 & 3)           # 2
println(10 | 3)           # 11
println(10 ^ 3)           # 9
println(~10)              # -11
println(10 << 2)          # 40
println(10 >> 2)          # 2

# Compound assignment
var n = 10
n += 5                    # 15
n -= 3                    # 12
n *= 2                    # 24
n /= 4                    # 6
n %= 4                    # 2
n **= 3                   # 8

# Increment / Decrement
var a = 5
println(++a)               # 6  (pre-increment)
println(a--)               # 6  (post-increment, returns old value)

# Float modulo
println(7.5 % 2.0)        # 1.5
```

### String Methods

```sub
var s = "Hello World"

println(s.length)              # 11
println(s.upper())             # HELLO WORLD
println(s.lower())             # hello world
println(s.substring(0, 5))     # Hello
println(s.contains("World"))   # true
println(s.replace("World", "SUB"))  # Hello SUB
println(s.trim())              # trims whitespace

var parts = "a,b,c".split(",")
println(parts[0])              # a
println(parts.length)          # 3

println(s.char_at(0))          # H
println(s.char_at(6))          # W
```

### Array Operations

```sub
# Literal
var arr = [10, 20, 30]

# Indexing
println(arr[0])                # 10
println(arr[2])                # 30

# Assignment
arr[1] = 99
println(arr[1])                # 99

# Length
println(arr.length)            # 3

# Push and pop
arr.push(40)
println(arr.length)            # 4
arr.pop()
println(arr.length)            # 3

# Join
println(arr.join(", "))        # 10, 99, 30

# Empty array
var empty = []
println(empty.length)          # 0
```

### Type Function

```sub
println(type(42))               # int
println(type(3.14))            # float
println(type("hello"))         # string
println(type(true))            # bool
println(type(null))            # null
println(type([1, 2, 3]))      # array
```

### Comments

```sub
# This is a single-line comment
```

---

## Built-in Functions

| Function | Description |
|----------|-------------|
| `print(x)` | Print value with newline |
| `show(x)` | Alias for `print()` |
| `println(args...)` | Print all args space-separated with newline |
| `input(prompt)` | Read a line from stdin |
| `str(x)` | Convert to string |
| `int(x)` | Convert to integer |
| `float(x)` | Convert to float |
| `to_string(x)` | Convert to string (alias) |
| `len(s)` | String / array length |
| `range(n)` | Generate range `[0, n)` |
| `range(a, b)` | Generate range `[a, b)` |
| `type(x)` | Return type name as string |
| `abs(x)` | Absolute value |
| `min(a, b)` | Minimum of two values |
| `max(a, b)` | Maximum of two values |
| `floor(x)` | Floor of float |
| `ceil(x)` | Ceiling of float |
| `round(x)` | Round to nearest integer |
| `sqrt(x)` | Square root |
| `push(arr, val)` | Push item onto array |
| `pop(arr)` | Pop item from array |
| `join(arr, sep)` | Join array elements with separator |
| `trim(s)` | Trim whitespace from string |
| `char_at(s, i)` | Get character at index |

---

## Architecture

```
source.sb
    |
    +-- Lexer       (src/core/lexer.c)
    +-- Parser      (src/core/parser_enhanced.c)
    +-- Semantic    (src/core/semantic.c)
    +-- Type System (src/core/type_system.c)
    |
    +-- subc -- native compiler (src/compilers/sub_native.c)
    |       +-- C backend (src/codegen/codegen.c) -> gcc/clang -> native binary
    |
    +-- sub  -- transpiler (src/compilers/sub.c)
    |       +-- src/codegen/codegen_multilang.c + codegen_rust.c + codegen_cpp.c
    |       +-- generates <input-stem>.<ext> automatically
    |
    +-- subi -- interpreter (src/compilers/subi.c)
            +-- tree-walk evaluator (src/core/interpreter.c) with all built-ins
```

---

## Supported Transpilation Targets

| Target | Flag | Output Extension |
|--------|------|-----------------|
| Python | `python` | `.py` |
| C | `c` | `.c` |
| C++ | `cpp` | `.cpp` |
| JavaScript | `js` | `.js` |
| TypeScript | `ts` | `.ts` |
| Rust | `rust` | `.rs` |
| Go | `go` | `.go` |
| Java | `java` | `.java` |
| Kotlin | `kotlin` | `.kt` |
| Swift | `swift` | `.swift` |
| Assembly | `asm` | `.asm` |
| CSS | `css` | `.css` |
| Ruby | `ruby` | `.rb` |

---

## Requirements

- **GCC or Clang** (for native compilation)
- **GNU Make**
- **Linux / macOS / Windows** (via MSYS2/MinGW or Git Bash — see CI config; WSL also works)

---

## Documentation

Comprehensive guides and specs are available in the [`docs/`](docs/) directory:

- [Language Specification](docs/LANGUAGE_SPEC.md)
- [Quick Start Guide](docs/guides/QUICKSTART.md)
- [Build Guide](docs/BUILD_GUIDE.md)
- [Installation Guide](docs/INSTALLATION_GUIDE.md)
- [Native Compilation Guide](docs/guides/NATIVE_COMPILATION.md)
- [Native Compiler Guide](docs/guides/NATIVE_COMPILER_GUIDE.md)
- [Multilang Transpilation Guide](docs/guides/MULTILANG_GUIDE.md)
- [Standard Library Guide](docs/guides/STDLIB_GUIDE.md)
- [Syntax Highlighting](docs/SYNTAX_HIGHLIGHTING.md)
- [Roadmap](docs/ROADMAP.md)
- [Contributing](docs/CONTRIBUTING.md)
- [Release Notes](docs/RELEASE_NOTES.md)

---

## Examples

### FizzBuzz

```sub
for i in range(1, 31) {
    if i % 15 == 0 {
        println("FizzBuzz")
    } elif i % 3 == 0 {
        println("Fizz")
    } elif i % 5 == 0 {
        println("Buzz")
    } else {
        println(i)
    }
}
```

### Fibonacci

```sub
function fib(n) {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

for i in range(10) {
    println(fib(i))
}
```

---

## Contributing

Contributions, bug reports, and feature requests are welcome!
Please open an issue or pull request at [github.com/subhobhai943/sub-lang](https://github.com/subhobhai943/sub-lang).

---

## License

MIT &copy; [Subhadip Sarkar](https://github.com/subhobhai943)
