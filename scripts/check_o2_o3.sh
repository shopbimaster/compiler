#!/bin/bash
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
TMPDIR=/tmp/test_lifix
mkdir -p "$TMPDIR"

CASES="matmul1 h-8-01 conv2d-1 01_mm1 many_mat_cal-1 h-5-01"

for case in $CASES; do
    SRC=/mnt/d/VSCodeProjects/compiler/test/performance/${case}.sy
    echo "=== ${case} ==="
    "${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${case}_o2.S" -o2 2>/dev/null
    "${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/${case}_o3.S" -o3 2>/dev/null
    if diff -q "${TMPDIR}/${case}_o2.S" "${TMPDIR}/${case}_o3.S" > /dev/null 2>&1; then
        echo "  O2 == O3 (interchange skipped)"
    else
        echo "  O2 != O3 (interchange or other O3 pass applied)"
        diff "${TMPDIR}/${case}_o2.S" "${TMPDIR}/${case}_o3.S" | head -30
    fi
done