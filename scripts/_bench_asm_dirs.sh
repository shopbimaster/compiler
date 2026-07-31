#!/bin/bash
# Link and run every assembly file that differs between two directories.
# Usage: _bench_asm_dirs.sh <before-dir> <after-dir> [csv] [work-dir]
set -u

before_dir=$1
after_dir=$2
csv=${3:-/tmp/bench_asm_dirs.csv}
work_dir=${4:-/tmp/bench_asm_dirs}
gcc=riscv64-linux-gnu-gcc
qemu=qemu-riscv64-static
sylib=build_b/libsylib.a

mkdir -p "$work_dir"
printf "case,status,before_us,after_us,after_over_before\n" > "$csv"

timed_us() {
    awk '
        /Timer@/ {
            line = $0
            sub(/^.*: /, "", line)
            split(line, part, "-")
            hours = part[1]; sub(/H$/, "", hours)
            minutes = part[2]; sub(/M$/, "", minutes)
            seconds = part[3]; sub(/S$/, "", seconds)
            micros = part[4]; sub(/us$/, "", micros)
            total += hours * 3600000000 + minutes * 60000000 + seconds * 1000000 + micros
        }
        END { print total + 0 }
    ' "$1"
}

run_one() {
    local executable=$1
    local input=$2
    local output=$3
    local timer=$4
    if [ -f "$input" ]; then
        timeout 120 "$qemu" "$executable" < "$input" > "$output" 2> "$timer"
    else
        timeout 120 "$qemu" "$executable" > "$output" 2> "$timer"
    fi
    printf "%s\n" "$?" > "$output.rc"
}

for before_asm in "$before_dir"/*.s; do
    [ -f "$before_asm" ] || continue
    name=$(basename "$before_asm" .s)
    after_asm="$after_dir/$name.s"
    [ -f "$after_asm" ] || continue
    cmp -s "$before_asm" "$after_asm" && continue

    before_elf="$work_dir/$name.before.elf"
    after_elf="$work_dir/$name.after.elf"
    if ! "$gcc" -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
            -o "$before_elf" "$before_asm" "$sylib" -lm 2> "$work_dir/$name.before.link" ||
       ! "$gcc" -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
            -o "$after_elf" "$after_asm" "$sylib" -lm 2> "$work_dir/$name.after.link"; then
        printf "%s,LINK_FAIL,,,\n" "$name" | tee -a "$csv"
        continue
    fi

    input="test/performance/$name.in"
    before_out="$work_dir/$name.before.out"
    after_out="$work_dir/$name.after.out"
    before_timer="$work_dir/$name.before.timer"
    after_timer="$work_dir/$name.after.timer"
    run_one "$before_elf" "$input" "$before_out" "$before_timer"
    run_one "$after_elf" "$input" "$after_out" "$after_timer"

    status=OK
    if ! cmp -s "$before_out" "$after_out" ||
       ! cmp -s "$before_out.rc" "$after_out.rc"; then
        status=DIFF
    fi
    before_us=$(timed_us "$before_timer")
    after_us=$(timed_us "$after_timer")
    ratio=$(awk -v before="$before_us" -v after="$after_us" \
        'BEGIN { if (before > 0) printf "%.4f", after / before }')
    printf "%s,%s,%s,%s,%s\n" \
        "$name" "$status" "$before_us" "$after_us" "$ratio" | tee -a "$csv"
done
