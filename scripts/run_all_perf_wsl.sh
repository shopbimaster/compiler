#!/usr/bin/env bash
# Build the compiler and run SysY performance tests through QEMU in WSL.
#
# Usage:
#   bash scripts/run_all_perf_wsl.sh             # run all 60 cases
#   bash scripts/run_all_perf_wsl.sh 03_sort1    # run selected cases
#   bash scripts/run_all_perf_wsl.sh --no-build 03_sort1 01_mm1
#
# Environment overrides:
#   PERF_TIMEOUT=300
#   COMPILER=/path/to/compiler
#   SYLIB=/path/to/libsylib.a
#   KEEP_PERF_WORK=1

set -uo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build-wsl"}
COMPILER=${COMPILER:-"$BUILD_DIR/compiler"}
SYLIB=${SYLIB:-"$BUILD_DIR/libsylib.a"}
PERF_DIR=${PERF_DIR:-"$ROOT_DIR/test/performance"}
PERF_TIMEOUT=${PERF_TIMEOUT:-300}
RISCV_GCC=${RISCV_GCC:-riscv64-linux-gnu-gcc}

DO_BUILD=1
CASES=()
for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        -h|--help)
            sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        --*)
            echo "Unknown option: $arg" >&2
            exit 2
            ;;
        *) CASES+=("$arg") ;;
    esac
done

find_qemu() {
    if command -v qemu-riscv64-static >/dev/null 2>&1; then
        command -v qemu-riscv64-static
    elif command -v qemu-riscv64 >/dev/null 2>&1; then
        command -v qemu-riscv64
    else
        return 1
    fi
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing command: $1" >&2
        exit 2
    fi
}

if [[ ! "$PERF_TIMEOUT" =~ ^[1-9][0-9]*$ ]]; then
    echo "PERF_TIMEOUT must be a positive integer, got: $PERF_TIMEOUT" >&2
    exit 2
fi

require_command cmake
require_command "$RISCV_GCC"
require_command timeout
QEMU=$(find_qemu) || {
    echo "Missing qemu-riscv64 or qemu-riscv64-static" >&2
    exit 2
}

if (( DO_BUILD )); then
    echo "[build] Updating WSL Release build..."
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        require_command clang++-18
        cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=clang++-18 || exit 2
    fi
    # The official submission builds the compiler, not local unit-test targets.
    # Building the explicit target also prevents stale development-only tests
    # from blocking performance-case execution.
    cmake --build "$BUILD_DIR" --target compiler --parallel "$(nproc)" || exit 2
    BUILD_DIR="$BUILD_DIR" bash "$SCRIPT_DIR/build_sylib.sh" || exit 2
fi

if [[ ! -x "$COMPILER" ]]; then
    echo "Compiler not found or not executable: $COMPILER" >&2
    exit 2
fi
if [[ ! -f "$SYLIB" ]]; then
    echo "Runtime library not found: $SYLIB" >&2
    exit 2
fi

