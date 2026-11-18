#!/usr/bin/env python3
"""bench/compile/gen.py - Generate synthetic SUB programs for compile scaling.

Usage:
    python3 bench/compile/gen.py functions N   > prog.sb
    python3 bench/compile/gen.py variables N   > prog.sb
    python3 bench/compile/gen.py nested N      > prog.sb
    python3 bench/compile/gen.py chain N       > prog.sb
    python3 bench/compile/gen.py strings N     > prog.sb

Kinds:

functions   N small functions fn fK(a, b) { let x = a*K+b; if x>100
            {return x-1} return x+1 } plus one println of the last one.
            This is the shape from the reported profiling problem.

variables   N top-level variables, each initialized from the previous one,
            then the last one printed.

nested      One deeply nested arithmetic expression of N binary nodes.

chain       A long if/elif/else chain of N conditions, each comparing the
            same variable against a different constant.

strings     N println calls, each with a distinct string literal, plus
            half as many duplicate literals (to exercise the literal pool).
"""
import sys

FN_BODY = """fn f{k}(a, b) {{
    let x = a*{k}+b;
    if x > 100 {{
        return x - 1
    }}
    return x + 1
}}"""


def functions(n: int) -> str:
    lines = [FN_BODY.format(k=k) for k in range(n)]
    lines.append(f"println(f{n-1}(1, 2))")
    return "\n".join(lines) + "\n"


def variables(n: int) -> str:
    lines = [f"let v{k} = {k};" for k in range(n)]
    lines.append(f"let vsum = v{n-1};")
    lines.append("println(vsum)")
    return "\n".join(lines) + "\n"


def nested(n: int) -> str:
    # Right-nested chain of additions: 1 + (2 + (3 + (... + N)))
    expr = str(n)
    for k in range(n - 1, 0, -1):
        expr = f"{k} + ({expr})"
    return f"println({expr})\n"


def chain(n: int) -> str:
    lines = ["let v = 0;"]
    for k in range(n):
        lines.append(f"if v == {k} {{ let _ = {k}; }}")
        lines.append("elif false { let _ = 0; }")
    lines.append("else { let _ = -1; }")
    lines.append("println(v)")
    return "\n".join(lines) + "\n"


def strings(n: int) -> str:
    lines = [f'println("lit{k}")' for k in range(n)]
    # duplicates: half of them repeated, to exercise dedup in the literal pool
    for k in range(n // 2):
        lines.append(f'println("lit{k}")')
    return "\n".join(lines) + "\n"


KINDS = {
    "functions": functions,
    "variables": variables,
    "nested": nested,
    "chain": chain,
    "strings": strings,
}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in KINDS:
        print(f"usage: {sys.argv[0]} { '|'.join(KINDS) } N", file=sys.stderr)
        return 2
    try:
        n = int(sys.argv[2])
    except ValueError:
        print(f"invalid N: {sys.argv[2]}", file=sys.stderr)
        return 2
    sys.stdout.write(KINDS[sys.argv[1]](n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
