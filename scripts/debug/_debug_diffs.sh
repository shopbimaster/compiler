#!/bin/bash
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
FUNC_DIR=/mnt/d/VSCodeProjects/compiler/test/functional
SYLIB_A=${BUILD_DIR}/libsylib.a
TMP=~/tmp2
mkdir -p $TMP
export TMPDIR=$TMP

for name in 53_scope2 78_side_effect 80_chaos_token 95_float; do
    echo "========== ${name} =========="
    src="${FUNC_DIR}/${name}.sy"
    asm="${TMP}/func_test_${name}.S"
    bin="${TMP}/func_test_${name}_bin"
    infile="${FUNC_DIR}/${name}.in"
    
    ${BUILD_DIR}/sysyc -S "$src" -o "$asm" -O0 2>&1
    
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>&1
    
    if [ -f "$infile" ]; then
        timeout 5 $QEMU "$bin" < "$infile" > "${TMP}/${name}_out.txt" 2>&1
    else
        timeout 5 $QEMU "$bin" > "${TMP}/${name}_out.txt" 2>&1
    fi
    ret=$?
    
    echo "Exit code: $ret"
    echo "--- OUTPUT ---"
    cat "${TMP}/${name}_out.txt"
    echo ""
    echo "--- EXPECTED ---"
    head -n -1 "${FUNC_DIR}/${name}.out"
    echo ""
done