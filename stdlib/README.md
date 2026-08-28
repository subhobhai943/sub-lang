# SUB Standard Library

```sub
import "math"
import "arrays"
import "strings"
import "io"
```

Four modules, written in SUB. Every function here produces identical output
through all ten backends and the interpreter — `tests/conformance/19_stdlib.sb`
and `20_stdlib_tour.sb` check exactly that on every push, the same way the
language itself is checked.

Run `examples/stdlib_tour.sb` to see the whole library working:

```console
$ subi examples/stdlib_tour.sb
$ sub  examples/stdlib_tour.sb rust    # or any other target
```

---

## How `import` finds a module

An import is resolved and spliced at parse time: the module is read, parsed,
and its top-level statements are inserted into your program. What each backend
compiles is one flat program, so a module's functions are ordinary functions
and its `let` is an ordinary global.

Search order:

1. **Next to the file doing the importing** — your own helpers work with no
   configuration.
2. **`$SUB_PATH`** — colon-separated (semicolon on Windows).
3. **`stdlib/` beside the executable** — how the release tarball is laid out.
4. **`../lib/sub/stdlib`** relative to the executable — for a prefix install.

The `.sb` extension is optional: `import "math"` and `import "math.sb"` are the
same. Importing one module twice splices it once, so two modules that both want
`strings` is fine. An import cycle is reported rather than followed.

A module that cannot be found lists every path it tried.

---

## math

```sub
import "math"
```

| | |
|---|---|
| `PI`, `E`, `TAU` | constants |
| `sign(x)` | `-1`, `0` or `1` |
| `clamp(x, lo, hi)` | confine to a range |
| `is_even(n)`, `is_odd(n)` | parity |
| `gcd(a, b)`, `lcm(a, b)` | Euclid; negatives give a positive result |
| `factorial(n)` | `factorial(0)` is 1; overflows past 20 |
| `fib(n)` | `fib(0)` is 0 |
| `is_prime(n)` | trial division to the square root |
| `ipow(base, exp)` | integer power, for when the exponent is a value |
| `hypot(a, b)` | `sqrt(a² + b²)` |
| `deg_to_rad(d)`, `rad_to_deg(r)` | angle conversion |
| `close_to(a, b, tol)` | compare floats without `==` |
| `round_to(x, places)` | round to a number of decimal places |

No trigonometry or logarithms: there are no `sin`/`log`/`exp` builtins to build
them on, and a series expansion in SUB would risk producing a different last
digit on different backends — which the conformance suite would (rightly)
reject.

## arrays

```sub
import "arrays"
```

| | |
|---|---|
| `sum_int(a)`, `sum_float(a)` | total |
| `product_int(a)` | product |
| `mean_float(a)` | mean; `0.0` for an empty array |
| `min_int(a)`, `max_int(a)`, `min_float(a)`, `max_float(a)` | extremes |
| `index_of_int(a, v)` | position, or `-1` |
| `has_int(a, v)`, `count_int(a, v)` | membership and tally |
| `reverse_int(a)` | reversed copy |
| `slice_int(a, start, stop)` | `[start, stop)`, bounds clipped |
| `concat_int(a, b)`, `repeat_int(v, n)` | build |
| `sorted_int(a)` | ascending copy; the input is left alone |
| `unique_int(a)` | distinct, in first-seen order |
| `is_sorted_int(a)` | check |

**Why `_int` and `_float` rather than one `sum`.** SUB has no generics, and
six of the ten backends must emit one concrete signature per function. A single
`sum()` returning an integer for one caller and a float for another does not
compile to Rust, Go, Java, Kotlin, Swift or C++. Splitting them is what makes
the library work everywhere rather than only under the interpreter.

## strings

```sub
import "strings"
```

| | |
|---|---|
| `is_empty(s)`, `is_blank(s)` | emptiness |
| `repeat(s, n)` | `s` written `n` times |
| `eq_ignore_case(a, b)` | compare ignoring case |
| `pad_left(s, w, pad)`, `pad_right(s, w, pad)` | pad to a width; never truncates |
| `center(s, w, pad)` | centre, odd column to the right |
| `line(s, n)` | a separator |

**This module is small on purpose.** SUB's character-level string builtins —
`substring`, `char_at`, `contains`, `replace`, `split`, `join` — are
implemented in the interpreter and are unmapped or unimplemented in every
compiled backend. Anything built on them would run under `subi` and fail to
compile under `sub` and `subc`, which is precisely the trap the previous
version of this library fell into.

What *is* portable is `len`, `upper`, `lower`, `trim`, `+` and comparison, so
that is what this module is built from. When those builtins are wired into the
backends, `find`, `starts_with` and `split_on` belong here.

## io

```sub
import "io"
```

| | |
|---|---|
| `red(s)` … `cyan(s)`, `bold(s)`, `dim(s)` | ANSI colour |
| `info(m)`, `ok(m)`, `warn(m)`, `fail(m)` | labelled lines |
| `heading(title)` | title with a rule under it |
| `row(cells, width)` | one aligned table row |
| `rule(columns, width)` | a rule the width of such a table |

Printing itself is a builtin (`print`, `println`); this is the layer above.
The colours are ANSI escapes — a terminal renders them, a pipe gets the bytes,
so write plain output when something else will read it.

---

## Known limits

These are properties of the language and the backends, not of the library, and
each is why some obvious function is missing:

- **No character-level string work outside the interpreter** (above). This is
  the biggest gap.
- **`str()` of an array** is interpreter-only; print arrays directly with
  `println(a)`.
- **No file or process access.** There are no builtins for either, so there is
  no `file` or `system` module. The versions that used to sit here called
  `open()` and `exec()`, which have never existed.
- **No higher-order functions**, so no `map`/`filter`/`reduce`.

## Writing your own module

A module is a `.sb` file. Anything it declares at the top level — functions,
`let`, `const` — becomes visible to whatever imports it.

```sub
# geometry.sb
const TAU_OVER_4 = 1.5707963267948966

fn area_of_circle(r: float): float {
    return 3.141592653589793 * r * r
}
```

```sub
import "geometry"
println(area_of_circle(2.0))
```

Two things are worth doing, and the modules here do both:

**Annotate the types.** `fn pad_left(s: string, width: int, pad: string): string`
rather than bare parameters. The statically typed backends have to name a type
for every parameter and return; an annotation settles it, and an explicit type
is never overridden by inference. Array parameters cannot be annotated — SUB
has no `array` type keyword — and are inferred from use.

**Keep each function's types consistent.** One function, one signature.
