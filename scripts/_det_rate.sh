#!/usr/bin/env bash
# How often does the compiler produce a different output for the same input?
# Prints the distribution of output hashes over N runs, for IR and asm.
#
# Usage: _det_rate.sh [case] [N]
set -u
cd "$(dirname "$0")/.."
CASE=${1:-crc1}
N=${2:-20}
CC=./build_wsl/compiler
SRC=test/performance/$CASE.sy
T=/tmp/_detr; rm -rf $T; mkdir -p $T

echo "case=$CASE N=$N"

for mode in ir asm; do
    if [ "$mode" = ir ]; then flag="--emit-ir"; ext=ll; else flag="-S"; ext=s; fi
    for i in $(seq "$N"); do
        $CC $flag -O1 -o "$T/$mode.$i.$ext" "$SRC" >/dev/null 2>&1
    done
    echo "--- $mode: distinct outputs (count hash) ---"
    md5sum $T/$mode.*.$ext | awk '{print $1}' | sort | uniq -c | sort -rn
done
