#!/bin/bash
# ================================================================
# SysY 编译器 全量 Functional 测试 (100 个测试用例)
# 用法: ./test_all_functional.sh
# ================================================================
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
FUNC_DIR="${SCRIPT_DIR}/test/functional"
SYLIB_A="${BUILD_DIR}/libsylib.a"

if [ ! -f "$SYLIB_A" ]; then
    echo "Building runtime library..."
    bash "${SCRIPT_DIR}/build/build_sylib.sh"
fi

COMPILE_OK=0
COMPILE_FAIL=0
LINK_OK=0
LINK_FAIL=0
RUN_OK=0
RUN_FAIL=0
RUN_SEGFAULT=0

echo "============================================================"
echo "SysY Compiler - Full Functional Test Suite (100 tests, O0)"
echo "============================================================"
echo ""

for src in "${FUNC_DIR}"/*.sy; do
    name=$(basename "$src" .sy)
    asm="/tmp/func_test_${name}.S"
    bin="/tmp/func_test_${name}_bin"
    infile="${FUNC_DIR}/${name}.in"
    
    # Compile
    if ! ${BUILD_DIR}/sysyc -S "$src" -o "$asm" -O0 2>/dev/null; then
        echo "  COMPILE FAIL: ${name}"
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        continue
    fi
    COMPILE_OK=$((COMPILE_OK + 1))
    
    # Assemble + Link
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "  LINK FAIL:    ${name}"
        LINK_FAIL=$((LINK_FAIL + 1))
        continue
    fi
    LINK_OK=$((LINK_OK + 1))
    
    # Run
    if [ -f "$infile" ]; then
        $QEMU "$bin" < "$infile" > /dev/null 2>&1
    else
        $QEMU "$bin" > /dev/null 2>&1
    fi
    ret=$?
    
    if [ $ret -eq 139 ]; then
        echo "  SEGFAULT:     ${name}"
        RUN_SEGFAULT=$((RUN_SEGFAULT + 1))
    elif [ $ret -ne 0 ]; then
        echo "  RUN FAIL:     ${name} (ret=$ret)"
        RUN_FAIL=$((RUN_FAIL + 1))
    else
        RUN_OK=$((RUN_OK + 1))
    fi
done

echo ""
echo "============================================================"
echo "Results:"
echo "  Compile:  ${COMPILE_OK} OK, ${COMPILE_FAIL} FAIL"
echo "  Link:     ${LINK_OK} OK, ${LINK_FAIL} FAIL"
echo "  Runtime:  ${RUN_OK} OK, ${RUN_FAIL} FAIL (non-zero), ${RUN_SEGFAULT} SEGFAULT"
echo "============================================================"