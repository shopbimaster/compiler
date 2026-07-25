#!/bin/bash
# Measure total instruction count across perf tests
cd /mnt/d/VSCodeProjects/compiler
total=0
count=0
fail=0
for src in test/performance/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm=/tmp/perf_meas_${name}.S
    if ! ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        fail=$((fail+1))
        continue
    fi
    n=$(grep -vE '^\s*(#|\.|$|[a-zA-Z_][a-zA-Z0-9_]*:)' "$asm" | wc -l)
    total=$((total + n))
    count=$((count+1))
done
echo "tests=$count failures=$fail total_instructions=$total"
