wsl bash -c '
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"
TMPDIR=/tmp/test_77
mkdir -p "$TMPDIR"
SRC=/mnt/d/VSCodeProjects/compiler/test/functional/77_substr.sy

echo "=== O0 baseline ==="
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/77_O0.S" -O0 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/77_O0_bin" "${TMPDIR}/77_O0.S" "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU "${TMPDIR}/77_O0_bin" > "${TMPDIR}/77_O0_out.txt" 2>/dev/null
O0_EXIT=$?
O0_MD5=$(md5sum "${TMPDIR}/77_O0_out.txt" | cut -d" " -f1)
echo "O0 exit=$O0_EXIT md5=$O0_MD5"

echo "=== O1 Mem2Reg PhiLowering ==="
"${BUILD_DIR}/compiler" -S "$SRC" -o "${TMPDIR}/77_O1.S" -O1 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/77_O1_bin" "${TMPDIR}/77_O1.S" "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU "${TMPDIR}/77_O1_bin" > "${TMPDIR}/77_O1_out.txt" 2>/dev/null
O1_EXIT=$?
O1_MD5=$(md5sum "${TMPDIR}/77_O1_out.txt" | cut -d" " -f1)
echo "O1 exit=$O1_EXIT md5=$O1_MD5"

if [ "$O0_MD5" = "$O1_MD5" ]; then
    echo "MATCH!"
else
    echo "DIFFER!"
fi
'