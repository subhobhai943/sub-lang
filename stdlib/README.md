# SUB Standard Library

```coffee
import "math"
import "arrays"
import "strings"
import "io"
import "path"
import "format"
import "testing"
import "encoding"
import "collections"
```

The original portable modules are written in SUB and produce identical output
through all the backends. The package-manager modules described below are
currently interpreter-oriented and compiled backends refuse their unsupported
dynamic/system operations explicitly.

The interpreter also provides the system layer needed by a future package
manager: `read_file`, `write_file`, `append_file`, `file_exists`, `is_dir`,
`mkdir`, `list_dir`, `remove`, `rename`, `env`, `args`, `exit`, and `exec`.
`exec(command)` invokes a shell and returns an object with `code` and `out`;
callers must not interpolate untrusted input into the command.

## json

`json_parse(text)` accepts JSON objects, arrays, strings, integers, floats,
booleans, and null. `json_stringify(value)` emits compact JSON. Invalid input
reports the byte position and exits with status 70. Unicode escapes above ASCII
are rejected explicitly because SUB currently has no character-from-codepoint
primitive.

## hash

`sha256(text)` is a runtime builtin exposed by this module. It returns a
lowercase hexadecimal SHA-256 digest and is implemented by the interpreter and
C backend. It is verified against the empty-string and `"abc"` FIPS 180-4
vectors.

## semver

`semver_parse`, `semver_compare`, and `semver_satisfies` support exact,
`^`, `~`, `>=`, `<`, and comma-joined ranges. Invalid versions are runtime
errors with the offending input.

## fs

`read_lines`, `write_lines`, `copy_file`, `ensure_dir`, and `walk` build on the
system filesystem builtins. `remove_tree` is recursive only within its
explicitly guarded relative path and refuses `/`, `..`, absolute paths, and
parent traversal segments.

Archive extraction is not yet exposed as a builtin: the current runtime does
not include a safe, cross-platform tar/gzip library, and this implementation
does not shell out to `tar` on untrusted archive paths.

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

```coffee
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
| `sign_float(x)`, `clamp_float(x, lo, hi)` | float variants of `sign`/`clamp` |
| `modulo(a, b)` | named modulo; `a % b` is preferred |
| `cbrt(x)` | cube root; works for negative inputs |

No trigonometry or logarithms: there are no `sin`/`log`/`exp` builtins to build
them on, and a series expansion in SUB would risk producing a different last
digit on different backends — which the conformance suite would (rightly)
reject.

## arrays

```coffee
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

```coffee
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
| `find(s, needle)` | index of the first occurrence, or `-1` |
| `starts_with(s, prefix)`, `ends_with(s, suffix)` | edges |
| `count_of(s, needle)` | how many non-overlapping times it occurs |
| `strip_prefix(s, p)`, `strip_suffix(s, x)` | remove if present |
| `truncate(s, width, ellipsis)` | cut to a width, marking that it was cut |
| `reverse(s)` | reversed |
| `capitalize(s)` | first letter up, the rest down |
| `title(s)` | every space-separated word capitalized |
| `words(s)` | space-separated words, empties dropped |
| `lines(s)` | split on newlines |
| `replace_first(s, old, new)` | only the first occurrence |
| `is_alpha(s)`, `is_digit(s)` | character classes |
| `is_numeric(s)` | digits, dots and a leading minus |
| `snake_case(s)`, `camel_case(s)` | case conversion |
| `slugify(s)` | URL/file-safe slug |
| `escape_html(s)` | `&amp;`, `&lt;`, `&gt;`, quotes |
| `indent(s, n)` | indent every line |
| `is_palindrome(s)` | reads the same backwards |
| `common_prefix(a, b)`, `common_suffix(a, b)` | longest shared edges |

This module used to stop at the padding functions. SUB's character-level
builtins — `substring`, `char_at`, `contains`, `replace`, `split`, `join` —
were implemented in the interpreter and unmapped in every compiled backend, so
anything built on them ran under `subi` and failed to compile under `sub` and
`subc`. They are wired into all ten backends now, which is what the second
half of this table is built from.

## io

```coffee
import "io"
```

| | |
|---|---|
| `red(s)` … `cyan(s)`, `bold(s)`, `dim(s)` | ANSI colour |
| `info(m)`, `ok(m)`, `warn(m)`, `fail(m)` | labelled lines |
| `heading(title)` | title with a rule under it |
| `row(cells, width)` | one aligned table row |
| `rule(columns, width)` | a rule the width of such a table |
| `progress(current, total, width)` | text progress bar |
| `plain(s)` | strip ANSI escape sequences |

Printing itself is a builtin (`print`, `println`); this is the layer above.
The colours are ANSI escapes — a terminal renders them, a pipe gets the bytes,
so `plain(s)` strips them when something else will read the output.

## path

```coffee
import "path"
```

| | |
|---|---|
| `path_join(parts)` | join with `/`, empty segments skipped |
| `dirname(p)` | everything before the last `/` |
| `basename(p)` | everything after the last `/` |
| `extension(p)` | the extension including the dot |
| `stem(p)` | the file name without its extension |
| `is_absolute(p)` | starts with `/` |
| `normalize(p)` | collapse `//`, drop `/./` and trailing slashes |

