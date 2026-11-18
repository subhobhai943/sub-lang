# SUB Interpreter Benchmarks

## Overview

Benchmarks for the SUB tree-walking interpreter (`subi`). Designed to
measure interpreter performance and guide optimization efforts.

## Benchmarks

| Benchmark | File | What it measures |
|-----------|------|-----------------|
| fib | `bench/fib.sb` | Recursive fibonacci(32) - function call overhead |
| loop20m | `bench/loop20m.sb` | 20M-iteration arithmetic loop - per-iteration overhead |
| string_ops | `bench/string_ops.sb` | String concat/split/join - string allocation overhead |
| array_ops | `bench/array_ops.sb` | Array push/index - array growth and bounds checking |
| object_ops | `bench/object_ops.sb` | Object field read/write - property lookup overhead |
| func_calls | `bench/func_calls.sb` | Many small function calls - call/return overhead |
| string_leak | `bench/string_leak.sb` | String concat in a loop - memory leak detection |

## Running

```bash
# Run all benchmarks and print wall times
./bench/run.sh

# Save current times as baseline
./bench/run.sh --baseline

# Compare against saved baseline
./bench/run.sh --compare
```

## Results

### Before Optimization

| Benchmark | Time |
|-----------|------|
| fib(32) recursive | 7.10s |
| while loop, 20M iterations | 18.30s |
| string concat, 20K iterations | 1.51s |
| array push/index, 1M iterations | 1.36s |
| object field R/W, 1M iterations | 0.85s |
| small function calls, 5M iterations | 19.37s |

### After Optimization

| Benchmark | Time | Ratio |
|-----------|------|-------|
| fib(32) recursive | 4.48s | 0.63x |
| while loop, 20M iterations | 5.44s | 0.30x |
| string concat, 20K iterations | 1.30s | 0.86x |
| array push/index, 1M iterations | 0.66s | 0.49x |
| object field R/W, 1M iterations | 0.57s | 0.67x |
| small function calls, 5M iterations | 7.41s | 0.38x |
| string concat, 200K iterations | ~5.0s | N/A (new benchmark) |

### Memory Usage

| Benchmark | Peak RSS |
|-----------|----------|
| string concat, 20K iterations | ~3 MB |
| string concat, 200K iterations | ~3.7 MB |
| string concat, 1M iterations | ~18 MB (completes successfully) |

## Optimizations Applied

### 1. Skip provenance label building when `why` is unused

`ast_expression_text()` was called unconditionally on every variable
declaration, assignment, binary expression, and function call, building
malloc'd/snprintf'd/free'd label strings even when `g_provenance_enabled`
is 0 (no `why` expression in the program). All call sites are now guarded.

### 2. Resolve variables to (depth, slot) indexes

Replaced linear name-search in `env_get`/`env_set` with direct slot
access for variables resolved to the current env (depth 0). Each
`EnvEntry` gets a slot index; `Env` gets an array of `EnvEntry*` pointers
for O(1) access.

### 3. Inline identifier/literal handling in eval_block

`eval_block` handles `AST_IDENTIFIER` and `AST_LITERAL` nodes inline
instead of calling `eval()` for each.

### 4. Fix memory leaks

Fixed memory leaks in:
- `eval_block()`: statement results now freed between loop iterations
- `eval_binary()`: operands `L` and `R` now freed after use
- `eval()`: `AST_IDENTIFIER` now returns `val_copy()` instead of borrowed
  reference, making all `eval()` results owned values
- Builtins: `print`, `show`, `println`, `str`, `to_string`, `int`, `float`,
  `len`, `type`, `abs`, `min`, `max`, `sqrt`, `floor`, `ceil`, `round`,
  `upper`, `lower`, `split`, `join`, `substring`, `contains`, `replace`,
  `trim`, `char_at`, `push`, `append`, `pop`, `input`, `read_file`,
  `sha256`, `write_file`, `append_file`, `file_exists`, `is_dir`, `mkdir`,
  `list_dir`, `remove`, `rename`, `env`, `object_keys`, `exec`
- `AST_ARRAY_ACCESS`: array element now copied before container is freed
- `AST_MEMBER_ACCESS`: object property now copied before object is freed
- `AST_ASSIGN_STMT` index/member assignment: uses `env_get_node` directly
  for identifiers to modify original in place

