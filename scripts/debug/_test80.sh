#!/bin/bash
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SYLIB_A=${BUILD_DIR}/libsylib.a
TMP=~/tmp2
export TMPDIR=$TMP

src=/mnt/d/VSCodeProjects/compiler/test/functional/80_chaos_token.sy
outfile=/mnt/d/VSCodeProjects/compiler/test/functional/80_chaos_token.out
${BUILD_DIR}/compiler -S "$src" -o "${TMP}/_80.S" -O0 2>&1
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMP}/_80_bin" "${TMP}/_80.S" "$SYLIB_A" 2>&1
timeout 5 $QEMU "${TMP}/_80_bin" > "${TMP}/_80_out.txt" 2>&1
echo "--- OUTPUT ---"
cat "${TMP}/_80_out.txt"
echo "--- EXPECTED first 5 lines ---"
head -5 "$outfile"