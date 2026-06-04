#!/bin/bash
SYLIB_A=/mnt/d/VSCodeProjects/compiler/build/libsylib.a
FUNC_DIR=/mnt/d/VSCodeProjects/compiler/test/functional
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
TMPDIR=${BUILD_DIR}/tmp
mkdir -p ${TMPDIR}
export TMPDIR
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64

test_one() {
    local name=$1
    local expected=${2:-0}
    local infile="${FUNC_DIR}/${name}.in"
    local src="${FUNC_DIR}/${name}.sy"
    local asm="${TMPDIR}/_t_${name}.S"
    local bin="${TMPDIR}/_t_${name}_bin"
    
    echo "=== Testing ${name} (expected=$expected) ==="
    if ! ${BUILD_DIR}/sysyc -S "$src" -o "$asm" -O0 2>&1; then
        echo "  COMPILE ERROR"
        return 1
    fi
    if ! TMPDIR=${TMPDIR} $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>&1; then
        echo "  LINK ERROR"
        return 1
    fi
    if [ -f "$infile" ]; then
        TMPDIR=${TMPDIR} $QEMU "$bin" < "$infile" > "${TMPDIR}/_t_${name}_out" 2>&1
    else
        TMPDIR=${TMPDIR} $QEMU "$bin" > "${TMPDIR}/_t_${name}_out" 2>&1
    fi
    local ret=$?
    echo "  Return code: $ret"
    if [ $ret -eq 139 ]; then
        echo "  SEGFAULT!"
        return 1
    elif [ $ret -ne "$expected" ]; then
        echo "  WRONG: got $ret, expected $expected"
        echo "  stdout:"
        cat ${TMPDIR}/_t_${name}_out
        return 1
    else
        echo "  PASS"
        echo "  stdout:"
        cat ${TMPDIR}/_t_${name}_out
        return 0
    fi
}

echo "========================================"
echo "Debugging failing test cases"
echo "========================================"

echo ""
test_one "87_many_params" 0
echo ""
test_one "88_many_params2" 0
echo ""
test_one "95_float" 0