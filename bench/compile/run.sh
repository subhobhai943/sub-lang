#!/bin/bash
# bench/compile/run.sh - Compile-time scaling benchmarks for the SUB toolchain.
#
# Usage:
#   ./bench/compile/run.sh              # print wall times at each size
#   ./bench/compile/run.sh --baseline   # save current times as baseline
#   ./bench/compile/run.sh --compare    # compare against saved baseline
#
# Measures, for N in 250 500 1000 2000 4000 and each program shape:
#   subc        native compiler,   `subc file -o out`      (wall time)
#   sub         C backend only,    `sub file c`             (transpile, no host cc)
#   subi        interpreter,       `subi file`              (parse + analyze + run)
# and prints the growth exponent between consecutive sizes:
#   exp = ln(t2/t1) / ln(N2/N1)   (1.0 = linear, >1 = super-linear)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
BENCH_DIR="$SCRIPT_DIR"
BASELINE_FILE="$BENCH_DIR/baseline.txt"
TMP_ROOT="${TMPDIR:-/tmp/kilo2}/bench-compile-$$"
GEN="$BENCH_DIR/gen.py"

SIZES=(250 500 1000 2000 4000)
SHAPES="functions variables nested chain strings"

MODE="${1:-run}"
REPEATS=3

cd "$ROOT_DIR"

# --- Build the tools -------------------------------------------------------
echo "Building SUB tools..."
make clean >/dev/null 2>&1 || true
make all >/dev/null 2>&1
for t in sub subc subi; do
    if [ ! -x "./$t" ]; then
        echo "ERROR: ./$t not found. Build failed." >&2
        exit 1
    fi
done

SUBC="./subc"
SUB="./sub"
SUBI="./subi"

mkdir -p "$TMP_ROOT"

# --- Helpers ---------------------------------------------------------------
now() { date +%s.%N; }

# best of $REPEATS runs of a command; prints seconds with 6 decimals
time_best() {
    local best=""
    for i in $(seq 1 "$REPEATS"); do
        local start end el rc=0
        start=$(now)
        "$@" >/dev/null 2>&1 || rc=$?
        end=$(now)
        el=$(awk "BEGIN {print $end - $start}")
        if [ -z "$best" ] || awk "BEGIN {exit !($el < $best)}"; then
            best=$el
        fi
        if [ $rc -ne 0 ]; then
            echo "WARNING: $* exited $rc" >&2
        fi
    done
    printf "%.6f" "$best"
}

gen_prog() {
    python3 "$GEN" "$1" "$2"
}

write_prog() {
    local shape="$1" n="$2" file="$TMP_ROOT/${shape}_${n}.sb"
    gen_prog "$shape" "$n" > "$file"
    echo "$file"
}

compile_subc() {
    local file="$1" out="$TMP_ROOT/$(basename "$1" .sb)"
    $SUBC "$file" -o "$out" >/dev/null 2>&1
}

transpile_sub() {
    local file="$1" c="$TMP_ROOT/$(basename "$1" .sb).c"
    $SUB "$file" c > "$c" 2>/dev/null
}

run_subi() {
    $SUBI "$1" >/dev/null 2>&1
}

exponent() {
    # exponent t1@N1 -> t2@N2
    awk -v n1="$1" -v t1="$2" -v n2="$3" -v t2="$4" \
        'BEGIN { if (t1 <= 0 || t2 <= 0) print "n/a"; else printf "%.2f", log(t2/t1)/log(n2/n1) }'
}

RESULTS_FILE="$BENCH_DIR/results.txt"
: > "$RESULTS_FILE"

echo "============================================"
echo " SUB compile-time scaling"
echo "============================================"
echo " sizes: ${SIZES[*]}"
echo ""

for shape in $SHAPES; do
    echo "----- $shape -----"
    printf "   N       subc(s)   sub(s)    subi(s)   exp_subc exp_sub exp_subi\n" >> "$RESULTS_FILE"
    echo "   N       subc(s)   sub(s)    subi(s)   exp_subc exp_sub exp_subi"
    prev_n=""; prev_subc=""; prev_sub=""; prev_subi=""
    for n in "${SIZES[@]}"; do
        file=$(write_prog "$shape" "$n")
        subc_t=$(time_best compile_subc "$file")
        sub_t=$(time_best transpile_sub "$file")
        subi_t=$(time_best run_subi "$file")

        ec=$( [ -n "$prev_n" ] && exponent "$prev_n" "$prev_subc" "$n" "$subc_t" || echo "-" )
        es=$( [ -n "$prev_n" ] && exponent "$prev_n" "$prev_sub"   "$n" "$sub_t"   || echo "-" )
        ei=$( [ -n "$prev_n" ] && exponent "$prev_n" "$prev_subi"  "$n" "$subi_t"  || echo "-" )

        printf "   %-6d %9s %9s %9s %8s %7s %7s\n" \
            "$n" "$subc_t" "$sub_t" "$subi_t" "$ec" "$es" "$ei"
        printf "   %-6d %9s %9s %9s %8s %7s %7s\n" \
            "$n" "$subc_t" "$sub_t" "$subi_t" "$ec" "$es" "$ei" >> "$RESULTS_FILE"

        prev_n=$n; prev_subc=$subc_t; prev_sub=$sub_t; prev_subi=$subi_t
    done
    echo ""
done

if [ "$MODE" = "--baseline" ]; then
    cp "$RESULTS_FILE" "$BASELINE_FILE"
    echo "Baseline saved to $BASELINE_FILE"
elif [ "$MODE" = "--compare" ]; then
    if [ ! -f "$BASELINE_FILE" ]; then
        echo "No baseline file found. Run with --baseline first." >&2
        exit 1
    fi
    echo ""
    echo "Comparison vs baseline:"
    awk 'NR==FNR{b[FNR]=$0; next} {print $0, " | baseline: ", b[FNR]}' "$BASELINE_FILE" "$RESULTS_FILE" | head -40
fi

rm -rf "$TMP_ROOT"
rm -f "$RESULTS_FILE"
