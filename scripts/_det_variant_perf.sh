#!/usr/bin/env bash
# Did the determinism fix pin the FAST or the SLOW register allocation?
#
# The fix makes each previously-drifting case settle on one specific variant.
# That is only neutral if the pinned variant is not slower than the alternatives
# it used to produce. Measure it:
#   1. sample the pre-fix compiler to collect the distinct asm variants,
#   2. time each variant (median of R runs, since single runs are noisy),
#   3. time the post-fix (pinned) asm the same way,
#   4. report where the pinned variant sits in the spread.
#
# Usage: _det_variant_perf.sh <N-compiles> <R-runs> <case...>
set -u
cd "$(dirname "$0")/.."
N=$1; R=$2; shift 2
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static
SYLIB=./build_b/libsylib.a
RA=src/backend/RegisterAllocator.cpp
T=/tmp/_detVP; rm -rf $T; mkdir -p $T

median() { printf '%s\n' "$@" | sort -n | awk '{a[NR]=$1} END{print a[int((NR+1)/2)]}'; }

time_asm() {   # $1=asm $2=case -> echoes median us
    local asm=$1 n=$2 exe=$T/t.elf in=test/performance/$2.in
    $GCC -static -o "$exe" "$asm" "$SYLIB" >/dev/null 2>&1 || { echo 0; return; }
    local ts=()
    for _ in $(seq "$R"); do
        local us
        if [ -f "$in" ]; then
            us=$($QEMU "$exe" < "$in" 2>&1 >/dev/null | grep -oE '[0-9]+us' | tr -d 'us')
        else
            us=$($QEMU "$exe" 2>&1 >/dev/null | grep -oE '[0-9]+us' | tr -d 'us')
        fi
        ts+=("${us:-0}")
    done
    median "${ts[@]}"
}

# post-fix (pinned) asm, from the current tree
mkdir -p $T/post
for n in "$@"; do
    ./build_wsl/compiler -S -O1 -o "$T/post/$n.s" "test/performance/$n.sy" >/dev/null 2>&1
done

# Pre-fix variants.
#
# Do NOT use `git stash push <file>` + `git stash pop` here. Once the fix is
# committed the file is clean, so the push is a no-op and the pop then applies
# an unrelated older stash entry -- that is exactly what happened here, dirtying
# CMakeLists.txt with a merge conflict. Revert to the parent commit's version of
# the file explicitly instead, and restore from git afterwards.
cp "$RA" /tmp/_ra_fixed.cpp
git show HEAD~1:"$RA" > "$RA" 2>/dev/null || { echo "cannot read pre-fix $RA"; exit 1; }
cmake --build build_wsl -j8 >/dev/null 2>&1
for n in "$@"; do
    mkdir -p "$T/$n"
    for i in $(seq "$N"); do
        ./build_wsl/compiler -S -O1 -o "$T/$n/c$i.s" "test/performance/$n.sy" >/dev/null 2>&1
    done
done
cp /tmp/_ra_fixed.cpp "$RA"
cmake --build build_wsl -j8 >/dev/null 2>&1


for n in "$@"; do
    echo "=== $n ==="
    pinned=$(md5sum "$T/post/$n.s" | awk '{print $1}')
    # unique pre-fix variants with counts
    declare -A rep=(); declare -A cnt=()
    for f in $T/$n/*.s; do
        h=$(md5sum "$f" | awk '{print $1}')
        cnt[$h]=$(( ${cnt[$h]:-0} + 1 )); rep[$h]=${rep[$h]:-$f}
    done
    for h in "${!rep[@]}"; do
        us=$(time_asm "${rep[$h]}" "$n")
        mark=""; [ "$h" = "$pinned" ] && mark="  <== PINNED by fix"
        printf '  variant %s  seen %2d/%d  median %s us%s\n' \
            "${h:0:8}" "${cnt[$h]}" "$N" "$us" "$mark"
    done
    if [ -z "${rep[$pinned]:-}" ]; then
        us=$(time_asm "$T/post/$n.s" "$n")
        printf '  variant %s  (not sampled pre-fix)  median %s us  <== PINNED by fix\n' \
            "${pinned:0:8}" "$us"
    fi
    unset rep cnt
done
