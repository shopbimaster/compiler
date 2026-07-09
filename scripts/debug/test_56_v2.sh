#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
for lvl in o0 o1 o2 o3 O1; do
    echo "=== $lvl ==="
    timeout 10 ./build/compiler -S -$lvl test/functional/56_sort_test2.sy -o /tmp/56_$lvl.S
    if [ $? -eq 0 ]; then
        echo "COMPILE OK"
    else
        echo "COMPILE FAIL/TIMEOUT"
        continue
    fi
    riscv64-linux-gnu-gcc -static /tmp/56_$lvl.S SysYlib/sylib.c -o /tmp/56_$lvl.out 2>/dev/null
    timeout 5 qemu-riscv64 /tmp/56_$lvl.out
    echo "RUNTIME EXIT=$?"
done
