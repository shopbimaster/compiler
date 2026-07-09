#!/bin/bash
set +e
cd /mnt/d/VSCodeProjects/compiler

# Regenerate the assembly
OPT_BISECT_O2=61 build/compiler -S test/performance/h-8-01.sy -o /tmp/h8_with_peephole.S -o2 2>/dev/null

echo "=== Lines 350-420 of assembly ==="
sed -n '350,420p' /tmp/h8_with_peephole.S

echo ""
echo "=== Where is s0 defined in the whole function? ==="
grep -n "  .*s0," /tmp/h8_with_peephole.S | head -30

echo ""
echo "=== Block labels ==="
grep -n "^\." /tmp/h8_with_peephole.S | head -40
