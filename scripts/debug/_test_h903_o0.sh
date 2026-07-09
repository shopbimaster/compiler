#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"

mkdir -p /tmp/h903
${BUILD_DIR}/compiler -S "${PROJECT_DIR}/test/performance/h-9-03.sy" -o /tmp/h903/O0.S -O0 2>&1
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/h903/O0_bin /tmp/h903/O0.S "${BUILD_DIR}/libsylib.a" 2>&1

start=$(date +%s.%N)
timeout 60 qemu-riscv64 /tmp/h903/O0_bin < "${PROJECT_DIR}/test/performance/h-9-03.in" > /tmp/h903/O0_out.txt 2>&1
ret=$?
end=$(date +%s.%N)
elapsed=$(echo "$end - $start" | bc)
echo "O0: ret=${ret} time=${elapsed}s lines=$(wc -l < /tmp/h903/O0_out.txt)"
