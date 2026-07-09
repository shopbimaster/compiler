#!/bin/bash
# Generate and inspect assembly for h-8-01 with and without IfConversion
set +e

PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"

src="${PROJECT_DIR}/test/performance/h-8-01.sy"

# Generate assembly with and without IfConversion
OPT_BISECT_O2=5 ${BUILD_DIR}/compiler -S "$src" -o /tmp/h8_before.S -o2 2>/dev/null
OPT_BISECT_O2=61 ${BUILD_DIR}/compiler -S "$src" -o /tmp/h8_after.S -o2 2>/dev/null

echo "=== Assembly size before/after IfConversion ==="
wc -l /tmp/h8_before.S /tmp/h8_after.S

echo ""
echo "=== Search for select labels in after assembly ==="
grep -n "select_false\|select_end" /tmp/h8_after.S | head -20

echo ""
echo "=== First select context (after IfConversion) ==="
grep -n -B 5 -A 10 "select_false_0" /tmp/h8_after.S | head -40

echo ""
echo "=== Diff (before -> after) first 80 lines ==="
diff /tmp/h8_before.S /tmp/h8_after.S | head -80
