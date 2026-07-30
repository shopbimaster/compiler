#!/bin/bash
# Alternate two already-linked RISC-V executables and report scored time.
# Usage: _bench_pair.sh <left-elf> <right-elf> <input> [runs] [csv]
set -eu

left=$1
right=$2
input=$3
runs=${4:-7}
csv=${5:-/tmp/bench_pair.csv}

printf "run,variant,timed_us\n" > "$csv"

measure() {
    local executable=$1
    local timer_file
    timer_file=$(mktemp)
    qemu-riscv64-static "$executable" < "$input" > /dev/null 2> "$timer_file"
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
    ' "$timer_file"
    rm -f "$timer_file"
}

for ((run = 1; run <= runs; ++run)); do
    left_us=$(measure "$left")
    printf "%d,left,%s\n" "$run" "$left_us" | tee -a "$csv"
    right_us=$(measure "$right")
    printf "%d,right,%s\n" "$run" "$right_us" | tee -a "$csv"
done

for variant in left right; do
    median=$(awk -F, -v variant="$variant" '$2 == variant { print $3 }' "$csv" |
        sort -n | awk '{ value[NR] = $1 } END {
            if (NR % 2) print value[(NR + 1) / 2]
            else print int((value[NR / 2] + value[NR / 2 + 1]) / 2)
        }')
    printf "median,%s,%s\n" "$variant" "$median"
done
