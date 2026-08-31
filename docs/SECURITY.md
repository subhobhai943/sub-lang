# Security Policy

## Supported versions

Fixes land on `main` and go out in the next release. Only the latest release
is supported; there are no backported patches for older ones.

| Version | Supported |
|---|---|
| 1.0.9 | yes |
| < 1.0.9 | no — upgrade |

Check yours with `subi --version`.

## Reporting a vulnerability

Please report privately, through GitHub's **[private vulnerability
reporting](https://github.com/subhobhai943/sub-lang/security/advisories/new)**
on this repository, rather than opening a public issue.

Useful things to include:

- the `.sb` source, or the input, that triggers it — as small as you can make it
- which tool: `subi`, `subc` (and `--native` or `--via-c`), or `sub <target>`
- the version (`subi --version`) and your OS and architecture
- what you observed: a crash, a hang, a wrong result, memory corruption under
  a sanitizer
- if you have one, the sanitizer output — the project builds clean under
  `-fsanitize=address,undefined`, so a report that reproduces there is
  immediately actionable

This is a small project with one maintainer. Expect an acknowledgement within
about a week. Please give a reasonable window to fix something before
disclosing it publicly, and tell us if you have a deadline of your own — we
would rather coordinate than be surprised.

## What is in scope

The compiler and interpreter are written in C and parse untrusted input, so
memory-safety problems there are real bugs and are what this policy is mostly
about:

- memory corruption in the lexer, parser, semantic analyser, interpreter or
  any backend, reached from a `.sb` file — buffer overflows, use-after-free,
  double frees, out-of-bounds reads
- the same in the machine-code backend's own output, or in the runtime
  routines it emits
- a crash or unbounded resource use in the tooling triggered by input that a
  user might reasonably be handed, such as a `.sb` file from a third party
- anything in the release artifacts themselves — the tarballs, the `.deb`
  packages or the Windows MSI

## What is not a vulnerability

Being clear about this saves everyone time:

- **Running a `.sb` program is running code.** There is no sandbox and none is
  planned. `subi hostile.sb` is as dangerous as running any other untrusted
  program, and that is by design, not a bug to report.
- **`#embed` blocks are code injection by design.** They exist so a program
  can drop raw target-language source into the output. A `.sb` file that uses
  `#embed` to emit hostile C, Python or JavaScript is using a documented
  feature.
- **Transpiled output is source, not a sandbox.** `sub program.sb rust`
  produces Rust that you then compile yourself. Treat generated code from an
  untrusted `.sb` file the way you would treat any untrusted source.
- **`subc --via-c` runs your C compiler**, honouring `$CC`. Pointing `$CC` at
  something is the caller's decision.
- A compile error, a bad diagnostic, or a backend disagreeing with the
  interpreter is a correctness bug — please
  [open an issue](https://github.com/subhobhai943/sub-lang/issues) rather than
  a security advisory. Those matter, and the conformance suite exists for
  them, but they are not this.

## What the language can and cannot reach

Worth stating plainly, because it bounds what a SUB program can do at all:

- There are **no file or process builtins**. A SUB program cannot open a file,
  spawn a process or open a socket. `input()` reads a line from stdin, and
  `print`/`println` write to stdout. That is the whole I/O surface.
- What a compiled program can reach is whatever its host language can reach,
  once `#embed` is involved.
- The interpreter has a recursion guard that measures actual stack use, so
  runaway recursion reports an error and exits rather than being killed by the
  OS.

## How the project tries to stay honest about this

- CI builds with `-Wall -Wextra` and fails on new warnings, across GCC and
  Clang, on Linux x86-64 and arm64, macOS arm64 and Windows.
- Every program in `tests/conformance/` is checked against the interpreter on
  all twelve build targets, so a backend that quietly means something
  different is caught rather than shipped.
- The suite is run by hand under AddressSanitizer and UndefinedBehaviorSanitizer
  when the interpreter or a runtime helper changes, and is clean of non-leak
  findings. This is not yet a CI job, so treat it as a habit rather than a
  guarantee.

None of that is a guarantee. If you find something these missed, the report is
welcome.
