#!/bin/bash
# Run performance tests with progress tracking
cd /mnt/d/VSCodeProjects/compiler
RESULTS_FILE=/tmp/perf_results.txt
> $RESULTS_FILE

for src in test/performance/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm="/tmp/perf_test/${name}.S"
    bin="/tmp/perf_test/${name}_bin"
    infile="test/performance/${name}.in"
    outfile="test/performance/${name}.out"
    mkdir -p /tmp/perf_test

    # Compile with 30s timeout
    if ! timeout 30 ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        echo "COMPILE_HANG_OR_FAIL: $name" >> $RESULTS_FILE
        echo "Done: $name (compile hang/fail)" >&2
        continue
    fi
    if ! riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" build/libsylib.a 2>/dev/null; then
        echo "LINK_FAIL: $name" >> $RESULTS_FILE
        echo "Done: $name (link fail)" >&2
        continue
    fi

    set +e
    if [ -f "$infile" ]; then
        timeout 15 qemu-riscv64 "$bin" < "$infile" > /tmp/perf_test/${name}_out.txt 2>/dev/null
    else
        timeout 15 qemu-riscv64 "$bin" > /tmp/perf_test/${name}_out.txt 2>/dev/null
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
        head -n -1 "$outfile" > /tmp/perf_test/${name}_expect.txt 2>/dev/null
        if [ ! -s /tmp/perf_test/${name}_expect.txt ]; then
            > /tmp/perf_test/${name}_expect.txt
        fi
        cp /tmp/perf_test/${name}_out.txt /tmp/perf_test/${name}_act.txt
        content=$(cat /tmp/perf_test/${name}_act.txt 2>/dev/null); printf '%s\n' "$content" > /tmp/perf_test/${name}_act.txt
        content=$(cat /tmp/perf_test/${name}_expect.txt 2>/dev/null); printf '%s\n' "$content" > /tmp/perf_test/${name}_expect.txt

        if diff -q /tmp/perf_test/${name}_act.txt /tmp/perf_test/${name}_expect.txt > /dev/null 2>&1; then
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

cat $RESULTS_FILE
