#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
for t in 61_sort_test7 62_percolation 64_calculator; do
  echo "=== $t ==="
  for opt in O0 O1 o2 o3; do
    if [ "$opt" = "O1" ]; then flag="-O1"; else flag="-$opt"; fi
    timeout 10 ./build/compiler -S test/functional/$t.sy -o /tmp/tt_${t}_$opt.S $flag 2>/dev/null
    rc=$?
    if [ $rc -ne 0 ]; then
      echo "  $opt: COMPILE FAIL, exit=$rc"
      continue
    fi
    riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_${t}_${opt}_bin /tmp/tt_${t}_$opt.S build/libsylib.a 2>/dev/null
    if [ $? -ne 0 ]; then
      echo "  $opt: LINK FAIL"
      continue
    fi
    infile="test/functional/$t.in"
    set +e
    if [ -f "$infile" ]; then
      timeout 3 qemu-riscv64 /tmp/tt_${t}_${opt}_bin < "$infile" > /dev/null 2>&1
    else
      timeout 3 qemu-riscv64 /tmp/tt_${t}_${opt}_bin > /dev/null 2>&1
    fi
    ret=$?
    set -e
    if [ $ret -eq 0 ]; then
      echo "  $opt: PASS"
    elif [ $ret -eq 124 ]; then
      echo "  $opt: TIMEOUT"
    elif [ $ret -eq 139 ]; then
      echo "  $opt: SEGFAULT"
    else
      echo "  $opt: FAIL, exit=$ret"
    fi
  done
  echo ""
done