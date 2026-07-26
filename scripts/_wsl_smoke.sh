#!/bin/bash
# 临时冒烟测试：验证 编译→链接→qemu→比对 整条链（实验用，可删）
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1
C=./build/compiler
SY=./build/libsylib.a
QEMU=qemu-riscv64-static
T=test/performance/01_mm1

echo "=== 编译 (V1: 默认 full, GVN off) ==="
$C -S -o /tmp/mm1.s "$T.sy" -O1 && echo "compile OK" || { echo "COMPILE FAIL"; exit 1; }

echo "=== 链接 ==="
riscv64-linux-gnu-gcc -march=rv64gc -mabi=lp64d -mcmodel=medany /tmp/mm1.s "$SY" -o /tmp/mm1.exe \
  && echo "link OK" || { echo "LINK FAIL"; exit 1; }

echo "=== qemu 运行 ==="
$QEMU /tmp/mm1.exe < "$T.in" > /tmp/mm1.out 2>/dev/null
echo "ret=$?"

echo "=== 输出比对 ==="
if diff <(sed -e 's/[[:space:]]*$//' /tmp/mm1.out) \
        <(sed -e 's/[[:space:]]*$//' "$T.out") >/dev/null 2>&1; then
  echo "OUTPUT MATCH"
else
  echo "OUTPUT DIFF:"
  echo "--- got (tail) ---"; tail -3 /tmp/mm1.out
  echo "--- exp (tail) ---"; tail -3 "$T.out"
fi

echo ""
echo "=== 快速验证 GVN 开关生效 (V3: full + GVN) ==="
OPT_ENABLE_GVN=1 $C -S -o /tmp/mm1_gvn.s "$T.sy" -O1 && echo "GVN-on compile OK"
echo "V1 汇编行数: $(wc -l < /tmp/mm1.s)   V3(GVN) 汇编行数: $(wc -l < /tmp/mm1_gvn.s)"
echo "V1 mv 数: $(grep -cE '^\s*(mv|fmv\.s)\b' /tmp/mm1.s)   V3 mv 数: $(grep -cE '^\s*(mv|fmv\.s)\b' /tmp/mm1_gvn.s)"
