#!/bin/bash
# ================================================================
# SysY Compiler - Loop Unrolling QEMU Test
# Usage: ./scripts/test_qemu_unroll.sh
# ================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SYSYC="${PROJECT_DIR}/build/sysyc"
PASS=0
FAIL=0

run_test() {
    local name="$1"
    local expected="$2"
    local opt="$3"
    echo -n "  ${name} (${opt}): "
    $SYSYC -S "${PROJECT_DIR}/test/${name}.sy" -o "/tmp/${name}_${opt}.S" -O "$opt" 2>/dev/null
    riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "/tmp/${name}_${opt}" "/tmp/${name}_${opt}.S" 2>/dev/null
    qemu-riscv64 "/tmp/${name}_${opt}" 2>/dev/null
    local actual=$?
    if [ "$actual" = "$expected" ]; then
        echo "PASS (got $actual)"
        return 0
    else
        echo "FAIL (expected $expected, got $actual)"
        return 1
    fi
}

echo "=== Loop Unrolling QEMU Tests ==="
for opt in 0 3; do
    if run_test "loop_unroll_4x" 8 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "loop_unroll_2x" 20 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
    if run_test "loop_unroll_full_4x" 12 "$opt"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
done
echo "=== Result: $PASS/$((PASS+FAIL)) passed ==="