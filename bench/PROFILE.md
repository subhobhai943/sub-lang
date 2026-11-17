# Interpreter Performance Profile

## Methodology

Built `subi` with `gcc -O2 -pg` (gprof) and ran each benchmark. gprof samples at 10ms intervals.

## Baseline Results (before optimization)

| Benchmark | Time | Notes |
|-----------|------|-------|
| fib(32) recursive | 7.10s | 35M eval calls |
| while loop, 20M iterations | 18.30s | ~915 ns/iter |
| string concat, 20K iterations | 1.51s | |
| array push/index, 1M iterations | 1.36s | |
| object field R/W, 1M iterations | 0.85s | |
| small function calls, 5M iterations | 19.37s | |

## Top Hotspots (loop20m, gprof)

| Function | % self time | self seconds | calls | Analysis |
|----------|-------------|--------------|-------|----------|
| `eval` | 57.0% | 2.65s | 120M | Tree-walk dispatch overhead — fundamental to interpreter |
| `eval_binary` | 19.3% | 0.90s | 60M | Arithmetic + comparison ops |
| `env_set` | 6.0% | 0.28s | 40M | Variable assignment — linear name search |
| `ast_expression_text` | 5.3% | 0.25s | 40M | **WASTED**: builds provenance label strings even when `why` is unused |
| `env_get` | 3.9% | 0.18s | 80M | Variable lookup — linear name search |
| `val_copy` | 3.0% | 0.14s | 40M | Value copying on assignment |
| `provenance_release` | 2.2% | 0.10s | 40M | **WASTED**: provenance refcounting when `why` is unused |

## Top Hotspots (fib(32), gprof)

| Function | % self time | self seconds | calls | Analysis |
|----------|-------------|--------------|-------|----------|
| `eval` | 47.5% | 0.85s | 35M | Tree-walk dispatch overhead |
| `eval_binary` | 15.1% | 0.27s | 17.6M | Arithmetic + comparison |
| `ast_expression_text` | 13.4% | 0.24s | 7M | **WASTED**: provenance label building |
| `env_get` | 9.5% | 0.17s | 24.7M | Variable lookup — linear name search |
| `val_copy` | 3.9% | 0.07s | 17.6M | Value copying |
| `env_define` | 2.2% | 0.04s | 7M | Env entry allocation |

## Planned Optimizations (in order)

1. **Skip provenance label building when `why` is unused** (`ast_expression_text` called unconditionally on every assignment) — ~5% savings. Already confirmed: `g_provenance_enabled` is checked inside `provenance_attach` but `ast_expression_text` is called *before* that check.

2. **Skip provenance_release when `why` is unused** — ~2% savings. `val_free` calls `provenance_release` unconditionally.

3. **Resolve variables to (depth, slot) indexes** — replace linear name-search in `env_get`/`env_set` with array indexing. ~10% savings. Requires one-time pass after parsing.

4. **Builtin dispatch: cache resolved function pointers** — replace `strcmp` chains with a lookup table or cached enum on the AST node.

5. **Reduce per-iteration env allocation in loops** — reuse loop environment instead of malloc/free per iteration.

## Target

- fib(32) under 1s with tree-walker alone
- 20M loop under 2s with tree-walker alone

## Notes

- `ast_expression_text` is called on every `AST_ASSIGN_STMT` with `op == "="`, building a `malloc`'d string, `snprintf`'ing into it, then `free`'ing it — all before `provenance_attach` checks `g_provenance_enabled` and returns early. This is pure waste for programs that never use `why`.
- `val_copy` on `AST_ASSIGN_STMT` copies the value even for ints/floats/bools (trivial struct copy, but still 40M calls for loop20m).
