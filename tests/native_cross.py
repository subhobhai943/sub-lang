#!/usr/bin/env python3
"""Cross-arch native conformance: compile SUB to a target's machine code and
run it (under qemu when the host differs), diffing against the interpreter."""
import subprocess, sys, tempfile, os, shutil
target = sys.argv[1] if len(sys.argv) > 1 else "aarch64-linux"
qemu = shutil.which("qemu-aarch64") or shutil.which("qemu-aarch64-static")
cases = [
    "print(42)", "print(6 * 7)", "print(2 + 3 * 4)",
    "let a = 6\nlet b = 7\nprint(a * b)",
    "print(100 - 58)", "print(-5 + 47)",
    "let x = 10\nprint(x * x - 58)",
    "print(0)", "print(9223372036854775807)", "print(-123456789)",
]
fail = 0
for src in cases:
    with tempfile.NamedTemporaryFile("w", suffix=".sb", delete=False) as f:
        f.write(src + "\n"); sb = f.name
    ref = subprocess.run(["./subi", sb], capture_output=True, text=True)
    out = "/tmp/subcross_" + os.path.basename(sb)
    c = subprocess.run(["./subc", sb, "--native", "--target", target, "-o", out],
                       capture_output=True, text=True)
    if c.returncode != 0:
        print("COMPILE FAIL", repr(src), c.stderr.strip()); fail += 1; os.unlink(sb); continue
    runner = [qemu, out] if qemu else [out]
    r = subprocess.run(runner, capture_output=True, text=True)
    if r.stdout != ref.stdout:
        print(f"MISMATCH {src!r}: target={r.stdout!r} subi={ref.stdout!r}"); fail += 1
    else:
        print("ok:", src.replace("\n", " ; "))
    os.unlink(sb)
print(f"{len(cases)-fail}/{len(cases)} passed")
sys.exit(1 if fail else 0)
