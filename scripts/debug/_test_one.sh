#!/bin/bash
BUILD_DIR="./build"
FUNC_DIR="./test/functional"
SYLIB_A="${BUILD_DIR}/libsylib.a"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64

test_one() {
    local name=$1
    local opt=$2
    local src="${FUNC_DIR}/${name}.sy"
    local asm="/tmp/_test_${name}.S"
    local bin="/tmp/_test_${name}_bin"
    local infile="${FUNC_DIR}/${name}.in"
    
    echo "=== Testing ${name} (${opt}) ==="
    ${BUILD_DIR}/compiler -S "$src" -o "$asm" -${opt} 2>&1
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>&1
    
    local ret=0
    if [ -f "$infile" ]; then
        $QEMU "$bin" < "$infile" > /dev/null 2>&1
        ret=$?
    else
        $QEMU "$bin" > /dev/null 2>&1
        ret=$?
    fi
    echo "Exit code: ${ret}"
}

test_one "92_register_alloc" "O0"
test_one "87_many_params" "O0"
test_one "88_many_params2" "O0"