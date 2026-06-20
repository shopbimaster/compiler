#!/bin/bash
# Batch bisect for remaining WA cases
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}/debug_wa_batch"

mkdir -p "$TMPDIR"

for TEST_CASE in matmul1 h-5-01 conv2d-1 h-8-01 01_mm1 h-1-01 many_mat_cal-1; do
    echo "=== $TEST_CASE ==="
    infile="${PROJECT_DIR}/test/performance/${TEST_CASE}.in"
    
    for LEVEL in O0 o1 o2 o3 O1; do
        asm="${TMPDIR}/${TEST_CASE}_${LEVEL}.S"
        bin="${TMPDIR}/${TEST_CASE}_${LEVEL}_bin"
        out="${TMPDIR}/${TEST_CASE}_${LEVEL}_out.txt"
        
        "${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -${LEVEL} 2>/dev/null
        $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
        
        if [ -f "$infile" ]; then
            timeout 30 $QEMU "$bin" < "$infile" > "$out" 2>/dev/null
        else
            timeout 30 $QEMU "$bin" > "$out" 2>/dev/null
        fi
        ret=$?
        
        MD5=$(md5sum "$out" 2>/dev/null | cut -d' ' -f1)
        if [ $ret -eq 124 ]; then
            echo "  ${LEVEL}: TIMEOUT"
        elif [ $ret -eq 139 ]; then
            echo "  ${LEVEL}: SEGFAULT"
        else
            echo "  ${LEVEL}: MD5=${MD5}"
        fi
    done
    
    O0_MD5=$(md5sum "${TMPDIR}/${TEST_CASE}_O0_out.txt" 2>/dev/null | cut -d' ' -f1)
    echo "  Result:"
    for LEVEL in o1 o2 o3 O1; do
        LEV_MD5=$(md5sum "${TMPDIR}/${TEST_CASE}_${LEVEL}_out.txt" 2>/dev/null | cut -d' ' -f1)
        if [ "$O0_MD5" = "$LEV_MD5" ]; then
            echo "    ${LEVEL}: MATCH O0"
        else
            echo "    ${LEVEL}: DIFFER from O0!"
        fi
    done
    echo ""
done

echo "Done."