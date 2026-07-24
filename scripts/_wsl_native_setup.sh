#!/bin/bash
# 临时脚本：拷贝到 WSL 原生目录并构建（消除 /mnt/c I/O 噪声）。实验用，可删。
set -e
SRC=/mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x
DST="$HOME/compiler"

echo "=== 清理并拷贝到 $DST ==="
rm -rf "$DST"
mkdir -p "$DST"
cp -r "$SRC"/src "$SRC"/include "$SRC"/test "$SRC"/tests \
      "$SRC"/scripts "$SRC"/grammar "$SRC"/SysYlib "$SRC"/CMakeLists.txt "$DST"/
echo "拷贝完成，大小: $(du -sh "$DST" | cut -f1)"

echo "=== 确认改动在位 ==="
echo "RA A/B 开关: $(grep -c 'RA_COALESCE_MODE' "$DST/src/backend/RegisterAllocator.cpp") 处"
echo "GVN 开关:    $(grep -c 'OPT_ENABLE_GVN' "$DST/src/opt/Optimizer.cpp") 处"

echo "=== cmake 配置 ==="
cd "$DST"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > /tmp/native_cfg.log 2>&1
tail -2 /tmp/native_cfg.log

echo "=== 构建 compiler ==="
cmake --build build -j"$(nproc)" > /tmp/native_build.log 2>&1
grep -E "Built target compiler|error:" /tmp/native_build.log | tail -2

echo "=== 构建 libsylib ==="
bash scripts/build_sylib.sh > /tmp/native_sylib.log 2>&1
ls -la build/compiler build/libsylib.a
echo "NATIVE_SETUP_EXIT=0"
