#!/usr/bin/env bash
# Deep determinism probe for the PRE-fix compiler on specific cases.
#
# _determinism.sh at N=3..5 called some cases stable, yet the blast-radius diff
# showed them changing. Either the fix genuinely moved them, or they are
# nondeterministic at a rate low enough that a handful of runs missed it.
# This distinguishes the two by sampling hard (N=30) with the PRE-fix compiler.
#
# Usage: _det_deep.sh <N> <case> [case...]
set -u
cd "$(dirname "$0")/.."
N=$1; shift
CC=./build_wsl/compiler
T=/tmp/_detD; rm -rf $T; mkdir -p $T

for n in "$@"; do
    for i in $(seq "$N"); do
        $CC -S -O1 -o "$T/$n.$i.s" "test/performance/$n.sy" >/dev/null 2>&1
    done
    v=$(md5sum $T/$n.*.s | awk '{print $1}' | sort -u | wc -l)
    printf '%-14s %d distinct outputs over %d runs%s\n' \
        "$n:" "$v" "$N" "$([ "$v" -gt 1 ] && echo '   <-- NONDETERMINISTIC')"
done
