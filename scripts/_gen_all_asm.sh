#!/bin/bash
# Compile every performance case to asm at -O1 into a directory.
# usage: _gen_all_asm.sh [outdir] [extra-env-assignments...]
set -u
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1
out=${1:-/tmp/_mm_all}
shift || true
mkdir -p "$out"
rm -f "$out"/*.s
ok=0; fail=0
for s in test/performance/*.sy; do
    b=$(basename "$s" .sy)
    if env "$@" ./build_wsl/compiler -S -O1 -o "$out/$b.s" "$s" >/dev/null 2>&1; then
        ok=$((ok + 1))
    else
        fail=$((fail + 1)); echo "  COMPILE_FAIL $b"
    fi
done
echo "compiled $ok ok, $fail failed -> $out"
