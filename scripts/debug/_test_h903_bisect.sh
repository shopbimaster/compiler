#!/bin/bash
# Bisect h-9-03 TIMEOUT in O2 stages
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"

mkdir -p /tmp/h903_bisect

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"
infile="${PROJECT_DIR}/test/performance/${name}.in"

for cutoff in 0 1 2 3 4 5 61 62 63 0; do
    asm="/tmp/h903_bisect/cutoff_${cutoff}.S"
    bin="/tmp/h903_bisect/cutoff_${cutoff}_bin"

    OPT_BISECT_O2=${cutoff} ${BUILD_DIR}/compiler -S "$src" -o "$asm" -O1 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null

    start=$(date +%s.%N)
    timeout 10 $QEMU "$bin" < "$infile" > /tmp/h903_bisect/out_${cutoff}.txt 2>/dev/null
    ret=$?
    end=$(date +%s.%N)
    elapsed=$(echo "$end - $start" | bc)
    echo "cutoff=${cutoff}: ret=${ret} time=${elapsed}s lines=$(wc -l < /tmp/h903_bisect/out_${cutoff}.txt)"
done
