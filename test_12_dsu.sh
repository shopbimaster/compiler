#!/bin/bash
SRC=/mnt/d/VSCodeProjects/compiler/test/h_functional/12_DSU.sy
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
TMPDIR=/tmp/test_12_dsu
INFILE=/mnt/d/VSCodeProjects/compiler/test/h_functional/12_DSU.in
mkdir -p "$TMPDIR"

echo "=== O0 ==="
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/12_DSU_O0.S" -O0
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/12_DSU_O0_bin" "${TMPDIR}/12_DSU_O0.S" "${BUILD_DIR}/libsylib.a"
timeout 5 qemu-riscv64 "${TMPDIR}/12_DSU_O0_bin" < "$INFILE" > "${TMPDIR}/12_DSU_O0_out.txt" 2>&1 && echo "O0 PASS" || echo "O0 FAIL"

echo "=== O1 (with Mem2Reg) ==="
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/12_DSU_O1.S" -O1
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/12_DSU_O1_bin" "${TMPDIR}/12_DSU_O1.S" "${BUILD_DIR}/libsylib.a"
timeout 5 qemu-riscv64 -strace "${TMPDIR}/12_DSU_O1_bin" < "$INFILE" > "${TMPDIR}/12_DSU_O1_out.txt" 2>"${TMPDIR}/12_DSU_O1_strace.txt" && echo "O1 PASS" || echo "O1 FAIL"
echo "=== Last 20 lines of strace ==="
tail -20 "${TMPDIR}/12_DSU_O1_strace.txt"