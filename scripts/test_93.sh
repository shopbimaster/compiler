#!/bin/bash
B=/mnt/d/VSCodeProjects/compiler/build
S=/mnt/d/VSCodeProjects/compiler/test/functional/93_nested_calls.sy
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
INPUT="1 2 3 4 1 2 3 4 5 6 7 8 9 10"

for opt in o0 o1 o2 o3; do
    echo "=== Compiling -${opt} ==="
    ${B}/compiler -S "$S" -o "/tmp/test93_${opt}.S" -${opt}
    ${GCC} -march=rv64gc -mabi=lp64d -static -o "/tmp/test93_${opt}_bin" "/tmp/test93_${opt}.S" "${B}/libsylib.a"
    echo "=== Running -${opt} ==="
    echo "$INPUT" | timeout 5 ${QEMU} "/tmp/test93_${opt}_bin"
    RET=$?
    echo "EXIT: ${RET}"
    echo ""
done