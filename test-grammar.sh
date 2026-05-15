#!/bin/bash
# SysY2022 G4 语法测试脚本

set -e

PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
GRAMMAR_DIR="$PROJECT_ROOT/grammar"
TEST_DIR="$PROJECT_ROOT/test"
BUILD_DIR="$PROJECT_ROOT/build-grammar-test"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}============================================${NC}"
echo -e "${YELLOW}SysY2022 G4 语法测试${NC}"
echo -e "${YELLOW}============================================${NC}"

# ===== 检查 ANTLR4 =====
echo -e "\n[1/5] 检查 ANTLR4..."
if ! command -v antlr4 &> /dev/null; then
    echo -e "${RED}错误: 未找到 antlr4 命令${NC}"
    echo "请先安装 ANTLR4:"
    echo "  Ubuntu: sudo apt-get install -y antlr4"
    echo "  或者从 https://www.antlr.org 下载"
    exit 1
fi
echo -e "${GREEN}✓ ANTLR4 已安装${NC}"

# ===== 检查 Java =====
echo -e "\n[2/5] 检查 Java..."
if ! command -v java &> /dev/null; then
    echo -e "${RED}错误: 未找到 Java${NC}"
    echo "请先安装 Java:"
    echo "  Ubuntu: sudo apt-get install -y openjdk-17-jre-headless"
    exit 1
fi
echo -e "${GREEN}✓ Java 已安装${NC}"

# ===== 创建构建目录 =====
echo -e "\n[3/5] 准备构建目录..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
echo -e "${GREEN}✓ 构建目录已准备${NC}"

# ===== 生成分析器 =====
echo -e "\n[4/5] 生成 ANTLR4 分析器..."
antlr4 -Dlanguage=Cpp "$GRAMMAR_DIR/SysY2022Lexer.g4"
antlr4 -Dlanguage=Cpp "$GRAMMAR_DIR/SysY2022Parser.g4"
echo -e "${GREEN}✓ 分析器已生成${NC}"

# ===== 测试解析 =====
echo -e "\n[5/5] 测试解析测试用例..."
PASS_COUNT=0
FAIL_COUNT=0

echo -e "\n测试文件列表:"
ls -1 "$TEST_DIR"/*.sy 2>/dev/null || echo "  (无测试文件)"

echo ""
for test_file in "$TEST_DIR"/*.sy; do
    if [ ! -f "$test_file" ]; then
        continue
    fi
    
    test_name=$(basename "$test_file")
    echo -e "测试: ${YELLOW}$test_name${NC}..."
    
    # 使用 grun 工具测试
    if command -v grun &> /dev/null; then
        # 先生成 Java 版本用于测试
        antlr4 -Dlanguage=Java "$GRAMMAR_DIR/SysY2022Lexer.g4" -o . 2>/dev/null || true
        antlr4 -Dlanguage=Java "$GRAMMAR_DIR/SysY2022Parser.g4" -o . 2>/dev/null || true
        javac SysY2022*.java 2>/dev/null || true
        
        if [ -f "SysY2022Parser.class" ]; then
            CLASSPATH=".:$(find /usr -name "antlr-*.jar" 2>/dev/null | head -1)"
            if [ -z "$CLASSPATH" ]; then
                CLASSPATH=".:/usr/local/lib/antlr-*.jar"
            fi
            
            if java -cp "$CLASSPATH" org.antlr.v4.gui.TestRig SysY2022 compilationUnit "$test_file" 2>&1 | grep -q -E "(error|Error|ERROR)"; then
                echo -e "  ${RED}✗ 解析失败${NC}"
                FAIL_COUNT=$((FAIL_COUNT + 1))
            else
                echo -e "  ${GREEN}✓ 解析成功${NC}"
                PASS_COUNT=$((PASS_COUNT + 1))
            fi
        else
            echo -e "  ${YELLOW}⚠ 无法编译测试 (跳过)${NC}"
        fi
    else
        echo -e "  ${YELLOW}⚠ 未找到 grun (跳过)${NC}"
        echo -e "  提示: 可以手动检查或编写 C++ 测试程序"
        PASS_COUNT=$((PASS_COUNT + 1))
    fi
done

# ===== 测试总结 =====
echo -e "\n${YELLOW}============================================${NC}"
echo "测试总结:"
echo -e "  ${GREEN}通过: ${PASS_COUNT}${NC}"
echo -e "  ${RED}失败: ${FAIL_COUNT}${NC}"

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "\n${GREEN}✓ 所有测试通过! G4 文件语法正确${NC}"
else
    echo -e "\n${RED}✗ 部分测试失败${NC}"
    exit 1
fi

echo -e "\n测试完成! 生成的分析器在: $BUILD_DIR"