if (( ${#CASES[@]} == 0 )); then
    while IFS= read -r sy; do
        CASES+=("$(basename "$sy" .sy)")
    done < <(find "$PERF_DIR" -maxdepth 1 -type f -name '*.sy' | sort)
fi

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/compiler-perf.XXXXXX")
cleanup() {
    if [[ ${KEEP_PERF_WORK:-0} == 1 ]]; then
        echo "Artifacts kept at: $WORK_DIR"
    else
        rm -rf -- "$WORK_DIR"
    fi
}
trap cleanup EXIT

normalize_output() {
    local input=$1
    sed 's/[[:space:]]*$//' "$input"
}

PASS=0
FAIL=0
TOTAL_SYSY_SECONDS=0
TOTAL_WALL_SECONDS=0
FAILED_CASES=()

printf '\n%-28s %-8s %12s %12s\n' \
    "Case" "Result" "SysY time" "QEMU wall"
printf '%-28s %-8s %12s %12s\n' \
    "----------------------------" "--------" "------------" "------------"

for name in "${CASES[@]}"; do
    sy="$PERF_DIR/$name.sy"
    input="$PERF_DIR/$name.in"
    expected="$PERF_DIR/$name.out"
    assembly="$WORK_DIR/$name.s"
    executable="$WORK_DIR/$name.elf"
    actual="$WORK_DIR/$name.actual"
    stderr_file="$WORK_DIR/$name.stderr"
    status=AC
    sysy_elapsed="-"
    wall_elapsed="-"

    if [[ ! -f "$sy" ]]; then
        status=NOTFOUND
    elif ! "$COMPILER" "$sy" -S -o "$assembly" -O1 \
            >"$WORK_DIR/$name.compiler.stdout" \
            2>"$WORK_DIR/$name.compiler.stderr"; then
        status=CFAIL
    elif ! "$RISCV_GCC" -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
            -o "$executable" "$assembly" "$SYLIB" -lm \
            >"$WORK_DIR/$name.link.stdout" \
            2>"$WORK_DIR/$name.link.stderr"; then
        status=LFAIL
    else
        start_ns=$(date +%s%N)
        if [[ -f "$input" ]]; then
            timeout "$PERF_TIMEOUT" "$QEMU" "$executable" \
                <"$input" >"$actual" 2>"$stderr_file"
        else
            timeout "$PERF_TIMEOUT" "$QEMU" "$executable" \
                </dev/null >"$actual" 2>"$stderr_file"
        fi
        rc=$?
        end_ns=$(date +%s%N)
        wall_elapsed=$(awk -v start="$start_ns" -v end="$end_ns" \
            'BEGIN { printf "%.4f", (end - start) / 1000000000 }')

        # SysY performance cases delimit the measured region with
        # starttime()/stoptime(). The runtime emits the accumulated interval
        # as "TOTAL: <h>H-<m>M-<s>S-<us>us" on stderr at process exit.
        total_line=$(grep '^TOTAL:' "$stderr_file" 2>/dev/null | tail -n 1)
        if [[ "$total_line" =~ ^TOTAL:\ ([0-9]+)H-([0-9]+)M-([0-9]+)S-([0-9]+)us$ ]]; then
            sysy_elapsed=$(awk \
                -v hours="${BASH_REMATCH[1]}" \
                -v minutes="${BASH_REMATCH[2]}" \
                -v seconds="${BASH_REMATCH[3]}" \
                -v micros="${BASH_REMATCH[4]}" \
                'BEGIN { printf "%.6f", hours * 3600 + minutes * 60 + seconds + micros / 1000000 }')
        fi

        if (( rc == 124 )); then
            status=TIMEOUT
        else
            # SysY judging treats main's exit status as the final output line.
            # Some programs intentionally do not print a trailing newline, so
            # keep the exit status on a separate line just like the grader.
            if [[ -s "$actual" ]] && [[ $(tail -c 1 "$actual" | od -An -tu1 | tr -d ' ') != 10 ]]; then
                printf '\n' >>"$actual"
            fi
            printf '%s\n' "$rc" >>"$actual"
            if [[ ! -f "$expected" ]]; then
                status=NO-OUT
            elif ! diff -q \
                    <(normalize_output "$actual") \
                    <(normalize_output "$expected") \
                    >/dev/null 2>&1; then
                status=WA
            fi
        fi
    fi

    if [[ "$sysy_elapsed" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        TOTAL_SYSY_SECONDS=$(awk -v total="$TOTAL_SYSY_SECONDS" -v value="$sysy_elapsed" \
            'BEGIN { printf "%.6f", total + value }')
        sysy_elapsed="${sysy_elapsed}s"
    fi
    if [[ "$wall_elapsed" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
        TOTAL_WALL_SECONDS=$(awk -v total="$TOTAL_WALL_SECONDS" -v value="$wall_elapsed" \
            'BEGIN { printf "%.6f", total + value }')
        wall_elapsed="${wall_elapsed}s"
    fi

    if [[ "$status" == AC || "$status" == NO-OUT ]]; then
        ((PASS+=1))
    else
        ((FAIL+=1))
        FAILED_CASES+=("$name:$status")
    fi
    printf '%-28s %-8s %12s %12s\n' \
        "$name" "$status" "$sysy_elapsed" "$wall_elapsed"
done

printf '\nSummary: %d passed, %d failed, SysY total %.6fs, QEMU wall total %.4fs\n' \
    "$PASS" "$FAIL" "$TOTAL_SYSY_SECONDS" "$TOTAL_WALL_SECONDS"
echo "Note: SysY time excludes QEMU startup and is preferred for local A/B comparisons."
echo "      Neither local timing column is comparable to the official BOOM board time."

if (( FAIL > 0 )); then
    printf 'Failed:'
    printf ' %s' "${FAILED_CASES[@]}"
    printf '\n'
    echo "Set KEEP_PERF_WORK=1 to retain compiler, linker, and runtime logs."
    exit 1
fi
