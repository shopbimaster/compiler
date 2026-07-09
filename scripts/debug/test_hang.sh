#!/bin/bash
# Test specific h_functional cases that might hang
cd /mnt/d/VSCodeProjects/compiler

for name in 22_matrix_multiply 23_json 24_array_only 25_scope3 26_scope4; do
    src="test/h_functional/${name}.sy"
    [ -f "$src" ] || continue
    echo -n "Testing $name... "
    asm="/tmp/hfunc_${name}.S"
    bin="/tmp/hfunc_${name}_bin"
    infile="test/h_functional/${name}.in"

    if ! ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        echo "COMPILE FAIL"
        continue
    fi
    if ! riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" build/libsylib.a 2>/dev/null; then
        echo "LINK FAIL"
        continue
    fi

    set +e
    if [ -f "$infile" ]; then
        timeout 8 qemu-riscv64 "$bin" < "$infile" > /tmp/hfunc_${name}_out.txt 2>/dev/null
    else
        timeout 8 qemu-riscv64 "$bin" > /tmp/hfunc_${name}_out.txt 2>/dev/null
    fi
    ret=$?
    set -e

    if [ $ret -eq 124 ]; then
        echo "TIMEOUT"
    elif [ $ret -eq 139 ]; then
        echo "SEGFAULT"
    else
        echo "OK exit=$ret"
    fi
done
