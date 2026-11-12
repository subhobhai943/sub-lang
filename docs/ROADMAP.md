# SUB Roadmap — Not Yet Implemented

The lexer already reserves keywords for a much larger language than what the
parser and code generators currently support. This document is the home for
those aspirational ideas so [`LANGUAGE_SPEC.md`](LANGUAGE_SPEC.md) only
describes what actually works today. Nothing below should be assumed to
compile.

## Type annotations & generics

```coffee
var price:float = 19.99
function identity<T>(value:T):T
    return value
end
```

## Classes / OOP

`class` now parses and runs: field declarations, `ClassName.new` instances,
member and index reads/writes (see the README's Classes & Objects section)
work through `subi`, both `subc` backends, and the python/javascript/ruby
transpilers. Still aspirational: methods in class bodies, `extends`,
`implements`, `this`, `super`, `static`, `private`/`public`/`protected` -
the lexer reserves all of them, and the interpreter reports a clear error
for a method inside a class body rather than silently dropping it.

## Error handling

```coffee
try
    var result = divide(10, 0)
catch DivisionError as e
    print("Error: " + e.message)
finally
    print("Cleanup complete")
end

throw CustomError("Something went wrong")
```

## Async

`async`, `await`, `yield` are reserved but unimplemented.

## Ternary operator, C-style `for`, `do...while`

```coffee
var result = condition ? value1 : value2
for (i = 0; i < 10; i++) { print(i) }
do { print(i) } while condition
```

## UI DSL

```coffee
ui.window(title="App", width=800, height=600)
    ui.button(text="Click", onclick=handler)
end
```

## Foreign Function Interface (FFI)

```coffee
ffi
    library: "mylib.so"
    function calculateSum(a:int, b:int):int
end
```

## Inline assembly / SIMD / parallel loops

```coffee
asm x86_64
    mov rax, 1
endasm

var result = simd.add(vec1, vec2)

parallel for i in range(1000) {
    process(i)
}
```

## Embedding beyond C/C++ (removed)

Embed blocks (`embed <lang> ... endembed`) have been removed from the
language. The lexer may still tokenize the words as a legacy detail, but the
feature is no longer part of SUB and should not be used.

## Build config / dependency manifests

```coffee
config
    name: "MyApp"
    targets: ["android", "ios", "web"]
end

dependencies
    "math-lib": "^2.0.0"
end
```

## Memory management primitives

`allocate`, `malloc`, `free` are documented ideas, not implemented builtins.

## Known bugs tracked here (not roadmap items, but adjacent)

- The parser does not treat parse errors as fatal: a file with syntax
  errors (e.g. `class` usage) can still report "Compilation successful"
  and produce output. This should eventually cause a non-zero exit.
- Several `stdlib/*.sb` modules (`io.sb`, `math.sb`, `string.sb`,
  `collections.sb`, `file.sb`, `system.sb`) currently fail semantic
  analysis when run through `sub`/`subc` directly (undefined functions,
  type errors) — see the `non_blocking` exclusion in
  `.github/workflows/code-quality-bot.yml`.
- ~~The `cpp`/`c++` transpile target produced invalid, non-compiling
  output~~ — fixed: `sub.c`/`sub_native.c` now dispatch to
  `codegen_cpp_generate()` (`src/codegen/codegen_cpp.c`), a real C++
  backend, instead of the old `codegen_generate_cpp()` in `codegen.c`,
  which reused the C code generator inside a C++ file wrapper.
- `show()`, documented in the README as an alias for `print()`, is
  rejected outright by semantic analysis (`src/core/semantic.c`'s builtin
  whitelist only recognizes `print`/`println`) — it never reaches codegen.
  Even if the semantic check were relaxed, every codegen backend
  (`codegen.c`, `codegen_multilang.c`, `codegen_cpp.c`) special-cases the
  literal string `"print"` only, so `show(...)` would transpile to a
  call to an undefined `show` function in every target language. Fixing
  this requires touching the builtin whitelist plus every codegen
  backend's print-detection branch. Until fixed, use `print()`.
