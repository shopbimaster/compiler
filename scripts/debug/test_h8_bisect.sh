#!/bin/bash
# Bisect h-8-01 SEGFAULT by OPT_BISECT_O2 cutoff stage
set +e

PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"

src="${PROJECT_DIR}/test/performance/h-8-01.sy"
infile="${PROJECT_DIR}/test/performance/h-8-01.in"

mkdir -p /tmp/h8_bisect

for cutoff in 5 61 62 63 0; do
    asm="/tmp/h8_bisect/cutoff_${cutoff}.S"
    bin="/tmp/h8_bisect/cutoff_${cutoff}_bin"

    OPT_BISECT_O2=$cutoff ${BUILD_DIR}/compiler -S "$src" -o "$asm" -o2 2>/dev/null
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null
    timeout 15 $QEMU "$bin" < "$infile" > /tmp/h8_bisect/out_${cutoff}.txt 2>/dev/null
    ret=$?
    echo "OPT_BISECT_O2=$cutoff: ret=$ret"
done
