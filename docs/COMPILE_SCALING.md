# Compile-time scaling

## Problem

The native compiler (`subc`) and the shared compile pipeline (lexing,
parsing, semantic analysis, signature inference) grew cubically with program
size: 250 functions 0.18s, 500 functions 0.96s, 1000 functions 6.3s,
2000 functions 34s (~6x per doubling, ~cubic). A self-hosted SUB compiler
has thousands of functions, so this had to be fixed without changing any
generated program.

## Results

Median of 11 runs, gcc 14.2 `-O2` build, `subc` native backend (x86-64
Linux). `subc file -o out` compile time for the synthetic N-function
benchmark (`bench/compile/gen.py functions N`):

| N | functions (s) | exponent | vs pre-fix |
|---|---------------|----------|------------|
| 250 | 0.009 | - | ~25x faster |
| 500 | 0.018 | ~1.0 | ~58x faster |
| 1000 | 0.068 | ~1.9 | ~104x faster |
| 2000 | 0.11 | ~0.7 | ~380x faster |
| 4000 | 0.57 | ~2.3 | ~350x faster |
| 8000 | 1.71 | ~1.6 | - |

Exponents below 2k are dominated by fixed per-compile costs (lexing, parsing,
process startup) and noisy at sub-100ms runtimes; 2k->4k->8k scale at
~1.6-2.4x per doubling, i.e. close to linear once fixed costs are out of the
way. Requirement: 2000 functions under 1 s -- met at ~0.1 s.

All comparisons, baseline numbers and the generator are under
`bench/compile/`.

## What was fixed

1. **Variable/element registries** (`src/codegen/codegen_infer.c`):
   `var_lookup`/`var_record`/`elem_lookup`/`elem_record` used linear scans
   over whole-program tables -- 53% of the profile (9.5M calls). They are now
   hash tables keyed by (owner function, name), with top-level scope as
   fallback, preserving the "own function first, then top-level" lookup order
   exactly. Names are interned so comparisons are pointer comparisons in the
   common case. The function table's fixed cap of 512 (which silently dropped
   functions past 512 from inference) is now growable.
2. **Call-site propagation** (`propagate_call_sites`): call sites are
   collected once and indexed by callee; each fixpoint round replays the
   list instead of re-walking the whole AST. Same rounds, same merge order,
   identical types.
3. **Single-visit AST walks**: statements live on both the `next` chain and
   `children[]`, so the naive walk visited each statement of an m-statement
   block O(m) times. All whole-AST passes now use a visited-node set
   (`collect_functions`, `infer_var_decls`, `stamp_empty_array_elems`,
   `propagate_var_types_to_identifiers`, `normalize_builtin_methods`, ...).
4. **Semantic scope exit** (`exit_scope`): removed the leaving scope's
   entries as a contiguous prefix of the symbol-table chain instead of
   re-scanning the whole table on every exit.
5. **Native bump heap**: 64 MB -> 2 GB (demand-zeroed, costs nothing until
   touched). Fixes `bench/string_ops.sb` segfaulting on ~1 GB of cumulative
   string concatenation churn; binary size on disk is unchanged.

## Correctness

Generated code is unchanged: ELF and C-backend output for every file in
tests/, examples/, applications/ and stdlib/ is byte-identical, or produces
identical program output where the native/C-backend fallback choice differs.
`python3 tests/run_tests.py` and `python3 tests/conformance.py` pass, and the
build is warning-free with `-Wall -Wextra`.

## Running

```bash
./bench/compile/run.sh              # print compile times at each size
./bench/compile/run.sh --baseline   # save current times as baseline
./bench/compile/run.sh --compare    # compare against saved baseline
```
