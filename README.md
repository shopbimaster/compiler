# SysY2022 Compiler

一个将 SysY2022 语言编译为 RV64GC 汇编的完整编译器。

## 项目结构

```
compiler/
├── include/
│   ├── frontend/       # 前端模块
│   ├── ir/             # 中间表示模块
│   ├── backend/        # 后端模块
│   └── utils/          # 工具模块
├── src/
│   ├── frontend/
│   ├── ir/
│   ├── backend/
│   └── utils/
├── grammar/            # ANTLR 语法文件
├── test/               # 测试用例
├── build/              # 构建目录
├── CMakeLists.txt
└── README.md
```

## 开发步骤

### 阶段 1：项目框架搭建
- 创建目录结构
- 配置 CMake 构建系统

### 阶段 2：工具模块
- Error：错误处理系统
- Logger：日志记录系统

### 阶段 3：前端模块
- ANTLR4 词法/语法定义
- AST 节点定义
- 语义分析器

### 阶段 4：中间表示模块
- IR 数据结构（BasicBlock, Instruction）
- IRBuilder（AST -> IR）
- Optimizer（优化 Pass）

### 阶段 5：后端模块
- TargetCodeGen（IR -> RV64 汇编）
- RegisterAllocator（寄存器分配）
- PeepholeOptimizer（窥孔优化）

### 阶段 6：集成与测试
- 完整编译器集成
- 基准测试程序验证

## 技术栈

- 开发语言：C++20
- 词法/语法分析：ANTLR4
- 目标平台：RV64GC (RISC-V 64)
- 内存模型：medany
- 运行环境：FPGA BOOM CPU

## 编译说明

```bash
mkdir build
cd build
cmake ..
make
```
