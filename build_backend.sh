#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler/build
make -j$(nproc) 2>&1 | tail -3
echo "=== BUILD OK ==="

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64

cd /mnt/d/VSCodeProjects/compiler
pass=0
fail=0

for test in hello arithmetic variable ifelse while_test func_call; do
    echo ""
    echo "========== ${test}.sy =========="
    if ! build/sysyc -S test/${test}.sy -o /tmp/${test}.S 2>/dev/null; then
        echo "COMPILE FAIL"
        fail=$((fail+1))
        continue
    fi
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o /tmp/${test}_bin /tmp/${test}.S 2>/dev/null; then
        echo "ASM FAIL"
        fail=$((fail+1))
        continue
    fi
    $QEMU /tmp/${test}_bin 2>/dev/null
    ret=$?
    echo "${test} exit: ${ret}"
    if [ $ret -eq 0 ]; then
        pass=$((pass+1))
    else
        fail=$((fail+1))
    fi
done

echo ""
echo "========== RESULT =========="
echo "pass: ${pass}  fail: ${fail}"