## format

```coffee
import "format"
```

| | |
|---|---|
| `fmt(template, values)` | replace `{0}`, `{1}`, ... with `values[0]`, ... |
| `pad_number(n, width)` | zero-pad an integer to a width |
| `repeat_char(ch, n)` | a character repeated n times |
| `table_row(cells, widths)` | one table row with per-column widths |
| `table_rule(widths)` | a horizontal rule matching `table_row` |

## testing

```coffee
import "testing"
```

| | |
|---|---|
| `assert(condition, msg)` | abort if falsy |
| `assert_eq(a, b, msg)` | abort if `a != b` |
| `assert_ne(a, b, msg)` | abort if `a == b` |
| `assert_true(val, msg)`, `assert_false(val, msg)` | boolean asserts |
| `test(name)` | print a test header |
| `summary()` | print the pass/fail tally |

A program that imports this module is its own test suite. An assertion failure
aborts the program, so the exit code is the verdict — no external runner needed.

## encoding

```coffee
import "encoding"
```

| | |
|---|---|
| `hex_encode(bytes)` | array of byte values → hex string |
| `hex_decode(s)` | hex string → array of byte values |
| `url_encode(s)` | percent-encode for a URL |

## collections

```coffee
import "collections"
```

| | |
|---|---|
| `set_union(a, b)`, `set_intersection(a, b)` | set algebra on arrays |
| `set_difference(a, b)`, `set_has(s, item)` | difference and membership |
| `peek(arr)`, `peek_front(arr)` | look without removing |

---

## Known limits

These are properties of the language and the backends, not of the library, and
each is why some obvious function is missing:

- **`str()` of an array** is interpreter-only; print arrays directly with
  `println(a)`.
- **No file or process access.** There are no builtins for either, so there is
  no `file` or `system` module. The versions that used to sit here called
  `open()` and `exec()`, which have never existed.
- **No higher-order functions**, so no `map`/`filter`/`reduce`.
- **No object key iteration.** SUB cannot list the keys of an object —
  `for k in obj` is a runtime error — so `collections` offers set algebra
  and stack/queue helpers only, with no `keys`/`values`/`merge`.
- **No char-from-code builtin**, so `encoding` offers `url_encode` but not
  `url_decode`: a percent escape cannot be turned back into its character
  in pure SUB.

## Writing your own module

A module is a `.sb` file. Anything it declares at the top level — functions,
`let`, `const` — becomes visible to whatever imports it.

```coffee
# geometry.sb
const TAU_OVER_4 = 1.5707963267948966

fn area_of_circle(r: float): float {
    return 3.141592653589793 * r * r
}
```

```coffee
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
