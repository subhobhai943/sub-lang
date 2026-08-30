<p align="center">
  <img src="docs/logo.png" alt="SUB lang" width="260" />
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
  <img src="https://img.shields.io/badge/version-1.0.8-brightgreen" alt="Version 1.0.8" />
  <img src="https://img.shields.io/badge/transpiles%20to-13%20languages-orange" alt="Transpiles to 13 languages" />
</p>

---

> **v1.0.8** — Native compiler that emits x86-64 machine code itself (no C compiler required), direct AST interpreter (`subi`), multi-target transpiler (`sub`), classes, UI component declarations, and embed blocks.

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
- [Standard Library](#standard-library)
- [Supported Transpilation Targets](#supported-transpilation-targets)
- [Numbers, division and errors](#numbers-division-and-errors)
- [Testing](#testing)
- [Architecture](#architecture)
- [Contributing & License](#license)

---

## What is SUB?

**SUB (Simple Universal Builder)** is a clean, versatile programming language designed for developer productivity and maximum portability:

- 🚀 **Native Compilation**: `subc` emits x86-64 machine code and writes the ELF executable itself. No assembler, no linker, no C compiler, no libc - a compiled SUB program is a self-contained static binary a few kilobytes in size.
- 🔄 **Multi-Target Transpilation**: Generates idiomatic code in Python, JavaScript, TypeScript, C, C++, Rust, Go, Java, Kotlin, Swift, Ruby, x86-64 Assembly, and CSS.
- ⚡ **Direct AST Interpretation**: Run `.sb` programs instantly or interactively in REPL mode via `subi`.
- 🎨 **Minimal, Expressive Syntax**: One canonical spelling per idea - `let`, `fn`, `null`, and brace-delimited `{}` blocks. Older spellings still compile, with a deprecation note.

---

## Key Highlights

- **A compiler, not a wrapper**: on x86-64 Linux `subc` goes from `.sb` straight to machine code in one process. Nothing is shelled out to, so the machine that compiles a SUB program needs no toolchain beyond `subc` itself.
- **Dynamic & Static Flexibility**: SUB is dynamically typed, but a shared inference pass gives every function a concrete signature when targeting a statically typed language - so `fn add(a, b)` becomes `long add(long, long)` in C and `fn add(a: i64, b: i64) -> i64` in Rust without annotations.
- **Standard Library**: four modules — `math`, `arrays`, `strings`, `io` — written in SUB and verified through every backend, loaded with `import`.
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

- On **x86-64 Linux**, `subc` emits machine code directly and needs no other
  tool. Everywhere else it falls back to generating C and building it with a
  host compiler, and says so when it does.
- `subc` generates code for the **host** platform and appends `.exe`
  automatically on Windows.
- On the fallback path the C compiler defaults to `clang` on macOS and `gcc`
  elsewhere. Set `CC` to override it:

  ```bash
  CC=clang ./subc hello.sb --via-c -o hello
  ```

- Console output is switched to UTF-8 on Windows so the banner and any
  box-drawing characters render correctly.

---

## The Three Tools

### 1. Native Compiler (`subc`)

Compiles SUB source code to a standalone native executable:

```bash
# Compile and create executable (default: ./hello)
./subc examples/hello_native.sb

# Specify custom executable name
./subc calculator.sb -o calc

# Run native binary
./calc
```

**Two backends.** On x86-64 Linux the default is the built-in machine-code
backend: `subc` encodes x86-64 instructions, emits its own runtime, and
writes a static ELF64 executable, all in-process. On other hosts, and for
programs using something the machine-code backend does not implement yet, it
generates C and hands that to a host compiler instead - printing a note
saying which construct forced the fallback.

```bash
./subc hello.sb --native     # require the machine-code backend; fail rather than fall back
./subc hello.sb --via-c      # generate C and build it with $CC
```

The difference is visible in what comes out:

| | machine-code backend | via a C compiler |
|---|---|---|
| Tools needed at compile time | none | a working `cc` |
| `examples/fibonacci.sb` binary | ~3.5 KB, static, no `libc` | ~16 KB, dynamically linked |
| Compile time | ~1 ms | ~100 ms |
| `fib(30)` runtime | ~13 ms | ~3 ms (`gcc -O2`) |

The generated code is unoptimised - one accumulator register, everything else
through memory - so it gives up roughly 4x against `gcc -O2` while still
running about 90x faster than the interpreter. What it buys is that nothing
else has to be installed.

**Not yet in the machine-code backend:** objects, classes, `input()`,
`try`/`catch`, iterating a string, and functions of more than six parameters.
Programs using these compile through the C backend automatically, and `subc`
names the construct that forced the fallback rather than failing silently.

Everything else compiles to machine code, including arrays (literals,
indexing, indexed assignment, `len`, `push`/`append`, `pop`, `for x in a`),
the string builtins (`substring`, `char_at`, `contains`, `replace`, `split`,
`join`), `switch`, `do`/`while`, `break`/`continue`, and top-level variables
read and assigned from inside functions.

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
./sub --version     # sub 1.0.8
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

```coffee
let name  = "SUB"        # string
let age   = 25           # integer
let pi    = 3.14159      # float
let ok    = true         # boolean
let empty = null         # empty value
const MAX = 100          # constant (cannot be reassigned)
```

### Functions

Declare functions with `fn`:

```coffee
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

A function can read and assign a variable declared at the top level:

```coffee
let calls = 0

fn bump() {
    calls = calls + 1
}

bump()
bump()
println(calls)            # 2
```

There is one `calls`, and every function shares it. A parameter or a `let` of
the same name inside a function is that function's own and leaves the
top-level one alone.

Each backend spells this the way its target does - a file-scope variable in C
and C++, a `static` field in Java, a `static mut` in Rust, a `$global` in
Ruby, a `global` statement in Python, a fixed cell in the machine-code
backend - so the program means the same thing through all ten.

### Classes & Objects

```coffee
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

```coffee
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

```coffee
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

Cases do not fall through: the clause that matches runs its body and nothing
else, so `break` at the end of a case is optional. A `break` in the middle of
a case skips the rest of it, and `continue` belongs to the loop around the
switch, not to the switch.

```coffee
let choice = 2

switch choice {
    case 1:
        println("First choice")
    case 2, 3:            # one clause, several values
        println("Second choice")
    default:
        println("Other choice")
}
```

The scrutinee can be any value, not just an integer — strings and floats
match too, and a case value can be any expression rather than a constant.
Every backend lowers a switch to an if/else chain for that reason, so all ten
agree on which clause runs.

### Exception Handling

```coffee
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
```coffee
let s = "Hello World"
println(s.length)              # 11
println(s.upper())             # HELLO WORLD
println(s.lower())             # hello world
println(s.substring(0, 5))     # Hello
println(s.contains("World"))   # true
println(s.replace("World", "SUB")) # Hello SUB
println(s.trim())              # trims whitespace
println(s.char_at(0))          # H

let parts = "a,b,c".split(",")
println(parts[0])              # a
println(parts.join("|"))       # a|b|c
```

`s.substring(0, 5)` and `substring(s, 0, 5)` are the same call: the parser
rewrites the method spelling into the function one, so both mean the same
thing under the interpreter and all ten backends.

#### Arrays
```coffee
let list = [1, 2, 3]
list.push(4)
println(list.length)           # 4
println(list.pop())            # 4
println(list.join(" - "))      # 1 - 2 - 3
```

### Embedded Code Blocks

Embed foreign target languages directly in SUB source:

```coffee
#embed c
int custom_c_add(int a, int b) {
    return a + b;
}
#endembed

println("Embedded C code compiled seamlessly!")
```

### Cross-Platform UI Declarations

```coffee
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
| `upper(s)` / `lower(s)` / `trim(s)` | Case and whitespace | `upper("hi")` |
| `substring(s, start[, end])` | A slice; both bounds are clamped | `substring("hello", 1, 3)` -> `"el"` |
| `char_at(s, i)` | One character; `i` may count from the end | `char_at("abc", -1)` -> `"c"` |
| `contains(s, needle)` | Substring test | `contains("hello", "ell")` |
| `replace(s, old, new)` | Every occurrence | `replace("a-b", "-", "+")` |
| `split(s[, sep])` | Cut into an array; empty fields kept | `split("a,,b", ",")` |
| `join(arr[, sep])` | Join an array into a string | `join(arr, ", ")` |

All ten backends and the interpreter agree on these, down to the edge cases:
`substring` clamps a negative bound to 0 rather than counting from the end,
`replace` with an empty pattern changes nothing, `split` keeps empty fields
and always yields at least one, and `char_at` outside the string stops the
program with the interpreter's exit status. `tests/conformance/21_strings.sb`
is what holds them to it.

---

## Standard Library

Four modules, written in SUB, in `stdlib/`. Every function in them produces
identical output through all ten backends — the conformance suite checks the
library the same way it checks the language.

```coffee
import "math"
import "arrays"
import "strings"
import "io"

heading("primes")
let primes = []
for n in range(2, 30) {
    if is_prime(n) {
        push(primes, n)
    }
}
println(primes)

let scores = [72, 91, 68, 91, 55, 83]
println(sorted_int(scores))
println(pad_left(str(max_int(scores)), 5, " "))
ok("done")
```

| Module | What it has |
|---|---|
| `math` | `PI`, `E`, `TAU`, `gcd`, `lcm`, `factorial`, `fib`, `is_prime`, `ipow`, `clamp`, `sign`, `hypot`, `close_to`, `round_to` |
| `arrays` | `sum_int`/`sum_float`, `min_*`/`max_*`, `mean_float`, `index_of_int`, `sorted_int`, `unique_int`, `reverse_int`, `slice_int`, `concat_int` |
| `strings` | `repeat`, `pad_left`, `pad_right`, `center`, `eq_ignore_case`, `is_empty`, `is_blank` |
| `io` | ANSI colour, `info`/`ok`/`warn`/`fail`, `heading`, `row`, `rule` |

`examples/stdlib_tour.sb` exercises all four; `stdlib/README.md` documents
every function, and is candid about what is missing and why.

### import

An import is resolved and spliced at parse time, so what each backend compiles
is one flat program: a module's functions are ordinary functions and its `let`
is an ordinary global. A module is looked for beside the importing file first,
then in `$SUB_PATH`, then in the `stdlib/` that ships beside the executable
(or `../lib/sub/stdlib` after `make install`). The `.sb` extension is
optional, importing the same module twice splices it once, and a cycle is
reported rather than followed.

Writing your own module is just writing a `.sb` file:

```coffee
# geometry.sb
fn area_of_circle(r: float): float {
    return 3.141592653589793 * r * r
}
```

```coffee
import "geometry"
println(area_of_circle(2.0))
```

Annotate parameter and return types in a module you intend to compile: the
statically typed backends need a concrete type for each, and an explicit
annotation is never overridden by inference.

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
| **Kotlin** | `./sub file.sb kotlin` | `file.kt` | ✅ executed in CI |
| **Swift** | `./sub file.sb swift` | `file.swift` | ✅ executed in CI |
| **TypeScript** | `./sub file.sb ts` | `file.ts` | ⚠️ shares the JS backend |
| **Assembly (x86-64)** | `./sub file.sb asm` | `file.asm` | ⚠️ incomplete - see below |
| **CSS** | `./sub file.sb css` | `file.css` | ⚠️ UI declarations only |

"Executed in CI" means the conformance suite compiles and runs the generated
code and diffs its output against the interpreter, on every push. Ten of the
thirteen targets are covered that way; the remaining three are generated but
never run, and the notes below say what that costs.

Kotlin and Swift were on the second list until recently, and the first run of
each found faults that had been there all along - Kotlin's `round()` broke
ties the wrong way and neither backend emitted anything at all for an array
literal. Reviewing generated code is not the same as running it.

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

**Native backend suite** - did `subc` really compile that without help?

```bash
python3 tests/native_backend.py
```

Compiles every conformance case with `PATH` emptied, runs the result with
`PATH` emptied, and reads the ELF header to confirm there is no dynamic
segment and no interpreter named. A green run means no compiler, assembler,
linker or `libc` was involved - which is the one thing the conformance suite
cannot tell you, since it only compares output. On hosts the backend does not
target it reports that and exits 0.

**Conformance suite** - does a program *mean the same thing* on every backend?

```bash
make                                     # build first
python3 tests/conformance.py             # every installed target
python3 tests/conformance.py python c    # just these
```

The interpreter is the reference. For each program in `tests/conformance/`,
the harness runs it under `subi`, then compiles it with `subc` through both
of its backends (`native` and `native-c`) and transpiles it to every target
whose toolchain is on `PATH`, builds and runs each result, and diffs the
output against the reference. That is what catches the class of
bug where the same source silently produces different answers depending on
how you ran it.

Targets whose toolchain is missing are reported and skipped, and a run in
which nothing actually executed is reported as a failure rather than a pass -
a missing toolchain can never look green.

Adding a case is just dropping a `.sb` file into `tests/conformance/`;
whatever the interpreter prints - and the status it exits with - becomes the
expected result. A program that ends in a runtime error is a legitimate case,
because how a program fails is part of what the language means.

> **What this cannot catch:** because the interpreter is the reference, a bug
> *in the interpreter* makes every backend agree on the wrong answer and the
> suite still passes. Interpreter behaviour has to be checked against
> independently known-correct values. Read a green run as "the
> implementations agree", not "the language is correct".

---

## Numbers, division and errors

A few behaviours are worth stating outright, because they are the ones where
languages disagree with each other and a transpiler can quietly inherit the
wrong one. SUB picks a single answer and every backend implements it.

**Integer division truncates toward zero**, and the remainder takes the sign
of the left operand - the C, Java and Go rule, not the Python and Ruby one:

```
 9 / 2   ->  4          -7 / 2   ->  -3
 7 % 3   ->  1          -7 % 3   ->  -1        7 % -3  ->  1
```

**Float remainder follows `fmod`**, which keeps the sign of the left operand:
`-7.5 % 3.0` is `-1.5`, not `1.5`.

**Dividing by zero is a runtime error**, for integers and floats alike:

```
RuntimeError: division by zero
```

and the program stops with exit status **70**. It is not an infinity, not a
NaN, not a silent zero, and not a crash. This is the interpreter's behaviour,
and the compiled and transpiled backends were brought into line with it -
before that, `7 / 0` raised SIGFPE in C, printed `Infinity` in JavaScript,
threw in Python and produced `0` in the native backend. Every backend now
routes `/` and `%` through a small generated helper, so this is also the one
place to change if a program wants IEEE infinities instead.

**An array is a value.** Binding one copies it; `push` and `pop` change the
array they are given:

```
let a = [1, 2]
let b = a
push(b, 3)
println(a)      # [1, 2]
println(b)      # [1, 2, 3]
```

Indexing out of range, and popping an empty array, are runtime errors with
exit status 70 - not a silent zero and not a language-specific exception:

```
RuntimeError: array index 5 out of bounds [0, 3)
```

**Floats print with six significant digits** (`printf`'s `%g`), trailing
zeros dropped, switching to exponent form outside `1e-4 … 1e+6`:

```
1.0 / 3.0  ->  0.333333        sqrt(16.0)  ->  4        1e20  ->  1e+20
```

Languages that print every digit needed to round-trip a double - JavaScript,
Python, Rust, Go, Java - go through a formatter that reproduces `%g`, so the
same program prints the same text everywhere.

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
             ├── subc  ─ Native Compiler (src/compilers/sub_native.c)
             │         ├── src/native/  x86-64 machine code -> static ELF64
             │         │                (default on x86-64 Linux; no external tools)
             │         └── src/codegen/codegen.c  C99 + runtime -> $CC -> binary
             │                          (fallback, and the path on other hosts)
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
