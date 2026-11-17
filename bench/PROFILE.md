# Interpreter Performance Profile

## Methodology

Built `subi` with `gcc -O2 -pg` (gprof) and ran each benchmark. gprof
samples at 10ms intervals.

## Baseline Results (before any optimization)

| Benchmark | Time | Notes |
|-----------|------|-------|
| fib(32) recursive | 7.10s | 35M eval calls |
| while loop, 20M iterations | 18.30s | ~915 ns/iter |
| string concat, 20K iterations | 1.51s | |
| array push/index, 1M iterations | 1.36s | |
| object field R/W, 1M iterations | 0.85s | |
| small function calls, 5M iterations | 19.37s | |

## Top Hotspots (loop20m, BEFORE optimization, gprof)

| Function | % self time | self seconds | calls | Analysis |
|----------|-------------|--------------|-------|----------|
| `eval` | 57.0% | 2.65s | 120M | Tree-walk dispatch overhead — fundamental |
| `eval_binary` | 19.3% | 0.90s | 60M | Arithmetic + comparison ops |
| `env_set` | 6.0% | 0.28s | 40M | Variable assignment — linear name search |
| `ast_expression_text` | 5.3% | 0.25s | 40M | **WASTED**: provenance label building |
| `env_get` | 3.9% | 0.18s | 80M | Variable lookup — linear name search |
| `val_copy` | 3.0% | 0.14s | 40M | Value copying on assignment |
| `provenance_release` | 2.2% | 0.10s | 40M | **WASTED**: provenance refcounting |

## Top Hotspots (loop20m, AFTER all optimizations, gprof)

| Function | % self time | self seconds | calls | Analysis |
|----------|-------------|--------------|-------|----------|
| `eval` | 44.0% | 1.84s | 160M | Tree-walk dispatch overhead — fundamental bottleneck |
| `eval_binary` | 21.3% | 0.89s | 60M | Arithmetic + comparison ops |
| `eval_block` | 9.5% | 0.40s | 20M | Block statement dispatch |
| `env_get_node` | 8.9% | 0.37s | 80M | Variable lookup (slot-cached for depth 0) |
| `env_set_node` | 4.6% | 0.19s | 40M | Variable assignment (slot-cached for depth 0) |
| `val_copy` | 3.8% | 0.16s | 40M | Value copying on assignment |
| `provenance_release` | 1.9% | 0.08s | 40M | Provenance refcounting (cheap when disabled) |
| `env_free` | 1.0% | 0.04s | 20M | Loop env deallocation |

## Optimizations Applied

### Commit 1: Skip provenance label building when `why` is unused

**File:** `src/core/interpreter.c`

`ast_expression_text()` was called unconditionally before
`provenance_attach()` checked `g_provenance_enabled`. All call sites
are now guarded.

**Measured impact (loop20m):** 18.30s → 5.89s (0.32x, 68% faster)

### Commit 2: Resolve variables to (depth, slot) indexes

**Files:** `src/core/interpreter.c`, `src/core/interpreter.h`,
`src/include/sub_compiler.h`, `src/core/parser_enhanced.c`

Added `slot` to `EnvEntry`, `slots`/`slot_count`/`slot_capacity` to `Env`,
`resolved_depth`/`resolved_slot` to `ASTNode`. Variables resolved to the
current env (depth 0) use O(1) slot access instead of linear name search.

**Measured impact (loop20m):** 5.89s → 5.34s; func_calls 10.52s → 7.41s

### Commit 3: Inline identifier/literal handling in eval_block

**File:** `src/core/interpreter.c`

`eval_block` now handles `AST_IDENTIFIER` and `AST_LITERAL` nodes inline
instead of calling `eval()` for each.

**Measured impact:** Marginal (within measurement noise).

## Remaining Bottleneck

`eval()` at 44% of runtime with 160M calls is the fundamental bottleneck.
Each call involves C function call/return overhead, switch-on-node-type
dispatch, and early-exit checks.

**Target:** fib(32) under 1s and 20M loop under 2s with tree-walker alone.

**Assessment:** NOT ACHIEVABLE with tree-walker optimizations alone.
Best tree-walker results:
- fib(32): ~4.5s (need ~4.5x more improvement)
- loop20m: ~5.4s (need ~2.7x more improvement)

**Next step:** Add a bytecode compiler + stack VM as a second mode
(`subi --vm`) per the optimization plan Step 2. Keep the tree-walker as
the reference implementation.

## Test Suite

All existing tests pass:
- `python3 tests/run_tests.py` — all checks pass
- `python3 tests/conformance.py` — pass (TypeScript target may fail in
  some environments; not touched)

ASAN/UBSAN: No new issues introduced. Pre-existing exit-path memory
leaks are unrelated to these optimizations.
