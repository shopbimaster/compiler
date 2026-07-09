#!/bin/bash
# Run h_functional tests with shorter timeout and show progress
cd /mnt/d/VSCodeProjects/compiler
BUILD_DIR=build
SYLIB_A=build/libsylib.a
TMPDIR=/tmp/hfunc_test
mkdir -p $TMPDIR

pass=0
fail=0
timeout_count=0
segfault=0
diff_count=0

for src in test/h_functional/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm="${TMPDIR}/${name}.S"
    bin="${TMPDIR}/${name}_bin"
    infile="test/h_functional/${name}.in"
    outfile="test/h_functional/${name}.out"

    if ! ${BUILD_DIR}/compiler -S "$src" -o "$asm" -O1 2>/dev/null; then
        echo "COMPILE FAIL: $name"
        fail=$((fail + 1))
        continue
    fi
    if ! riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "LINK FAIL: $name"
        fail=$((fail + 1))
        continue
    fi

    set +e
    if [ -f "$infile" ]; then
        timeout 8 qemu-riscv64 "$bin" < "$infile" > "${TMPDIR}/${name}_out.txt" 2>/dev/null
    else
        timeout 8 qemu-riscv64 "$bin" > "${TMPDIR}/${name}_out.txt" 2>/dev/null
    fi
    ret=$?
    set -e

    if [ $ret -eq 124 ]; then
        echo "TIMEOUT: $name"
        timeout_count=$((timeout_count + 1))
    elif [ $ret -eq 139 ]; then
        echo "SEGFAULT: $name"
        segfault=$((segfault + 1))
    elif [ ! -f "$outfile" ]; then
        pass=$((pass + 1))
    else
        head -n -1 "$outfile" > "${TMPDIR}/${name}_expect.txt" 2>/dev/null
        if [ ! -s "${TMPDIR}/${name}_expect.txt" ]; then
            > "${TMPDIR}/${name}_expect.txt"
        fi
        cp "${TMPDIR}/${name}_out.txt" "${TMPDIR}/${name}_act.txt"
        # normalize
        content=$(cat "${TMPDIR}/${name}_act.txt" 2>/dev/null); printf '%s\n' "$content" > "${TMPDIR}/${name}_act.txt"
        content=$(cat "${TMPDIR}/${name}_expect.txt" 2>/dev/null); printf '%s\n' "$content" > "${TMPDIR}/${name}_expect.txt"

        if diff -q "${TMPDIR}/${name}_act.txt" "${TMPDIR}/${name}_expect.txt" > /dev/null 2>&1; then
            pass=$((pass + 1))
        else
            echo "DIFF: $name"
            diff "${TMPDIR}/${name}_act.txt" "${TMPDIR}/${name}_expect.txt" | head -5
            diff_count=$((diff_count + 1))
        fi
    fi
done

echo ""
echo "=== Summary ==="
echo "Pass: $pass"
echo "Diff: $diff_count"
echo "Segfault: $segfault"
echo "Timeout: $timeout_count"
echo "Fail: $fail"
