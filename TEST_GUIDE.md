# SysY2022 编译器 — WSL 单元测试操作手册

## 前置条件

- Windows 11 已启用 WSL，安装 Ubuntu 子系统
- 项目位于 Windows 文件系统：`D:\VSCodeProjects\compiler`
- WSL 内对应的路径为：`/mnt/d/VSCodeProjects/compiler`

> **如果 WSL 中尚需安装依赖，执行以下命令（仅首次）：**
> ```bash
> sudo apt-get update
> sudo apt-get install -y build-essential cmake openjdk-17-jdk-headless
> sudo wget -O /usr/local/lib/antlr-4.13.1-complete.jar \
>   https://www.antlr.org/download/antlr-4.13.1-complete.jar
> sudo tee /usr/local/bin/antlr4 <<'EOF'
> #!/bin/bash
> exec java -jar /usr/local/lib/antlr-4.13.1-complete.jar "$@"
> EOF
> sudo chmod +x /usr/local/bin/antlr4
> ```

---

## 一、前端 G4 语法文件测试

### 第 1 步：进入 WSL Ubuntu 并定位项目

```bash
# 在 PowerShell 中进入 WSL（或在现有 WSL 终端执行）
wsl -d Ubuntu
```

```bash
# 定位到项目目录
cd /mnt/d/VSCodeProjects/compiler
```

### 第 2 步：用 ANTLR4 生成 Java 版本的分析器

```bash
# 创建临时输出目录
mkdir -p /tmp/sysy-test
```

```bash
# ⚠️ 关键：Lexer 和 Parser 必须用同一条命令生成到同一目录
# Parser 依赖 Lexer 的 .tokens 文件，分开执行会导致 "cannot find tokens file" 报错
antlr4 -Dlanguage=Java -o /tmp/sysy-test grammar/SysY2022Lexer.g4 grammar/SysY2022Parser.g4
```

### 第 3 步：编译 Java 分析器

```bash
# 使用 javac 编译生成的 .java 文件
javac -cp ".:/usr/local/lib/antlr-4.13.1-complete.jar" /tmp/sysy-test/*.java
```

### 第 4 步：用 TestRig 测试真实用例

```bash
# 设置 CLASSPATH 包含 ANTLR JAR 和生成的类文件
export CLASSPATH="/tmp/sysy-test:/usr/local/lib/antlr-4.13.1-complete.jar"
```

```bash
# 测试 hello.sy（无输出 = 解析成功；有报错信息 = 语法错误）
java org.antlr.v4.gui.TestRig SysY2022 compilationUnit test/hello.sy
```

```bash
# 测试 float_test.sy
java org.antlr.v4.gui.TestRig SysY2022 compilationUnit test/float_test.sy
```

> **期望结果**：两条命令均无报错，终端静默返回。表示 `hello.sy` 和 `float_test.sy` 的语法完全正确。

---

## 二、IR 模块单元测试

### 第 5 步：回到项目目录并创建构建目录

```bash
cd /mnt/d/VSCodeProjects/compiler
```

```bash
# 创建独立的构建目录（避免污染源码）
mkdir -p build && cd build
```

### 第 6 步：CMake 配置

```bash
# 生成 Makefile（C++ 标准为 C++20）
cmake ..
```

> **期望输出**：末尾显示 `Build files have been written to: .../build`

### 第 7 步：编译

```bash
# 4 线程并行编译 sysy_ir 库 + 测试可执行文件
make -j4
```

> **期望输出**：依次编译 `IR.cpp` → `IRBuilder.cpp` → `libsysy_ir.a` → `test_ir`，无 error。

### 第 8 步：运行 IR 单元测试

```bash
# 执行全部 18 项单元测试
./test_ir
```

> **期望输出**：
> ```
> === IR 框架单元测试 ===
> [Type System]       9/9  PASSED
> [Def-Use Chain]     2/2  PASSED
> [Constants]         1/1  PASSED
> [Instructions]      3/3  PASSED
> [Module/BB/Func]    1/1  PASSED
> [IRBuilder E2E]     2/2  PASSED
> 
> -------- Generated IR --------
> define i32 @main() {
> entry:
>   ret i32 0
> }
> -------------------------------
> 
> === 结果: 18 passed, 0 failed ===
> ```

### 第 9 步（可选）：清理构建产物

```bash
cd /mnt/d/VSCodeProjects/compiler && rm -rf build
```

---

## 三、一键快速测试脚本

将以下内容复制到 WSL 终端中执行，一次性跑完前端语法测试 + IR 单元测试：

```bash
cd /mnt/d/VSCodeProjects/compiler

echo "========== 1/2: 前端 G4 语法测试 =========="
mkdir -p /tmp/sysy-test
antlr4 -Dlanguage=Java -o /tmp/sysy-test grammar/SysY2022Lexer.g4 grammar/SysY2022Parser.g4 \
  && javac -cp ".:/usr/local/lib/antlr-4.13.1-complete.jar" /tmp/sysy-test/*.java \
  && export CLASSPATH="/tmp/sysy-test:/usr/local/lib/antlr-4.13.1-complete.jar" \
  && java org.antlr.v4.gui.TestRig SysY2022 compilationUnit test/hello.sy \
  && java org.antlr.v4.gui.TestRig SysY2022 compilationUnit test/float_test.sy \
  && echo "✅ G4 语法测试全部通过！"

echo ""
echo "========== 2/2: IR 模块单元测试 =========="
rm -rf build && mkdir build && cd build \
  && cmake .. && make -j4 && ./test_ir \
  && echo "✅ IR 单元测试全部通过！"

echo ""
echo "🎉 全量测试完成！"
```

---

## 测试结果速查

| 模块 | 测试项 | 通过标志 |
|-----|--------|---------|
| 前端 G4 | `hello.sy` 解析 | 终端无报错 |
| 前端 G4 | `float_test.sy` 解析 | 终端无报错 |
| IR Type System | 唯一性、toString | 9/9 PASSED |
| IR Def-Use | 引用链维护 | 2/2 PASSED |
| IR Constants | 缓存验证 | 1/1 PASSED |
| IR Instructions | 创建方法 | 3/3 PASSED |
| IR Module | dump() 输出 | 1/1 PASSED |
| IRBuilder | main→ret 端到端 | 2/2 PASSED |