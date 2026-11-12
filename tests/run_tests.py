#!/usr/bin/env python3
"""
Assertion-based regression tests for the SUB toolchain.

Runs a small set of canonical .sb programs through all three tools
(sub, subc, subi) and checks that their *actual program output* matches
an expected value, not just that the tool exited 0. Each tool mixes its
own banner/log lines into stdout ahead of the program's own output, so
program output is checked as a suffix of what the tool printed.
"""
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
EXAMPLES_DIR = os.path.join(ROOT_DIR, "examples")

EXE = ".exe" if sys.platform == "win32" else ""
SUB = os.path.join(ROOT_DIR, "sub" + EXE)
SUBC = os.path.join(ROOT_DIR, "subc" + EXE)
SUBI = os.path.join(ROOT_DIR, "subi" + EXE)


def fizzbuzz_output():
    lines = []
    for i in range(1, 31):
        if i % 15 == 0:
            lines.append("FizzBuzz")
        elif i % 3 == 0:
            lines.append("Fizz")
        elif i % 5 == 0:
            lines.append("Buzz")
        else:
            lines.append(str(i))
    return "\n".join(lines)


def fibonacci_output():
    def fib(n):
        return n if n <= 1 else fib(n - 1) + fib(n - 2)
    return "\n".join(str(fib(i)) for i in range(10))


FIXTURES = [
    ("hello_native.sb", "Hello, World\nSUB Language - Simple Universal Builder\n30"),
    ("fizzbuzz.sb", fizzbuzz_output()),
    ("fibonacci.sb", fibonacci_output()),
    # A class program that used to die with "undefined variable 'Point'"
    # through subi and segfault through subc's C backend. The c++ target
    # honestly refuses it (see the SKIP in the cpp step).
    ("../tests/test_class.sb", "10\n20"),
]


failures = []

# GitHub Actions renders ::error:: lines as annotations, which are visible
# on the run without downloading the log - downloading a log needs
# repository admin, so on this repo a failure otherwise reports nothing but
# "exit code 1".
ANNOTATE = bool(os.environ.get("GITHUB_ACTIONS"))


def fail(label, detail=""):
    """Record a failure, print it, and annotate it when running in CI."""
    print(f"  FAIL {label}")
    if detail:
        for line in str(detail).strip().splitlines():
            print(f"       {line}")
    failures.append(label)
    if ANNOTATE:
        flat = " ".join(str(detail).split())[:800]
        print(f"::error::{label}{': ' + flat if flat else ''}")


def check_suffix(actual, expected, label):
    if actual.rstrip("\n").endswith(expected.rstrip("\n")):
        print(f"  OK   {label}")
        return True
    fail(label, f"expected (suffix): {expected!r} | actual: {actual!r}")
    return False


def run(cmd, cwd=None):
    try:
        result = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True,
                                 encoding="utf-8", errors="replace", timeout=30)
        # On Windows the C runtime writes "\n" as "\r\n", so every expected
        # value in this file - written with "\n" - failed to match even when
        # the program was correct. The line ending is not part of what the
        # program means, so it is normalised away rather than duplicated
        # into every fixture.
        return result.returncode, result.stdout.replace("\r\n", "\n")
    except (subprocess.TimeoutExpired, OSError) as e:
        return 1, str(e)


def tool_available(name):
    """which() alone isn't reliable on Windows: python3/python often resolve
    to a Microsoft Store "App Execution Alias" stub that's on PATH but exits
    non-zero without actually running anything. Confirm the tool actually
    executes before relying on it."""
    from shutil import which
    if which(name) is None:
        return False
    try:
        rc, _ = run([name, "--version"])
        return rc == 0
    except OSError:
        return False


def test_interpreter(sb_file, expected):
    rc, out = run([SUBI, sb_file])
    label = f"subi: {os.path.basename(sb_file)}"
    if rc != 0:
        fail(label, f"exit {rc}: {out}")
        return
    check_suffix(out, expected, label)


def test_native_compile(sb_file, expected):
    label = f"subc: {os.path.basename(sb_file)}"
    if not tool_available("gcc"):
        print(f"  SKIP {label} (gcc not found)")
        return
    out_name = os.path.join(ROOT_DIR, "_regtest_bin")
    rc, out = run([SUBC, sb_file, "-o", out_name])
    if rc != 0:
        fail(label, f"compile exit {rc}: {out}")
        return
    bin_path = out_name + EXE
    if not os.path.exists(bin_path):
        fail(label, f"binary not produced: {bin_path}")
        return
    rc, run_out = run([bin_path])
    os.remove(bin_path)
    c_file = out_name + ".c"
    if os.path.exists(c_file):
        os.remove(c_file)
    if rc != 0:
        fail(label, f"run exit {rc}: {run_out}")
        return
    check_suffix(run_out, expected, label)


