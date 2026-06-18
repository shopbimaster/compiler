#!/bin/bash
# Test individual optimization passes for 61_sort_test7
cd /mnt/d/VSCodeProjects/compiler
SRC="test/functional/61_sort_test7.sy"
IN="test/functional/61_sort_test7.in"

# Helper: compile with specific passes and test
test_pass() {
  local label="$1"
  local passes="$2"
  echo -n "  $label: "
  timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_bisect.S -O0 2>/dev/null
  # We can't selectively enable passes, so we need to test at different levels
  # Let's test O1 vs O1 without certain passes
  timeout 3 qemu-riscv64 /tmp/tt_bisect_bin < "$IN" > /dev/null 2>&1
  ret=$?
  case $ret in
    0) echo "PASS";;
    124) echo "TIMEOUT";;
    139) echo "SEGFAULT";;
    *) echo "FAIL exit=$ret";;
  esac
}

echo "=== 61_sort_test7 ==="
echo "O0 baseline:"
timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_O0.S -O0 2>/dev/null
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_O0_bin /tmp/tt_O0.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_O0_bin < "$IN" > /dev/null 2>&1
ret=$?
set -e
echo "  O0: exit=$ret (0=pass)"

echo "O1 baseline:"
timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_O1.S -O1 2>/dev/null
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_O1_bin /tmp/tt_O1.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_O1_bin < "$IN" > /dev/null 2>&1
ret=$?
set -e
echo "  O1: exit=$ret (139=segfault)"

echo "O0 + -o1 (which is just O1 level):"
timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_o1.S -o1 2>/dev/null
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_o1_bin /tmp/tt_o1.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_o1_bin < "$IN" > /dev/null 2>&1
ret=$?
set -e
echo "  o1: exit=$ret"

echo "O2:"
timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_O2.S -O2 2>/dev/null
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_O2_bin /tmp/tt_O2.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_O2_bin < "$IN" > /dev/null 2>&1
ret=$?
set -e
echo "  O2: exit=$ret"

echo "O2 level (-o2):"
timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_o2.S -o2 2>/dev/null
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_o2_bin /tmp/tt_o2.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_o2_bin < "$IN" > /dev/null 2>&1
ret=$?
set -e
echo "  o2: exit=$ret"

echo "O3 level (-o3):"
timeout 10 ./build/compiler -S "$SRC" -o /tmp/tt_o3.S -o3 2>/dev/null
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/tt_o3_bin /tmp/tt_o3.S build/libsylib.a 2>/dev/null
set +e
timeout 3 qemu-riscv64 /tmp/tt_o3_bin < "$IN" > /dev/null 2>&1
ret=$?
set -e
echo "  o3: exit=$ret"

echo ""
echo "Compare O1 vs O2 assembly sizes:"
wc -l /tmp/tt_O1.S /tmp/tt_O2.S