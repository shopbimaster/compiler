#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$script_dir/.." && pwd)"
build="${BUILD_DIR:-$repo/build}"
work="${PERF_DEBUG_DIR:-/tmp/compiler-perf-debug}"
qemu="${QEMU_RISCV64:-qemu-riscv64}"

mkdir -p "$work"
cmake --build "$build" --target compiler -j"$(nproc)"
if [[ ! -f "$build/libsylib.a" ]]; then
    BUILD_DIR="$build" bash "$repo/scripts/build_sylib.sh"
fi

compiler="$build/compiler"
sylib="$build/libsylib.a"
perf="$repo/test/performance"
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
        timeout 120 "$qemu" "$elf" < "$perf/$name.in" > "$raw" 2>/dev/null
    else
        timeout 120 "$qemu" "$elf" > "$raw" 2>/dev/null
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

    local insn mem calls
    insn=$(grep -cE '^[[:space:]]+[a-z]' "$asm")
    mem=$(grep -cE '^[[:space:]]+(lw|sw|ld|sd|flw|fsw)[[:space:]]' "$asm" || true)
    calls=$(grep -cE '^[[:space:]]+call[[:space:]]' "$asm" || true)
    printf '%-20s %s rc=%s insn=%s mem=%s call=%s\n' \
        "$name" "$verdict" "$rc" "$insn" "$mem" "$calls"
}

if [[ "$#" -eq 0 ]]; then
    set -- crypto-1 crypto-2 crypto-3 many_mat_cal-1 matmul1 01_mm1
fi

for name in "$@"; do
    run_case "$name"
done
