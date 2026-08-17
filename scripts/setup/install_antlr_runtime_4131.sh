#!/bin/bash
# 在新 WSL VM 内构建并安装 ANTLR 4.13.1 C++ runtime（对齐云端）
set -e
cd /tmp
rm -rf antlr_rt && mkdir antlr_rt && cd antlr_rt
echo "== 下载 4.13.1 C++ runtime 源码 =="
wget -q -O rt.zip https://www.antlr.org/download/antlr4-cpp-runtime-4.13.1-source.zip
unzip -q rt.zip -d src
echo "== CMake 配置（clang++）=="
cmake -S src -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DANTLR_BUILD_CPP_TESTS=OFF \
    -DCMAKE_CXX_COMPILER=clang++ > /tmp/antlr_cmake.log 2>&1
echo "== 编译（parallel）=="
cmake --build build --parallel "$(nproc)" > /tmp/antlr_build.log 2>&1
echo "== 安装 =="
cmake --install build > /tmp/antlr_install.log 2>&1
ldconfig
echo "== 验证 =="
ls /usr/local/lib/libantlr4-runtime* 2>/dev/null || { echo "RUNTIME 库缺失"; exit 1; }
[ -f /usr/local/include/antlr4-runtime/atn/SerializedATNView.h ] && echo "SerializedATNView.h OK (4.13.1 特征)"
echo "ANTLR_RUNTIME_DONE"
