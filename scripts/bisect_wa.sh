#!/bin/bash
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}/debug_wa"

mkdir -p "$TMPDIR"

TEST_CASE="${1:-matmul1}"
TIMEOUT_SEC="${2:-30}"

echo "=== Bisecting ${TEST_CASE} ==="
echo ""

# Compile and run with each optimization level
for LEVEL in O0 o1 o2 o3 O1; do
    echo "--- Level: ${LEVEL} ---"
    asm="${TMPDIR}/${TEST_CASE}_${LEVEL}.S"
    bin="${TMPDIR}/${TEST_CASE}_${LEVEL}_bin"
    out="${TMPDIR}/${TEST_CASE}_${LEVEL}_out.txt"
    infile="${PROJECT_DIR}/test/performance/${TEST_CASE}.in"
    
    # Compile
    "${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -${LEVEL} 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
    
    # Run with timeout
    if [ -f "$infile" ]; then
        timeout ${TIMEOUT_SEC} $QEMU "$bin" < "$infile" > "$out" 2>/dev/null
    else
        timeout ${TIMEOUT_SEC} $QEMU "$bin" > "$out" 2>/dev/null
    fi
    ret=$?
    
    if [ $ret -eq 124 ]; then
        echo "  TIMEOUT (>${TIMEOUT_SEC}s)"
    elif [ $ret -eq 139 ]; then
        echo "  SEGFAULT"
    else
        echo "  Exit: $ret, Output: $(head -c 200 "$out")"
        echo "  MD5: $(md5sum "$out" | cut -d' ' -f1)"
    fi
    echo ""
done

# Compare O0 with each level
echo "--- Comparison vs O0 ---"
O0_MD5=$(md5sum "${TMPDIR}/${TEST_CASE}_O0_out.txt" 2>/dev/null | cut -d' ' -f1)
for LEVEL in o1 o2 o3 O1; do
    LEV_MD5=$(md5sum "${TMPDIR}/${TEST_CASE}_${LEVEL}_out.txt" 2>/dev/null | cut -d' ' -f1)
    if [ "$O0_MD5" = "$LEV_MD5" ]; then
        echo "  ${LEVEL}: MATCH O0"
    else
        echo "  ${LEVEL}: DIFFER from O0!"
    fi
done

echo ""
echo "Done."