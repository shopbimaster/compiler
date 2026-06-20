#!/bin/bash
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR=/tmp/test_lifix
mkdir -p "$TMPDIR"

TEST_CASE="${1:-h-5-01}"
SRC=/mnt/d/VSCodeProjects/compiler/test/performance/${TEST_CASE}.sy

echo "=== Testing ${TEST_CASE} ==="

# O0 baseline
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${TEST_CASE}_O0.S" -O0 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/${TEST_CASE}_O0_bin" "${TMPDIR}/${TEST_CASE}_O0.S" "$SYLIB_A" 2>/dev/null
timeout 30 $QEMU "${TMPDIR}/${TEST_CASE}_O0_bin" > "${TMPDIR}/${TEST_CASE}_O0_out.txt" 2>/dev/null
O0_MD5=$(md5sum "${TMPDIR}/${TEST_CASE}_O0_out.txt" | cut -d' ' -f1)
echo "O0: $O0_MD5"

# O3
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${TEST_CASE}_O3.S" -o3 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/${TEST_CASE}_O3_bin" "${TMPDIR}/${TEST_CASE}_O3.S" "$SYLIB_A" 2>/dev/null
timeout 30 $QEMU "${TMPDIR}/${TEST_CASE}_O3_bin" > "${TMPDIR}/${TEST_CASE}_O3_out.txt" 2>/dev/null
O3_MD5=$(md5sum "${TMPDIR}/${TEST_CASE}_O3_out.txt" | cut -d' ' -f1)
echo "O3: $O3_MD5"

if [ "$O0_MD5" = "$O3_MD5" ]; then
    echo "MATCH!"
else
    echo "DIFFER!"
fi