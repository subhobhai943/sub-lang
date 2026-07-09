# SUB Language Specification

This document describes the SUB grammar as it is actually implemented by
the lexer (`src/core/lexer.c`), parser (`src/core/parser_enhanced.c`), and
code generators (`src/codegen/`). If a `.sb` file uses something not listed
here, it will fail to parse or fail semantic analysis.

For features that are planned but not yet implemented, see
[`ROADMAP.md`](ROADMAP.md).

## 1. Introduction

SUB (Simple Universal Builder) is a small, easy-to-learn language that is
interpreted directly (`subi`), compiled to a native binary via a C backend
(`subc`), or transpiled to one of several target languages (`sub`).

## 2. Syntax

### 2.1 Comments

```sub
# Single-line comment
```

Comments start with `#` and run to the end of the line. There is no
block-comment syntax.

### 2.2 Variables

```sub
var name = "John"
var age = 25
const MAX = 100
```

`var` declares a mutable variable, `const` declares one that cannot be
reassigned. Types are always inferred from the initializer — there is no
type-annotation syntax (`name:type`).

### 2.3 Data Types

- **String** — text in double or single quotes, with escape sequences
- **Integer** — whole numbers, including hex (`0x..`), binary (`0b..`), and octal (`0o..`) literals
- **Float** — floating-point numbers
- **Boolean** — `true` / `false`
- **Null** — `null`

### 2.4 Functions

```sub
function add(a, b) {
    return a + b
}

# fn and def are accepted as synonyms for function
fn multiply(a, b) {
    return a * b
}

def subtract(a, b) {
    return a - b
}
```

### 2.5 Conditionals

```sub
if x > 5 {
    print("big")
} elif x > 0 {
    print("small positive")
} else {
    print("non-positive")
}
```

Both brace-block (`{ }`) and `end`-block styles are accepted for any
control-flow construct:

```sub
if x > 5
    print("big")
else
    print("small")
end
```

### 2.6 Loops

```sub
for i in range(5) {
    print(i)
}

for i in range(1, 10) {
    print(i)
}

var i = 0
while i < 5 {
    print(i)
    i = i + 1
}
```

### 2.7 Built-in Functions

| Function        | Description                    |
|-----------------|---------------------------------|
| `print(x)`      | Print value with newline        |
| `show(x)`       | Alias for `print()`             |
| `input(prompt)` | Read a line from stdin          |
| `str(x)`        | Convert to string                |
| `int(x)`        | Convert to integer               |
| `float(x)`      | Convert to float                 |
| `len(s)`        | String / array length            |
| `range(n)`      | Generate range `[0, n)`          |
| `range(a, b)`   | Generate range `[a, b)`          |

### 2.8 Embedding Foreign Code

```sub
embed c
    #include <math.h>
    double square(double x) { return x * x; }
endembed
```

`embed <lang> ... endembed` blocks are recognized by the parser for the
`c` and `cpp` targets specifically; any other language tag is accepted
lexically but passed through as an opaque code block rather than being
semantically understood.

## 3. Reserved Keywords

`var`, `const`, `let`, `function`, `return`, `if`, `elif`, `else`, `for`,
`while`, `do`, `end`, `break`, `continue`, `embed`, `endembed`, `true`,
`false`, `null`

The lexer also recognizes a larger set of keywords reserved for future use
(`class`, `try`/`catch`/`finally`/`throw`, `async`/`await`/`yield`, `ui`,
`extends`, `new`, `this`, etc.) — see [`ROADMAP.md`](ROADMAP.md) for status.

## 4. File Extension

SUB source files use the `.sb` extension.

## 5. Compiler Phases

1. **Lexical Analysis** — tokenization (`src/core/lexer.c`)
2. **Syntax Analysis** — parsing into an AST (`src/core/parser_enhanced.c`)
3. **Semantic Analysis** — type checking and validation (`src/core/semantic.c`)
4. **Code Generation** — target-specific output (`src/codegen/`), or direct
   tree-walking evaluation (`src/core/interpreter.c`) for `subi`

## 6. Supported Transpilation Targets

See the table in the main [README](../README.md#supported-transpilation-targets).

---
**Status**: Reflects the implementation as of this document's last edit.
