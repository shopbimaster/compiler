#!/bin/bash
# ================================================================
# Run all tests with -O1 (测评服务器级别，映射到 OALL = O1+O2+O3)
# 小写 -o1/-o2/-o3 仅用于本地逐级调试
# ================================================================
set +e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}"

run_one_suite() {
    local suite_name="$1"
    local test_dir="$2"
    local timeout_s="$3"
    local tmp_prefix="$4"

    local total=0 pass=0 compile_fail=0 link_fail=0
    local diff_fail=0 segfault=0 timeout=0

    echo ""
    echo "============================================================"
    echo "  ${suite_name} (O1)"
    echo "============================================================"
    echo ""

    mkdir -p "${TMPDIR}/${tmp_prefix}"

    for src in "${test_dir}"/*.sy; do
        [ -f "$src" ] || continue
        local name
        name=$(basename "$src" .sy)
        local asm="${TMPDIR}/${tmp_prefix}/${name}.S"
        local bin="${TMPDIR}/${tmp_prefix}/${name}_bin"
        local infile="${test_dir}/${name}.in"
        local outfile="${test_dir}/${name}.out"
        total=$((total + 1))

        # Compile
        if ! "${BUILD_DIR}/compiler" -S "$src" -o "$asm" -O1 2>/dev/null; then
            echo "  COMPILE_FAIL: ${name}"
            compile_fail=$((compile_fail + 1))
            continue
        fi

        # Link
        if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
            echo "  LINK_FAIL:    ${name}"
            link_fail=$((link_fail + 1))
            continue
        fi

        # Run
        local ret=0
        if [ -f "$infile" ]; then
            timeout ${timeout_s} $QEMU "$bin" < "$infile" > "${TMPDIR}/${tmp_prefix}/${name}_out.txt" 2>/dev/null || ret=$?
        else
            timeout ${timeout_s} $QEMU "$bin" > "${TMPDIR}/${tmp_prefix}/${name}_out.txt" 2>/dev/null || ret=$?
        fi

        if [ $ret -eq 124 ]; then
            echo "  TIMEOUT:      ${name}"
            timeout=$((timeout + 1))
        elif [ $ret -eq 139 ]; then
            echo "  SEGFAULT:     ${name}"
            segfault=$((segfault + 1))
        elif [ ! -f "$outfile" ]; then
            pass=$((pass + 1))
        else
            # Compare output
            head -n -1 "$outfile" > "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" 2>/dev/null
            if [ ! -s "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" ]; then
                > "${TMPDIR}/${tmp_prefix}/${name}_expect.txt"
            fi

            printf '%s\n' "$(cat "${TMPDIR}/${tmp_prefix}/${name}_out.txt")" > "${TMPDIR}/${tmp_prefix}/${name}_act.txt"
            printf '%s\n' "$(cat "${TMPDIR}/${tmp_prefix}/${name}_expect.txt")" > "${TMPDIR}/${tmp_prefix}/${name}_expect.txt"

            if diff -q "${TMPDIR}/${tmp_prefix}/${name}_act.txt" "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" > /dev/null 2>&1; then
                pass=$((pass + 1))
            else
                echo "  OUTPUT_DIFF:  ${name}"
                diff_fail=$((diff_fail + 1))
                diff "${TMPDIR}/${tmp_prefix}/${name}_act.txt" "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" | head -8
                echo "  ---"
            fi
        fi
    done

    echo ""
    echo "  Compile:  $(printf '%3d' $((total - compile_fail))) OK, $(printf '%3d' $compile_fail) FAIL"
    echo "  Link:     $(printf '%3d' $((total - compile_fail - link_fail))) OK, $(printf '%3d' $link_fail) FAIL"
    echo "  Runtime:  $(printf '%3d' $pass) OK, $(printf '%3d' $diff_fail) DIFF, $(printf '%3d' $segfault) SEGFAULT, $(printf '%3d' $timeout) TIMEOUT"

    # Return non-zero if any failures
    if [ $compile_fail -gt 0 ] || [ $link_fail -gt 0 ] || [ $diff_fail -gt 0 ] || [ $segfault -gt 0 ] || [ $timeout -gt 0 ]; then
        return 1
    fi
    return 0
}

# ================================================================
# Main
# ================================================================
echo "============================================================"
echo "  O1 (OALL) Full Test Suite"
echo "============================================================"
echo ""

run_one_suite "Functional Tests" "${PROJECT_DIR}/test/functional" 5 "func"
run_one_suite "H_Functional Tests" "${PROJECT_DIR}/test/h_functional" 15 "hfunc"
run_one_suite "Performance Tests" "${PROJECT_DIR}/test/performance" 15 "perf"