# G4 语法文件部署测试指南

## 一、目标平台环境

- **操作系统**: Ubuntu 24.04 LTS (x86_64)
- **工具链**: ANTLR4 4.13.1+, Java 17+, CMake 3.20+, GCC 11+

---

## 二、环境准备

### 步骤 1: 安装基础依赖

```bash
# 更新包管理器
sudo apt-get update

# 安装基础工具
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    unzip
```

### 步骤 2: 安装 Java

```bash
# 安装 OpenJDK 17
sudo apt-get install -y openjdk-17-jre-headless

# 验证安装
java -version
```

### 步骤 3: 安装 ANTLR4

#### 方式 A: 从 apt 安装（推荐）

```bash
sudo apt-get install -y antlr4

# 验证安装
antlr4 -version
```

#### 方式 B: 手动下载安装

```bash
# 下载 ANTLR4 JAR
cd /usr/local/lib
sudo wget https://www.antlr.org/download/antlr-4.13.1-complete.jar

# 设置别名（可选）
echo 'alias antlr4="java -jar /usr/local/lib/antlr-4.13.1-complete.jar"' >> ~/.bashrc
source ~/.bashrc

# 验证安装
antlr4 -version
```

### 步骤 4: 安装 ANTLR4 C++ Runtime

```bash
# 方式 1: 从源码编译（推荐，与 CMakeLists.txt 配合）
# (CMakeLists-test.txt 会自动处理)

# 方式 2: 预编译包（如果有的话）
# 查找是否有可用的包
apt-cache search antlr4-runtime
```

---

## 三、获取项目

```bash
# 从版本控制系统获取项目，或者直接复制项目文件
cd /path/to/workspace

# 如果使用 git
# git clone <repository_url>
cd compiler

# 查看项目结构
ls -la
```

---

## 四、快速测试（Shell 脚本）

### 方法 A: 使用自动化脚本（推荐）

```bash
# 给脚本添加执行权限
chmod +x test-grammar.sh

# 运行测试脚本
./test-grammar.sh
```

脚本会自动完成:
1. 环境检查
2. 生成分析器
3. 运行测试

### 方法 B: 手动测试（使用 grun）

```bash
# 1. 生成 Java 版本的分析器
cd grammar
antlr4 -Dlanguage=Java SysY2022Lexer.g4
antlr4 -Dlanguage=Java SysY2022Parser.g4
javac SysY2022*.java

# 2. 设置 CLASSPATH
export CLASSPATH=".:/usr/local/lib/antlr-4.13.1-complete.jar:$CLASSPATH"

# 3. 运行测试（图形化界面）
grun SysY2022 compilationUnit ../test/hello.sy -gui

# 或者仅打印 ParseTree
grun SysY2022 compilationUnit ../test/hello.sy -tree
```

---

## 五、完整测试（C++ 测试程序）

### 步骤 1: 创建测试构建目录

```bash
cd /path/to/compiler
mkdir -p build-test
cd build-test
```

### 步骤 2: 配置 CMake

```bash
# 使用测试专用的 CMakeLists
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=../CMakeLists-test.txt

# 或者复制并重命名 CMakeLists-test.txt
cd ..
cp CMakeLists-test.txt CMakeLists.txt
cd build-test
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### 步骤 3: 编译

```bash
# 这会自动下载并编译 ANTLR4（如果需要）
# 第一次编译可能需要较长时间
make -j$(nproc)
```

### 步骤 4: 运行测试

```bash
# 测试单个文件
./test-grammar ../test/hello.sy

# 测试多个文件
for f in ../test/*.sy; do
    echo -e "\n=== 测试: $f ==="
    ./test-grammar "$f"
done
```

---

## 六、测试用例

### 基础测试用例 (test/hello.sy)

```c
int main() {
    return 0;
}
```

### 浮点测试 (test/float_test.sy)

```c
int main() {
    float a = 1.5;
    float b = 2.5;
    return 0;
}
```

### 完整测试套件

如果有官方测试用例:

```bash
# 复制官方测试用例到 test/ 目录
cp -r /path/to/official/testcases/* test/

# 批量测试
cd build-test
for f in ../test/*.sy; do
    echo -e "\n测试: $f"
    ./test-grammar "$f"
    if [ $? -eq 0 ]; then
        echo "✓ 通过"
    else
        echo "✗ 失败"
    fi
done
```

---

## 七、故障排除

### 问题 1: 找不到 antlr4 命令

```bash
# 检查安装
which antlr4
ls -la /usr/local/lib/antlr-*.jar

# 手动运行
java -jar /usr/local/lib/antlr-4.13.1-complete.jar grammar/SysY2022Lexer.g4
```

### 问题 2: 编译时找不到 antlr4-runtime.h

```bash
# 检查 CMake 输出中关于 ANTLR4 的信息
# 确认 ANTLR4_INCLUDE_DIR 变量是否正确设置

# 手动指定路径
cmake .. -DANTLR4_INCLUDE_DIR=/path/to/antlr4/include \
         -DANTLR4_LIBRARY=/path/to/antlr4/lib/libantlr4-runtime.a
```

### 问题 3: Java 版本问题

```bash
# 检查 Java 版本
java -version

# 安装正确版本
sudo apt-get install -y openjdk-17-jre-headless

# 切换默认 Java 版本
sudo update-alternatives --config java
```

---

## 八、验证检查清单

在部署测试完成后，确认以下内容:

- [ ] 环境准备完成（Java, ANTLR4, CMake, GCC）
- [ ] 可以成功运行 `./test-grammar.sh`
- [ ] 可以成功编译 `test-grammar` 程序
- [ ] `test-grammar` 可以正确解析 `test/hello.sy`
- [ ] 没有语法错误或警告
- [ ] ParseTree 输出看起来合理

---

## 九、下一步

语法测试通过后，可以继续:
1. 实现 `IRBuilder.cpp`
2. 实现后端代码生成
3. 集成完整编译器
