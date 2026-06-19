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

echo "=== Analyzing ${TEST_CASE} ==="
echo ""

# Source code
echo "--- Source (.sy) ---"
cat "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy"
echo ""

# Input file
echo "--- Input file ---"
if [ -f "${PROJECT_DIR}/test/performance/${TEST_CASE}.in" ]; then
    cat "${PROJECT_DIR}/test/performance/${TEST_CASE}.in"
else
    echo "(none)"
fi
echo ""

# Expected output
echo "--- Expected output (first 20 lines) ---"
if [ -f "${PROJECT_DIR}/test/performance/${TEST_CASE}.out" ]; then
    head -20 "${PROJECT_DIR}/test/performance/${TEST_CASE}.out"
else
    echo "(none)"
fi
echo ""

# Compile with O1 and O0
echo "--- Compiling O1 ---"
"${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "${TMPDIR}/${TEST_CASE}_O1.S" -O1
echo "--- Compiling O0 ---"
"${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "${TMPDIR}/${TEST_CASE}_O0.S" -O0

# Link
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/${TEST_CASE}_O1_bin" "${TMPDIR}/${TEST_CASE}_O1.S" "$SYLIB_A" 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/${TEST_CASE}_O0_bin" "${TMPDIR}/${TEST_CASE}_O0.S" "$SYLIB_A" 2>/dev/null

# Run O0
echo "--- Running O0 ---"
if [ -f "${PROJECT_DIR}/test/performance/${TEST_CASE}.in" ]; then
    $QEMU "${TMPDIR}/${TEST_CASE}_O0_bin" < "${PROJECT_DIR}/test/performance/${TEST_CASE}.in" > "${TMPDIR}/${TEST_CASE}_O0_out.txt" 2>/dev/null
else
    $QEMU "${TMPDIR}/${TEST_CASE}_O0_bin" > "${TMPDIR}/${TEST_CASE}_O0_out.txt" 2>/dev/null
fi
echo "O0 exit code: $?"
echo "O0 output (first 20 lines):"
head -20 "${TMPDIR}/${TEST_CASE}_O0_out.txt"
echo ""

# Run O1
echo "--- Running O1 ---"
if [ -f "${PROJECT_DIR}/test/performance/${TEST_CASE}.in" ]; then
    $QEMU "${TMPDIR}/${TEST_CASE}_O1_bin" < "${PROJECT_DIR}/test/performance/${TEST_CASE}.in" > "${TMPDIR}/${TEST_CASE}_O1_out.txt" 2>/dev/null
else
    $QEMU "${TMPDIR}/${TEST_CASE}_O1_bin" > "${TMPDIR}/${TEST_CASE}_O1_out.txt" 2>/dev/null
fi
echo "O1 exit code: $?"
echo "O1 output (first 20 lines):"
head -20 "${TMPDIR}/${TEST_CASE}_O1_out.txt"
echo ""

# Compare O0 vs O1 output
echo "--- Diff O0 vs O1 ---"
diff "${TMPDIR}/${TEST_CASE}_O0_out.txt" "${TMPDIR}/${TEST_CASE}_O1_out.txt" && echo "IDENTICAL" || echo "DIFFERENT"
echo ""

# Compare O1 vs expected
echo "--- Diff O1 vs expected ---"
if [ -f "${PROJECT_DIR}/test/performance/${TEST_CASE}.out" ]; then
    diff <(head -n -1 "${PROJECT_DIR}/test/performance/${TEST_CASE}.out" 2>/dev/null) "${TMPDIR}/${TEST_CASE}_O1_out.txt" && echo "IDENTICAL" || echo "DIFFERENT"
else
    echo "(no expected output)"
fi

echo ""
echo "Done."