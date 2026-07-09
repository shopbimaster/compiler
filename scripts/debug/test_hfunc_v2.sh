#!/bin/bash
# Run h_functional tests one by one, writing results to a file
cd /mnt/d/VSCodeProjects/compiler
RESULTS_FILE=/tmp/hfunc_results.txt
> $RESULTS_FILE

for src in test/h_functional/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm="/tmp/hfunc_fast/${name}.S"
    bin="/tmp/hfunc_fast/${name}_bin"
    infile="test/h_functional/${name}.in"
    outfile="test/h_functional/${name}.out"
    mkdir -p /tmp/hfunc_fast

    if ! ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        echo "COMPILE_FAIL: $name" >> $RESULTS_FILE
        continue
    fi
    if ! riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" build/libsylib.a 2>/dev/null; then
        echo "LINK_FAIL: $name" >> $RESULTS_FILE
        continue
    fi

    set +e
    if [ -f "$infile" ]; then
        timeout 8 qemu-riscv64 "$bin" < "$infile" > /tmp/hfunc_fast/${name}_out.txt 2>/dev/null
    else
        timeout 8 qemu-riscv64 "$bin" > /tmp/hfunc_fast/${name}_out.txt 2>/dev/null
    fi
    ret=$?
    set -e

    if [ $ret -eq 124 ]; then
        echo "TIMEOUT: $name" >> $RESULTS_FILE
    elif [ $ret -eq 139 ]; then
        echo "SEGFAULT: $name" >> $RESULTS_FILE
    elif [ ! -f "$outfile" ]; then
        echo "OK: $name (no expected output)" >> $RESULTS_FILE
    else
        head -n -1 "$outfile" > /tmp/hfunc_fast/${name}_expect.txt 2>/dev/null
        if [ ! -s /tmp/hfunc_fast/${name}_expect.txt ]; then
            > /tmp/hfunc_fast/${name}_expect.txt
        fi
        cp /tmp/hfunc_fast/${name}_out.txt /tmp/hfunc_fast/${name}_act.txt
        content=$(cat /tmp/hfunc_fast/${name}_act.txt 2>/dev/null); printf '%s\n' "$content" > /tmp/hfunc_fast/${name}_act.txt
        content=$(cat /tmp/hfunc_fast/${name}_expect.txt 2>/dev/null); printf '%s\n' "$content" > /tmp/hfunc_fast/${name}_expect.txt

        if diff -q /tmp/hfunc_fast/${name}_act.txt /tmp/hfunc_fast/${name}_expect.txt > /dev/null 2>&1; then
            echo "OK: $name" >> $RESULTS_FILE
        else
            echo "DIFF: $name" >> $RESULTS_FILE
        fi
    fi
    # Print progress to stderr
    echo "Done: $name (ret=$ret)" >&2
done

echo "=== Results ===" >> $RESULTS_FILE
grep -c "^OK:" $RESULTS_FILE >> $RESULTS_FILE
echo "OK count above" >> $RESULTS_FILE

cat $RESULTS_FILE
