#!/bin/bash
# 二分法定位 O3 中哪个 Pass 导致 WA
# 用法: bash scripts/bisect_o3_passes.sh <case_name>

BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
TMPDIR=/tmp/bisect_test
mkdir -p "$TMPDIR"

CASE="$1"
if [ -z "$CASE" ]; then
    echo "Usage: $0 <case_name> (e.g. matmul1, h-5-01)"
    exit 1
fi

SRC=/mnt/d/VSCodeProjects/compiler/test/performance/${CASE}.sy
if [ ! -f "$SRC" ]; then
    echo "Source file not found: $SRC"
    exit 1
fi

echo "=== Testing ${CASE} ==="

# Test O0 (baseline)
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${CASE}_o0.S" -o0 2>/dev/null
echo -n "O0: "
"${BUILD_DIR}/compiler" "$SRC" -o "${TMPDIR}/${CASE}_o0" -o0 2>/dev/null

# Test O1
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${CASE}_o1.S" -o1 2>/dev/null
echo -n "O1: "
"${BUILD_DIR}/compiler" "$SRC" -o "${TMPDIR}/${CASE}_o1" -o1 2>/dev/null

# Test O2
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${CASE}_o2.S" -o2 2>/dev/null
echo -n "O2: "
"${BUILD_DIR}/compiler" "$SRC" -o "${TMPDIR}/${CASE}_o2" -o2 2>/dev/null

# Test O3 (full)
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${CASE}_o3.S" -o3 2>/dev/null
echo -n "O3: "
"${BUILD_DIR}/compiler" "$SRC" -o "${TMPDIR}/${CASE}_o3" -o3 2>/dev/null

echo ""
echo "=== Comparing O2 vs O3 ASM ==="
diff "${TMPDIR}/${CASE}_o2.S" "${TMPDIR}/${CASE}_o3.S" | head -50