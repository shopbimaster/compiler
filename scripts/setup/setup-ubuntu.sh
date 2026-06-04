#!/bin/bash
# Ubuntu 24.04 一键部署脚本
# 用于快速设置 G4 语法测试环境

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
    git \
    wget \
    openjdk-17-jre-headless

echo -e "${GREEN}✓ 基础依赖已安装${NC}"

# ===== 安装 ANTLR4 =====
echo -e "\n[4/6] 安装 ANTLR4..."
if ! command -v antlr4 &> /dev/null; then
    echo "正在下载 ANTLR4..."
    sudo wget -q -O /usr/local/lib/antlr-4.13.1-complete.jar \
        https://www.antlr.org/download/antlr-4.13.1-complete.jar

    # 创建 antlr4 脚本
    cat << 'EOF' | sudo tee /usr/local/bin/antlr4 > /dev/null
#!/bin/bash
exec java -jar /usr/local/lib/antlr-4.13.1-complete.jar "$@"
EOF
    sudo chmod +x /usr/local/bin/antlr4

    echo -e "${GREEN}✓ ANTLR4 已安装${NC}"
else
    echo -e "${GREEN}✓ ANTLR4 已经安装${NC}"
fi

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

echo -n "  ANTLR4: "
if antlr4 -version &>/dev/null; then
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
echo "  2. 或者完整 C++ 测试:"
echo "     cd $SCRIPT_DIR"
echo "     mkdir -p build-test && cd build-test"
echo "     cp ../CMakeLists-test.txt ../CMakeLists.txt"
echo "     cmake .."
echo "     make -j4"
echo "     ./test-grammar ../test/hello.sy"
echo ""
echo "  3. 详细文档:"
echo "     查看 DEPLOY_TEST_GUIDE.md"
echo ""
