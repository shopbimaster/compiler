# SysY2022 编译器开发指南

## 一、项目概述

本项目是一个将 SysY2022 语言编译为 RV64GC 汇编的完整编译器，分为前端、中间表示、后端三个主要模块。

### 项目架构图

```
输入 (.sy)
   ↓
[前端]
   ├─ ANTLR4 Lexer (词法分析)
   ├─ ANTLR4 Parser (语法分析)
   ├─ AST Builder (构建抽象语法树)
   └─ Semantic Analyzer (语义分析)
   ↓
[中间表示]
   ├─ IR (IR数据结构)
   ├─ IRBuilder (AST→IR)
   └─ Optimizer (IR优化)
   ↓
[后端]
   ├─ RegisterAllocator (寄存器分配)
   ├─ TargetCodeGen (IR→RV64汇编)
   └─ PeepholeOptimizer (窥孔优化)
   ↓
输出 (.s)
```

---

## 二、开发步骤与阶段测试方案

### 阶段 1：项目框架搭建 ✅
**目标**：建立完整的项目结构和构建系统

**已完成**：
- 创建 CMakeLists.txt
- 创建目录结构
- 工具模块头文件 (Error.h, Logger.h)
- 前端模块头文件 (AST.h, ASTBuilder.h, SemanticAnalyzer.h)
- IR 模块头文件 (IR.h, IRBuilder.h)
- 后端模块头文件 (TargetCodeGen.h, RegisterAllocator.h, PeepholeOptimizer.h)
- 主程序入口

**技术学习需求**：
- C++20 新特性
- CMake 构建系统

**测试方案**：验证项目可以正确编译（先不实现功能，只验证结构）

---

### 阶段 2：工具模块实现
**目标**：实现 Error 和 Logger 模块

**任务**：
- 实现 Error 类的完整功能
- 实现 Logger 类的完整功能
- 编写单元测试

**技术学习需求**：
- C++ 类设计
- 文件 I/O 操作
- 时间处理

**测试方案**：
```cpp
// test/TestUtils.cpp
#include "utils/Error.h"
#include "utils/Logger.h"

int main() {
    // 测试 ErrorReporter
    ErrorReporter reporter("test.sy");
    reporter.report(ErrorType::SYNTAX, "Missing semicolon", 10, 5);
    
    // 测试 Logger
    Logger::getInstance().info("Test log message");
    return reporter.hasError() ? 0 : 1;
}
```

---

### 阶段 3：ANTLR4 集成与语法文件完善
**目标**：正确集成 ANTLR4 并完善词法/语法定义

**需要外部拓展**：
1. **下载 ANTLR4 工具**：
   ```bash
   # Ubuntu 24.04
   sudo apt-get update
   sudo apt-get install -y antlr4
   # 或手动下载
   wget https://www.antlr.org/download/antlr-4.13.1-complete.jar
   ```

2. **生成 C++ 词法/语法分析器**：
   ```bash
   antlr4 -Dlanguage=Cpp grammar/SysY2022Lexer.g4
   antlr4 -Dlanguage=Cpp grammar/SysY2022Parser.g4
   ```
   生成的文件需要放到 `src/frontend/` 目录下。

**技术学习需求**：
- ANTLR4 语法规则 (EBNF)
- ANTLR4 Visitor 模式
- ANTLR4 C++ runtime 使用

**测试方案**：
```c
// test/hello.sy
int main() {
    return 0;
}
```
编写测试程序验证 Parser 可以正确解析。

---

### 阶段 4：前端模块实现
**目标**：实现 ASTBuilder 和 SemanticAnalyzer

**任务**：
- 实现 AST 节点的 accept 方法
- 实现 ASTBuilder (继承自 ANTLR4 Visitor)
- 实现 SemanticAnalyzer (符号表、类型检查)

**技术学习需求**：
- 访问者设计模式
- 符号表设计
- 类型系统原理

**测试方案**：
```c
// test/semantic_test.sy
int a;
int b[10];

int func(int x, int y[]) {
    int c = a + x;
    return c;
}
```
测试语义分析是否能正确处理变量、数组、函数。

---

### 阶段 5：中间表示模块实现
**目标**：实现 IRBuilder 并生成 LLVM 风格的 IR

**任务**：
- 完善 IR 数据结构
- 实现 IRBuilder (继承自 AST Visitor)
- 实现 IR 的打印功能用于调试

**技术学习需求**：
- LLVM IR 基础知识
- SSA (静态单赋值) 形式
- 控制流图构建

**测试方案**：
输出 IR 到文件，检查结构是否正确。

---

### 阶段 6：后端模块实现
**目标**：实现从 IR 到 RV64 汇编的转换

**任务**：
- 实现 RegisterAllocator (线性扫描算法)
- 实现 TargetCodeGen (RV64 指令发射)
- 实现 PeepholeOptimizer (窥孔优化)

**关键技术约束**：
- 目标平台：RV64GC
- 内存模型：medany (±2GB PC相对寻址)
- ABI 遵从：RISC-V System V ABI

