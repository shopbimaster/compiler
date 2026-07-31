#!/usr/bin/env bash
# conv2d-2 is the one case in the blast radius not yet explained.
#
# It looked stable at N=30 with the pre-fix compiler, yet its asm changed after
# the fix. Two possibilities:
#   (a) it is nondeterministic at a low rate, and the post-fix output is simply
#       one of the variants it already produced -> the fix did not perturb it;
#   (b) it is genuinely deterministic pre-fix and the fix moved it -> the fix
#       has reach beyond previously-tied intervals, which I do not want.
#
# Sample the pre-fix compiler hard, then check whether the post-fix hash appears
# among the pre-fix variants.
set -u
cd "$(dirname "$0")/.."
N=${1:-120}
RA=src/backend/RegisterAllocator.cpp
POST_ASM=/tmp/_ref_post/conv2d-2.s
[ -f "$POST_ASM" ] || { echo "run _det_blast_radius.sh first"; exit 1; }
post=$(md5sum "$POST_ASM" | awk '{print $1}')

cp "$RA" /tmp/_ra_fixed.cpp
git stash push -q "$RA" || exit 1
cmake --build build_wsl -j8 >/dev/null 2>&1

T=/tmp/_detC; rm -rf $T; mkdir -p $T
for i in $(seq "$N"); do
    ./build_wsl/compiler -S -O1 -o "$T/$i.s" test/performance/conv2d-2.sy >/dev/null 2>&1
done
echo "pre-fix variants over $N runs:"
md5sum $T/*.s | awk '{print $1}' | sort | uniq -c | sort -rn
echo "post-fix hash: $post"
if md5sum $T/*.s | awk '{print $1}' | grep -q "$post"; then
    echo "=> post-fix output IS one of the pre-fix variants (case (a): not perturbed)"
else
    echo "=> post-fix output is NEW (case (b): the fix changed this case)"
fi

git stash pop -q
cp /tmp/_ra_fixed.cpp "$RA"
cmake --build build_wsl -j8 >/dev/null 2>&1
echo "fix restored and rebuilt"
