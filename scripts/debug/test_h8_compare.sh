#!/bin/bash
set +e
cd /mnt/d/VSCodeProjects/compiler

src="test/performance/h-8-01.sy"
infile="test/performance/h-8-01.in"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="build/libsylib.a"

# Generate assembly with IfConversion (cutoff=61) — with peephole
OPT_BISECT_O2=61 build/compiler -S "$src" -o /tmp/h8_peephole.S -o2 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o /tmp/h8_peephole_bin /tmp/h8_peephole.S "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU /tmp/h8_peephole_bin < "$infile" > /tmp/h8_peephole_out.txt 2>/dev/null
ret_peephole=$?
echo "With peephole (cutoff=61): ret=$ret_peephole"

# Now manually strip peephole by generating raw assembly
# We need to temporarily disable peephole in Compiler.cpp
# Actually, let's just check if the issue is the SELECT local labels
# by looking at the before-IfConversion assembly (which works)
OPT_BISECT_O2=5 build/compiler -S "$src" -o /tmp/h8_no_ifconv.S -o2 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o /tmp/h8_no_ifconv_bin /tmp/h8_no_ifconv.S "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU /tmp/h8_no_ifconv_bin < "$infile" > /tmp/h8_no_ifconv_out.txt 2>/dev/null
ret_no_ifconv=$?
echo "Without IfConversion (cutoff=5): ret=$ret_no_ifconv"

# Compare outputs (if both succeeded)
if [ $ret_peephole -eq 0 ] && [ $ret_no_ifconv -eq 0 ]; then
    echo ""
    echo "=== Output diff (peephole vs no_ifconv) ==="
    diff /tmp/h8_peephole_out.txt /tmp/h8_no_ifconv_out.txt | head -20
fi

# Check assembly sizes
echo ""
echo "=== Assembly sizes ==="
wc -l /tmp/h8_peephole.S /tmp/h8_no_ifconv.S

# Look for the key difference: the SELECT region
echo ""
echo "=== SELECT region in peephole assembly ==="
grep -n -B 2 -A 10 "select_false_0" /tmp/h8_peephole.S

echo ""
echo "=== Same region in no-ifconv assembly (search for and_merge_19) ==="
grep -n -B 5 -A 10 "and_merge_19" /tmp/h8_no_ifconv.S | head -30
