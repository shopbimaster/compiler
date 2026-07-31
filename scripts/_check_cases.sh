#!/usr/bin/env bash
# Correctness check for named performance cases at the current HEAD.
#
# Written as a file rather than an inline command on purpose: the nested
# quoting needed to run this through `wsl -e bash -lc "..."` from PowerShell
# mangles $ and ! and broke twice.
#
# Comparison follows the SysY convention already recorded in MEMORY.md: the
# final line of the expected .out is the exit code, and a newline must be
# inserted first when stdout does not end in one. Getting this wrong silently
# reports passing cases as failures.
#
# Usage: _check_cases.sh <case>...
set -u
cd "$(dirname "$0")/.."
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static
SYLIB=./build_b/libsylib.a
DIR=test/performance
T=/tmp/_chk; mkdir -p $T
fail=0

for n in "$@"; do
    src=$DIR/$n.sy
    if [ ! -f "$src" ]; then echo "$n: no such case"; fail=1; continue; fi

    if ! ./build_wsl/compiler -S -O1 -o "$T/$n.s" "$src" >/dev/null 2>&1; then
        echo "$n: COMPILE FAIL"; fail=1; continue
    fi
    if ! $GCC -static -o "$T/$n.elf" "$T/$n.s" "$SYLIB" >/dev/null 2>&1; then
        echo "$n: LINK FAIL"; fail=1; continue
    fi

    if [ -f "$DIR/$n.in" ]; then
        $QEMU "$T/$n.elf" < "$DIR/$n.in" > "$T/$n.raw" 2>/dev/null
    else
        $QEMU "$T/$n.elf" > "$T/$n.raw" 2>/dev/null
    fi
    rc=$?

    last=$(tail -c1 "$T/$n.raw" 2>/dev/null | od -An -tu1 | tr -d ' ')
    if [ -s "$T/$n.raw" ] && [ "$last" != "10" ]; then
        { cat "$T/$n.raw"; echo; echo "$rc"; } > "$T/$n.norm"
    else
        { cat "$T/$n.raw"; echo "$rc"; } > "$T/$n.norm"
    fi

    if [ ! -f "$DIR/$n.out" ]; then
        echo "$n: ran, exit=$rc (no .out to compare)"
        continue
    fi
    if diff -q <(sed 's/[[:space:]]*$//' "$T/$n.norm") \
               <(sed 's/[[:space:]]*$//' "$DIR/$n.out") >/dev/null; then
        echo "$n: OK"
    else
        echo "$n: MISMATCH (exit=$rc)"
        echo "   expected tail: $(tail -c 60 "$DIR/$n.out" | tr '\n' '|')"
        echo "   actual   tail: $(tail -c 60 "$T/$n.norm" | tr '\n' '|')"
        fail=1
    fi
done
exit $fail
