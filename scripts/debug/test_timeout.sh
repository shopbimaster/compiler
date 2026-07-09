#!/bin/bash
# Test the 4 functional TIMEOUT cases at OALL
cd /mnt/d/VSCodeProjects/compiler

for t in 56_sort_test2 76_n_queens 78_side_effect 85_long_code; do
    echo "=== $t ==="
    ./build/compiler -S -O1 test/functional/$t.sy -o /tmp/$t.S 2>&1 | tail -1
    riscv64-linux-gnu-gcc -static /tmp/$t.S SysYlib/sylib.c -o /tmp/$t.out 2>&1
    timeout 6 qemu-riscv64 /tmp/$t.out > /tmp/$t.out.txt 2>&1
    ec=$?
    if [ $ec -eq 124 ]; then
        echo "EXIT=TIMEOUT"
    else
        echo "EXIT=$ec"
        head -5 /tmp/$t.out.txt
    fi
    echo ""
done
