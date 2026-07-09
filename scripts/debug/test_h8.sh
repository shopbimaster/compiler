#!/bin/bash
# Test h-8-01/02/03 at different optimization levels to find where SEGFAULT is introduced
set +e

PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="${BUILD_DIR}/libsylib.a"

mkdir -p /tmp/h8_test

for name in h-8-01 h-8-02 h-8-03; do
    src="${PROJECT_DIR}/test/performance/${name}.sy"
    infile="${PROJECT_DIR}/test/performance/${name}.in"

    echo "============================================================"
    echo "  Testing ${name}"
    echo "============================================================"

    for opt in O0 o1 o2 o3 O1; do
        asm="/tmp/h8_test/${name}_${opt}.S"
        bin="/tmp/h8_test/${name}_${opt}_bin"

        if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${opt} 2>/tmp/h8_test/err.txt; then
            echo "  ${opt}: COMPILE FAIL"
            head -3 /tmp/h8_test/err.txt
            continue
        fi

        if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
            echo "  ${opt}: LINK FAIL"
            continue
        fi

        timeout 15 $QEMU "$bin" < "$infile" > /tmp/h8_test/out.txt 2>/dev/null
        ret=$?
        echo "  ${opt}: ret=${ret}"
    done
    echo ""
done
