#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"

mkdir -p /tmp/h903_test

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"
infile="${PROJECT_DIR}/test/performance/${name}.in"

for opt in O0 o1 o2 o3 O1; do
    asm="/tmp/h903_test/${name}_${opt}.S"
    bin="/tmp/h903_test/${name}_${opt}_bin"

    if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${opt} 2>/tmp/h903_test/err.txt; then
        echo "  ${opt}: COMPILE FAIL"
        head -3 /tmp/h903_test/err.txt
        continue
    fi

    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "  ${opt}: LINK FAIL"
        continue
    fi

    start=$(date +%s.%N)
    timeout 60 $QEMU "$bin" < "$infile" > /tmp/h903_test/out_${opt}.txt 2>/dev/null
    ret=$?
    end=$(date +%s.%N)
    elapsed=$(echo "$end - $start" | bc)
    echo "  ${opt}: ret=${ret} time=${elapsed}s lines=$(wc -l < /tmp/h903_test/out_${opt}.txt)"
done
