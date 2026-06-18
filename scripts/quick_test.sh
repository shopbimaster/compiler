#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
timeout 10 ./build/compiler -S test/functional/61_sort_test7.sy -o /tmp/tt_test.S -O1 2>&1
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_test_bin /tmp/tt_test.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_test_bin < test/functional/61_sort_test7.in > /dev/null 2>&1
ret=$?
set -e
if [ $ret -eq 0 ]; then echo "PASS"; elif [ $ret -eq 139 ]; then echo "SEGFAULT"; else echo "FAIL exit=$ret"; fi