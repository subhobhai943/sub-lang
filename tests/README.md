# Test Files

This directory contains SUB language test files (.sb).

## Test Files

- example.sb - Example program
- simple_test.sb - Simple test case
- test_*.sb - Various compiler test cases

These files are used to test the compiler functionality.

## Cross-backend conformance (`conformance.py`)

`run_tests.py` checks that each tool runs without crashing. `conformance.py`
checks something stricter: that a program **means the same thing** on every
backend.

The interpreter (`subi`) is the reference. For each program in
`tests/conformance/`, the harness runs it under the interpreter, then
transpiles it to every target whose toolchain is installed, builds and runs
the result, and diffs the output against the reference.

```bash
make                              # build first
python3 tests/conformance.py      # every installed target
python3 tests/conformance.py python c   # just these
```

Targets whose toolchain is missing are reported and skipped. A run in which
nothing actually executed is reported as a failure rather than a pass, so a
missing toolchain can never look green.

Adding a case is just dropping a `.sb` file into `tests/conformance/`; whatever
the interpreter prints becomes the expected output.

### Target status

| Target | Verified by |
|--------|-------------|
| Python, JavaScript, Ruby, C, C++, Rust, Go, Java | executed against the interpreter by this harness |
| Swift, Kotlin | generated code reviewed by hand - no toolchain available in the dev container |

Install `swiftc` / `kotlinc` and re-run to cover the last two; the harness
picks up whatever is on `PATH`.
