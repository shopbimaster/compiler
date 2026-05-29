#!/bin/bash
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR="/mnt/d/VSCodeProjects/compiler/build"
FUNC_DIR="/mnt/d/VSCodeProjects/compiler/test/functional"

run_test() {
    local name=$1
    local expected=$2
    local opt=$3
    local src="${FUNC_DIR}/${name}.sy"
    local asm="/tmp/qemu_test_${name}.S"
    local bin="/tmp/qemu_test_${name}_bin"
    local label="${name} (${opt})"

    if ! ${BUILD_DIR}/sysyc -S "$src" -o "$asm" -${opt} 2>/dev/null; then
        echo "  FAIL: $label - compile error"
        return 1
    fi
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" 2>/dev/null; then
        echo "  FAIL: $label - assembler error"
        return 1
    fi
    result=$($QEMU "$bin" 2>/dev/null; echo $?)
    if [ "$result" = "$expected" ]; then
        echo "  PASS: $label -> return $result"
        return 0
    else
        echo "  FAIL: $label - got $result, expected $expected"
        return 1
    fi
}

PASS=0
FAIL=0

for opt in O0 O3; do
    echo ""
    echo "=== QEMU End-to-End Tests (${opt}) ==="
    if run_test "00_main" 3 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "01_var_defn2" 10 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "11_add2" 9 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "26_while_test1" 3 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "29_break" 201 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
done

echo ""
echo "=== Result: $PASS passed, $FAIL failed ==="
exit $FAIL