#!/bin/bash
# Systematically bisect which optimization pass in OALL (命令行 -O1) causes SEGFAULT
set -e
cd /mnt/d/VSCodeProjects/compiler
SRC="test/functional/61_sort_test7.sy"
IN="test/functional/61_sort_test7.in"

run_test() {
  local label="$1"
  local asm="/tmp/bisect_${label}.S"
  local bin="/tmp/bisect_${label}_bin"
  timeout 10 ./build/compiler -S "$SRC" -o "$asm" -O1 2>/dev/null
  riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" build/libsylib.a 2>/dev/null
  set +e
  timeout 3 qemu-riscv64 "$bin" < "$IN" > /dev/null 2>&1
  ret=$?
  set -e
  if [ $ret -eq 0 ]; then echo "  $label: PASS"; elif [ $ret -eq 139 ]; then echo "  $label: SEGFAULT"; elif [ $ret -eq 124 ]; then echo "  $label: TIMEOUT"; else echo "  $label: FAIL exit=$ret"; fi
}

echo "=== Testing OALL with different pass combinations ==="
run_test "baseline_OALL"