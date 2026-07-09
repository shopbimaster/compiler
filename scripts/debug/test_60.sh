#!/bin/bash
# Test 60_sort_test6 at various optimization levels
cd /mnt/d/VSCodeProjects/compiler

for level in o1 o2 o3 O0 O1; do
    echo "=== $level ==="
    ./build/compiler -S "-$level" test/functional/60_sort_test6.sy -o /tmp/60_$level.S 2>&1 || continue
    riscv64-linux-gnu-gcc -static /tmp/60_$level.S build/libsylib.a -o /tmp/60_$level.elf 2>&1 || continue
    timeout 5 qemu-riscv64 /tmp/60_$level.elf 2>&1
    echo "EXIT=$?"
done
