#!/bin/bash
SYLIB_A=/mnt/d/VSCodeProjects/compiler/build/libsylib.a
FUNC_DIR=/mnt/d/VSCodeProjects/compiler/test/functional
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64

${BUILD_DIR}/sysyc -S ${FUNC_DIR}/87_many_params.sy -o /tmp/t87.S -O0
${GCC} -march=rv64gc -mabi=lp64d -static -o /tmp/t87_bin /tmp/t87.S ${SYLIB_A}
echo "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16" | ${QEMU} -d in_asm,cpu /tmp/t87_bin 2>/tmp/t87_trace.log
echo "Exit: $?"
echo "=== Last 10 lines of trace ==="
tail -10 /tmp/t87_trace.log