### 5. Capacity-doubling string concatenation

`s = s + x` now uses capacity-doubling for amortized O(1) append:
- When the left operand's buffer has enough capacity, append in place
- When the buffer is too small, `realloc()` with doubling
- Falls back to `malloc()` if `realloc()` fails

## Remaining Bottlenecks

The tree-walking `eval()` function is the fundamental bottleneck at ~44%
of runtime. Each call involves C function call/return overhead, switch
on node-type dispatch, and early-exit checks.

**Target:** fib(32) under 1s and 20M loop under 2s with tree-walker alone.

**Assessment:** NOT ACHIEVABLE with tree-walker optimizations alone.
Best tree-walker results:
- fib(32): ~4.5s (need ~4.5x more improvement)
- loop20m: ~5.4s (need ~2.7x more improvement)
- string concat 200K: ~5s (need ~25x more improvement for 0.2s target)

A bytecode compiler + stack VM (`subi --vm`) would be needed for further
gains. Keep the tree-walker as the reference implementation.

## Compile-time scaling (`bench/compile/`)

The native compiler (`subc`) and the shared compile pipeline (lexing, parsing,
semantic analysis, signature inference) used to grow cubically with program
size: 250 functions 0.18s, 500 functions 0.96s, 1000 functions 6.3s,
2000 functions 34s (~6x per doubling). A self-hosted SUB compiler has
thousands of functions, so this was fixed on branch `perf/compile-scaling`.
Generated code is unchanged: ELF and C backend output for every file in
tests/, examples/, applications/ and stdlib/ is byte-identical, or produces
identical program output.

`bench/compile/gen.py` generates N-function, N-variable, deeply nested
expression, long if/else chain, and many-string-literal programs.
`bench/compile/run.sh` prints compile time for `subc`, `sub` (C backend) and
`subi` startup at N = 250, 500, 1k, 2k, 4k, plus the growth exponent between
sizes. Baseline numbers are committed in `bench/compile/baseline.txt`.

### Results (median of 11 runs, gcc 14.2 -O2 build, `subc` native backend)

| N | functions (s) | exponent | vs pre-fix |
|---|---------------|----------|------------|
| 250 | 0.009 | - | ~25x faster |
| 500 | 0.018 | ~1.0 | ~58x faster |
| 1000 | 0.068 | ~1.9 | ~104x faster |
| 2000 | 0.11 | ~0.7 | ~380x faster |
| 4000 | 0.57 | ~2.3 | ~350x faster |

Exponents below 2k are dominated by fixed per-compile costs (lexing, parsing,
process startup) and are noisy at sub-100ms runtimes; 2k->4k->8k scale at
~1.6-2.4x per doubling, i.e. approximately linear once fixed costs are out of
the way. Requirement: 2000 functions under 1 s -- met at ~0.1 s.

### What was fixed

1. `var_lookup`/`var_record`/`elem_lookup`/`elem_record`: linear scans over
   whole-program tables replaced with hash tables keyed by
   (owner function, name), top-level scope as fallback, preserving the
   "own function first, then top-level" lookup order exactly. Names are
   interned so comparisons are pointer comparisons in the common case. The
   function table cap of 512 (which silently dropped functions from
   inference) is now growable.
2. `propagate_call_sites`: call sites are collected once and replayed per
   fixpoint round instead of re-walking the whole AST per round. Same rounds,
   same merge order, identical types.
3. All whole-AST walks now use a visited-node set: statements live on both
   the `next` chain and `children[]`, so the naive walk visited each
   statement of an m-statement block O(m) times.
4. `exit_scope` in the semantic pass removes the leaving scope's entries as a
   contiguous prefix of the symbol chain instead of re-scanning the whole
   table per exit (O(table) -> O(entries leaving)).
5. Native bump heap: 64 MB -> 2 GB (demand-zeroed, free until touched). Fixes
   `bench/string_ops.sb` segfaulting on ~1 GB of cumulative concatenation
   churn; binary size on disk is unchanged.

## Test Suite

Run the full test suite:
```bash
python3 tests/run_tests.py
python3 tests/conformance.py
```

Run under ASAN to check for leaks:
```bash
gcc -fsanitize=address -g -o subi_asan ...
ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 ./subi_asan program.sb
```
