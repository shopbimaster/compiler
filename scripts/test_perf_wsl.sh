#!/bin/bash
# WSL 性能测试脚本(过滤 Timer 输出)
# Usage: ./scripts/test_perf_wsl.sh [case_name]
# 不带参数测试全部,带参数测试单个用例

set -e

C=./build/compiler
GCC=riscv64-linux-gnu-gcc
QEMU=$(which qemu-riscv64-static 2>/dev/null || which qemu-riscv64)
SYLIB=./build/libsylib.a
PERF=test/performance
WORK=/tmp/perf_test_$$

mkdir -p "$WORK"

# 过滤 Timer 输出行
filter_timer() {
    grep -v "^Timer@" | grep -v "^TOTAL:"
}

# 规范化输出(去除行尾空格,确保末尾换行)
normalize() {
    local file="$1"
    if [ ! -s "$file" ]; then
        echo "0"
        return
    fi
    local last=$(tail -c1 "$file" 2>/dev/null | od -An -tu1 | tr -d ' ')
    if [ "$last" != "10" ]; then
        { cat "$file"; echo; }
    else
        cat "$file"
    fi
}

test_one() {
    local name="$1"
    local sy="$PERF/$name.sy"

    if [ ! -f "$sy" ]; then
        echo "$name: SKIP (not found)"
        return 1
    fi

    # 编译
    if ! "$C" -S -o "$WORK/$name.s" "$sy" -O1 >/dev/null 2>&1; then
        echo "$name: CFAIL"
        return 1
    fi

    # 链接
    if ! "$GCC" -static -o "$WORK/$name.elf" "$WORK/$name.s" "$SYLIB" -lm >/dev/null 2>&1; then
        echo "$name: LFAIL"
        return 1
    fi

    # 运行
    local in="$PERF/$name.in"
    local rc=0
    if [ -f "$in" ]; then
        timeout 30 "$QEMU" "$WORK/$name.elf" <"$in" >"$WORK/$name.raw" 2>/dev/null
        rc=$?
    else
        timeout 30 "$QEMU" "$WORK/$name.elf" >"$WORK/$name.raw" 2>/dev/null
        rc=$?
    fi

    if [ $rc -eq 124 ]; then
        echo "$name: TIMEOUT"
        return 1
    fi

    # 过滤 Timer
    filter_timer < "$WORK/$name.raw" > "$WORK/$name.filt"

    # 追加退出码
    echo "$rc" >> "$WORK/$name.filt"

    # 规范化
    normalize "$WORK/$name.filt" > "$WORK/$name.norm"

    # 比对
    local out="$PERF/$name.out"
    if [ ! -f "$out" ]; then
        echo "$name: OK (no expected output)"
        return 0
    fi

    normalize "$out" > "$WORK/$name.exp"

    if diff -q <(sed 's/[[:space:]]*$//' "$WORK/$name.norm") \
               <(sed 's/[[:space:]]*$//' "$WORK/$name.exp") >/dev/null 2>&1; then
        echo "$name: OK"
        return 0
    else
        echo "$name: WRONG"
        return 1
    fi
}

# 主逻辑
if [ $# -gt 0 ]; then
    # 单个用例
    test_one "$1"
    exit $?
else
    # 全部用例
    PASS=0
    FAIL=0
    FAILED=""

    for sy in "$PERF"/*.sy; do
        name=$(basename "$sy" .sy)
        if test_one "$name"; then
            PASS=$((PASS+1))
        else
            FAIL=$((FAIL+1))
            FAILED="$FAILED $name"
        fi
    done

    echo ""
    echo "========================================="
    echo "PASS: $PASS/60"
    echo "FAIL: $FAIL"
    [ -n "$FAILED" ] && echo "Failed:$FAILED"
    echo "========================================="

    [ $PASS -eq 60 ] && exit 0 || exit 1
fi
