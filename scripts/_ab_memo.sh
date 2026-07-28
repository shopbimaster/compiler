#!/bin/bash
# A/B test: recursive memoization ON vs OFF, measured with wall-clock time.
# Purpose: verify whether the memo pass actually changes runtime behaviour,
# since the sylib timer output proved unreliable.
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1

CC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static

run_variant() {
  local tag="$1"
  local disable="$2"
  echo "=== variant: $tag (OPT_DISABLE='$disable') ==="
  for i in 1 2 3; do
    local sy="test/performance/knapsack_naive-${i}.sy"
    local in="test/performance/knapsack_naive-${i}.in"
    local exp="test/performance/knapsack_naive-${i}.out"
    local s="/tmp/k_${tag}_${i}.s"
    local elf="/tmp/k_${tag}_${i}.elf"

    OPT_DISABLE="$disable" ./build_wsl/compiler -S -O1 -o "$s" "$sy" >/dev/null 2>&1
    local memo_syms
    memo_syms=$(grep -c '__opt_memo' "$s")
    $CC -static -o "$elf" "$s" SysYlib/sylib.c -I SysYlib 2>/dev/null

    local start end elapsed out
    start=$(date +%s.%N)
    out=$($QEMU "$elf" < "$in" 2>/dev/null | head -1)
    end=$(date +%s.%N)
    elapsed=$(echo "$end - $start" | bc)

    local want
    want=$(head -1 "$exp")
    local verdict="OK"
    [ "$out" = "$want" ] || verdict="MISMATCH(got=$out want=$want)"

    printf 'case%d: wall=%.2fs memo_syms=%s %s\n' "$i" "$elapsed" "$memo_syms" "$verdict"
  done
}

run_variant "on" ""
run_variant "off" "recursiveMemoization"
