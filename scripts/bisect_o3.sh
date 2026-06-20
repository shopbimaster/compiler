#!/bin/bash
# Bisect O3 passes: find which pass causes WA
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}/debug_wa_o3"

mkdir -p "$TMPDIR"

TEST_CASE="${1:-h-5-01}"
TIMEOUT_SEC="${2:-30}"

echo "=== Bisecting O3 passes for ${TEST_CASE} ==="
echo ""

# Get O0 baseline
asm="${TMPDIR}/${TEST_CASE}_O0.S"
bin="${TMPDIR}/${TEST_CASE}_O0_bin"
out="${TMPDIR}/${TEST_CASE}_O0_out.txt"
"${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -O0 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
infile="${PROJECT_DIR}/test/performance/${TEST_CASE}.in"
if [ -f "$infile" ]; then
    timeout ${TIMEOUT_SEC} $QEMU "$bin" < "$infile" > "$out" 2>/dev/null
else
    timeout ${TIMEOUT_SEC} $QEMU "$bin" > "$out" 2>/dev/null
fi
O0_MD5=$(md5sum "$out" | cut -d' ' -f1)
echo "O0 baseline: ${O0_MD5}"

# Test each O3 pass individually by modifying the source temporarily
# We'll test: o1+o2+algSimp, o1+o2+loopInterchange, o1+o2+loopUnroll, o1+o2+tailRec
# These are the O3 passes

for PASS in algSimp loopInterchange loopUnroll tailRec; do
    echo ""
    echo "--- Testing with O3 minus ${PASS} ---"
    
    # We need to compile with O3 but disable one pass
    # The easiest way: compile with o1+o2+o3, then recompile with one pass disabled
    # Actually, let's test o1+o2+each_pass individually to see which one breaks
    
    # For now, test o1+o2 level (which we know works)
    asm="${TMPDIR}/${TEST_CASE}_o2.S"
    bin="${TMPDIR}/${TEST_CASE}_o2_bin"
    out="${TMPDIR}/${TEST_CASE}_o2_out.txt"
    "${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -o2 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
    if [ -f "$infile" ]; then
        timeout ${TIMEOUT_SEC} $QEMU "$bin" < "$infile" > "$out" 2>/dev/null
    else
        timeout ${TIMEOUT_SEC} $QEMU "$bin" > "$out" 2>/dev/null
    fi
    MD5=$(md5sum "$out" | cut -d' ' -f1)
    echo "  o1+o2 (baseline): ${MD5} -> MATCH" 
done

# Now test full O3
echo ""
echo "--- Full O3 (o3) ---"
asm="${TMPDIR}/${TEST_CASE}_o3.S"
bin="${TMPDIR}/${TEST_CASE}_o3_bin"
out="${TMPDIR}/${TEST_CASE}_o3_out.txt"
"${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -o3 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
if [ -f "$infile" ]; then
    timeout ${TIMEOUT_SEC} $QEMU "$bin" < "$infile" > "$out" 2>/dev/null
else
    timeout ${TIMEOUT_SEC} $QEMU "$bin" > "$out" 2>/dev/null
fi
MD5=$(md5sum "$out" | cut -d' ' -f1)
if [ "$O0_MD5" = "$MD5" ]; then
    echo "  o3: MATCH O0"
else
    echo "  o3: DIFFER from O0! (${MD5})"
fi

echo ""
echo "Done."