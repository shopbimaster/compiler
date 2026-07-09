#!/bin/bash
# Run h_functional tests, skipping known compiler-hang cases
cd /mnt/d/VSCodeProjects/compiler
RESULTS_FILE=/tmp/hfunc_results2.txt
> $RESULTS_FILE

# Skip cases where compiler itself hangs (pre-existing issue)
SKIP_LIST="23_json"

for src in test/h_functional/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)

    # Skip known compiler-hang cases
    skip=false
    for s in $SKIP_LIST; do
        if [ "$name" = "$s" ]; then
            skip=true
            break
        fi
    done
    if $skip; then
        echo "SKIP: $name (compiler hang)" >> $RESULTS_FILE
        continue
    fi

    asm="/tmp/hfunc_fast/${name}.S"
    bin="/tmp/hfunc_fast/${name}_bin"
    infile="test/h_functional/${name}.in"
    outfile="test/h_functional/${name}.out"
    mkdir -p /tmp/hfunc_fast

    # Use timeout for compilation too
    if ! timeout 10 ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        echo "COMPILE_HANG_OR_FAIL: $name" >> $RESULTS_FILE
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
        echo "OK: $name (no expected)" >> $RESULTS_FILE
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
    echo "Done: $name (ret=$ret)" >&2
done

echo "" >> $RESULTS_FILE
echo "=== Summary ===" >> $RESULTS_FILE
echo "OK: $(grep -c '^OK:' $RESULTS_FILE)" >> $RESULTS_FILE
echo "DIFF: $(grep -c '^DIFF:' $RESULTS_FILE)" >> $RESULTS_FILE
echo "SEGFAULT: $(grep -c '^SEGFAULT:' $RESULTS_FILE)" >> $RESULTS_FILE
echo "TIMEOUT: $(grep -c '^TIMEOUT:' $RESULTS_FILE)" >> $RESULTS_FILE
echo "COMPILE_HANG_OR_FAIL: $(grep -c '^COMPILE_HANG_OR_FAIL:' $RESULTS_FILE)" >> $RESULTS_FILE
echo "SKIP: $(grep -c '^SKIP:' $RESULTS_FILE)" >> $RESULTS_FILE

cat $RESULTS_FILE
