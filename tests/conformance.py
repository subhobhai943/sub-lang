#!/usr/bin/env python3
"""
SUB cross-backend conformance harness.

The interpreter (`subi`) is the reference implementation. For each program in
tests/conformance/, this runs the interpreter, then transpiles the same program
to every target language whose toolchain is installed, builds and runs it, and
diffs the output against the reference.

That is the only way to catch the class of bug where a program silently means
something different depending on which backend you ran it through.

Usage:
    python3 tests/conformance.py            # every target available
    python3 tests/conformance.py python c   # only these targets
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(ROOT, "tests", "conformance")
SUB = os.path.join(ROOT, "sub")
SUBI = os.path.join(ROOT, "subi")

TIMEOUT = 30


def have(tool):
    return shutil.which(tool) is not None


# target -> (generated file extension, how to build, how to run)
# `build` is a list of argv templates run in order; {src} and {exe} are filled in.
TARGETS = {
    "python":     dict(ext="py",    need="python3", build=[], run=["python3", "{src}"]),
    "javascript": dict(ext="js",    need="node",    build=[], run=["node", "{src}"]),
    "ruby":       dict(ext="rb",    need="ruby",    build=[], run=["ruby", "{src}"]),
    "c":          dict(ext="c",     need="gcc",
                       build=[["gcc", "-o", "{exe}", "{src}", "-lm"]],
                       run=["{exe}"]),
    "cpp":        dict(ext="cpp",   need="g++",
                       build=[["g++", "-std=c++17", "-o", "{exe}", "{src}"]],
                       run=["{exe}"]),
    "rust":       dict(ext="rs",    need="rustc",
                       build=[["rustc", "-A", "warnings", "-o", "{exe}", "{src}"]],
                       run=["{exe}"]),
    "go":         dict(ext="go",    need="go",
                       build=[["go", "build", "-o", "{exe}", "{src}"]],
                       run=["{exe}"]),
    "java":       dict(ext="java",  need="java",
                       build=[], run=["java", "{src}"]),
    "swift":      dict(ext="swift", need="swiftc",
                       build=[["swiftc", "-o", "{exe}", "{src}"]],
                       run=["{exe}"]),
    "kotlin":     dict(ext="kt",    need="kotlinc",
                       build=[["kotlinc", "{src}", "-include-runtime", "-d", "{exe}.jar"]],
                       run=["java", "-jar", "{exe}.jar"]),
}


def run(argv, cwd):
    try:
        p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                           timeout=TIMEOUT)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "timed out after %ds" % TIMEOUT
    except OSError as e:
        return -1, "", str(e)


def normalize(text):
    """Compare on meaningful content only: trailing whitespace and a trailing
    newline are not semantic differences between language runtimes."""
    return [ln.rstrip() for ln in text.strip().splitlines()]


def check(case, target, spec, workdir):
    """Returns (status, detail). status is 'pass', 'fail', or 'skip'."""
    name = os.path.splitext(os.path.basename(case))[0]

    rc, want, err = run([SUBI, case], ROOT)
    if rc != 0:
        return "skip", "interpreter failed: %s" % (err.strip() or rc)

    rc, _, err = run([SUB, case, target], workdir)
    if rc != 0:
        return "fail", "transpile failed: %s" % err.strip()[:200]

    # The transpiler names output after the input file, except Java, which must
    # match its public class name.
    src = "SubProgram.java" if target == "java" else "%s.%s" % (name, spec["ext"])
    if not os.path.exists(os.path.join(workdir, src)):
        return "fail", "expected generated file %s, none produced" % src

    exe = os.path.join(workdir, "%s_%s_bin" % (name, target))
    for step in spec["build"]:
        argv = [a.format(src=src, exe=exe) for a in step]
        rc, out, err = run(argv, workdir)
        if rc != 0:
            return "fail", "build failed: %s" % (err.strip() or out.strip())[:300]

    argv = [a.format(src=src, exe=exe) for a in spec["run"]]
    rc, got, err = run(argv, workdir)
    if rc != 0:
        return "fail", "runtime error: %s" % (err.strip() or rc)[:300]

    if normalize(got) != normalize(want):
        return "fail", "output differs\n    expected: %r\n    actual:   %r" % (
            normalize(want)[:6], normalize(got)[:6])

    return "pass", ""


def main():
    wanted = sys.argv[1:] or list(TARGETS)
    unknown = [t for t in wanted if t not in TARGETS]
    if unknown:
        print("unknown target(s): %s" % ", ".join(unknown))
        return 2

    for binary in (SUB, SUBI):
        if not os.path.exists(binary):
            print("missing %s - run `make` first" % binary)
            return 2

    if not os.path.isdir(CASES):
        print("no conformance cases at %s" % CASES)
        return 2

    cases = sorted(os.path.join(CASES, f)
                   for f in os.listdir(CASES) if f.endswith(".sb"))
    if not cases:
        print("no .sb cases in %s" % CASES)
        return 2

    active, missing = [], []
    for t in wanted:
        (active if have(TARGETS[t]["need"]) else missing).append(t)

    if missing:
        print("skipping (toolchain not installed): %s\n" % ", ".join(missing))

    failures, skipped, passed = [], [], 0
    for case in cases:
        name = os.path.basename(case)
        print(name)
        for target in active:
            with tempfile.TemporaryDirectory() as workdir:
                shutil.copy(case, workdir)
                local = os.path.join(workdir, os.path.basename(case))
                status, detail = check(local, target, TARGETS[target], workdir)
            mark = {"pass": "  ok  ", "fail": " FAIL ", "skip": " skip "}[status]
            print("  [%s] %-11s %s" % (mark, target, detail))
            if status == "fail":
                failures.append((name, target, detail))
            elif status == "skip":
                skipped.append((name, target, detail))
            else:
                passed += 1
        print()

    total = len(cases) * len(active)
    print("=" * 62)
    print("%d passed, %d failed, %d skipped (of %d)"
          % (passed, len(failures), len(skipped), total))
    if failures:
        return 1
    # A run where nothing actually executed is not a green run.
    if passed == 0:
        print("nothing ran - treating as failure")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
