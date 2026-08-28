#!/usr/bin/env python3
"""
Checks the claim that makes the native backend worth having: that `subc`
produces a working executable with no C toolchain involved at all.

tests/conformance.py already checks that a natively compiled program prints
what the interpreter prints. What it cannot check is *how* the binary came to
exist, and that is the part that used to be untrue - `subc` said "native
compiler" while shelling out to gcc. So this asks three things the
conformance run does not:

  1. subc --native compiles with PATH emptied, so no compiler, assembler or
     linker could have been found even if one were wanted;
  2. the result runs with PATH emptied and prints the right answer;
  3. the result is a static ELF with no dynamic section, so it is not
     quietly depending on a libc at load time either.

On a host the machine-code backend does not target, this exits 0 after
saying so: there is nothing to test, and nothing is broken.
"""

import os
import platform
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUBI = os.path.join(ROOT, "subi")
SUBC = os.path.join(ROOT, "subc")
CASES = os.path.join(ROOT, "tests", "conformance")

TIMEOUT = 60

# An environment with nothing on PATH. `env -i`-style: if subc reaches for a
# compiler here it will not find one, and the test fails rather than passing
# on a machine that happens to have gcc installed.
BARE_ENV = {"PATH": "/nonexistent", "HOME": os.environ.get("HOME", "/tmp")}


def run(argv, env=None, cwd=None):
    p = subprocess.run(argv, capture_output=True, text=True,
                       encoding="utf-8", errors="replace",
                       timeout=TIMEOUT, env=env, cwd=cwd)
    return p.returncode, p.stdout, p.stderr


def supported():
    return platform.system() == "Linux" and platform.machine() == "x86_64"


def is_static_elf(path):
    """True when the file is an ELF64 executable with no PT_DYNAMIC segment.

    Read directly rather than shelling out to `file` or `readelf`, which
    would defeat the point of a test about not needing other tools.
    """
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        return False, "not an ELF64 file"
    phoff = int.from_bytes(data[0x20:0x28], "little")
    phentsize = int.from_bytes(data[0x36:0x38], "little")
    phnum = int.from_bytes(data[0x38:0x3A], "little")
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type = int.from_bytes(data[off:off + 4], "little")
        if p_type == 2:               # PT_DYNAMIC
            return False, "has a dynamic segment"
        if p_type == 3:               # PT_INTERP
            return False, "names an interpreter"
    return True, ""


def main():
    if not supported():
        print("native backend does not target %s/%s - nothing to check"
              % (platform.system(), platform.machine()))
        return 0

    for binary in (SUBI, SUBC):
        if not os.path.exists(binary):
            print("missing %s - run `make` first" % binary)
            return 2

    cases = sorted(os.path.join(CASES, f)
                   for f in os.listdir(CASES) if f.endswith(".sb"))
    if not cases:
        print("no .sb cases in %s" % CASES)
        return 2

    failures = []
    for case in cases:
        name = os.path.basename(case)
        # A case that ends in a runtime error is still a case: the compiled
        # program has to fail the same way, with the same output before it
        # and the same exit status.
        want_rc, want, err = run([SUBI, case])

        with tempfile.TemporaryDirectory() as work:
            exe = os.path.join(work, "prog")
            rc, out, err = run([SUBC, case, "--native", "-o", exe],
                               env=BARE_ENV, cwd=work)
            if rc != 0:
                failures.append((name, "subc --native failed with an empty "
                                       "PATH: %s" % (err.strip() or out.strip())[:200]))
                continue

            ok, why = is_static_elf(exe)
            if not ok:
                failures.append((name, "produced binary %s" % why))
                continue

            rc, got, err = run([exe], env=BARE_ENV, cwd=work)
            if got.strip().splitlines() != want.strip().splitlines():
                failures.append((name, "output differs from the interpreter"))
                continue
            if rc != want_rc:
                failures.append((name, "exit status %s, interpreter exits %s%s"
                                 % (rc, want_rc,
                                    (": " + err.strip()[:120]) if err.strip() else "")))
                continue

        print("  ok   %s" % name)

    print()
    if failures:
        for name, why in failures:
            print("  FAIL %s: %s" % (name, why))
        print("\n%d of %d cases failed" % (len(failures), len(cases)))
        return 1
    print("%d cases compiled and ran with no toolchain on PATH" % len(cases))
    return 0


if __name__ == "__main__":
    sys.exit(main())
