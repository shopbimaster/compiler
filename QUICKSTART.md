# G4 语法测试 - 快速开始

## 🚀 最简单方式：在 Ubuntu 24.04 上部署

### 1. 运行一键部署脚本

```bash
cd /path/to/compiler
chmod +x setup-ubuntu.sh
./setup-ubuntu.sh
```

### 2. 运行快速测试脚本

```bash
chmod +x test-grammar.sh
./test-grammar.sh
```

---

## 📋 完整测试方式

### 步骤 1: 环境准备

```bash
# 如果没有使用一键脚本
sudo apt-get update
sudo apt-get install -y build-essential cmake openjdk-17-jre-headless

# 下载 ANTLR4
sudo wget -O /usr/local/lib/antlr-4.13.1-complete.jar \
    https://www.antlr.org/download/antlr-4.13.1-complete.jar

# 创建包装脚本
echo '#!/bin/bash' | sudo tee /usr/local/bin/antlr4
echo 'exec java -jar /usr/local/lib/antlr-4.13.1-complete.jar "$@"' | sudo tee -a /usr/local/bin/antlr4
sudo chmod +x /usr/local/bin/antlr4
```

### 步骤 2: C++ 测试程序

```bash
mkdir -p build-test && cd build-test
cp ../CMakeLists-test.txt ../CMakeLists.txt
cmake ..
make -j4

# 测试一个文件
./test-grammar ../test/hello.sy

# 测试全部文件
for f in ../test/*.sy; do
    echo -e "\n--- $f ---"
    ./test-grammar "$f"
done
```

---

## 📁 文件说明

| 文件 | 用途 |
|-----|------|
| `setup-ubuntu.sh` | 一键部署脚本（Ubuntu 24.04） |
| `test-grammar.sh` | 快速语法测试脚本 |
| `CMakeLists-test.txt` | 语法测试专用 CMake 配置 |
| `src/test-grammar.cpp` | C++ 测试程序 |
| `DEPLOY_TEST_GUIDE.md` | 详细部署文档 |

---

## ✅ 成功标志

看到以下输出表示测试通过：

```
正在解析: ../test/hello.sy

✅ 解析成功!
ParseTree: ...
```

---

## 🔧 如果遇到问题

查看 `DEPLOY_TEST_GUIDE.md` 获取详细故障排除指南。
