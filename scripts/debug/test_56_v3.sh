#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
for lvl in o0 o1 o2 o3; do
    echo "=== $lvl ==="
    ./build/compiler -S -$lvl test/functional/56_sort_test2.sy -o /tmp/56_$lvl.S 2>/dev/null
    riscv64-linux-gnu-gcc -static /tmp/56_$lvl.S SysYlib/sylib.c -o /tmp/56_$lvl.out 2>/dev/null
    time qemu-riscv64 /tmp/56_$lvl.out 2>&1
    echo "---"
done
