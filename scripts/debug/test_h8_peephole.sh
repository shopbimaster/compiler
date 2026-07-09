#!/bin/bash
set +e
cd /mnt/d/VSCodeProjects/compiler

src="test/performance/h-8-01.sy"
infile="test/performance/h-8-01.in"

# Generate assembly with IfConversion (cutoff=61) — this has the SEGFAULT
OPT_BISECT_O2=61 build/compiler -S "$src" -o /tmp/h8_with_peephole.S -o2 2>/dev/null

# Generate assembly WITHOUT peephole by temporarily patching Compiler.cpp
# Actually, let's just test the assembly with peephole first to confirm SEGFAULT
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB_A="build/libsylib.a"

$GCC -march=rv64gc -mabi=lp64d -static -o /tmp/h8_bin /tmp/h8_with_peephole.S "$SYLIB_A" 2>/dev/null
timeout 15 $QEMU /tmp/h8_bin < "$infile" > /tmp/h8_out.txt 2>/dev/null
ret=$?
echo "With peephole (cutoff=61): ret=$ret"

# Now let's look at the SELECT region in the assembly
echo ""
echo "=== SELECT region (with peephole) ==="
grep -n -A 15 "and_rhs_18" /tmp/h8_with_peephole.S | head -30

echo ""
echo "=== Looking for potential peephole cross-label issues ==="
# Look for patterns where an instruction is followed by a label and then another instruction
awk '/^\.\./ { in_label=1; next } in_label && /^  [a-z]/ { print NR": "last_line" -> LABEL -> "$0; in_label=0 } /^  [a-z]/ { last_line=$0 }' /tmp/h8_with_peephole.S | head -20
