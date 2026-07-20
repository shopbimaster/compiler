#!/bin/bash
# Ubuntu 24.04 一键部署脚本
# 对齐 2026 技术方案的提交构建环境，并安装本项目所需工具链

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

echo -e "${YELLOW}============================================${NC}"
echo -e "${YELLOW}SysY2022 G4 测试环境自动部署${NC}"
echo -e "${YELLOW}============================================${NC}"

# ===== 检查是否是 Ubuntu =====
echo -e "\n[1/6] 检查操作系统..."
if ! grep -q "Ubuntu 24.04" /etc/os-release 2>/dev/null; then
    echo -e "${YELLOW}警告: 不是 Ubuntu 24.04，尝试继续...${NC}"
else
    echo -e "${GREEN}✓ Ubuntu 24.04 检查通过${NC}"
fi

# ===== 更新系统 =====
echo -e "\n[2/6] 更新系统软件包..."
if [ "$1" != "--no-update" ]; then
    sudo apt-get update -q
else
    echo -e "${YELLOW}跳过更新 (--no-update)${NC}"
fi

# ===== 安装依赖 =====
echo -e "\n[3/6] 安装依赖..."
sudo apt-get install -y -q \
    build-essential \
    cmake \
    clang-18 \
    git \
    unzip \
    wget \
    openjdk-17-jdk-headless \
    gcc-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    qemu-user

echo -e "${GREEN}✓ 基础依赖已安装${NC}"

# ===== 安装 ANTLR4 4.13.1 生成器与 C++ runtime =====
echo -e "\n[4/6] 安装 ANTLR4 4.13.1..."
ANTLR_VERSION=4.13.1
ANTLR_JAR="/usr/local/lib/antlr-${ANTLR_VERSION}-complete.jar"
if [ ! -f "$ANTLR_JAR" ]; then
    echo "正在下载 ANTLR4 生成器..."
    sudo wget -q -O "$ANTLR_JAR" \
        "https://www.antlr.org/download/antlr-${ANTLR_VERSION}-complete.jar"
fi

cat << 'EOF' | sudo tee /usr/local/bin/antlr4 > /dev/null
#!/bin/bash
exec java -jar /usr/local/lib/antlr-4.13.1-complete.jar "$@"
EOF
sudo chmod +x /usr/local/bin/antlr4

if [ ! -f /usr/local/include/antlr4-runtime/atn/SerializedATNView.h ] ||
   [ ! -f /usr/local/include/antlr4-runtime/atn/ParserATNSimulatorOptions.h ]; then
    echo "正在构建 ANTLR4 C++ runtime ${ANTLR_VERSION}..."
    ANTLR_BUILD_ROOT=$(mktemp -d)
    trap 'rm -rf "$ANTLR_BUILD_ROOT"' EXIT
    wget -q -O "$ANTLR_BUILD_ROOT/runtime.zip" \
        "https://www.antlr.org/download/antlr4-cpp-runtime-${ANTLR_VERSION}-source.zip"
    unzip -q "$ANTLR_BUILD_ROOT/runtime.zip" -d "$ANTLR_BUILD_ROOT/src"
    cmake -S "$ANTLR_BUILD_ROOT/src" -B "$ANTLR_BUILD_ROOT/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DANTLR_BUILD_CPP_TESTS=OFF
    cmake --build "$ANTLR_BUILD_ROOT/build" --parallel "$(nproc)"
    sudo cmake --install "$ANTLR_BUILD_ROOT/build"
    sudo ldconfig
    rm -rf "$ANTLR_BUILD_ROOT"
    trap - EXIT
else
    echo -e "${GREEN}✓ ANTLR4 C++ runtime ${ANTLR_VERSION} 已安装${NC}"
fi
echo -e "${GREEN}✓ ANTLR4 ${ANTLR_VERSION} 已安装${NC}"

# ===== 验证 =====
echo -e "\n[5/6] 验证安装..."

echo -n "  Java: "
if java -version &>/dev/null; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "  CMake: "
if cmake --version &>/dev/null; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "  Clang 18: "
if clang++-18 --version &>/dev/null; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

echo -n "  ANTLR4: "
if [ -f /usr/local/include/antlr4-runtime/atn/SerializedATNView.h ] &&
   [ -f /usr/local/include/antlr4-runtime/atn/ParserATNSimulatorOptions.h ]; then
    echo -e "${GREEN}✓${NC}"
else
    echo -e "${RED}✗${NC}"
fi

# ===== 提示 =====
echo -e "\n[6/6] 环境准备完成!"
echo -e "\n${GREEN}============================================${NC}"
echo -e "${GREEN}部署完成! 下一步:${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo "  1. 快速测试:"
echo "     cd $SCRIPT_DIR"
echo "     chmod +x test-grammar.sh"
echo "     ./test-grammar.sh"
echo ""
echo "  2. 按官方 Ubuntu 24.04 / Clang 18 环境构建:"
echo "     cd $(cd "$SCRIPT_DIR/../.." && pwd)"
echo "     cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18"
echo "     cmake --build build-release --parallel"
echo "     ctest --test-dir build-release --output-on-failure"
echo ""
echo "  3. 详细文档:"
echo "     查看 DEPLOY_TEST_GUIDE.md"
echo ""
