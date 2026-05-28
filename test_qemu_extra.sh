#!/bin/bash
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYSC=/mnt/d/VSCodeProjects/compiler/build/sysyc
TEST_DIR=/mnt/d/VSCodeProjects/compiler/test

run_test() {
    local name=$1 expected=$2
    echo -n "${name}: "
    $SYSC -S $TEST_DIR/${name}.sy -o /tmp/${name}.S 2>/dev/null || { echo 'COMPILE FAIL'; return 1; }
    $GCC -march=rv64gc -mabi=lp64d -static -o /tmp/${name}_bin /tmp/${name}.S 2>/dev/null || { echo 'ASM FAIL'; return 1; }
    local actual=$($QEMU /tmp/${name}_bin 2>/dev/null; echo $?)
    if [ "$actual" = "$expected" ]; then
        echo "exit=${actual} OK"
        return 0
    else
        echo "exit=${actual} expected=${expected} FAIL"
        return 1
    fi
}

pass=0
fail=0

run_test recursive_mul_shift 30 && pass=$((pass+1)) || fail=$((fail+1))
run_test recursive_mul 21 && pass=$((pass+1)) || fail=$((fail+1))
run_test div_chain 2 && pass=$((pass+1)) || fail=$((fail+1))
run_test mul_simple 21 && pass=$((pass+1)) || fail=$((fail+1))
run_test break_test 201 && pass=$((pass+1)) || fail=$((fail+1))

echo ""
echo "==== RESULT: ${pass}/$((pass+fail)) passed ===="