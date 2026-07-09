#!/bin/bash
set +e
cd /mnt/d/VSCodeProjects/compiler

src="test/performance/h-8-01.sy"
infile="test/performance/h-8-01.in"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="build/libsylib.a"

# Test with peephole (cutoff=61 = after IfConversion)
OPT_BISECT_O2=61 build/compiler -S "$src" -o /tmp/h8_peephole.S -o2 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o /tmp/h8_peephole_bin /tmp/h8_peephole.S "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU /tmp/h8_peephole_bin < "$infile" > /tmp/h8_peephole_out.txt 2>/dev/null
ret_with=$?
echo "With peephole (cutoff=61): ret=$ret_with"

# Test without peephole (cutoff=61 = after IfConversion)
NO_PEEPHOLE=1 OPT_BISECT_O2=61 build/compiler -S "$src" -o /tmp/h8_nopeephole.S -o2 2>/dev/null
$GCC -march=rv64gc -mabi=lp64d -static -o /tmp/h8_nopeephole_bin /tmp/h8_nopeephole.S "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU /tmp/h8_nopeephole_bin < "$infile" > /tmp/h8_nopeephole_out.txt 2>/dev/null
ret_without=$?
echo "Without peephole (cutoff=61): ret=$ret_without"

# Compare SELECT region in both
echo ""
echo "=== SELECT region WITH peephole ==="
grep -n -B 2 -A 10 "select_false_0" /tmp/h8_peephole.S

echo ""
echo "=== SELECT region WITHOUT peephole ==="
grep -n -B 2 -A 10 "select_false_0" /tmp/h8_nopeephole.S
