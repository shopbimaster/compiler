#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"
mkdir -p /tmp/h903

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"
infile="${PROJECT_DIR}/test/performance/${name}.in"

for opt in O0 o1; do
    asm="/tmp/h903/${name}_${opt}.S"
    bin="/tmp/h903/${name}_${opt}_bin"
    ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${opt} 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
    start=$(date +%s.%N)
    timeout 10 $QEMU "$bin" < "$infile" > /tmp/h903/out_${opt}.txt 2>/dev/null
    ret=$?
    end=$(date +%s.%N)
    elapsed=$(echo "$end - $start" | bc)
    echo "${opt}: ret=${ret} time=${elapsed}s lines=$(wc -l < /tmp/h903/out_${opt}.txt)"
done
