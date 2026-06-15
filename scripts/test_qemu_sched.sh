#!/bin/bash
# ================================================================
# SysY Compiler - Instruction Scheduling QEMU Test
# Usage: ./scripts/test_qemu_sched.sh
# ================================================================
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SYSYC="${PROJECT_DIR}/build/compiler"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
PASS=0
FAIL=0

run_test() {
    local name="$1"
    local expected="$2"
    local opt="$3"
    echo -n "  ${name} (O${opt}): "
    $SYSYC -S "${PROJECT_DIR}/test/${name}.sy" -o "/tmp/${name}_O${opt}.S" -O "$opt" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "COMPILE FAIL"
        FAIL=$((FAIL + 1))
        return 1
    fi
    $GCC -march=rv64gc -mabi=lp64d -static -o "/tmp/${name}_O${opt}" "/tmp/${name}_O${opt}.S" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "ASM FAIL"
        FAIL=$((FAIL + 1))
        return 1
    fi
    set +e
    $QEMU "/tmp/${name}_O${opt}" 2>/dev/null
    local actual=$?
    set -e
    if [ "$actual" = "$expected" ]; then
        echo "PASS (got $actual)"
        PASS=$((PASS + 1))
        return 0
    else
        echo "FAIL (expected $expected, got $actual)"
        FAIL=$((FAIL + 1))
        return 1
    fi
}

echo "=== Instruction Scheduling QEMU Tests ==="

# instr_sched_basic: compute(3, 4) = 3+4 + 3*2 + 4*3 = 7+6+12 = 25
EXPECTED_BASIC=25

for opt in 0 3; do
    run_test "instr_sched_basic" "$EXPECTED_BASIC" "$opt"
done

echo "=== Result: $PASS/$((PASS + FAIL)) passed ==="
exit 0