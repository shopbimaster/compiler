#!/bin/bash
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
TMPDIR=/tmp
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64

SRC=/mnt/d/VSCodeProjects/compiler/test/functional/93_nested_calls.sy
INPUT="1 2 3 4 1 2 3 4 5 6 7 8 9 10"

for opt in o0 o1 o2 o3; do
    echo "=== Testing -${opt} ==="
    ${BUILD_DIR}/compiler -S "$SRC" -o "${TMPDIR}/test93_${opt}.S" -${opt} 2>&1
    ${GCC} -march=rv64gc -mabi=lp64d -static -o "${TMPDIR}/test93_${opt}_bin" "${TMPDIR}/test93_${opt}.S" "${BUILD_DIR}/libsylib.a" 2>&1
    echo "$INPUT" | timeout 5 ${QEMU} "${TMPDIR}/test93_${opt}_bin" > "${TMPDIR}/test93_${opt}_out.txt" 2>&1
    RET=$?
    echo "exit: ${RET}"
    if [ $RET -eq 0 ]; then
        cat "${TMPDIR}/test93_${opt}_out.txt"
    fi
    echo ""
done