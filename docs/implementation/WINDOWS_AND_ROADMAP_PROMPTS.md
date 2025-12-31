### Instance 1: Windows Compatibility Header & Core Fixes
```text
Role: Portability Engineer
Project: SUB Language Compiler
Context: The compiler fails on Windows (CI) because of POSIX headers (<strings.h>) and functions (strdup, strcasecmp).
Task:
1. Create/Update `windows_compat.h`:
   - Add `#ifdef _WIN32` guards.
   - Map `strcasecmp` -> `_stricmp`.
   - Map `strdup` -> `_strdup`.
   - If `_MSC_VER` is defined, safely include `<string.h>` instead of `<strings.h>`.
2. Modify `sub_multilang.c`, `lexer.c`, `parser_enhanced.c`, `ir.c`:
   - Replace `#include <strings.h>` with `#include "windows_compat.h"`.
   - Ensure all string functions use the logic from the compat header.
3. Goal: Compilation must pass with `x86_64-w64-mingw32-gcc` (MinGW).
```

### Instance 2: CI/CD & Build System Overhaul
```text
Role: DevOps Engineer
Project: SUB Language Compiler
Context: GitHub Actions "build-and-release" failed on Windows.
Task:
1. Analyze `Makefile`:
   - Add specific target for Windows (e.g., `make .exe`).
   - Use `$(CC)` and `$(CFLAGS)` variables flexibly.
   - Detect OS (`uname -s` or `OS` env var) to link `-lws2_32` or other Windows libs if needed (though likely not for pure compiler).
2. Create `.github/workflows/windows_fix.yml` (simulated):
   - Propose a workflow file that installs MinGW (`choco install mingw`) before building.
   - OR fix the existing workflow to use proper shell env.
3. Goal: Ensure `make` works on Linux, macOS, and Windows (via MinGW).
```

### Instance 3: Rust Backend Implementation (Skeleton)
```text
Role: Language Feature Developer
Project: SUB Language Compiler
Context: Roadmap Goal: "Rust Compiler Architecture Integration".
Task:
1. Create `codegen_rust.c` and `codegen_rust.h`.
2. Implement `codegen_rust(ASTNode *node)`:
   - Map `AST_VAR_DECL` -> `let mut name: type = val;`.
   - Map `AST_FUNCTION_DECL` -> `fn name(args) -> type { ... }`.
   - Handle basic control flow (`if`, `while` -> `loop`).
3. Modify `sub_multilang.c`:
   - Add "rust" to target list.
   - Call `codegen_rust` when target is "rust".
```

### Instance 4: C++ Backend Implementation (Skeleton)
```text
Role: Language Feature Developer
Project: SUB Language Compiler
Context: Roadmap Goal: "C++ Compiler Architecture Integration".
Task:
1. Create `codegen_cpp.c` and `codegen_cpp.h`.
2. Implement `codegen_cpp(ASTNode *node)`:
   - Use `std::string` instead of `char*` where appropriate (if utilizing C++ stdlib).
   - Use `auto` for variable declarations.
   - Map `print()` to `std::cout << ... << std::endl`.
3. Modify `sub_multilang.c`:
   - Add "cpp", "cpp17", "cpp20" targets.
   - Pass a `CPPVerc` flag to the generator.
```

### Instance 5: C Backend Cleanup & Optimization
```text
Role: Optimization Engineer
Project: SUB Language Compiler
Context: Roadmap Goal: "Improved C code generation".
Task:
1. Refactor `codegen.c` (or `codegen_c_backend.c` if renamed):
   - Ensure generated C code is standard compliant (C99).
   - Add header includes (`<stdio.h>`, `<stdlib.h>`, `<stdbool.h>`) only once at the top.
   - Fix any potential memory leaks in *generated* code (e.g., freeing strings).
2. Implement `optimize_c_output(ASTNode *node)` stub:
   - Perform simple dead code elimination before generation.
```

### Instance 6: Transpiler Refactoring (Project Structure)
```text
Role: Software Architect
Project: SUB Language Compiler
Context: The project has many "codegen_*.c" files now. We need to standardize how targets are added.
Task:
1. Create `targets.h`:
   - Define enum `TargetLanguage { LANG_C, LANG_CPP, LANG_RUST, LANG_PYTHON, ... }`.
   - Declare unified function pointer type `typedef void (*CodegenFn)(ASTNode*, FILE*)`.
2. Refactor `sub_multilang.c`:
   - Use a registry/lookup table instead of massive `if/else if/else` block for targets.
   - `get_codegen_for_target(char *name) -> CodegenFn`.
3. Verify `CodeGen` struct or interface usage.
```
