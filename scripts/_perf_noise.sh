#!/usr/bin/env bash
# How noisy is the timing harness itself?
#
# The A/B table showed 03_sort1 +112% and fft1 +248% after the determinism fix,
# yet both cases' asm is byte-identical before and after. Identical machine code
# cannot run twice as slow, so those deltas must be measurement noise.
# Quantify it: run the SAME binary N times and report the spread.
#
# Usage: _perf_noise.sh [N] [case...]
set -u
cd "$(dirname "$0")/.."
N=${1:-5}; shift || true
CASES=${*:-03_sort1 fft1 conv2d-2}
QEMU=qemu-riscv64-static
GCC=riscv64-linux-gnu-gcc
SYLIB=./build_b/libsylib.a
T=/tmp/_perfN; mkdir -p $T

for n in $CASES; do
    src=test/performance/$n.sy
    [ -f "$src" ] || continue
    ./build_wsl/compiler -S -O1 -o "$T/$n.s" "$src" >/dev/null 2>&1
    $GCC -static -o "$T/$n.elf" "$T/$n.s" "$SYLIB" >/dev/null 2>&1 || continue
    in=test/performance/$n.in
    times=()
    for i in $(seq "$N"); do
        if [ -f "$in" ]; then
            us=$($QEMU "$T/$n.elf" < "$in" 2>&1 >/dev/null | grep -oE 'TOTAL: [0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us' | sed -E 's/.*-([0-9]+)us/\1/')
        else
            us=$($QEMU "$T/$n.elf" 2>&1 >/dev/null | grep -oE 'TOTAL: [0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us' | sed -E 's/.*-([0-9]+)us/\1/')
        fi
        times+=("${us:-0}")
    done
    printf '%-12s' "$n:"
    printf ' %s' "${times[@]}"
    min=$(printf '%s\n' "${times[@]}" | sort -n | head -1)
    max=$(printf '%s\n' "${times[@]}" | sort -n | tail -1)
    if [ "$min" -gt 0 ]; then
        printf '   spread=%.1fx\n' "$(echo "$max/$min" | bc -l)"
    else
        printf '   (no timing)\n'
    fi
done
