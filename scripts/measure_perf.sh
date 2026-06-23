#!/bin/bash
# 测量性能测试用例的实际运行时间（O1优化）
cd "$(dirname "$0")/.."
mkdir -p /tmp/perf_measure

measure() {
  local name="$1"
  local src="test/performance/${name}.sy"
  if [ ! -f "$src" ]; then return; fi
  local asm="/tmp/perf_measure/${name}.S"
  local bin="/tmp/perf_measure/${name}_bin"
  ./build/compiler -S "$src" -o "$asm" -O1 >/dev/null 2>&1
  if [ $? -ne 0 ]; then echo "COMPILE_FAIL: $name"; return; fi
  riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" ./build/libsylib.a 2>/dev/null
  if [ $? -ne 0 ]; then echo "LINK_FAIL: $name"; return; fi
  local infile="${src%.sy}.in"
  local start=$(date +%s%N)
  if [ -f "$infile" ]; then
    timeout 120 qemu-riscv64 "$bin" < "$infile" >/dev/null 2>/dev/null
  else
    timeout 120 qemu-riscv64 "$bin" >/dev/null 2>/dev/null
  fi
  local ret=$?
  local end=$(date +%s%N)
  local ms=$(( (end - start) / 1000000 ))
  if [ $ret -eq 124 ]; then
    echo "TIMEOUT_120s: $name"
  elif [ $ret -eq 0 ]; then
    echo "${ms}ms: $name"
  else
    echo "FAIL_RET${ret}: $name"
  fi
}

echo "=== Slow candidates from test12 ==="
for name in \
  huffman-01 huffman-02 huffman-03 \
  conv2d-1 conv2d-2 conv2d-3 \
  many_mat_cal-1 many_mat_cal-2 many_mat_cal-3 \
  knapsack_naive-1 knapsack_naive-2 knapsack_naive-3 \
  h-1-03 h-4-03 \
  sl1 sl2 sl3 \
  matmul1 matmul2 matmul3 \
  crypto-1 crypto-2 crypto-3 \
  01_mm1 01_mm2 01_mm3 \
  transpose0 transpose1 transpose2 \
  fft0 fft1 fft2 \
  ; do
  measure "$name"
done

echo ""
echo "Done."