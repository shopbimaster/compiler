#!/bin/bash
# ================================================================
# 测量性能测试用例的运行时间
# ================================================================
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
OPT="${1:-O1}"
TMPDIR="/tmp/perf_time"

mkdir -p "$TMPDIR"

echo "Measuring performance test runtimes (${OPT})..."
echo "============================================="
echo ""

for src in "${PROJECT_DIR}/test/performance"/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm="${TMPDIR}/${name}.S"
    bin="${TMPDIR}/${name}_bin"
    infile="${PROJECT_DIR}/test/performance/${name}.in"
    
    # Compile
    if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${OPT} 2>/dev/null; then
        echo "SKIP: ${name} (compile fail)"
        continue
    fi
    
    # Link
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "SKIP: ${name} (link fail)"
        continue
    fi
    
    # Run with time
    START=$(date +%s%N)
    if [ -f "$infile" ]; then
        timeout 15 $QEMU "$bin" < "$infile" > /dev/null 2>&1 || true
    else
        timeout 15 $QEMU "$bin" > /dev/null 2>&1 || true
    fi
    END=$(date +%s%N)
    ELAPSED_MS=$(( (END - START) / 1000000 ))
    
    echo "${name}: ${ELAPSED_MS}ms"
done

echo ""
echo "Done."