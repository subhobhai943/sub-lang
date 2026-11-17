#!/bin/bash
# bench/run.sh - Build SUB tools and run all benchmarks
#
# Usage:
#   ./bench/run.sh              # run all benchmarks, print wall times
#   ./bench/run.sh --baseline   # save current times as baseline
#   ./bench/run.sh --compare    # compare against saved baseline
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BENCH_DIR="$SCRIPT_DIR"
BASELINE_FILE="$BENCH_DIR/baseline.txt"

MODE="${1:-run}"

cd "$ROOT_DIR"

# Build everything
echo "Building SUB tools..."
make clean >/dev/null 2>&1 || true
make all 2>&1 | tail -3
echo ""

SUBI="./subi"

if [ ! -x "$SUBI" ]; then
    echo "ERROR: subi binary not found. Build failed."
    exit 1
fi

RESULTS_FILE="$BENCH_DIR/results.txt"

# List of benchmarks: name, file, expected_iterations (for reference)
declare -a BENCHS=(
    "fib:bench/fib.sb"
    "loop20m:bench/loop20m.sb"
    "string_ops:bench/string_ops.sb"
    "array_ops:bench/array_ops.sb"
    "object_ops:bench/object_ops.sb"
    "func_calls:bench/func_calls.sb"
)

run_bench() {
    local name="$1"
    local file="$2"
    local start end elapsed
    start=$(date +%s.%N)
    "$SUBI" "$file" >/dev/null 2>&1
    local rc=$?
    end=$(date +%s.%N)
    elapsed=$(awk "BEGIN {print $end - $start}")
    echo "${name}: ${elapsed}s (exit=$rc)"
    echo "${name} ${elapsed}" >> "$RESULTS_FILE"
}

echo "========================================"
echo " SUB Interpreter Benchmarks"
echo "========================================"
echo ""

# Clear temp results
> "$RESULTS_FILE"

for entry in "${BENCHS[@]}"; do
    name="${entry%%:*}"
    file="${entry#*:}"
    run_bench "$name" "$file"
done

echo ""
echo "========================================"
echo " Results:"
cat "$RESULTS_FILE"
echo "========================================"

if [ "$MODE" = "--baseline" ]; then
    cp "$RESULTS_FILE" "$BASELINE_FILE"
    echo "Baseline saved to $BASELINE_FILE"
elif [ "$MODE" = "--compare" ]; then
    if [ ! -f "$BASELINE_FILE" ]; then
        echo "No baseline file found. Run with --baseline first."
        exit 1
    fi
    echo ""
    echo "Comparison vs baseline:"
    while read -r name time; do
        baseline_time=$(grep "^${name} " "$BASELINE_FILE" | awk '{print $2}')
        if [ -n "$baseline_time" ]; then
            ratio=$(awk "BEGIN {print $time / $baseline_time}")
            echo "  ${name}: ${time}s vs baseline ${baseline_time}s (${ratio}x)"
        fi
    done < "$RESULTS_FILE"
fi

rm -f "$RESULTS_FILE"
