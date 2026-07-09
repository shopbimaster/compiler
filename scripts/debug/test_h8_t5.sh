#!/bin/bash
set +e
cd /mnt/d/VSCodeProjects/compiler

# Generate assembly without peephole
NO_PEEPHOLE=1 OPT_BISECT_O2=61 build/compiler -S test/performance/h-8-01.sy -o /tmp/h8_nopeephole.S -o2 2>/dev/null

echo "=== All uses of t5 in the assembly ==="
grep -n "t5" /tmp/h8_nopeephole.S | head -30

echo ""
echo "=== All uses of t3, t4 in the assembly ==="
grep -n "  .*t3\b\|  .*t4\b" /tmp/h8_nopeephole.S | head -20

echo ""
echo "=== Context around the SELECT (lines 430-460) ==="
sed -n '430,460p' /tmp/h8_nopeephole.S

echo ""
echo "=== Looking at where t5 is first defined ==="
grep -n "  li      t5\|  mv      t5\|  lw      t5\|  ld      t5\|  add     t5\|  sub     t5" /tmp/h8_nopeephole.S | head -20
