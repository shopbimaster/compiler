#!/bin/bash
export TMPDIR=~/tmp3
mkdir -p ~/tmp/perf

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PERF_DIR="${SCRIPT_DIR}/test/performance"
SYLIB_A="${BUILD_DIR}/libsylib.a"

COMPILE_OK=0; COMPILE_FAIL=0; LINK_OK=0; LINK_FAIL=0
RUN_OK=0; RUN_OUTPUT_DIFF=0; RUN_SEGFAULT=0; RUN_TIMEOUT=0

norm() {
    local content
    content=$(cat "$1" 2>/dev/null)
    printf '%s\n' "$content" > "$1"
}

echo "============================================================"
echo "SysY Compiler - Performance Test Suite (O0 correctness)"
echo "============================================================"
echo ""

for src in "${PERF_DIR}"/*.sy; do
    name=$(basename "$src" .sy)
    asm=~/tmp/perf/${name}.S
    bin=~/tmp/perf/${name}_bin
    infile="${PERF_DIR}/${name}.in"
    outfile="${PERF_DIR}/${name}.out"
    
    if ! ${BUILD_DIR}/sysyc -S "$src" -o "$asm" -O0 2>/dev/null; then
        echo "  COMPILE FAIL: ${name}"
        COMPILE_FAIL=$((COMPILE_FAIL + 1))
        continue
    fi
    COMPILE_OK=$((COMPILE_OK + 1))
    
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB_A" 2>/dev/null; then
        echo "  LINK FAIL:    ${name}"
        LINK_FAIL=$((LINK_FAIL + 1))
        continue
    fi
    LINK_OK=$((LINK_OK + 1))
    
    if [ -f "$infile" ]; then
        timeout 15 $QEMU "$bin" < "$infile" > ~/tmp/perf/${name}_out.txt 2>/dev/null
    else
        timeout 15 $QEMU "$bin" > ~/tmp/perf/${name}_out.txt 2>/dev/null
    fi
    ret=$?
    
    if [ $ret -eq 124 ]; then
        echo "  TIMEOUT:      ${name}"
        RUN_TIMEOUT=$((RUN_TIMEOUT + 1))
    elif [ $ret -eq 139 ]; then
        echo "  SEGFAULT:     ${name}"
        RUN_SEGFAULT=$((RUN_SEGFAULT + 1))
    elif [ ! -f "$outfile" ]; then
        RUN_OK=$((RUN_OK + 1))
    else
        head -n -1 "$outfile" > ~/tmp/perf/${name}_expect.txt 2>/dev/null
        if [ ! -s ~/tmp/perf/${name}_expect.txt ]; then
            > ~/tmp/perf/${name}_expect.txt
        fi
        
        cp ~/tmp/perf/${name}_out.txt ~/tmp/perf/${name}_act.txt
        norm ~/tmp/perf/${name}_act.txt
        norm ~/tmp/perf/${name}_expect.txt
        
        if diff -q ~/tmp/perf/${name}_act.txt ~/tmp/perf/${name}_expect.txt > /dev/null 2>&1; then
            RUN_OK=$((RUN_OK + 1))
        else
            echo "  OUTPUT DIFF:  ${name}"
            RUN_OUTPUT_DIFF=$((RUN_OUTPUT_DIFF + 1))
            diff ~/tmp/perf/${name}_act.txt ~/tmp/perf/${name}_expect.txt | head -8
            echo "  ---"
        fi
    fi
done

echo ""
echo "============================================================"
echo "Results:"
echo "  Compile:  ${COMPILE_OK} OK, ${COMPILE_FAIL} FAIL"
echo "  Link:     ${LINK_OK} OK, ${LINK_FAIL} FAIL"
echo "  Runtime:  ${RUN_OK} OK, ${RUN_OUTPUT_DIFF} OUTPUT_DIFF, ${RUN_SEGFAULT} SEGFAULT, ${RUN_TIMEOUT} TIMEOUT"
echo "============================================================"