# Contributing to SUB

Thanks for wanting to work on this. This page is what the project actually
looks like today, so you can get a change reviewed without guessing.

---

## The one rule that matters

**The interpreter is the specification.** `subi` defines what a program prints
and what status it exits with. Every other backend has to agree with it, and
`tests/conformance.py` is what holds them to it: it runs each program in
`tests/conformance/` under the interpreter, then transpiles, builds and runs
the same program through every target whose toolchain is installed, and diffs
the output and the exit status.

That has two consequences worth internalising before you start:

- **A change to one backend is almost never finished.** If you fix how Go
  spells something, ask what Rust, Swift, Kotlin and the machine-code backend
  do with the same program. The suite will tell you, but it is faster to think
  about it first.
- **A bug in the interpreter makes all ten backends agree on the wrong
  answer** and the suite still passes. Interpreter behaviour has to be checked
  against something independent — what C, Python and Ruby actually do, or a
  value you worked out by hand. This has bitten us: `split("a,,b", ",")`
  returned two fields for months because nothing outside the interpreter could
  run `split` at all.

---

## Getting set up

```bash
git clone https://github.com/subhobhai943/sub-lang.git
cd sub-lang
make            # builds sub, subc and subi
```

You need a C compiler (GCC or Clang) and Make. Nothing else is required to
build or to run the machine-code backend.

The three tools:

| | |
|---|---|
| `subi` | interprets a `.sb` file, or starts a REPL with no arguments |
| `subc` | compiles to a native binary — `--native` emits x86-64 machine code itself, `--via-c` goes through your C compiler |
| `sub`  | transpiles to another language: `python`, `javascript`, `typescript`, `java`, `c`, `cpp`, `rust`, `go`, `swift`, `kotlin`, `ruby`, `assembly`, `css`, plus the `android` / `ios` / `web` / `windows` / `macos` / `linux` platform targets |

Run `./sub --help`, `./subc --help` or `./subi --help` for the current list.

---

## Testing

```bash
python3 tests/run_tests.py            # the tools start, build and run
python3 tests/conformance.py          # every target whose toolchain is installed
python3 tests/conformance.py c rust   # just these
python3 tests/grammar_keywords.py     # the editor grammar matches the lexer
```

`conformance.py` skips a target whose compiler is not installed and says so.
You do not need all twelve locally — CI runs the full set — but **run what you
have** before opening a pull request, and say in the PR which targets you
could not run.

### Adding a conformance case

Drop a `.sb` file into `tests/conformance/`, named `NN_topic.sb`. There is no
expected-output file: the interpreter produces it. Aim each case at behaviour
that could plausibly differ between backends — integer division, float
formatting, string edges, out-of-range indices — rather than at whether the
language runs at all.

A program that stops with a runtime error is a legitimate case. Every backend
has to agree on the error too: the interpreter exits 70, and so must the rest,
with the same output before the error. `11_division_by_zero.sb`,
`14_array_bounds.sb` and `22_char_at_range.sb` are the examples to copy.

---

## Where things live

```
src/core/        lexer, parser, semantic analysis, the interpreter, modules
src/codegen/     the transpiling backends and the shared type inference
src/native/      the x86-64 machine-code backend and its ELF writer
src/compilers/   the sub / subc / subi front ends
stdlib/          the standard library, written in SUB
tests/conformance/   the differential suite
.vscode-extension/   the editor grammar and snippets
```

Two files carry more weight than their size suggests:

- **`src/codegen/codegen_infer.c`** — the shared type inference. Every typed
  backend asks it what an expression is and what an array holds. Most
  "backend X emits the wrong type" bugs are really bugs here, and fixing them
  here fixes them everywhere at once.
- **`src/core/parser_enhanced.c`** — among other things, it rewrites the
  method spelling of a builtin (`s.substring(0, 3)`) into the function
  spelling (`substring(s, 0, 3)`), so everything downstream sees one shape.
  Adding a builtin with a method spelling means adding its name to that list,
  not adding a branch to ten backends.

---

## Adding a builtin

This is the most common non-trivial contribution, and the order matters:

