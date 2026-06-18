#!/bin/bash
set -e
cd /mnt/d/VSCodeProjects/compiler
for t in 61_sort_test7 62_percolation 64_calculator 68_brainfk; do
  echo "=== $t ==="
  timeout 10 ./build/compiler -S test/functional/$t.sy -o /tmp/t_$t.S -O1 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    echo "COMPILE FAIL, exit=$rc"
    continue
  fi
  riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/t_${t}_bin /tmp/t_$t.S build/libsylib.a 2>&1
  if [ $? -ne 0 ]; then
    echo "LINK FAIL"
    continue
  fi
  echo "Running..."
  timeout 10 qemu-riscv64 /tmp/t_${t}_bin 2>&1
  echo "Exit: $?"
  echo ""
done