#!/bin/bash
# Generate all perf assemblies and survey patterns in one session
cd /mnt/d/VSCodeProjects/compiler
rm -f /tmp/survey_*.S
for src in test/performance/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    build/compiler -S "$src" -o /tmp/survey_${name}.S -O1 2>/dev/null
done

echo "=== instruction frequency (top 25) ==="
cat /tmp/survey_*.S | grep -vE '^[[:space:]]*(#|\.|$|[a-zA-Z_][a-zA-Z0-9_]*:)' | \
    awk '{print $1}' | sort | uniq -c | sort -rn | head -25

echo ""
echo "=== li constant reuse: top 15 repeated ==="
cat /tmp/survey_*.S | grep -E '^[[:space:]]*li[[:space:]]' | \
    sed 's/^[[:space:]]*//' | sort | uniq -c | sort -rn | head -15

echo ""
echo "=== redundant 'li rd, 0' count ==="
cat /tmp/survey_*.S | grep -cE '^[[:space:]]*li[[:space:]]+\w+,[[:space:]]*0[[:space:]]*$'

echo ""
echo "=== callee-saved save/restore count ==="
echo -n "sd ra/sN: "; cat /tmp/survey_*.S | grep -cE '^[[:space:]]*sd[[:space:]]+(ra|s[0-9]+),'
echo -n "ld ra/sN: "; cat /tmp/survey_*.S | grep -cE '^[[:space:]]*ld[[:space:]]+(ra|s[0-9]+),'

echo ""
echo "=== total instruction count ==="
cat /tmp/survey_*.S | grep -vE '^[[:space:]]*(#|\.|$|[a-zA-Z_][a-zA-Z0-9_]*:)' | wc -l
