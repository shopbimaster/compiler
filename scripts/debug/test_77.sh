#!/bin/bash
# Test 77_substr at various optimization levels
cd /mnt/d/VSCodeProjects/compiler

for level in o1 o2 o3 O0 O1; do
    echo "=== $level ==="
    ./build/compiler -S "-$level" test/functional/77_substr.sy -o /tmp/77_$level.S 2>&1 || continue
    riscv64-linux-gnu-gcc -static /tmp/77_$level.S build/libsylib.a -o /tmp/77_$level.elf 2>&1 || continue
    timeout 5 qemu-riscv64 /tmp/77_$level.elf 2>&1
    echo "EXIT=$?"
done