def test_transpile_and_run(sb_file, expected, lang, ext, runner):
    label = f"sub {lang}: {os.path.basename(sb_file)}"
    stem = os.path.splitext(os.path.basename(sb_file))[0]
    out_file = os.path.join(ROOT_DIR, stem + ext)
    rc, out = run([SUB, sb_file, lang])
    if rc != 0 or not os.path.exists(out_file):
        fail(label, f"transpile failed, exit {rc}: {out}")
        return
    if runner is None or not tool_available(runner[0]):
        print(f"  SKIP {label} (transpiled OK; runtime '{runner[0] if runner else '?'}' not available)")
        os.remove(out_file)
        return
    rc, run_out = run(runner + [out_file])
    os.remove(out_file)
    if rc != 0:
        fail(label, f"run exit {rc}: {run_out}")
        return
    check_suffix(run_out, expected, label)


def free_port():
    """A port the kernel guarantees is free right now."""
    import socket
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def run_net_checks():
    """Run tests/test_net.sb.in against freshly started local servers.

    Returns a list of (label, ok, detail). The generated program and the
    servers live in one temp directory; net_download writes there, so the
    final net_fetch reads the downloaded file back through the server -
    which only works if the download really happened.
    """
    import subprocess, tempfile, threading, http.server, functools, shutil

    results = []

    # The hosted macOS runners drop incoming loopback connections (their
    # pf/stealth firewall sits on 127.0.0.1 too), so a listener started in
    # the test never answers a connect() from the built binary - exit 70
    # there is the runner, not the language. Linux CI covers every net
    # path end to end; on macOS the check reports SKIP instead of a lie.
    if sys.platform == "darwin":
        return [("test_net.sb (all backends)", None,
                 "loopback blocked by hosted macOS runners; covered on Linux")]

    workdir = tempfile.mkdtemp(prefix="sub_net_test_")
    port_file, port_echo = free_port(), free_port()
    shutil.copy(os.path.join(SCRIPT_DIR, "net_echo_server.py"), workdir)
    with open(os.path.join(workdir, "data.txt"), "w") as f:
        f.write("hello from sub-lang net test")

    # Generate the program with the real ports.
    with open(os.path.join(SCRIPT_DIR, "test_net.sb.in")) as f:
        prog = f.read()
    prog = prog.replace("__PORT_FILE__", str(port_file)).replace("__PORT_ECHO__", str(port_echo))
    sb_path = os.path.join(workdir, "test_net.sb")
    with open(sb_path, "w") as f:
        f.write(prog)

    expected = ("hello from sub-lang net test\n"
                "ECHO:ping-123\n"
                "28\n"
                "hello from sub-lang net test")

    # File server: serves the workdir, so out.txt becomes fetchable.
    handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                                directory=workdir)
    httpd_files = http.server.ThreadingHTTPServer(("127.0.0.1", port_file), handler)
    threading.Thread(target=httpd_files.serve_forever, daemon=True).start()

    # Echo server: same process, different thread.
    sys.path.insert(0, workdir)
    import importlib
    netmod_dir = os.path.join(workdir, "net_echo_server.py")
    # run the echo server in-process via its handler class instead
    import http.server as _hs
    class EchoHandler(_hs.BaseHTTPRequestHandler):
        def _reply(self):
            n = int(self.headers.get('Content-Length', 0) or 0)
            data = self.rfile.read(n) if n else b''
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'ECHO:' + data)
        do_GET = do_POST = do_PUT = _reply
        def log_message(self, *args): pass
    httpd_echo = _hs.ThreadingHTTPServer(("127.0.0.1", port_echo), EchoHandler)
    threading.Thread(target=httpd_echo.serve_forever, daemon=True).start()

    try:
        # interpreter
        rc, out = run([SUBI, sb_path], cwd=workdir)
        results.append(("subi: test_net.sb", rc == 0 and out.rstrip("\n").endswith(expected), f"exit {rc}: {out[:400]}"))
        # subc (default: machine code, auto-falls back to C for net builtins)
        if tool_available("gcc"):
            exe = os.path.join(workdir, "net_compiled")
            rc, out = run([SUBC, sb_path, "-o", exe], cwd=workdir)
            if rc != 0:
                results.append(("subc: test_net.sb", False, f"compile exit {rc}: {out[:400]}"))
            else:
                rc, out = run([exe], cwd=workdir)
                results.append(("subc: test_net.sb", rc == 0 and out.rstrip("\n").endswith(expected), f"exit {rc}: {out[:400]}"))
        # transpilers: python and javascript
        for lang, ext, runner in (("python", ".py", ["python3"]), ("js", ".js", ["node"])):
            if not tool_available(runner[0]):
                results.append((f"sub {lang}: test_net.sb", None, "runtime not available"))
                continue
            out_file = os.path.join(workdir, "test_net" + ext)
            rc, out = run([SUB, sb_path, lang], cwd=workdir)
            if rc != 0 or not os.path.exists(out_file):
                results.append((f"sub {lang}: test_net.sb", False, f"transpile exit {rc}: {out[:400]}"))
                continue
            rc, out = run(runner + [out_file], cwd=workdir)
            results.append((f"sub {lang}: test_net.sb", rc == 0 and out.rstrip("\n").endswith(expected), f"exit {rc}: {out[:400]}"))
    finally:
        httpd_files.shutdown()
        httpd_echo.shutdown()
        shutil.rmtree(workdir, ignore_errors=True)
    return results


