#!/usr/bin/env bash
set -euo pipefail

repo=/mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x
native="$HOME/compiler"
work=/tmp/codex-crypto-debug

mkdir -p "$work"
cp "$repo/src/opt/Mem2Reg.cpp" "$native/src/opt/Mem2Reg.cpp"
cp "$repo/src/opt/PhiLowering.cpp" "$native/src/opt/PhiLowering.cpp"
cp "$repo/src/Compiler.cpp" "$native/src/Compiler.cpp"
cp "$repo/src/backend/TargetCodeGen.cpp" "$native/src/backend/TargetCodeGen.cpp"
cp "$repo/src/backend/RegisterAllocator.cpp" "$native/src/backend/RegisterAllocator.cpp"
cmake --build "$native/build" --target compiler -j"$(nproc)"

compiler="$native/build/compiler"
sylib="$native/build/libsylib.a"
perf="$native/test/performance"
opt_level="${CODEX_OPT_LEVEL:--O1}"

md5sum "$compiler"

run_case() {
    local name="$1"
    local asm="$work/$name.s"
    local elf="$work/$name.elf"
    local raw="$work/$name.raw"
    local normalized="$work/$name.norm"
    local rc

    "$compiler" -S -o "$asm" "$perf/$name.sy" "$opt_level" >/dev/null
    riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
        "$asm" "$sylib" -o "$elf"

    set +e
    if [[ -f "$perf/$name.in" ]]; then
        timeout 60 qemu-riscv64-static "$elf" < "$perf/$name.in" > "$raw" 2>/dev/null
    else
        timeout 60 qemu-riscv64-static "$elf" > "$raw" 2>/dev/null
    fi
    rc=$?
    set -e

    cp "$raw" "$normalized"
    if [[ ! -s "$raw" || "$(tail -c1 "$raw" | od -An -tu1 | tr -d ' ')" == 10 ]]; then
        printf '%s\n' "$rc" >> "$normalized"
    else
        printf '\n%s\n' "$rc" >> "$normalized"
    fi

    local verdict=WRONG
    if diff -q \
        <(sed 's/[[:space:]]*$//' "$normalized") \
        <(sed 's/[[:space:]]*$//' "$perf/$name.out") >/dev/null; then
        verdict=PASS
    fi

    local insn
    insn=$(grep -cE '^[[:space:]]+[a-z]' "$asm")
    printf '%-18s %s rc=%s insn=%s\n' "$name" "$verdict" "$rc" "$insn"
}

if [[ "$#" -eq 0 ]]; then
    set -- crypto-1 crypto-2 crypto-3 many_mat_cal-1 matmul1 01_mm1
fi

for name in "$@"; do
    run_case "$name"
done
