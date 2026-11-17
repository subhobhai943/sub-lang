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

## Running

```bash
# Run all benchmarks and print wall times
./bench/run.sh

# Save current times as baseline
./bench/run.sh --baseline

# Compare against saved baseline
./bench/run.sh --compare
```

## Baseline Results (before optimization)

| Benchmark | Time |
|-----------|------|
| fib(32) recursive | 7.10s |
| while loop, 20M iterations | 18.30s |
| string concat, 20K iterations | 1.51s |
| array push/index, 1M iterations | 1.36s |
| object field R/W, 1M iterations | 0.85s |
| small function calls, 5M iterations | 19.37s |

## Results After All Optimizations

| Benchmark | Before | After | Ratio |
|-----------|--------|-------|-------|
| fib(32) recursive | 7.10s | 4.48s | 0.63x |
| while loop, 20M iterations | 18.30s | 5.44s | 0.30x |
| string concat, 20K iterations | 1.51s | 1.30s | 0.86x |
| array push/index, 1M iterations | 1.36s | 0.66s | 0.49x |
| object field R/W, 1M iterations | 0.85s | 0.57s | 0.67x |
| small function calls, 5M iterations | 19.37s | 7.41s | 0.38x |

## Optimizations Applied

### 1. Skip provenance label building when `why` is unused

`ast_expression_text()` was called unconditionally on every variable
declaration, assignment, binary expression, and function call, building
malloc'd/snprintf'd/free'd label strings even when `g_provenance_enabled`
is 0 (no `why` expression in the program). `provenance_attach()` already
checked `g_provenance_enabled` and returned early, but the label
construction happened *before* that check.

All `ast_expression_text`/`provenance_attach` call sites are now guarded
with `g_provenance_enabled` so programs that never use `why` pay zero cost.

**Impact:** loop20m 18.30s → 5.89s (68% faster), fib 7.10s → 4.14s.

### 2. Resolve variables to (depth, slot) indexes

Replaced linear name-search in `env_get`/`env_set` with direct slot
access for variables resolved to the current env (depth 0). Each
`EnvEntry` gets a slot index; `Env` gets an array of `EnvEntry*` pointers
for O(1) access. `AST_IDENTIFIER` nodes cache `(depth, slot)` on first
successful lookup. Depth > 0 lookups (parent env) still use linear search
since depth is context-dependent.

**Impact:** loop20m 5.89s → 5.34s, func_calls 10.52s → 7.41s.

### 3. Inline identifier/literal handling in eval_block

Avoid full `eval()` dispatch for `AST_IDENTIFIER` and `AST_LITERAL` nodes
inside `eval_block` by handling them inline. Saves a function call and
switch dispatch per variable/literal reference in statement blocks.

**Impact:** marginal (measurement noise).

## Remaining Bottlenecks

The tree-walking `eval()` function is the fundamental bottleneck at ~44%
of runtime with 160M calls for loop20m. Each call involves C function
call/return overhead + switch-on-node-type dispatch + early-exit checks.

**Target:** fib(32) under 1s and 20M loop under 2s with tree-walker alone.

**Assessment:** Not achievable with tree-walker optimizations alone.
The best tree-walker result is fib(32) at ~4.5s and loop20m at ~5.4s.
Reaching the targets requires a bytecode compiler + stack VM
(`subi --vm`) as described in the optimization plan (Step 2).

## Test Suite

Run the full test suite:
```bash
python3 tests/run_tests.py
python3 tests/conformance.py
```

All existing tests pass after each optimization.
