#!/bin/bash
# Quick instruction count across all performance test cases
cd "$(dirname "$0")/.."
C=build/compiler
PERF=test/performance
total=0
count=0
for src in "$PERF"/*.sy; do
    name=$(basename "$src" .sy)
    "$C" -S "$src" -o "/tmp/p_${name}.S" -O1 2>/dev/null
    n=$(grep -cE '^[[:space:]]+[a-z]' "/tmp/p_${name}.S" 2>/dev/null)
    n=${n:-0}
    total=$((total + n))
    count=$((count + 1))
    echo "$name $n"
done
echo "TOTAL $total CASES $count"
