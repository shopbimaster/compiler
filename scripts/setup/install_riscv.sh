#!/bin/bash
riscv64-unknown-elf-gcc --version 2>&1 | head -2
echo "---"
which spike
echo "---"
echo 'int main(){return 42;}' > /tmp/test.c
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d -o /tmp/test /tmp/test.c 2>&1
echo "compile: $?"
spike pk /tmp/test 2>&1
echo "spike exit: $?"