#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
build/compiler -S test/functional/71_full_conn.sy -o /tmp/71.S -O1 2>/dev/null
riscv64-linux-gnu-gcc -static /tmp/71.S build/libsylib.a -o /tmp/71_bin
echo "=== our output (first 5) ==="
qemu-riscv64 /tmp/71_bin < test/functional/71_full_conn.in 2>/dev/null | head -5
echo "=== expected (first 5) ==="
head -n -1 test/functional/71_full_conn.out | head -5
echo "=== full match? ==="
qemu-riscv64 /tmp/71_bin < test/functional/71_full_conn.in 2>/dev/null > /tmp/71_out.txt
head -n -1 test/functional/71_full_conn.out > /tmp/71_exp.txt
diff /tmp/71_exp.txt /tmp/71_out.txt && echo "MATCH" || echo "DIFF"