def main():
    missing = [p for p in (SUB, SUBC, SUBI) if not os.path.exists(p)]
    if missing:
        # Name the ones that are missing. "sub/subc/subi not found" does not
        # say whether the build failed, whether the .exe suffix was expected,
        # or which of the three is absent.
        msg = ("not found: %s (looked next to %s; run `make all` first)"
               % (", ".join(os.path.basename(m) for m in missing), ROOT_DIR))
        print("Error: " + msg)
        if ANNOTATE:
            print(f"::error::{msg}")
        sys.exit(1)

    for fname, expected in FIXTURES:
        sb_file = os.path.join(EXAMPLES_DIR, fname)
        print(f"\n=== {fname} ===")
        test_interpreter(sb_file, expected)
        test_native_compile(sb_file, expected)
        test_transpile_and_run(sb_file, expected, "python", ".py", ["python3"])
        test_transpile_and_run(sb_file, expected, "js", ".js", ["node"])
        # C++ needs a compile step first, so it's handled separately below
        # rather than through test_transpile_and_run's single-runner shape.
        stem = os.path.splitext(fname)[0]
        if tool_available("g++"):
            label = f"sub cpp(compiled): {fname}"
            rc, out = run([SUB, sb_file, "cpp"])
            cpp_file = os.path.join(ROOT_DIR, stem + ".cpp")
            if "not supported by the" in out and "transpiler" in out:
                # Honest refusal: the c++ target cannot express this
                # construct and says so. Nothing to test there.
                print(f"  SKIP {label} ({' '.join(l for l in out.splitlines() if 'not supported' in l)[:60]})")
            elif rc == 0 and os.path.exists(cpp_file):
                bin_path = os.path.join(ROOT_DIR, stem + "_cpp" + EXE)
                rc, out = run(["g++", cpp_file, "-o", bin_path])
                if rc == 0 and os.path.exists(bin_path):
                    rc, run_out = run([bin_path])
                    check_suffix(run_out, expected, label)
                    os.remove(bin_path)
                else:
                    fail(label, f"g++ compile failed: {out}")
            else:
                fail(label, f"transpile failed: {out}")
            if os.path.exists(cpp_file):
                os.remove(cpp_file)
        else:
            print(f"  SKIP sub cpp(compiled): {fname} (g++ not found)")

    # --- Special-purpose checks ---

    # 64-bit integer precision. int+int used to go through a double, so
    # 2**53 + 1 rounded to 2**53 through subi while the native backend kept
    # the exact value. Only the interpreter and subc are int64-native here;
    # the JS target genuinely cannot represent this value, which is why it
    # is not a plain FIXTURE.
    print("\n=== int64 precision ===")
    sb_file = os.path.join(SCRIPT_DIR, "test_int64.sb")
    expected = "9007199254740993\n4611686018427387905"
    test_interpreter(sb_file, expected)
    if tool_available("gcc"):
        out_name = os.path.join(ROOT_DIR, "_regtest_i64")
        label = "subc (via C): test_int64.sb"
        rc, out = run([SUBC, sb_file, "--via-c", "-o", out_name])
        if rc == 0 and os.path.exists(out_name + EXE):
            rc, run_out = run([out_name + EXE])
            os.remove(out_name + EXE)
            if rc != 0:
                fail(label, f"run exit {rc}: {run_out}")
            else:
                check_suffix(run_out, expected, label)
        else:
            fail(label, f"compile exit {rc}: {out}")
    # and the machine-code backend with no compiler involved at all
    label = "subc (machine code): test_int64.sb"
    out_name = os.path.join(ROOT_DIR, "_regtest_i64n")
    rc, out = run([SUBC, sb_file, "--native", "-o", out_name])
    if rc == 0 and os.path.exists(out_name + EXE):
        rc, run_out = run([out_name + EXE])
        os.remove(out_name + EXE)
        if rc != 0:
            fail(label, f"run exit {rc}: {run_out}")
        else:
            check_suffix(run_out, expected, label)
    elif "not available for this host" not in out:
        fail(label, f"compile exit {rc}: {out}")

    # --- Networking: net_fetch / net_post / net_download ---
    # Starts its own servers on loopback (an http.server for files and a
    # tiny echo handler for POST), so the check is offline-safe: nothing
    # leaves the machine, and both servers die with the test.
    print("\n=== networking (local servers on 127.0.0.1) ===")
    net_results = run_net_checks()
    for label, ok, detail in net_results:
        if ok is None:
            print(f"  SKIP {label} ({detail})")
        elif ok:
            print(f"  OK   {label}")
        else:
            fail(label, detail)

    print(f"\n{'='*40}")
    if failures:
        print(f"{len(failures)} check(s) failed:")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print("All checks passed.")
    sys.exit(0)


if __name__ == "__main__":
    main()
