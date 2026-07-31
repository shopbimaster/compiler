#!/bin/bash
# Differential check for the memoization purity precondition.
# Compiles test/_memo_purity_bug.sy with recursiveMemoization ON and OFF and
# compares the program output. They must match; a mismatch means the pass
# memoized a function whose result is not a function of its arguments alone.
set -u
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1

SRC="${1:-test/_memo_purity_bug.sy}"
CC=./build_wsl/compiler
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static
LIB=./build_b/libsylib.a

run_variant() {
    local tag="$1" disable="$2"
    local asm="/tmp/memo_${tag}.s" exe="/tmp/memo_${tag}.elf"
    if [ -n "$disable" ]; then
        OPT_DISABLE="$disable" $CC -S -O1 -o "$asm" "$SRC" 2>/tmp/memo_${tag}.err
    else
        $CC -S -O1 -o "$asm" "$SRC" 2>/tmp/memo_${tag}.err
    fi
    if [ $? -ne 0 ]; then
        echo "[$tag] COMPILE FAILED"; tail -3 /tmp/memo_${tag}.err; return 1
    fi
    local nsym
    nsym=$(grep -c '__opt_memo' "$asm")
    echo "[$tag] __opt_memo symbol references: $nsym"
    $GCC -march=rv64gc -mabi=lp64d -mcmodel=medany -static \
        -o "$exe" "$asm" "$LIB" -lm 2>/tmp/memo_${tag}.link
    if [ $? -ne 0 ]; then
        echo "[$tag] LINK FAILED"; tail -5 /tmp/memo_${tag}.link; return 1
    fi
    $QEMU "$exe" > "/tmp/memo_${tag}.out" 2>/dev/null
    echo "[$tag] exit=$?"
    echo "[$tag] stdout:"
    sed 's/^/    /' "/tmp/memo_${tag}.out"
}

echo "### source: $SRC"
run_variant on ""
echo ""
run_variant off "recursiveMemoization"
echo ""
if diff -q /tmp/memo_on.out /tmp/memo_off.out >/dev/null 2>&1; then
    echo "RESULT: MATCH (memoization preserved semantics)"
else
    echo "RESULT: *** MISMATCH -- memoization changed the answer ***"
    diff /tmp/memo_off.out /tmp/memo_on.out | sed 's/^/    /'
fi
