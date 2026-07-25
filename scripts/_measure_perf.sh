#!/bin/bash
# Measure total instruction count across perf tests for a given config
cd /mnt/d/VSCodeProjects/compiler
RA="${RA_ALLOCATOR:-graph}"
GVN_OFF="${OPT_DISABLE_GVN:-0}"
ENV_STR=""
[ "$RA" = "linear" ] && ENV_STR="RA_ALLOCATOR=linear"
[ "$GVN_OFF" = "1" ] && ENV_STR="$ENV_STR OPT_DISABLE_GVN=1"

total_insn=0
count=0
fail=0
for src in test/performance/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm=/tmp/perf_meas_${name}.S
    if ! env $ENV_STR build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        fail=$((fail+1))
        continue
    fi
    # count non-empty, non-label, non-directive lines as instructions
    n=$(grep -vE '^\s*(#|\.|$|[a-zA-Z_][a-zA-Z0-9_]*:)' "$asm" | wc -l)
    total_insn=$((total_insn + n))
    count=$((count+1))
done
echo "config: RA=$RA GVN_OFF=$GVN_OFF"
echo "tests compiled: $count, failures: $fail"
echo "total instructions: $total_insn"
