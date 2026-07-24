#!/bin/bash
# 冒烟测试2：多个用例快速验证链路（实验用，可删）
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1
C=./build/compiler
SY=./build/libsylib.a
QEMU=qemu-riscv64-static

for name in h-1-01 03_sort1 01_mm1; do
  T=test/performance/$name
  [ -f "$T.sy" ] || { echo "$name: 无此用例"; continue; }
  $C -S -o /tmp/t.s "$T.sy" -O1 >/dev/null 2>&1 || { echo "$name: COMPILE FAIL"; continue; }
  riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany /tmp/t.s "$SY" -o /tmp/t.exe 2>/dev/null \
    || { echo "$name: LINK FAIL"; continue; }
  if [ -f "$T.in" ]; then
    timeout 20 $QEMU /tmp/t.exe < "$T.in" > /tmp/t.out 2>/dev/null
  else
    timeout 20 $QEMU /tmp/t.exe > /tmp/t.out 2>/dev/null
  fi
  ret=$?
  # SysY 约定：把返回码作为输出最后一行，再与 .out 比对
  printf '%s\n' "$ret" >> /tmp/t.out
  if diff <(sed -e 's/[[:space:]]*$//' /tmp/t.out) \
          <(sed -e 's/[[:space:]]*$//' "$T.out") >/dev/null 2>&1; then
    echo "$name: MATCH (ret=$ret)"
  else
    echo "$name: DIFF (ret=$ret)  got_lines=$(wc -l </tmp/t.out) exp_lines=$(wc -l <"$T.out")"
  fi
done
