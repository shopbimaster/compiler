#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
for t in 60_sort_test6 77_substr; do
    echo "--- $t (O1 uppercase = OALL) ---"
    ./build/compiler -S -O1 test/functional/$t.sy -o /tmp/$t.S
    riscv64-linux-gnu-gcc -static /tmp/$t.S SysYlib/sylib.c -o /tmp/$t.out
    qemu-riscv64 /tmp/$t.out
    echo "EXIT=$?"
done
