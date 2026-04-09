# Release Notes

## v1.0.7-beta

### New Features
- **Go (Golang) code generation** — full AST-to-Go transpilation with idiomatic output: tab indentation (gofmt), conditional `import "fmt"`, `for` loops (no while in Go), `defer/recover` for try/catch, `panic` for throw, structs for classes, slices and maps for arrays/objects. Two-pass architecture emits functions at package level and statements in `func main()`.
- **Embedded Go support** — `#embed go` / `#endembed` blocks pass through native Go code directly.

### Bug Fixes
- **Fix macOS crash** — unescaped `%s`/`%d` format specifiers in a string literal passed to the variadic `sb_append()` function caused `vsnprintf` to read garbage off the stack. On macOS ARM64 this read `0x23` ('#') as a char pointer and segfaulted in `strlen`. This bug existed since the C codegen was written and is why macOS builds never passed.
- **Fix broken build on main** — commit `5814fa0` (symbol table addition) introduced forward references to `hash()` and `StringBuilder` in `utils.c`, plus a missing `#include`. The project could not compile since March 9.
- **Fix target registry routing** — `language_info_table` and `target_registry` arrays were missing 3 enum entries, causing every language from Go onward to silently map to the wrong codegen (e.g., `go` ran Ruby, `ruby` hit "not implemented").
- **Fix native compiler linker errors** — `compiler_compile()` in `utils.c` referenced symbols only available in the transpiler target. Added weak symbol stubs for cross-target compatibility.
- **Fix Windows file reading** — `fopen("r")` text mode caused `fread`/`ftell` size mismatch due to `\r\n` translation. Changed to `fopen("rb")`.
- **Fix parser use-after-free** — shared AST node references caused double-free during cleanup. Added visited-node tracking with iterative next-chain traversal (avoids stack overflow on macOS's 512KB default stack).
- **Fix CI smoke test syntax** — `test_simple.sb` used hash-prefixed syntax the parser doesn't support.

### CI/CD Improvements
- Updated macOS runners from deprecated macos-13/14 to macos-26 (Apple Silicon).
- Removed ARM64 runners (ubuntu-24.04-arm64, windows-11-arm64) that require paid plans and never executed.
- Fixed code quality bot workflows to skip on fork PRs (token permissions).
- **First passing CI build in the project's history.**

### Test Files
- `tests/test_go.sb` — comprehensive Go codegen test
- `tests/test_embed_go.sb` — embedded Go passthrough test
- `tests/test_go_edge_cases.sb` — edge cases (empty functions, nested control flow, arrays, maps)

### Components
- **sub**: Compiler/transpiler driver
- **subc**: Native compiler driver

### Supported Platforms
- Linux x86_64
- macOS ARM64 (Apple Silicon)
- Windows x86_64 (MinGW)
