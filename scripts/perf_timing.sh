#!/bin/bash
# Performance timing script
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}/perf_time"

mkdir -p "$TMPDIR"

echo "=== Performance Timing (O1) ==="
echo ""

for src in "${PROJECT_DIR}/test/performance/"*.sy; do
    name=$(basename "$src" .sy)
    asm="$TMPDIR/${name}.S"
    bin="$TMPDIR/${name}_bin"
    infile="${PROJECT_DIR}/test/performance/${name}.in"
    
    "$BUILD_DIR/compiler" -S "$src" -o "$asm" -O1 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
    
    start=$(date +%s%N)
    if [ -f "$infile" ]; then
        timeout 15 $QEMU "$bin" < "$infile" > /dev/null 2>/dev/null
    else
        timeout 15 $QEMU "$bin" > /dev/null 2>/dev/null
    fi
    end=$(date +%s%N)
    elapsed=$(( (end - start) / 1000000 ))
    if [ $elapsed -gt 100 ]; then
        printf "  %-30s %6d ms\n" "$name" $elapsed
    fi
done

echo ""
echo "Done."