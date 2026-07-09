#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"

name="11_BST"
src="${PROJECT_DIR}/test/h_functional/${name}.sy"
infile="${PROJECT_DIR}/test/h_functional/${name}.in"

for opt in O0 o1 o2 o3 O1; do
    asm="/tmp/${name}_${opt}.S"
    bin="/tmp/${name}_${opt}_bin"
    build/compiler -S "$src" -o "$asm" -${opt} 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
    timeout 5 $QEMU "$bin" < "$infile" > "/tmp/${name}_${opt}_out.txt" 2>/dev/null
    ret=$?
    echo "  ${opt}: ret=${ret} lines=$(wc -l < /tmp/${name}_${opt}_out.txt)"
done
