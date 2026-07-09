#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
mkdir -p /tmp/h8_test

for name in h-8-01 h-8-02 h-8-03; do
    src="${PROJECT_DIR}/test/performance/${name}.sy"
    infile="${PROJECT_DIR}/test/performance/${name}.in"
    exp="${PROJECT_DIR}/test/performance/${name}.out"
    asm="/tmp/h8_test/${name}_O1.S"
    bin="/tmp/h8_test/${name}_O1_bin"

    ${BUILD_DIR}/compiler -S "${src}" -o "${asm}" -O1 >/dev/null 2>&1
    riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "${bin}" "${asm}" "${BUILD_DIR}/libsylib.a" 2>/dev/null
    qemu-riscv64 "${bin}" < "${infile}" > "/tmp/h8_test/act_${name}.txt" 2>/dev/null

    if diff -q "${exp}" "/tmp/h8_test/act_${name}.txt" >/dev/null; then
        echo "${name}: PASS"
    else
        echo "${name}: FAIL"
        echo "--- expected (head 5) ---"
        head -5 "${exp}"
        echo "--- actual (head 5) ---"
        head -5 "/tmp/h8_test/act_${name}.txt"
    fi
done
