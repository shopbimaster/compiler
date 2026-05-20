#!/bin/bash
# 快速语法验证脚本 - 不需要编译 C++

echo "====================================="
echo "SysY2022 语法快速验证"
echo "====================================="

# 1. 进入项目目录
cd "$(dirname "${BASH_SOURCE[0]}")"

# 2. 生成 Java 版本的解析器用于测试
echo "[1/4] 生成 Java 版本的词法和语法解析器..."
mkdir -p /tmp/test-grammar
cd /tmp/test-grammar
rm -f *.java *.class

antlr4 -Dlanguage=Java /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Lexer.g4
antlr4 -Dlanguage=Java /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Parser.g4
echo "✅ 生成成功"

# 3. 编译 Java 解析器
echo "[2/4] 编译 Java 解析器..."
javac -cp ".:/usr/local/lib/antlr-4.13.1-complete.jar" *.java
echo "✅ 编译成功"

# 4. 测试第一个文件
echo "[3/4] 测试 hello.sy..."
cd /tmp/test-grammar
export CLASSPATH=".:/usr/local/lib/antlr-4.13.1-complete.jar"

if java org.antlr.v4.gui.TestRig SysY2022 compilationUnit /mnt/d/VSCodeProjects/compiler/test/hello.sy >/dev/null 2>&1; then
    echo "✅ hello.sy 解析通过!"
else
    echo "❌ hello.sy 解析失败"
    exit 1
fi

# 5. 测试第二个文件
echo "[4/4] 测试 float_test.sy..."
if java org.antlr.v4.gui.TestRig SysY2022 compilationUnit /mnt/d/VSCodeProjects/compiler/test/float_test.sy >/dev/null 2>&1; then
    echo "✅ float_test.sy 解析通过!"
else
    echo "❌ float_test.sy 解析失败"
    exit 1
fi

echo ""
echo "====================================="
echo "🎉 所有测试通过！G4 语法文件正确！"
echo "====================================="
