# Release Notes

## v1.0.8

The stable release of the v1.0.8 line. Most of the work since the betas is
in making one SUB program mean the same thing however you run it.

### `subc` compiles to machine code itself

On x86-64 Linux `subc` no longer needs a C compiler. It encodes x86-64
instructions, emits its own runtime and writes a static ELF64 executable, in
one process - no assembler, no linker, no libc. `examples/fibonacci.sb`
becomes a 3.6 KB static binary instead of a 16 KB dynamically linked one, and
compiles in about a millisecond rather than a hundred.

Programs using something the machine-code backend does not implement yet
(objects, classes, `input()`, string slicing, `try`/`catch`, `switch`,
`do`/`while`) still build through the C backend, and `subc` names the
construct that forced the fallback. `--native` refuses to fall back;
`--via-c` forces the old path.

### Arrays work everywhere

Literals, indexing, indexed assignment, `len`, `push`/`append`, `pop` and
`for x in a` now behave the same in all eleven executed backends. Before
this, arrays were broken in every one of them, each differently - C printed
the array's address, C++ declared `vector<string>` for a list of integers,
and Kotlin and Swift emitted nothing at all for an array literal.

### One answer per question

Several operations had a different answer per backend and now have one:

- **Division by zero** is a runtime error with exit status 70, for integers
  and floats alike. It used to raise SIGFPE in C, print `Infinity` in
  JavaScript, throw in Python and return 0 in the native backend.
- **Integer division** truncates toward zero and **float remainder** follows
  `fmod`, everywhere.
- **Floats print with six significant digits** (`printf`'s `%g`). Python,
  JavaScript, Ruby, Rust, Go and Java were printing every digit needed to
  round-trip a double.
- **Array indices** count from the end when negative, and going out of range
  is an error rather than a silent `null`.
- `break` and `continue` were silently dropped by six backends, turning a
  loop that terminates into one that does not.

### Fixed

- The interpreter segfaulted on `let b = a` followed by `push(b, x)` - two
  bindings shared one array and both freed it.
- The Windows build had never worked. `<windows.h>`, included for a single
  console call, declares a `TokenType` that collides with SUB's.
- `subc` refused every absolute path on Windows, because it built a shell
  command string and rejected the colon in a drive letter. It spawns the
  compiler directly now, so paths with spaces work too.
- Java declared SUB's 64-bit integers as `int`, and `for x in <collection>`
  had no case at all - the loop silently ran ten times over nothing.

### Verified

Fourteen conformance programs run under the interpreter and through eleven
implementations - the machine-code backend, the C backend, Python,
JavaScript, Ruby, C, C++, Rust, Go, Java and Kotlin - with the output and
exit status of each compared against the interpreter, on every push. Swift
joined that list in this release. Windows, macOS, and Linux on x86-64 and
arm64 all build clean.

### Components

- **sub**: transpiler (SUB to Python, JavaScript, C, C++, Rust, Go, Java,
  Kotlin, Swift, Ruby and more)
- **subc**: native compiler (machine code directly on x86-64 Linux, via a C
  compiler elsewhere)
- **subi**: tree-walk interpreter and REPL

### Supported Platforms

- Linux x86_64
- Linux arm64 (aarch64)
- macOS arm64 (Apple Silicon)
- Windows x86_64 (MSI installer)

---

## v1.0.8-alpha

### Changes
- **Windows installer**: Switched from a plain `.zip` archive to a proper `.msi` installer built with WiX Toolset v4.
  The installer registers all three tools system-wide and adds them to the system `PATH` automatically.
- **Linux packages**: Added native `.deb` package format for Debian, Ubuntu, and derivatives (x86_64 / arm64).
- Generic `.tar.gz` archives are still provided for all platforms as before.

### Components
- **sub**: Transpiler (SUB → Python, JavaScript, C, Go, Rust, and more)
- **subc**: Native compiler (SUB → machine binary via C backend)
- **subi**: Tree-walk interpreter (direct execution)

### Supported Platforms
- Linux x86_64
- Linux arm64 (aarch64)
- macOS arm64 (Apple Silicon)
- Windows x86_64 (MSI installer)

---

## v1.0.8-beta

### Changes
- **Removed macOS x86_64 (Intel) builds** — GitHub Actions no longer provides free Intel macOS runners. Only Apple Silicon (arm64) builds are shipped going forward.

### Components
- **sub**: Transpiler (SUB → Python, JavaScript, C, Go, Rust, and more)
- **subc**: Native compiler (SUB → machine binary via C backend)
- **subi**: Tree-walk interpreter (direct execution)

### Supported Platforms
- Linux x86_64
- Linux arm64 (aarch64)
- macOS arm64 (Apple Silicon)
- Windows x86_64 (MinGW)
