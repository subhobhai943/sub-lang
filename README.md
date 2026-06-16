# SUB Programming Language

<p align="center">
  <img src="https://raw.githubusercontent.com/subhobhai943/sub-lang/main/logo.svg" alt="SUB Language Logo" width="480" />
</p>

> **v2.0.0** — Native compiler via C-backend, working interpreter, fixed transpiler naming, simplified syntax

[![Build](https://github.com/subhobhai943/sub-lang/actions/workflows/ci.yml/badge.svg)](https://github.com/subhobhai943/sub-lang/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

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
- `subc`   — the compiler (produces a native binary)
- `subc`   — the transpiler (same binary, different mode)
- `subi`   — the interpreter

---

## Usage

### Compile to native binary
```bash
./subc hello.sb              # produces ./hello
./subc hello.sb myapp        # produces ./myapp
```

### Transpile to another language
```bash
./subc hello.sb python            # produces hello.py
./subc hello.sb c                 # produces hello.c
./subc hello.sb js                # produces hello.js
./subc hello.sb rust              # produces hello.rs
./subc hello.sb go                # produces hello.go

# Custom output name:
./subc first.sb python first      # produces first.py
./subc first.sb c    mylib        # produces mylib.c
```

### Interpret directly
```bash
./subi hello.sb
```

---

## Language Syntax

### Variables
```sub
var name = "Subhadip"
var age  = 18
var pi   = 3.14
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

if x > 5 {
    show("big")
} else {
    show("small")
}

# Alternative: end-style blocks
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

# Short form: fn or def are also accepted
fn add(a, b) {
    return a + b
}
```

### String Operations
```sub
var s = "Hello"
show(len(s))           # 5
show(s + " World")    # Hello World
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

## Architecture

```
source.sb
    │
    ├─ Lexer      (src/core/lexer.c)
    ├─ Parser     (src/core/parser.c)      ← recursion depth guard
    ├─ Semantic   (src/core/semantic.c)
    │
    ├─ Compiler   (src/compilers/sub_native_compiler.c)
    │       └─ C backend → gcc/clang → native binary
    │
    ├─ Transpiler (src/compilers/sub_multilang.c)
    │       └─ generates <input-stem>.<ext> automatically
    │
    └─ Interpreter (src/core/interpreter.c + src/compilers/subi.c)
            └─ tree-walk evaluator with all built-ins
```

---

## Built-in Functions

| Function | Description |
|---|---|
| `print(x)` | Print value with newline |
| `show(x)` | Alias for print() |
| `input(prompt)` | Read a line from stdin |
| `str(x)` | Convert to string |
| `int(x)` | Convert to integer |
| `float(x)` | Convert to float |
| `len(s)` | String/array length |

---

## License

MIT © Subhadip Sarkar
