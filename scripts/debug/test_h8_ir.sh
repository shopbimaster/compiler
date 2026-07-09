#!/bin/bash
# Generate IR before and after IfConversion to compare
set +e

PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"

src="${PROJECT_DIR}/test/performance/h-8-01.sy"

# Before IfConversion (cutoff=5, after LICM)
OPT_BISECT_O2=5 ${BUILD_DIR}/compiler -o "$src" -O0 2>/dev/null > /tmp/h8_before_ifconv.ir
# Hmm, -O0 doesn't run O2. Need to use -o2 with bisect.
OPT_BISECT_O2=5 ${BUILD_DIR}/compiler -o2 "$src" 2>/dev/null > /tmp/h8_before_ifconv.ir
OPT_BISECT_O2=61 ${BUILD_DIR}/compiler -o2 "$src" 2>/dev/null > /tmp/h8_after_ifconv.ir

echo "=== IR size before IfConversion ==="
wc -l /tmp/h8_before_ifconv.ir
echo "=== IR size after IfConversion ==="
wc -l /tmp/h8_after_ifconv.ir

echo ""
echo "=== Diff (before -> after IfConversion) ==="
diff /tmp/h8_before_ifconv.ir /tmp/h8_after_ifconv.ir | head -100

echo ""
echo "=== Search for 'ifconv' or 'select' in after IR ==="
grep -n "ifconv\|select\|SELECT" /tmp/h8_after_ifconv.ir | head -30

echo ""
echo "=== Search for PHI in after IR ==="
grep -n "phi\|PHI" /tmp/h8_after_ifconv.ir | head -30
