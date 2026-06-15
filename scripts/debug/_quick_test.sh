#!/bin/bash
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SRC=/mnt/d/VSCodeProjects/compiler/test/functional
TMPDIR=~/tmp
mkdir -p ${TMPDIR}
export TMPDIR

echo "=== Basic tests ==="
tests="00_main 01_var_defn2 26_while_test1 29_break 73_int_io"
for t in $tests; do
    ${BUILD_DIR}/compiler -S ${SRC}/${t}.sy -o ${TMPDIR}/${t}.S -O0 2>/dev/null
    riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o ${TMPDIR}/${t}_bin ${TMPDIR}/${t}.S ${BUILD_DIR}/libsylib.a 2>/dev/null
    if [ -f "${SRC}/${t}.in" ]; then
        qemu-riscv64 ${TMPDIR}/${t}_bin < ${SRC}/${t}.in > /dev/null 2>&1
    else
        qemu-riscv64 ${TMPDIR}/${t}_bin > /dev/null 2>&1
    fi
    ret=$?
    if [ $ret -eq 139 ]; then
        echo "  ${t}: SEGFAULT"
    elif [ $ret -ne 0 ]; then
        echo "  ${t}: FAIL (ret=$ret)"
    else
        echo "  ${t}: PASS"
    fi
done