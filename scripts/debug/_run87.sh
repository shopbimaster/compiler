#!/bin/bash
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
FUNC_DIR=/mnt/d/VSCodeProjects/compiler/test/functional
TMPDIR=${BUILD_DIR}/tmptest
mkdir -p ${TMPDIR}
export TMPDIR

# 87_many_params
${BUILD_DIR}/compiler -S ${FUNC_DIR}/87_many_params.sy -o ${TMPDIR}/t87.S -O0
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o ${TMPDIR}/t87_bin ${TMPDIR}/t87.S ${BUILD_DIR}/libsylib.a
echo "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16" | qemu-riscv64 -d in_asm ${TMPDIR}/t87_bin 2>${TMPDIR}/t87_trace.log >${TMPDIR}/t87_out
echo "87_many_params exit=$?"
echo "Last 30 lines of trace:"
tail -30 ${TMPDIR}/t87_trace.log