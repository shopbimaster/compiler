#!/bin/bash
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR=/tmp/test_mem2reg
mkdir -p "$TMPDIR"

# 64_calculator with O2 (Mem2Reg enabled)
SRC=/mnt/d/VSCodeProjects/compiler/test/functional/64_calculator.sy
echo "=== 64_calculator O2 ==="
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/64_calc_O2.S" -o2 2>&1
echo "Compile OK"
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/64_calc_O2_bin" "${TMPDIR}/64_calc_O2.S" "$SYLIB_A" 2>&1
echo "Link OK"
timeout 10 $QEMU "${TMPDIR}/64_calc_O2_bin" < /mnt/d/VSCodeProjects/compiler/test/functional/64_calculator.in > "${TMPDIR}/64_calc_O2_out.txt" 2>&1
QEMU_EXIT=$?
echo "QEMU exit: $QEMU_EXIT"
if [ $QEMU_EXIT -eq 0 ]; then
    echo "64_calculator: PASS"
else
    echo "64_calculator: FAIL (exit=$QEMU_EXIT)"
fi

echo ""

# 77_substr with O2
SRC=/mnt/d/VSCodeProjects/compiler/test/functional/77_substr.sy
echo "=== 77_substr O2 ==="
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/77_substr_O2.S" -o2 2>&1
echo "Compile OK"
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/77_substr_O2_bin" "${TMPDIR}/77_substr_O2.S" "$SYLIB_A" 2>&1
echo "Link OK"
timeout 10 $QEMU "${TMPDIR}/77_substr_O2_bin" < /mnt/d/VSCodeProjects/compiler/test/functional/77_substr.in > "${TMPDIR}/77_substr_O2_out.txt" 2>&1
QEMU_EXIT=$?
echo "QEMU exit: $QEMU_EXIT"
if [ $QEMU_EXIT -eq 0 ]; then
    echo "77_substr: PASS"
else
    echo "77_substr: FAIL (exit=$QEMU_EXIT)"
fi