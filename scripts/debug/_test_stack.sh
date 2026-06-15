#!/bin/bash
# Test large stack frame
cat > /tmp/large_stack.sy << 'EOF'
int main() {
    int a[5000];
    putint(a[0]);
    return 0;
}
EOF
./build/compiler -S /tmp/large_stack.sy -o /tmp/large_stack.S -O0 2>&1
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o /tmp/large_stack_bin /tmp/large_stack.S build/libsylib.a 2>&1
qemu-riscv64 /tmp/large_stack_bin > /dev/null 2>&1
echo "Exit: $?"