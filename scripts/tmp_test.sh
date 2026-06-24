#!/bin/bash
set -e
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR=/tmp/test_cfg
mkdir -p "$TMPDIR"

TEST_NAME="$1"
SRC="/mnt/d/VSCodeProjects/compiler/test/functional/${TEST_NAME}.sy"

echo "=== Testing ${TEST_NAME} ==="

# O0 baseline
echo "--- O0 ---"
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${TEST_NAME}_O0.S" -O0
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/${TEST_NAME}_O0" "${TMPDIR}/${TEST_NAME}_O0.S" "$SYLIB_A"
timeout 10 $QEMU "${TMPDIR}/${TEST_NAME}_O0" > "${TMPDIR}/${TEST_NAME}_O0_out.txt" 2>&1 || true
echo "O0 exit: $?"
cat "${TMPDIR}/${TEST_NAME}_O0_out.txt"

# O1 (includes SimplifyCFG)
echo "--- O1 ---"
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${TEST_NAME}_O1.S" -O1
echo "Compile OK"
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/${TEST_NAME}_O1" "${TMPDIR}/${TEST_NAME}_O1.S" "$SYLIB_A"
timeout 10 $QEMU "${TMPDIR}/${TEST_NAME}_O1" > "${TMPDIR}/${TEST_NAME}_O1_out.txt" 2>&1 || true
echo "QEMU exit: $?"
cat "${TMPDIR}/${TEST_NAME}_O1_out.txt"