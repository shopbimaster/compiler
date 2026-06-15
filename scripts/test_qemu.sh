#!/bin/bash
# ================================================================
# SysY 编译器 QEMU 端到端测试 (快速测试)
# 用法: ./test_qemu.sh
# ================================================================
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
FUNC_DIR="${SCRIPT_DIR}/test/functional"
SYLIB_A="${BUILD_DIR}/libsylib.a"

# 如果运行时库不存在，自动构建
if [ ! -f "$SYLIB_A" ]; then
    echo "Building runtime library..."
    bash "${SCRIPT_DIR}/build/build_sylib.sh"
fi

# 标准测试：只比较返回值（忽略 stdout 输出）
run_test() {
    local name=$1
    local expected=$2
    local opt=$3
    local src="${FUNC_DIR}/${name}.sy"
    local asm="/tmp/qemu_test_${name}.S"
    local bin="/tmp/qemu_test_${name}_bin"
    local label="${name} (${opt})"

    if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${opt} 2>/dev/null; then
        echo "  FAIL: $label - compile error"
        return 1
    fi
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "  FAIL: $label - assembler/link error"
        return 1
    fi
    $QEMU "$bin" > /dev/null 2>&1
    local ret=$?
    if [ "$ret" = "$expected" ]; then
        echo "  PASS: $label -> return $ret"
        return 0
    else
        echo "  FAIL: $label - return $ret, expected $expected"
        return 1
    fi
}

# 带输入文件的测试：只比较返回值
run_test_with_input() {
    local name=$1
    local expected=$2
    local opt=$3
    local src="${FUNC_DIR}/${name}.sy"
    local infile="${FUNC_DIR}/${name}.in"
    local asm="/tmp/qemu_test_${name}.S"
    local bin="/tmp/qemu_test_${name}_bin"
    local label="${name} (${opt})"

    if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${opt} 2>/dev/null; then
        echo "  FAIL: $label - compile error"
        return 1
    fi
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "  FAIL: $label - assembler/link error"
        return 1
    fi
    if [ -f "$infile" ]; then
        $QEMU "$bin" < "$infile" > /dev/null 2>&1
    else
        $QEMU "$bin" > /dev/null 2>&1
    fi
    local ret=$?
    if [ "$ret" = "$expected" ]; then
        echo "  PASS: $label -> return $ret"
        return 0
    else
        echo "  FAIL: $label - return $ret, expected $expected"
        return 1
    fi
}

PASS=0
FAIL=0

for opt in O0 O3; do
    echo ""
    echo "=== QEMU End-to-End Tests (${opt}) ==="
    # 基础测试（不依赖运行时库）
    if run_test "00_main" 3 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "01_var_defn2" 10 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "11_add2" 9 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "26_while_test1" 3 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "29_break" 201 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi

    # 运行时库 I/O 测试
    echo "  --- Runtime Library Tests ---"
    if run_test_with_input "73_int_io" 0 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test_with_input "95_float" 0 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test_with_input "92_register_alloc" 194 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test_with_input "87_many_params" 0 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "21_if_test2" 0 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "51_short_circuit3" 0 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "78_side_effect" 37 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "88_many_params2" 0 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
done

echo ""
echo "=== Result: $PASS passed, $FAIL failed ==="
exit $FAIL