#!/bin/bash
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SRC=/mnt/d/VSCodeProjects/compiler/test/functional
TMPDIR=~/tmp
mkdir -p ${TMPDIR}
export TMPDIR

echo "=== Failing tests ==="
tests="87_many_params 88_many_params2 95_float"
for t in $tests; do
    echo ""
    echo "--- ${t} ---"
    ${BUILD_DIR}/compiler -S ${SRC}/${t}.sy -o ${TMPDIR}/${t}.S -O0 2>&1
    riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o ${TMPDIR}/${t}_bin ${TMPDIR}/${t}.S ${BUILD_DIR}/libsylib.a 2>&1
    if [ -f "${SRC}/${t}.in" ]; then
        qemu-riscv64 -d in_asm ${TMPDIR}/${t}_bin < ${SRC}/${t}.in > ${TMPDIR}/${t}_out 2>${TMPDIR}/${t}_trace.log
    else
        qemu-riscv64 -d in_asm ${TMPDIR}/${t}_bin > ${TMPDIR}/${t}_out 2>${TMPDIR}/${t}_trace.log
    fi
    ret=$?
    if [ $ret -eq 139 ]; then
        echo "  SEGFAULT"
        echo "  Last 20 asm instructions:"
        tail -20 ${TMPDIR}/${t}_trace.log
    elif [ $ret -ne 0 ]; then
        echo "  FAIL (ret=$ret)"
    else
        echo "  PASS"
        cat ${TMPDIR}/${t}_out
    fi
done