1. **Implement it in the interpreter** (`src/core/interpreter.c`), in both the
   function and method spellings, and work out the edge cases deliberately —
   negative indices, empty arguments, out of range. Whatever you decide is now
   the specification.
2. **Declare its type** in `src/core/semantic.c` (so it is not "Undefined
   function") and in the table at the top of `src/codegen/codegen_infer.c` (so
   the typed backends know what it returns).
3. **Write the conformance case first**, and watch it fail on nine backends.
4. **Implement it in each backend**, running the case as you go.
5. If it has a method spelling, add its name to `is_builtin_method()` in the
   parser.

Do not reach for the target language's obvious spelling without checking it
against the interpreter. Python's `s[a:b]` counts a negative start from the
end where SUB clamps it to 0; `str.replace` with an empty pattern inserts
between every character where SUB returns the string unchanged; Ruby's
`split` drops trailing empty fields; C's `strtok` treats the separator as a
set of characters. Each of those was a real bug. Every backend calls a helper
in its own prelude for exactly this reason.

---

## Adding a backend

There is no plugin interface — a backend is a function that walks the AST and
appends to a `StringBuilder`. The shortest path:

1. Read `src/codegen/codegen_cpp.c`. It is the smallest complete backend and
   shows the shape: a builtin-spelling table, a type mapper, an expression
   generator, a statement generator, and a runtime prelude.
2. Add your generator, and a case for the target name in
   `src/compilers/sub.c`.
3. Add the target to `TARGETS` in `tests/conformance.py` with its file
   extension, the tool it needs, and how to build and run it.
4. Get all of `tests/conformance/` passing. Expect this to be the bulk of the
   work — the existing suite is dense with the cases that break backends.

---

## Style

Match the file you are editing. Beyond that:

- 4 spaces, no tabs. K&R bracing. `snake_case` functions, `PascalCase` types,
  `UPPER_SNAKE_CASE` constants.
- **Comments say why, not what.** The codebase is full of notes explaining
  which bug a line prevents, and they are the most valuable thing in it. If
  you fix something subtle, leave the reason behind: the next person will
  otherwise "simplify" it straight back.
- The build must stay warning-free. CI compiles with `-Wall -Wextra` and
  fails on new warnings.
- No trailing whitespace, and no reformatting of code you did not change.

---

## Pull requests

Branch off `main`, and in the description say:

- what changed and why,
- which targets you ran locally and which you could not,
- any behaviour that is now different, however small.

CI runs on every pull request: Linux (gcc and clang, x86-64 and arm64), macOS
arm64, Windows via mingw, the full twelve-target conformance sweep, the MSI
build, and the syntax-highlighting checks. All of it has to be green.

Two things CI catches that local testing usually does not:

- **Windows is LLP64** — `long` is 32 bits there and a pointer is 64. Casting
  a pointer through `long` works everywhere else and silently truncates on
  Windows. Anything stored in a SUB array goes through
  `(long long)(intptr_t)`.
- **Swift and Kotlin** are only in the full conformance job. If you do not
  have those toolchains, say so in the PR rather than claiming a clean run.

---

## Reporting a bug

Open an issue with the `.sb` program that shows it, what you ran
(`subi` / `subc` / `sub <target>`), what you expected, what happened, and your
OS and version (`./subi --version`). A backend disagreeing with the
interpreter is always a bug — say which backend, and the suite will usually
reproduce it in one case.

For anything with a security dimension, see [SECURITY.md](SECURITY.md)
instead.

---

## Things that need doing

Currently open, roughly in order of how much they would help:

- **A language server.** Design and staging are settled; the first step is
  turning diagnostics into data rather than text on stderr, which is worth
  doing on its own.
- **`str()` of an array** is still interpreter-only.
- **Error line numbers from imported modules** are ambiguous with the main
  file's, because a spliced module's nodes carry only a line.
- **The machine-code backend** does not yet do objects, classes, `input()`,
  `try`/`catch`, iterating a string, or functions of more than six
  parameters. Programs using them fall back to the C backend, and `subc` names
  what forced the fallback.

Ask on an issue before starting something large, so two people do not write
the same backend.
