#!/usr/bin/env python3
"""Cross-arch native conformance: compile SUB to a target's machine code and
run it (under qemu when the host differs), diffing stdout AND exit status
against the interpreter."""
import subprocess, sys, tempfile, os, shutil
target = sys.argv[1] if len(sys.argv) > 1 else "aarch64-linux"
qemu = shutil.which("qemu-aarch64") or shutil.which("qemu-aarch64-static")
cases = [
    "print(42)", "print(6 * 7)", "print(2 + 3 * 4)",
    "let a = 6\nlet b = 7\nprint(a * b)",
    "print(100 - 58)", "print(-5 + 47)",
    "let x = 10\nprint(x * x - 58)",
    "print(0)", "print(9223372036854775807)", "print(-123456789)",
    # milestone 2: division / modulo (truncate toward zero), comparisons
    "print(7 / 2)", "print(7 % 3)", "print(-7 / 2)", "print(-7 % 3)",
    "print(3 < 5)", "print(5 <= 5)", "print(6 > 7)", "print(6 == 6)", "print(6 != 6)",
    # control flow
    "if 3 < 5 { print(1) } else { print(0) }",
    "let s = 85\nif s >= 90 { print(65) } elif s >= 80 { print(66) } else { print(67) }",
    "let n = 3\nwhile n > 0 { print(n)\nn = n - 1 }",
    "let s = 0\nfor i in range(1, 6) { s = s + i }\nprint(s)",
    "for i in range(10) { if i == 3 { break }\nprint(i) }",
    "for i in range(5) { if i % 2 == 0 { continue }\nprint(i) }",
    # functions + recursion
    "fn add(a, b) { return a + b }\nprint(add(20, 22))",
    "fn fib(n) { if n <= 1 { return n }\nreturn fib(n - 1) + fib(n - 2) }\nprint(fib(10))",
    "let calls = 0\nfn bump() { calls = calls + 1 }\nbump()\nbump()\nprint(calls)",
    # runtime error: division by zero -> exit 70
    "print(1 / 0)",
]
fail = 0
for src in cases:
    with tempfile.NamedTemporaryFile("w", suffix=".sb", delete=False) as f:
        f.write(src + "\n"); sb = f.name
    ref = subprocess.run(["./subi", sb], capture_output=True, text=True)
    outp = "/tmp/subcross_" + os.path.basename(sb)
    c = subprocess.run(["./subc", sb, "--native", "--target", target, "-o", outp],
                       capture_output=True, text=True)
    if c.returncode != 0:
        print("COMPILE FAIL", repr(src), c.stderr.strip()); fail += 1; os.unlink(sb); continue
    runner = [qemu, outp] if qemu else [outp]
    r = subprocess.run(runner, capture_output=True, text=True)
    if r.stdout != ref.stdout or r.returncode != ref.returncode:
        print(f"MISMATCH {src!r}: target=({r.stdout!r},{r.returncode}) subi=({ref.stdout!r},{ref.returncode})")
        fail += 1
    else:
        print("ok:", src.replace("\n", " ; "))
    os.unlink(sb)
print(f"{len(cases)-fail}/{len(cases)} passed")
sys.exit(1 if fail else 0)
