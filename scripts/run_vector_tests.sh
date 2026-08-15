#!/bin/bash
# 向量测试套：编译 → 链接 → qemu 运行 → 输出对比
# 用法: bash scripts/run_vector_tests.sh [O0|O1]
set -uo pipefail

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SYLIB="$BUILD/libsylib.a"
DIR="$ROOT/test/vector"
OPT="${1:-O0}"
TMP=/tmp/vec_test
mkdir -p "$TMP"

# 确保 sylib 含 vec_*（重建）
bash "$ROOT/scripts/build_sylib.sh" >/dev/null

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
pass=0; cfail=0; lfail=0; diff=0; seg=0; tout=0

for src in "$DIR"/*.sy; do
  [ -f "$src" ] || continue
  name=$(basename "$src" .sy)
  asm="$TMP/$name.S"; bin="$TMP/$name.bin"
  expect="$DIR/$name.out"

  if ! "$BUILD/compiler" -S "$src" -o "$asm" -"$OPT" 2>"$TMP/$name.cerr"; then
    echo -e "  ${RED}COMPILE FAIL${NC}: $name"; sed -n '1,3p' "$TMP/$name.cerr"; cfail=$((cfail+1)); continue
  fi
  if ! $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB" 2>"$TMP/$name.lerr"; then
    echo -e "  ${RED}LINK FAIL${NC}:    $name"; sed -n '1,3p' "$TMP/$name.lerr"; lfail=$((lfail+1)); continue
  fi
  infile="$DIR/$name.in"
  if [ -f "$infile" ]; then
    timeout 30 $QEMU "$bin" < "$infile" > "$TMP/$name.act" 2>/dev/null
  else
    timeout 30 $QEMU "$bin" > "$TMP/$name.act" 2>/dev/null
  fi
  ret=$?
  if [ $ret -eq 124 ]; then echo -e "  ${YELLOW}TIMEOUT${NC}:      $name"; tout=$((tout+1)); continue; fi
  if [ $ret -eq 139 ]; then echo -e "  ${RED}SEGFAULT${NC}:     $name"; seg=$((seg+1)); continue; fi

  if [ ! -f "$expect" ]; then
    echo -e "  ${GREEN}OK (no .out)${NC}: $name (ret=$ret)"; pass=$((pass+1)); continue
  fi
  # 归一化（去尾部空行）
  printf '%s\n' "$(cat "$TMP/$name.act")" > "$TMP/$name.actn"
  printf '%s\n' "$(cat "$expect")" > "$TMP/$name.expn"
  if diff -q "$TMP/$name.actn" "$TMP/$name.expn" >/dev/null 2>&1; then
    echo -e "  ${GREEN}PASS${NC}:         $name"; pass=$((pass+1))
  else
    echo -e "  ${RED}DIFF${NC}:         $name"; echo "    期望: $(cat "$expect"|tr '\n' '|')"; echo "    实际: $(cat "$TMP/$name.actn"|tr '\n' '|')"; diff=$((diff+1))
  fi
done

echo ""
echo -e "结果(${OPT}): ${GREEN}PASS=$pass${NC} ${RED}CFAIL=$cfail LFAIL=$lfail DIFF=$diff SEG=$seg${NC} ${YELLOW}TIMEOUT=$tout${NC}"
[ $((cfail+lfail+diff+seg)) -eq 0 ]
