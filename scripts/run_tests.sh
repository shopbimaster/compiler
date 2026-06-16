#!/bin/bash
# ================================================================
# SysY Compiler - Unified Test Runner
# ================================================================
# Usage:
#   ./scripts/run_tests.sh func [O1|o0|o1|o2|o3]  # functional tests
#   ./scripts/run_tests.sh hfunc [O1|o0|o1|o2|o3]  # h_functional tests
#   ./scripts/run_tests.sh perf [O1|o0|o1|o2|o3]   # performance tests
#   ./scripts/run_tests.sh all [O1|o0|o1|o2|o3]     # all three suites
#   ./scripts/run_tests.sh quick                        # quick smoke test
# ================================================================
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}"

SUITE="${1:-quick}"
OPT="${2:-O0}"
TIMEOUT_SEC=15

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ================================================================
# Utility: normalize output (strip trailing newlines, add one)
# ================================================================
norm() {
    local content
    content=$(cat "$1" 2>/dev/null)
    printf '%s\n' "$content" > "$1"
}

# ================================================================
# Run one test suite
# ================================================================
run_suite() {
    local suite_name="$1"
    local test_dir="$2"
    local timeout_s="$3"
    local tmp_prefix="$4"

    local compile_ok=0 compile_fail=0 link_ok=0 link_fail=0
    local run_ok=0 run_diff=0 run_seg=0 run_timeout=0

    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  ${suite_name} (${OPT})${NC}"
    echo -e "${CYAN}============================================================${NC}"
    echo ""

    mkdir -p "${TMPDIR}/${tmp_prefix}"

    for src in "${test_dir}"/*.sy; do
        [ -f "$src" ] || continue
        local name=$(basename "$src" .sy)
        local asm="${TMPDIR}/${tmp_prefix}/${name}.S"
        local bin="${TMPDIR}/${tmp_prefix}/${name}_bin"
        local infile="${test_dir}/${name}.in"
        local outfile="${test_dir}/${name}.out"

        # Compile
        if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${OPT} 2>/dev/null; then
            echo -e "  ${RED}COMPILE FAIL${NC}: ${name}"
            compile_fail=$((compile_fail + 1))
            continue
        fi
        compile_ok=$((compile_ok + 1))

        # Link
        if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
            echo -e "  ${RED}LINK FAIL${NC}:    ${name}"
            link_fail=$((link_fail + 1))
            continue
        fi
        link_ok=$((link_ok + 1))

        # Run
        if [ -f "$infile" ]; then
            timeout ${timeout_s} $QEMU "$bin" < "$infile" > "${TMPDIR}/${tmp_prefix}/${name}_out.txt" 2>/dev/null
        else
            timeout ${timeout_s} $QEMU "$bin" > "${TMPDIR}/${tmp_prefix}/${name}_out.txt" 2>/dev/null
        fi
        local ret=$?

        if [ $ret -eq 124 ]; then
            echo -e "  ${YELLOW}TIMEOUT${NC}:      ${name}"
            run_timeout=$((run_timeout + 1))
        elif [ $ret -eq 139 ]; then
            echo -e "  ${RED}SEGFAULT${NC}:     ${name}"
            run_seg=$((run_seg + 1))
        elif [ ! -f "$outfile" ]; then
            run_ok=$((run_ok + 1))
        else
            # Compare output
            head -n -1 "$outfile" > "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" 2>/dev/null
            if [ ! -s "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" ]; then
                > "${TMPDIR}/${tmp_prefix}/${name}_expect.txt"
            fi

            cp "${TMPDIR}/${tmp_prefix}/${name}_out.txt" "${TMPDIR}/${tmp_prefix}/${name}_act.txt"
            norm "${TMPDIR}/${tmp_prefix}/${name}_act.txt"
            norm "${TMPDIR}/${tmp_prefix}/${name}_expect.txt"

            if diff -q "${TMPDIR}/${tmp_prefix}/${name}_act.txt" "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" > /dev/null 2>&1; then
                run_ok=$((run_ok + 1))
            else
                echo -e "  ${RED}OUTPUT DIFF${NC}:  ${name}"
                run_diff=$((run_diff + 1))
                diff "${TMPDIR}/${tmp_prefix}/${name}_act.txt" "${TMPDIR}/${tmp_prefix}/${name}_expect.txt" | head -8
                echo "  ---"
            fi
        fi
    done

    echo ""
    echo -e "  ${GREEN}Compile:  ${compile_ok} OK${NC}, ${RED}${compile_fail} FAIL${NC}"
    echo -e "  ${GREEN}Link:     ${link_ok} OK${NC}, ${RED}${link_fail} FAIL${NC}"
    echo -e "  ${GREEN}Runtime:  ${run_ok} OK${NC}, ${RED}${run_diff} DIFF${NC}, ${RED}${run_seg} SEGFAULT${NC}, ${YELLOW}${run_timeout} TIMEOUT${NC}"

    # Return non-zero if any failures
    if [ $compile_fail -gt 0 ] || [ $link_fail -gt 0 ] || [ $run_diff -gt 0 ] || [ $run_seg -gt 0 ]; then
        return 1
    fi
    return 0
}

# ================================================================
# Quick smoke test
# ================================================================
run_quick() {
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  Quick Smoke Test${NC}"
    echo -e "${CYAN}============================================================${NC}"
    echo ""

    local tests=("00_main:3" "01_var_defn2:10" "11_add2:9" "26_while_test1:3" "29_break:201")
    local pass=0 fail=0

    for entry in "${tests[@]}"; do
        local name="${entry%%:*}"
        local expected="${entry##*:}"
        local src="${PROJECT_DIR}/test/functional/${name}.sy"
        local asm="${TMPDIR}/quick_${name}.S"
        local bin="${TMPDIR}/quick_${name}_bin"

        if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -O0 2>/dev/null; then
            echo -e "  ${RED}FAIL${NC}: ${name} - compile error"
            fail=$((fail + 1))
            continue
        fi
        if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
            echo -e "  ${RED}FAIL${NC}: ${name} - link error"
            fail=$((fail + 1))
            continue
        fi
        local ret
        $QEMU "$bin" > /dev/null 2>&1
        ret=$?
        if [ "$ret" = "$expected" ]; then
            echo -e "  ${GREEN}PASS${NC}: ${name} -> return ${ret}"
            pass=$((pass + 1))
        else
            echo -e "  ${RED}FAIL${NC}: ${name} -> return ${ret}, expected ${expected}"
            fail=$((fail + 1))
        fi
    done

    echo ""
    echo -e "  ${GREEN}${pass} passed${NC}, ${RED}${fail} failed${NC}"
    return $fail
}

# ================================================================
# Main
# ================================================================
# Ensure runtime library exists
if [ ! -f "$SYLIB_A" ]; then
    echo -e "${YELLOW}Building runtime library...${NC}"
    bash "${PROJECT_DIR}/scripts/build/build_sylib.sh"
fi

# Ensure compiler exists
if [ ! -f "${BUILD_DIR}/compiler" ]; then
    echo -e "${RED}Error: compiler not found at ${BUILD_DIR}/compiler${NC}"
    echo "Run: cmake --build build --target compiler"
    exit 1
fi

case "$SUITE" in
    quick)
        run_quick
        ;;
    func)
        run_suite "Functional Tests" "${PROJECT_DIR}/test/functional" 5 "func"
        ;;
    hfunc)
        run_suite "H_Functional Tests" "${PROJECT_DIR}/test/h_functional" 15 "hfunc"
        ;;
    perf)
        run_suite "Performance Tests" "${PROJECT_DIR}/test/performance" 15 "perf"
        ;;
    all)
        run_suite "Functional Tests" "${PROJECT_DIR}/test/functional" 5 "func"
        run_suite "H_Functional Tests" "${PROJECT_DIR}/test/h_functional" 15 "hfunc"
        run_suite "Performance Tests" "${PROJECT_DIR}/test/performance" 15 "perf"
        ;;
    *)
        echo "Usage: $0 [quick|func|hfunc|perf|all] [O0|O1|O2|O3|Oall]"
        echo ""
        echo "  quick   - Quick smoke test (5 cases)"
        echo "  func    - Functional tests (100 cases)"
        echo "  hfunc   - H_functional tests (40 cases)"
        echo "  perf    - Performance tests (60 cases)"
        echo "  all     - All three suites"
        echo ""
        echo "Default: quick O0"
        exit 1
        ;;
esac