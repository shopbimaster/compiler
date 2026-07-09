#!/bin/bash
# Bisect which O3 pass introduces 60_sort_test6 SEGFAULT
cd /mnt/d/VSCodeProjects/compiler

# Save original
cp src/opt/Optimizer.cpp /tmp/Optimizer.cpp.bak

test_config() {
    local name=$1
    echo "=== Testing: $name ==="
    cd /mnt/d/VSCodeProjects/compiler/build && make -j$(nproc) 2>&1 | tail -1
    cd /mnt/d/VSCodeProjects/compiler
    ./build/compiler -S -O1 test/functional/60_sort_test6.sy -o /tmp/60_test.S 2>&1
    riscv64-linux-gnu-gcc -static /tmp/60_test.S build/libsylib.a -o /tmp/60_test.elf 2>&1
    timeout 5 qemu-riscv64 /tmp/60_test.elf 2>&1
    echo "EXIT=$?"
}

# Test each O3 pass
passes=("loopInterchange" "loopStrengthReduce" "gepStrengthReduce" "loopFullUnroll" "loopUnrolling")
for pass in "${passes[@]}"; do
    # Disable this pass
    sed -i "s|if ($pass(mod)) {|if (false \&\& $pass(mod)) {|g" src/opt/Optimizer.cpp
    test_config "no_$pass"
    cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp
done

# Restore
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp
make -C build -j$(nproc) 2>&1 | tail -1

echo "=== Done ==="
