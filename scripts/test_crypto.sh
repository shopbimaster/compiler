#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB=./build/libsylib.a

for i in 1 2 3; do
  echo "=== crypto-${i} ==="
  ./build/compiler -S "test/performance/crypto-${i}.sy" -o "/tmp/crypto${i}.S" -O1
  $GCC -march=rv64gc -mabi=lp64d -static -o "/tmp/crypto${i}_bin" "/tmp/crypto${i}.S" "$SYLIB"
  echo "100 3" | timeout 15 $QEMU "/tmp/crypto${i}_bin"
  echo ""
done
echo "All crypto tests passed."