#!/usr/bin/env bash
# Is the nondeterminism driven by pointer values (i.e. ASLR)?
#
# If the output is unstable normally but stable under `setarch -R` (ASLR off),
# then some container keyed on IR::Value* is being iterated in an order that
# depends on the heap addresses, and that order is reaching codegen.
#
# Usage: _det_aslr.sh [case] [N]
set -u
cd "$(dirname "$0")/.."
CASE=${1:-crc1}
N=${2:-10}
CC=./build_wsl/compiler
SRC=test/performance/$CASE.sy
T=/tmp/_detA; rm -rf $T; mkdir -p $T

count_variants() {   # $1 = tag, rest = prefix command
    local tag=$1; shift
    for i in $(seq "$N"); do
        "$@" $CC -S -O1 -o "$T/$tag.$i.s" "$SRC" >/dev/null 2>&1
    done
    local v
    v=$(md5sum $T/$tag.*.s | awk '{print $1}' | sort -u | wc -l)
    echo "$tag: $v distinct outputs over $N runs"
}

count_variants aslr_on
count_variants aslr_off setarch "$(uname -m)" -R