**技术学习需求**：
- RISC-V RV64GC 指令集手册
- RISC-V 汇编语言
- 寄存器分配算法
- 窥孔优化模式
- RISC-V medany 内存模型

**测试方案**：
```c
// test/codegen_test.sy
int main() {
    int a = 1;
    int b = 2;
    return a + b;
}
```
生成汇编并用 RISC-V 工具链验证。

---

### 阶段 7：集成与系统测试
**目标**：集成所有模块并通过基准测试

**任务**：
- 实现 Compiler 类 (src/Compiler.cpp)
- 集成所有模块
- 通过基准测试程序验证

**需要外部工具**：
- RISC-V GCC 工具链 (用于汇编链接验证)
- Spike RISC-V 模拟器 (用于运行测试)
- BOOM CPU FPGA 环境 (最终验证)

---

## 三、技术学习路线图

### 前置知识
1. **C++20** (必需)
   - 智能指针 (unique_ptr, shared_ptr)
   - 移动语义
   - std::variant, std::optional
   - 范围 for 循环
   - Lambda 表达式

2. **编译原理基础** (必需)
   - 词法分析、语法分析
   - 抽象语法树 (AST)
   - 语义分析
   - 中间代码生成
   - 目标代码生成
   - 代码优化

### 分模块学习路线

#### 模块 1：ANTLR4 (约 1-2 周)
- 学习资源：
  - ANTLR4 官方文档: https://www.antlr.org/
  - 《The Definitive ANTLR 4 Reference》
- 实践：实现简单的计算器语言

#### 模块 2：RISC-V 架构 (约 1-2 周)
- 学习资源：
  - RISC-V 官方文档: https://riscv.org/
  - 《RISC-V Reader》
  - RISC-V 指令集手册 Volume I & II
- 实践：手写简单的 RV64 汇编程序

#### 模块 3：LLVM IR (约 1 周)
- 学习资源：
  - LLVM 官方文档: https://llvm.org/docs/
  - 《Getting Started with LLVM Core Libraries》
- 实践：用 clang -S -emit-llvm 生成 IR 并分析

#### 模块 4：寄存器分配 (约 1 周)
- 学习资源：
  - 《Engineering a Compiler》第 13 章
  - 线性扫描寄存器分配论文
- 实践：实现简化版的线性扫描算法

---

## 四、目录结构说明

```
compiler/
├── include/
│   ├── Compiler.h              # 编译器主接口
│   ├── frontend/
│   │   ├── AST.h               # 抽象语法树定义
│   │   ├── ASTBuilder.h        # ANTLR ParseTree → AST
│   │   └── SemanticAnalyzer.h  # 语义分析
│   ├── ir/
│   │   ├── IR.h                # 中间表示定义
│   │   └── IRBuilder.h         # AST → IR
│   ├── backend/
│   │   ├── TargetCodeGen.h     # IR → RV64 汇编
│   │   ├── RegisterAllocator.h # 寄存器分配
│   │   └── PeepholeOptimizer.h # 窥孔优化
│   └── utils/
│       ├── Error.h             # 错误处理
│       └── Logger.h            # 日志记录
├── src/
│   ├── main.cpp                # 程序入口
│   ├── Compiler.cpp            # 编译器实现
│   ├── frontend/
│   │   ├── AST.cpp
│   │   ├── ASTBuilder.cpp
│   │   ├── SemanticAnalyzer.cpp
│   │   ├── SysY2022Lexer.cpp   # ANTLR 生成
│   │   └── SysY2022Parser.cpp  # ANTLR 生成
│   ├── ir/
│   │   ├── IR.cpp
│   │   └── IRBuilder.cpp
│   └── backend/
│       ├── TargetCodeGen.cpp
│       ├── RegisterAllocator.cpp
│       └── PeepholeOptimizer.cpp
├── grammar/
│   ├── SysY2022Lexer.g4        # 词法规则
│   └── SysY2022Parser.g4       # 语法规则
├── test/                       # 测试用例
├── build/                      # 构建目录
├── CMakeLists.txt
├── README.md
└── DEVELOPMENT_GUIDE.md        # 本文档
```

---

## 五、在 Ubuntu 24.04 上的编译说明

### 1. 安装依赖

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    antlr4 \
    openjdk-17-jdk-headless \
    libantlr4-runtime-dev
```

### 2. 克隆/进入项目

```bash
cd /path/to/compiler
```

### 3. 生成 ANTLR 分析器

```bash
antlr4 -Dlanguage=Cpp -o src/frontend grammar/SysY2022Lexer.g4
antlr4 -Dlanguage=Cpp -o src/frontend grammar/SysY2022Parser.g4
```

### 4. 编译项目

```bash
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 六、后续开发指引

请按以下顺序实现各模块的 `.cpp` 文件：

1. **utils/**  - 最简单，可先实现
2. **frontend/** - 需要先处理好 ANTLR
3. **ir/** - 核心中间表示
4. **backend/** - 复杂的后端生成
5. **Compiler.cpp** - 集成所有模块

如果在实现过程中遇到问题，请随时沟通！
