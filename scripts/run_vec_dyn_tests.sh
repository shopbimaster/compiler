#!/bin/bash
# 《前端+变长》 变长向量测试运行器（test/functional/vec_dynamic/*.sy）
# Usage (in WSL Ubuntu-24.04-eval):
#   bash scripts/run_vec_dyn_tests.sh [O0|O1|o0|o1|o2|o3]
set -e

OPT="${1:-O0}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${PROJECT_DIR}/build"
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
SYLIB="${BUILD}/libsylib.a"
TEST_DIR="${PROJECT_DIR}/test/functional/vec_dynamic"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}============================================================${NC}"
echo -e "${CYAN}  变长向量测试 (Variable-length Vec, ${OPT})${NC}"
echo -e "${CYAN}============================================================${NC}"
echo ""

mkdir -p /tmp/vec_dyn

pass=0 fail=0
for src in "${TEST_DIR}"/*.sy; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .sy)
    asm="/tmp/vec_dyn/${name}.S"
    bin="/tmp/vec_dyn/${name}_bin"
    cerr="/tmp/vec_dyn/${name}_cerr.txt"
    lerr="/tmp/vec_dyn/${name}_lerr.txt"
    outfile="${TEST_DIR}/${name}.out"

    if ! "${BUILD}/compiler" -S "$src" -o "$asm" -"${OPT}" >/dev/null 2>"$cerr"; then
        echo -e "  ${RED}COMPILE FAIL${NC}: ${name}"
        head -20 "$cerr"
        fail=$((fail + 1))
        continue
    fi

    if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB" 2>"$lerr"; then
        echo -e "  ${RED}LINK FAIL${NC}:    ${name}"
        head -10 "$lerr"
        fail=$((fail + 1))
        continue
    fi

    set +e
    # 只捕获 stdout；sylib 的 after_main 把 "TOTAL:..." 写到 stderr，丢弃
    out=$($QEMU "$bin" 2>/dev/null)
    rc=$?
    set -e

    if [ ! -f "$outfile" ]; then
        echo -e "  ${YELLOW}NO .OUT${NC}:   ${name} => [${out}] rc=${rc}"
        continue
    fi

    exp=$(head -n -1 "$outfile")
    if [ "$out" = "$exp" ]; then
        echo -e "  ${GREEN}PASS${NC}: ${name}"
        pass=$((pass + 1))
    else
        echo -e "  ${RED}DIFF${NC}: ${name}"
        echo "    got: [$out]"
        echo "    exp: [$exp]"
        fail=$((fail + 1))
    fi
done

echo ""
echo -e "  ${GREEN}${pass} passed${NC}, ${RED}${fail} failed${NC}"
exit $fail
