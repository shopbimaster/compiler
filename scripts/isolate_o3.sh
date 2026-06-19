#!/bin/bash
# Test O3 passes individually to find which one causes WA
set -e

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR="${TMPDIR:-/tmp}/debug_wa"

mkdir -p "$TMPDIR"

TEST_CASE="${1:-01_mm1}"
TIMEOUT_SEC="${2:-60}"

echo "=== Isolating O3 passes for ${TEST_CASE} ==="
echo ""

# First, compile and run O0 as reference
asm="${TMPDIR}/${TEST_CASE}_O0.S"
bin="${TMPDIR}/${TEST_CASE}_O0_bin"
out="${TMPDIR}/${TEST_CASE}_O0_out.txt"
infile="${PROJECT_DIR}/test/performance/${TEST_CASE}.in"

"${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -O0 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
if [ -f "$infile" ]; then
    timeout ${TIMEOUT_SEC} $QEMU "$bin" < "$infile" > "$out" 2>/dev/null
else
    timeout ${TIMEOUT_SEC} $QEMU "$bin" > "$out" 2>/dev/null
fi
O0_MD5=$(md5sum "$out" | cut -d' ' -f1)
echo "O0 reference: MD5=${O0_MD5}"
echo "  Output: $(head -c 200 "$out")"
echo ""

# Test o2 (which is correct) as another reference
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
O2_MD5=$(md5sum "$out" | cut -d' ' -f1)
echo "o2 reference: MD5=${O2_MD5}"
echo ""

# Test o3 (all O3 passes) - expected to be wrong
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
O3_MD5=$(md5sum "$out" | cut -d' ' -f1)
echo "o3 (all 4 passes): MD5=${O3_MD5}"
[ "$O3_MD5" = "$O0_MD5" ] && echo "  MATCH O0" || echo "  DIFFER from O0!"
echo ""

# Now test by temporarily disabling each O3 pass
# We do this by modifying Optimizer.cpp and recompiling

echo "=== Need to compile with each pass individually disabled ==="
echo "Run the following tests manually or modify Optimizer.cpp"
echo ""

# Test: o2 + algebraicSimplification only
echo "--- o2 + algebraicSimplification only ---"
asm="${TMPDIR}/${TEST_CASE}_o2_alg.S"
bin="${TMPDIR}/${TEST_CASE}_o2_alg_bin"
out="${TMPDIR}/${TEST_CASE}_o2_alg_out.txt"
"${BUILD_DIR}/compiler" -S "${PROJECT_DIR}/test/performance/${TEST_CASE}.sy" -o "$asm" -o3 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
# We can't easily test individual passes without modifying code
# But we can check the generated assembly
echo "Check assembly: ${asm}"
echo "Done."