#!/usr/bin/env bash
# Is the compiler deterministic? Compile each case N times and compare.
#
# This matters a great deal for A/B work: if the same source can produce
# different asm across runs, then any "this change altered 32 cases" signal is
# untrustworthy, and so is any single-run timing comparison.
#
# Usage: _determinism.sh [N] [glob]
set -u
cd "$(dirname "$0")/.."
N=${1:-3}
GLOB=${2:-*}
CC=./build_wsl/compiler
T=/tmp/_det; mkdir -p $T; rm -f $T/*.s

stable=0; unstable=0; failed=0
for src in test/performance/${GLOB}.sy; do
    [ -f "$src" ] || continue
    n=$(basename "$src" .sy)
    ok=1
    for i in $(seq "$N"); do
        if ! $CC -S -O1 -o "$T/$n.$i.s" "$src" >/dev/null 2>&1; then
            ok=0; break
        fi
    done
    if [ $ok -eq 0 ]; then failed=$((failed+1)); echo "  COMPILE_FAIL: $n"; continue; fi
    same=1
    for i in $(seq 2 "$N"); do
        cmp -s "$T/$n.1.s" "$T/$n.$i.s" || { same=0; break; }
    done
    if [ $same -eq 1 ]; then
        stable=$((stable+1))
    else
        unstable=$((unstable+1))
        d=$(diff "$T/$n.1.s" "$T/$n.2.s" | grep -c '^[<>]')
        echo "  NONDETERMINISTIC: $n ($d differing lines between run1/run2)"
    fi
done
echo "stable=$stable nondeterministic=$unstable compile_fail=$failed (N=$N)"
