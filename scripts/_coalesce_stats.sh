#!/bin/bash
# How often does coalescePhis fire across the performance suite, and via which
# acceptance path (classic RMW vs. the newer disjoint-interval rule)?
set -u
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1
tot=0
rmw=0
dis=0
both=0
for s in test/performance/*.sy; do
    o=$(RA_COALESCE_TRACE=1 ./build_wsl/compiler -S -O1 -o /dev/null "$s" 2>&1 |
        grep 'phi-coalesce')
    [ -z "$o" ] && continue
    n=$(printf '%s\n' "$o" | grep -c 'phi-coalesce')
    a=$(printf '%s\n' "$o" | grep -c 'rmw=1 disjoint=0')
    b=$(printf '%s\n' "$o" | grep -c 'rmw=0 disjoint=1')
    c=$(printf '%s\n' "$o" | grep -c 'rmw=1 disjoint=1')
    tot=$((tot + n)); rmw=$((rmw + a)); dis=$((dis + b)); both=$((both + c))
done
echo "coalescePhis accepted : $tot"
echo "  via classic RMW only: $rmw"
echo "  via disjoint only   : $dis"
echo "  both criteria       : $both"
