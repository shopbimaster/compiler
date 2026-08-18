#!/bin/bash
# SysY2026 张量测试：编译+链接+运行 test/functional/tensor/*.sy，与 .out 比对
# 用法: ./scripts/run_tensor_tests.sh [O0|O1]   (默认 O0)
set -u
OPT="${1:-O0}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
pass=0; fail=0
for src in "${PROJECT_DIR}"/test/functional/tensor/*.sy; do
    n=$(basename "$src" .sy)
    asm="/tmp/tensor_${n}.S"; bin="/tmp/tensor_${n}"; got="/tmp/tensor_${n}.got"
    if ! "${PROJECT_DIR}/build/compiler" -S "$src" -o "$asm" -"${OPT}" 2>/dev/null; then
        echo "  COMPILE FAIL: $n"; fail=$((fail+1)); continue
    fi
    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "${PROJECT_DIR}/build/libsylib.a" 2>/dev/null; then
        echo "  LINK FAIL:    $n"; fail=$((fail+1)); continue
    fi
    timeout 15 $QEMU "$bin" > "$got" 2>/dev/null
    # 归一化：去掉尾部空行后比对
    if diff -q <(printf '%s\n' "$(cat "$got")") \
              <(printf '%s\n' "$(cat "${PROJECT_DIR}/test/functional/tensor/${n}.out")") >/dev/null; then
        echo "  PASS: $n"; pass=$((pass+1))
    else
        echo "  FAIL: $n (diff vs ${n}.out)"; fail=$((fail+1))
    fi
done
echo "tensor [$OPT]: $pass PASS, $fail FAIL"
[ $fail -eq 0 ]
