#!/bin/bash
set +e
cd /mnt/d/VSCodeProjects/compiler

# Generate assembly with IfConversion (cutoff=61 = after IfConversion)
OPT_BISECT_O2=61 build/compiler -S test/performance/h-8-01.sy -o /tmp/h8_after.S -o2 2>/dev/null

echo "=== Lines 390-440 of after-IfConversion assembly ==="
sed -n '390,440p' /tmp/h8_after.S

echo ""
echo "=== Where is s0 defined/used? ==="
grep -n "  .*s0" /tmp/h8_after.S | head -20

echo ""
echo "=== Where is s11 defined/used? ==="
grep -n "  .*s11" /tmp/h8_after.S | head -20

echo ""
echo "=== Look for the icmp that produces %slt (s0) ==="
grep -n "slt\|sge" /tmp/h8_after.S | head -20
