#!/bin/bash
# Bisect which O2 pass introduces 77_substr SEGFAULT
# Strategy: temporarily modify Optimizer.cpp to disable specific passes, rebuild, test
cd /mnt/d/VSCodeProjects/compiler

# Save original
cp src/opt/Optimizer.cpp /tmp/Optimizer.cpp.bak

# Function to test a specific configuration
test_config() {
    local name=$1
    echo "=== Testing: $name ==="
    cd /mnt/d/VSCodeProjects/compiler/build && make -j$(nproc) 2>&1 | tail -3
    cd /mnt/d/VSCodeProjects/compiler
    ./build/compiler -S -O1 test/functional/77_substr.sy -o /tmp/77_test.S 2>&1
    riscv64-linux-gnu-gcc -static /tmp/77_test.S build/libsylib.a -o /tmp/77_test.elf 2>&1
    timeout 5 qemu-riscv64 /tmp/77_test.elf 2>&1
    echo "EXIT=$?"
}

# Test 1: Disable Mem2Reg (just comment it out)
sed -i 's|if (mem2reg(mod)) {|if (false \&\& mem2reg(mod)) {|' src/opt/Optimizer.cpp
test_config "no_mem2reg"
# Restore
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 2: Disable LICM
sed -i 's|if (loopInvariantCodeMotion(mod)) {|if (false \&\& loopInvariantCodeMotion(mod)) {|' src/opt/Optimizer.cpp
# There are 2 LICM calls - disable both
sed -i '0,/if (false && loopInvariantCodeMotion(mod)) {/! s|if (loopInvariantCodeMotion(mod)) {|if (false \&\& loopInvariantCodeMotion(mod)) {|' src/opt/Optimizer.cpp
test_config "no_licm"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 3: Disable CodeSink
sed -i 's|if (codeSink(mod)) {|if (false \&\& codeSink(mod)) {|' src/opt/Optimizer.cpp
test_config "no_codesink"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 4: Disable inlineExpansion
sed -i 's|inlineExpansion(mod);|// inlineExpansion(mod);|' src/opt/Optimizer.cpp
test_config "no_inline"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 5: Disable globalVariablePromotion
sed -i 's|if (globalVariablePromotion(mod)) {|if (false \&\& globalVariablePromotion(mod)) {|' src/opt/Optimizer.cpp
test_config "no_gvp"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 6: Disable SimplifyCFG
sed -i 's|if (simplifyCFG(mod)) {|if (false \&\& simplifyCFG(mod)) {|g' src/opt/Optimizer.cpp
test_config "no_simplifycfg"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 7: Disable instCombine
sed -i 's|if (instCombine(mod)) {|if (false \&\& instCombine(mod)) {|g' src/opt/Optimizer.cpp
test_config "no_instcombine"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 8: Disable BasicBlockReordering
sed -i 's|if (basicBlockReordering(mod)) {|if (false \&\& basicBlockReordering(mod)) {|' src/opt/Optimizer.cpp
test_config "no_bbreorder"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 9: Disable SCCP
sed -i 's|if (sparseConditionalConstantPropagation(mod)) {|if (false \&\& sparseConditionalConstantPropagation(mod)) {|g' src/opt/Optimizer.cpp
test_config "no_sccp"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 10: Disable copyPropagation
sed -i 's|if (copyPropagation(mod)) {|if (false \&\& copyPropagation(mod)) {|' src/opt/Optimizer.cpp
test_config "no_copyprop"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 11: Disable loadElimination
sed -i 's|if (loadElimination(mod)) {|if (false \&\& loadElimination(mod)) {|g' src/opt/Optimizer.cpp
test_config "no_loadelim"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 12: Disable DSE
sed -i 's|if (deadStoreElimination(mod)) {|if (false \&\& deadStoreElimination(mod)) {|g' src/opt/Optimizer.cpp
test_config "no_dse"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 13: Disable ADCE
sed -i 's|if (adce(mod)) {|if (false \&\& adce(mod)) {|' src/opt/Optimizer.cpp
test_config "no_adce"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Test 14: Disable IfConversion
sed -i 's|if (ifConversion(mod)) {|if (false \&\& ifConversion(mod)) {|' src/opt/Optimizer.cpp
test_config "no_ifconv"
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp

# Restore original
cp /tmp/Optimizer.cpp.bak src/opt/Optimizer.cpp
make -C build -j$(nproc) 2>&1 | tail -3

echo "=== Done ==="
