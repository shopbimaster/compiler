#!/bin/bash
set +e
PROJECT_DIR="/mnt/d/VSCodeProjects/compiler"
BUILD_DIR="${PROJECT_DIR}/build"
mkdir -p /tmp/h903_asm

name="h-9-03"
src="${PROJECT_DIR}/test/performance/${name}.sy"

${BUILD_DIR}/compiler -S "$src" -o /tmp/h903_asm/o1.S -o1 2>/dev/null
${BUILD_DIR}/compiler -S "$src" -o /tmp/h903_asm/o2.S -o2 2>/dev/null

echo "=== o1.S size ==="
wc -l /tmp/h903_asm/o1.S
echo "=== o2.S size ==="
wc -l /tmp/h903_asm/o2.S

echo ""
echo "=== o1.S prime_factors function ==="
awk '/^prime_factors:/,/^\.size.*prime_factors/' /tmp/h903_asm/o1.S

echo ""
echo "=== o2.S prime_factors function ==="
awk '/^prime_factors:/,/^\.size.*prime_factors/' /tmp/h903_asm/o2.S
