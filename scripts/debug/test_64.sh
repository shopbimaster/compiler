#!/bin/bash
# Bisect 64_calculator SEGFAULT across optimization levels
cd /mnt/d/VSCodeProjects/compiler

for lvl in o1 o2 o3 O1; do
    echo "============================="
    echo "Testing O=$lvl"
    if ! ./build/compiler -S -$lvl test/functional/64_calculator.sy -o /tmp/64_test.S 2>/dev/null; then
        echo "  COMPILE FAIL"
        continue
    fi
    if ! riscv64-linux-gnu-gcc -static /tmp/64_test.S SysYlib/sylib.c -o /tmp/64_test.out 2>/dev/null; then
        echo "  LINK FAIL"
        continue
    fi
    timeout 5 qemu-riscv64 /tmp/64_test.out < test/functional/64_calculator.in > /tmp/64_test.txt 2>&1
    ec=$?
    if [ $ec -eq 139 ]; then
        echo "  RESULT: SEGFAULT"
    elif [ $ec -eq 124 ]; then
        echo "  RESULT: TIMEOUT"
    else
        echo "  RESULT: OK exit=$ec"
        echo "  OUTPUT: $(cat /tmp/64_test.txt)"
    fi
done
