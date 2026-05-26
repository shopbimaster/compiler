#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler/build
make -j$(nproc) 2>&1 | tail -5
echo "=== BUILD OK ==="

cd /mnt/d/VSCodeProjects/compiler
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64

pass=0
fail=0
tests=0

run_test() {
    local name=$1
    local expected=$2
    tests=$((tests+1))
    echo -n "${name}: "
    build/sysyc -S test/${name}.sy -o /tmp/${name}.S 2>/dev/null || { echo "COMPILE FAIL"; fail=$((fail+1)); return; }
    $GCC -march=rv64gc -mabi=lp64d -static -o /tmp/${name}_bin /tmp/${name}.S 2>/dev/null || { echo "ASM FAIL"; fail=$((fail+1)); return; }
    local actual=$($QEMU /tmp/${name}_bin 2>/dev/null; echo $?)
    if [ "$actual" = "$expected" ]; then
        echo "exit=${actual} OK"
        pass=$((pass+1))
    else
        echo "exit=${actual} expected=${expected} FAIL"
        fail=$((fail+1))
    fi
}

run_test hello 0
run_test arithmetic 7
run_test variable 42
run_test ifelse 0
run_test while_test 5
run_test func_call 7
run_test global_var 42
run_test float_cmp 1
run_test array_1d 6
run_test array_2d 7
run_test array_init 6

echo ""
echo "==== RESULT: ${pass}/${tests} passed, ${fail} failed ===="