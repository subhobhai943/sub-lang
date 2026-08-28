# `src/native` — the x86-64 machine-code backend

This is what makes `subc` a compiler rather than a wrapper around one. It
turns a checked SUB AST into x86-64 instructions and writes a static ELF64
executable, in-process. No assembler, no linker, no C compiler, no libc.

```
   AST  ──▶  x64_codegen.c  ──▶  bytes  ──▶  elf64.c  ──▶  ./program
                  │
                  └── x64_runtime.c   (allocator, stdout, number formatting,
                                        string handling — also emitted as
                                        machine code)
```

## Files

| File | What it does |
|---|---|
| `x64.h` | Memory layout, register names, the encoder's public interface |
| `x64_emit.c` | Instruction encoder: roughly forty instructions, one function each |
| `x64_internal.h` | Shared context — call/data fixups, the runtime routine table |
| `x64_runtime.c` | The runtime, written as calls into the encoder |
| `x64_codegen.c` | AST walk that emits code |
| `elf64.c` | Writes the ELF header, two program headers, and the text |
| `native.h` | `native_compile()`, the one entry point |

## How a binary is laid out

```
0x400000  ELF header + 2 program headers        R|X
          entry stub      (point the heap at the BSS, call main, exit)
          runtime routines
          user functions
          __sub_main      (everything at the top level of the program)
          string literals
0x500000  heap pointer, formatting scratch, then 64 MB of bump heap   R|W
```

Everything is addressed with absolute 64-bit immediates rather than
RIP-relative displacements. Addresses inside the text are not known until the
whole thing is laid out, and patching an 8-byte immediate afterwards is a
memcpy, where patching a displacement means recomputing it against the end of
a variable-length instruction.

The second segment has `p_filesz` 0 and a large `p_memsz`, so the heap costs
nothing on disk and is faulted in lazily. Nothing is ever freed: SUB programs
are short-lived, and a bump pointer buys simplicity at a cost that has not
mattered yet.

## Calling convention

Deliberately not the System V ABI — both sides of every call are generated
here, so a simpler rule is worth more than compatibility with code that will
never be linked in:

- arguments in `RDI, RSI, RDX, RCX, R8, R9`; at most six
- **a double travels as its raw bit pattern in a general register**, so call
  sites look the same whatever the static types are
- result in `RAX`
- `RBX` and `R12`–`R15` are callee-saved

## Types

The backend compiles statically typed code. The types come from
`src/codegen/codegen_infer.c` — the same pass the C, Rust, Java, Go, Swift
and Kotlin backends use — so a program compiled here and the same program
transpiled elsewhere agree on what every expression is, and a fix to
inference fixes all of them at once.

An int or bool is `RAX` directly, a double is `RAX` holding its bits, a
string is a pointer into the bump heap. `null` in a numeric context is a
quiet NaN, matching what the statically typed transpiler targets emit.

## Code shape

One accumulator: every expression leaves its result in `RAX`, and a binary
operator evaluates its left side, pushes it, evaluates its right side, and
pops. Locals live in the frame at `[rbp - 8*(slot+1)]`.

This gives up perhaps 4x against `gcc -O2` and is still about 90x faster than
the interpreter. It was chosen because correctness is easy to see and adding
a construct never requires thinking about register allocation. Register
allocation is the obvious next thing to do here, not a thing that was skipped
by accident.

## Float formatting

`rt_f2s` reproduces printf's `%g`, because that is what the interpreter uses
and therefore what every backend has to match: six significant digits,
trailing zeros dropped, exponent form outside 1e-4 … 1e+6.

The value is scaled by a single power of ten (in chunks of 10^22, each of
which is exact) and the six digits are read out with integer division. The
scaling and rounding happen on the **x87 stack**, not in SSE: both round to
nearest with ties to even, but x87 carries a 64-bit mantissa where a double
carries 53, and those eleven bits decide the cases that land on a boundary.
In SSE, `2.305 * 25.010` scaled by 10^4 rounds to exactly 576480.5 and the
tie breaks down to 57.648; the wider intermediate sees that the product is
above the halfway point and prints 57.6481, as printf does.

Against glibc on 4000 random doubles — including subnormals and values near
the top of the exponent range — the output is identical.

## What is not here yet

Objects, classes, `input()`, string slicing, `try`/`catch`, and functions of
more than six parameters. These are reported by name through `nc_fail()` and
the driver falls back to the C backend, so an unimplemented construct is
never mis-compiled — it is declined, out loud.

Arrays, `switch`, `do`/`while`, `break`/`continue` and `for x in <array>` all
compile here now; the list above is what is left.

## Testing

`tests/native_backend.py` compiles and runs every conformance case with
`PATH` emptied and reads the ELF header to confirm there is no dynamic
segment. That is the part `tests/conformance.py` cannot check: conformance
compares output, and output looks the same whether or not gcc was involved.
