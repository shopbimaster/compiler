#!/bin/bash
# 云端对齐环境自检（在 Ubuntu-24.04-eval VM 内运行）
# 只读检查，不修改任何东西
echo "===== 系统 ====="
grep PRETTY_NAME /etc/os-release

echo "===== 编译器（云端用 clang++）====="
clang++ --version | head -1
echo "  clang++ 路径: $(command -v clang++)"

echo "===== ANTLR 4.13.1（云端标准）====="
ls -la /usr/local/lib/antlr-4.13.1-complete.jar 2>/dev/null && echo "  生成器 jar OK"
ls /usr/local/lib/libantlr4-runtime.so.4.13.1 2>/dev/null && echo "  C++ runtime .so OK"
ls /usr/local/lib/libantlr4-runtime.a 2>/dev/null && echo "  C++ runtime .a  OK"
[ -f /usr/local/include/antlr4-runtime/antlr4-runtime.h ] && echo "  runtime 头文件 OK"
[ -f /usr/local/include/antlr4-runtime/atn/SerializedATNView.h ] && echo "  SerializedATNView.h OK（4.13.1 特征）"

echo "===== RISC-V 交叉工具链 ====="
riscv64-linux-gnu-gcc --version | head -1

echo "===== qemu 用户态 ====="
qemu-riscv64 --version | head -1

echo "===== Java（ANTLR 生成器依赖）====="
java -version 2>&1 | head -1

echo "===== 常用构建工具 ====="
cmake --version | head -1
make --version | head -1

echo "ENV_VERIFY_DONE"
