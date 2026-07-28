#!/usr/bin/env bash
# Blast radius of the determinism fix: which cases' asm actually changed?
#
# Builds the pre-fix compiler (git stash of RegisterAllocator.cpp), records its
# asm for every performance case, restores the fix, rebuilds, and diffs.
#
# Expectation for a minimally-invasive fix: only the cases that were previously
# nondeterministic (crc1/crc2/crc3/huffman-01) may differ; the other 56 must be
# byte-identical, proving the new tiebreaker only orders previously-tied pairs.
set -u
cd "$(dirname "$0")/.."
CC=./build_wsl/compiler
PRE=/tmp/_ref_pre
POST=/tmp/_ref_post
SAVE=/tmp/_ra_fixed.cpp
RA=src/backend/RegisterAllocator.cpp

gen() {   # $1 = outdir
    mkdir -p "$1"; rm -f "$1"/*.s
    for f in test/performance/*.sy; do
        n=$(basename "$f" .sy)
        $CC -S -O1 -o "$1/$n.s" "$f" >/dev/null 2>&1
    done
}

echo "== building PRE-fix compiler =="
cp "$RA" "$SAVE"
git stash push -q "$RA" || { echo "stash failed"; exit 1; }
cmake --build build_wsl -j8 >/dev/null 2>&1
gen "$PRE"

echo "== restoring fix and rebuilding =="
git stash pop -q
cp "$SAVE" "$RA"
cmake --build build_wsl -j8 >/dev/null 2>&1
gen "$POST"

echo "== diff =="
same=0; diff_list=()
for f in "$PRE"/*.s; do
    n=$(basename "$f" .s)
    if cmp -s "$f" "$POST/$n.s"; then
        same=$((same+1))
    else
        diff_list+=("$n")
    fi
done
echo "identical=$same changed=${#diff_list[@]}"
for n in "${diff_list[@]:-}"; do [ -n "$n" ] && echo "  changed: $n"; done
