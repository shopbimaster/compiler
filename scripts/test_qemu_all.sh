#!/bin/bash
# ================================================================
# SysY 编译器全量回归测试脚本
# 用法: ./test_qemu_all.sh [O1|o0|o1|o2|o3]
# 默认: O1 (对应编译器 -O1，即最高优化)
# ================================================================
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
FUNC_DIR="${SCRIPT_DIR}/test/functional"
SYLIB_A="${BUILD_DIR}/libsylib.a"
SYSC="${BUILD_DIR}/compiler"

OPT_LEVEL="${1:-O1}"
TMP_DIR="/tmp/sysy_test_$$"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0
TOTAL=0

# ================================================================
# 检查前置条件
# ================================================================
check_prereqs() {
    local missing=0
    if ! command -v "$GCC" &>/dev/null; then
        echo -e "${RED}Error: $GCC not found${NC}"
        missing=1
    fi
    if ! command -v "$QEMU" &>/dev/null; then
        echo -e "${RED}Error: $QEMU not found${NC}"
        missing=1
    fi
    if [ ! -f "$SYLIB_A" ]; then
        echo -e "${YELLOW}Warning: $SYLIB_A not found, building...${NC}"
        bash "${SCRIPT_DIR}/build/build_sylib.sh"
    fi
    if [ ! -f "$SYLIB_A" ]; then
        echo -e "${RED}Error: Cannot build runtime library${NC}"
        missing=1
    fi
    if [ ! -f "$SYSC" ]; then
        echo -e "${RED}Error: compiler not found at $SYSC${NC}"
        missing=1
    fi
    return $missing
}

# ================================================================
# 运行单个测试
# ================================================================
run_test() {
    local name="$1"
    local src="${FUNC_DIR}/${name}.sy"
    local infile="${FUNC_DIR}/${name}.in"
    local outfile="${FUNC_DIR}/${name}.out"
    local asm="${TMP_DIR}/${name}.S"
    local bin="${TMP_DIR}/${name}_bin"
    local label="${name} (${OPT_LEVEL})"

    TOTAL=$((TOTAL + 1))

    # 跳过不存在的源文件
    if [ ! -f "$src" ]; then
        echo -e "  ${YELLOW}SKIP${NC}: $label - source not found"
        SKIP=$((SKIP + 1))
        return 0
    fi

    # 编译 .sy → 汇编
    if ! "${SYSC}" -S "$src" -o "$asm" -${OPT_LEVEL} 2>/dev/null; then
        echo -e "  ${RED}FAIL${NC}: $label - compile error"
        FAIL=$((FAIL + 1))
        return 1
    fi

    # 汇编 + 链接
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo -e "  ${RED}FAIL${NC}: $label - assembler/link error"
        FAIL=$((FAIL + 1))
        return 1
    fi

    # 运行
    local result
    if [ -f "$infile" ]; then
        result=$($QEMU "$bin" < "$infile" 2>/dev/null; echo "EXIT:$?")
    else
        result=$($QEMU "$bin" < /dev/null 2>/dev/null; echo "EXIT:$?")
    fi

    # 分离 stdout 和 exit code
    local exit_code=$(echo "$result" | grep "^EXIT:" | sed 's/EXIT://')
    local stdout_output=$(echo "$result" | grep -v "^EXIT:")

    local expected_exit=0
    local expected_stdout=""

    if [ -f "$outfile" ]; then
        expected_stdout=$(cat "$outfile")
        # 如果 .out 文件只有一行且是纯数字，则作为 exit code
        if [ "$(echo "$expected_stdout" | wc -l)" -eq 1 ] && [[ "$expected_stdout" =~ ^-?[0-9]+$ ]]; then
            expected_exit="$expected_stdout"
            expected_stdout=""
        fi
    fi

    # 比较结果
    local test_pass=1

    if [ -n "$expected_stdout" ]; then
        # 比较 stdout
        if [ "$stdout_output" != "$expected_stdout" ]; then
            echo -e "  ${RED}FAIL${NC}: $label - stdout mismatch"
            echo "    expected: '$expected_stdout'"
            echo "    got:      '$stdout_output'"
            FAIL=$((FAIL + 1))
            return 1
        fi
    fi

    if [ "$exit_code" != "$expected_exit" ]; then
        echo -e "  ${RED}FAIL${NC}: $label - exit code $exit_code, expected $expected_exit"
        FAIL=$((FAIL + 1))
        return 1
    fi

    echo -e "  ${GREEN}PASS${NC}: $label"
    PASS=$((PASS + 1))
    return 0
}

# ================================================================
# 主流程
# ================================================================
main() {
    echo "================================================================"
    echo "  SysY Compiler Regression Test (${OPT_LEVEL})"
    echo "================================================================"

    check_prereqs || exit 1

    mkdir -p "$TMP_DIR"
    trap "rm -rf $TMP_DIR" EXIT

    echo ""

    # 获取所有 .sy 文件并按数字排序
    local test_files=$(ls "${FUNC_DIR}"/*.sy 2>/dev/null | sort -V)
    if [ -z "$test_files" ]; then
        echo "No test files found in ${FUNC_DIR}"
        exit 1
    fi

    for src in $test_files; do
        local name=$(basename "$src" .sy)
        run_test "$name" || true
    done

    echo ""
    echo "================================================================"
    echo "  Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}, ${YELLOW}${SKIP} skipped${NC} (total: ${TOTAL})"
    echo "================================================================"

    if [ $FAIL -gt 0 ]; then
        exit 1
    fi
    exit 0
}

main "$@"