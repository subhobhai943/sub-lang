# Memory Management in the SUB Interpreter

## Ownership Model

The SUB interpreter uses an **ownership model** for `SubVal` heap-allocated data
(strings, arrays, objects). Every heap-allocated `SubVal` has exactly one owner
at any time. The owner is responsible for calling `val_free()` when the value
is no longer needed.

### Ownership Rules

1. **`eval()` returns owned values**: Every call to `eval()` returns a `SubVal`
   that the caller owns. The caller must call `val_free()` on it when done,
   unless it transfers ownership to another structure (e.g. `env_define`,
   `array_push`, `object_set`).

2. **`env_get()` / `env_get_node()` return borrowed references**: These return a
   reference to a value stored in an environment. The caller must NOT free the
   returned value; it remains owned by the environment.

3. **`env_define()` / `env_set_node()` take copies**: These call `val_copy()` on
   the value being stored, so the caller retains ownership of the original and
   must free it separately.

4. **`eval_binary()` returns owned values**: The result of `eval_binary()` is
   an owned value. Operands `L` and `R` are freed inside `eval_binary()` after
   use (since `eval()` returns owned values for both).

5. **`array_push()` and `object_set()` take ownership**: The array/object takes
   ownership of the pushed/stored value. The caller must NOT free it
   separately.

6. **`array_pop()` and `object_get()` return borrowed references**: These return
   a reference to a value stored in an array/object. The caller must NOT free
   the returned value.

### Key Functions

| Function | Ownership semantics |
|----------|-------------------|
| `eval()` | Returns owned value; caller frees |
| `env_get()` | Returns borrowed reference; do not free |
| `env_get_node()` | Returns borrowed reference; do not free |
| `env_define()` | Copies value via `val_copy()`; caller frees original |
| `env_set_node()` | Copies value via `val_copy()`; caller frees original |
| `eval_binary()` | Returns owned value; frees operands internally |
| `array_push()` | Takes ownership of item; caller does not free |
| `array_pop()` | Returns borrowed reference; do not free |
| `object_set()` | Copies value via `val_copy()`; caller frees original |
| `object_get()` | Returns borrowed reference; do not free |
| `val_copy()` | Returns deep copy; caller frees |
| `val_free()` | Frees heap-allocated data in a SubVal |

## Provenance Tracking

The interpreter optionally tracks value provenance (provenance of where each
value came from) when `g_provenance_enabled` is set (i.e. when the program
contains a `why` expression). Provenance is a tree of `SubProvenance` nodes
with reference counting.

### Provenance Rules

1. **`provenance_attach()` creates a new provenance node**: It takes ownership
   of the `a` and `b` parent provenances (via `provenance_retain`) and assigns
   the new node to `v->provenance` (replacing any previous provenance).

2. **`val_free()` releases provenance**: It calls `provenance_release()` on the
   value's provenance, which decrements the refcount and frees the node when
   the count reaches zero.

3. **Provenance attach must happen before freeing operands**: In
   `eval_binary()`, `provenance_attach()` is called before `val_free(L)` and
   `val_free(R)`, so the parent provenances are retained before the operands
   are freed.

## Common Pitfalls

### Double-free

A double-free occurs when `val_free()` is called twice on the same heap-allocated
data. This can happen when:
- Two `SubVal` structs share the same `sv` pointer (shallow copy instead of
  `val_copy()`), and both are freed.
- A value is returned from a function AND also stored in an array/object
  without copying, and both are freed.

**Fix**: Always use `val_copy()` when you need an independent copy, and ensure
only one owner calls `val_free()`.

### Use-after-free

A use-after-free occurs when a pointer is accessed after the memory it points
to has been freed. This can happen when:
- Returning `container.arr->items[i]` from `AST_ARRAY_ACCESS` without first
  copying it, then `val_free(container)` frees the array and all its items.
- Returning `object_get(obj.obj, member)` from `AST_MEMBER_ACCESS` without
  first copying it, then `val_free(obj)` frees the object and all its values.

**Fix**: Use `val_copy()` on the value before freeing the container:
```c
SubVal result = val_copy(container.arr->items[i]);
val_free(container);
return result;
```

### Memory leaks

A memory leak occurs when heap-allocated data is never freed. Common sources:
- `eval()` results not freed by `eval_block()` between loop iterations.
- `eval_binary()` operands `L` and `R` not freed.
- Builtin arguments not freed after use.
- `AST_IDENTIFIER` returning borrowed references that are never tracked.

**Fix**: Ensure every `eval()` result is either freed or transferred to an
owner (environment, array, object) that will free it eventually.

## String Concatenation Performance

String concatenation (`s = s + x`) uses **capacity-doubling** for amortized
O(1) performance:

1. When the left operand is a string with sufficient buffer capacity, the
   right operand is appended in place via `memcpy()`.
2. When the buffer is too small, `realloc()` doubles the capacity.
3. When reallocation fails, a fresh `malloc()` buffer is allocated.

This avoids the O(n²) behavior of allocating a new buffer of exactly the right
size on every append.

## Testing

Run the interpreter under AddressSanitizer to detect leaks and use-after-free:

```bash
gcc -fsanitize=address -g -o subi_asan ...
ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 ./subi_asan program.sb
```

Run the full test suite:
```bash
python3 tests/run_tests.py
```

Run benchmarks:
```bash
./bench/run.sh
```
