#!/bin/bash
# Compare O0 and judge-level O1 output against GCC's execution of the same
# SysY source. This remains useful when official .out files are unavailable.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${PROJECT_DIR}/build}"
SUITE="${1:-functional}"
LIMIT="${2:-20}"
TIMEOUT_SEC="${TIMEOUT_SEC:-15}"

case "$SUITE" in
    functional|h_functional|performance) ;;
    *)
        echo "Usage: $0 [functional|h_functional|performance] [limit|all]" >&2
        exit 2
        ;;
esac

for tool in riscv64-linux-gnu-gcc qemu-riscv64 timeout diff; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Error: required tool not found: $tool" >&2
        exit 2
    fi
done

if [ ! -x "$BUILD_DIR/compiler" ]; then
    echo "Error: compiler not found at $BUILD_DIR/compiler" >&2
    exit 2
fi

if [ ! -f "$BUILD_DIR/libsylib.a" ]; then
    bash "$SCRIPT_DIR/build_sylib.sh" || exit 2
fi

TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/sysy-diff.XXXXXX")"
trap 'rm -rf "$TMP_ROOT"' EXIT

run_case() {
    local bin="$1"
    local input="$2"
    local output="$3"
    local status

    if [ -f "$input" ]; then
        timeout "$TIMEOUT_SEC" qemu-riscv64 "$bin" < "$input" > "$output" 2>/dev/null
    else
        timeout "$TIMEOUT_SEC" qemu-riscv64 "$bin" > "$output" 2>/dev/null
    fi
    status=$?
    RUN_STATUS=$status
    printf '\n__EXIT__=%d\n' "$status" >> "$output"
    return "$status"
}

total=0
passed=0
failed=0
skipped=0
partial=0

for src in "$PROJECT_DIR/test/$SUITE"/*.sy; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .sy)"
    if [ -n "${CASE_FILTER:-}" ] && [[ ! "$name" =~ $CASE_FILTER ]]; then
        continue
    fi
    if [ "$LIMIT" != "all" ] && [ "$total" -ge "$LIMIT" ]; then
        break
    fi
    total=$((total + 1))

    input="${src%.sy}.in"
    case_dir="$TMP_ROOT/$name"
    mkdir -p "$case_dir"

    if ! "$BUILD_DIR/compiler" -S "$src" -o "$case_dir/o0.S" -O0 >/dev/null 2>&1 ||
       ! "$BUILD_DIR/compiler" -S "$src" -o "$case_dir/o1.S" -O1 >/dev/null 2>&1; then
        echo "FAIL  $name (compiler error)"
        failed=$((failed + 1))
        continue
    fi

    if ! riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
            "$case_dir/o0.S" "$BUILD_DIR/libsylib.a" -o "$case_dir/o0" >/dev/null 2>&1 ||
       ! riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
            "$case_dir/o1.S" "$BUILD_DIR/libsylib.a" -o "$case_dir/o1" >/dev/null 2>&1; then
        echo "FAIL  $name (link error)"
        failed=$((failed + 1))
        continue
    fi

    # SysY is a C subset. Pre-including sylib.h supplies runtime declarations
    # and expands starttime()/stoptime() for the GCC reference executable.
    if ! riscv64-linux-gnu-gcc -x c -std=c11 -O0 -fcommon \
            -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
            -I"$PROJECT_DIR/SysYlib" -include "$PROJECT_DIR/SysYlib/sylib.h" \
            "$src" "$PROJECT_DIR/SysYlib/sylib.c" -o "$case_dir/ref" >/dev/null 2>&1; then
        run_case "$case_dir/o0" "$input" "$case_dir/o0.out" || true
        o0_status=$RUN_STATUS
        run_case "$case_dir/o1" "$input" "$case_dir/o1.out" || true
        o1_status=$RUN_STATUS
        if [ "$o0_status" -eq 124 ] || [ "$o1_status" -eq 124 ]; then
            echo "FAIL  $name (O0/O1 timeout)"
            failed=$((failed + 1))
            continue
        fi
        if diff -q "$case_dir/o0.out" "$case_dir/o1.out" >/dev/null; then
            echo "PART  $name (O0/O1 agree; GCC rejected source)"
            partial=$((partial + 1))
        else
            echo "FAIL  $name (O0/O1 mismatch; GCC rejected source)"
            failed=$((failed + 1))
        fi
        continue
    fi

    run_case "$case_dir/ref" "$input" "$case_dir/ref.out" || true
    ref_status=$RUN_STATUS
    run_case "$case_dir/o0" "$input" "$case_dir/o0.out" || true
    o0_status=$RUN_STATUS
    run_case "$case_dir/o1" "$input" "$case_dir/o1.out" || true
    o1_status=$RUN_STATUS

    if [ "$o0_status" -eq 124 ] || [ "$o1_status" -eq 124 ]; then
        echo "FAIL  $name (O0/O1 timeout)"
        failed=$((failed + 1))
        continue
    fi

    if [ "$ref_status" -eq 124 ]; then
        if diff -q "$case_dir/o0.out" "$case_dir/o1.out" >/dev/null; then
            echo "PART  $name (O0/O1 agree; GCC reference timeout)"
            partial=$((partial + 1))
        else
            echo "FAIL  $name (O0/O1 mismatch; GCC reference timeout)"
            failed=$((failed + 1))
        fi
        continue
    fi

    if diff -q "$case_dir/ref.out" "$case_dir/o0.out" >/dev/null &&
       diff -q "$case_dir/ref.out" "$case_dir/o1.out" >/dev/null; then
        echo "PASS  $name"
        passed=$((passed + 1))
    else
        o0_relation="diff"
        o1_relation="diff"
        o0_o1_relation="diff"
        diff -q "$case_dir/ref.out" "$case_dir/o0.out" >/dev/null && o0_relation="match"
        diff -q "$case_dir/ref.out" "$case_dir/o1.out" >/dev/null && o1_relation="match"
        diff -q "$case_dir/o0.out" "$case_dir/o1.out" >/dev/null && o0_o1_relation="match"
        echo "FAIL  $name (ref/O0=$o0_relation, ref/O1=$o1_relation, O0/O1=$o0_o1_relation)"
        if [ "${DIAG_LEVELS:-0}" = "1" ]; then
            stage_summary=""
            for stage in o1 o2 o3; do
                relation="error"
                if "$BUILD_DIR/compiler" -S "$src" -o "$case_dir/$stage.S" "-$stage" >/dev/null 2>&1 &&
                   riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
                       "$case_dir/$stage.S" "$BUILD_DIR/libsylib.a" -o "$case_dir/$stage" >/dev/null 2>&1; then
                    run_case "$case_dir/$stage" "$input" "$case_dir/$stage.out" || true
                    if [ "$RUN_STATUS" -eq 124 ]; then
                        relation="timeout"
                    elif diff -q "$case_dir/ref.out" "$case_dir/$stage.out" >/dev/null; then
                        relation="match"
                    else
                        relation="diff"
                    fi
                fi
                stage_summary="$stage_summary $stage=$relation"
            done
            echo "      stages:$stage_summary"
        fi
        failed=$((failed + 1))
    fi
done

echo ""
echo "Differential result: $passed referenced, $partial O0/O1-only, $failed failed, $skipped skipped ($total selected)"
[ "$failed" -eq 0 ]
