# SysY2022 编译器开发指南

## 一、项目架构

### 简化后的架构

```
输入 (.sy)
   ↓
[ANTLR4 词法/语法分析]
   ↓
[IRBuilder - 直接从 ParseTree 生成 IR]
   ↓
[IR - 中间表示]
   ↓
[后端]
   ├─ RegisterAllocator (寄存器分配)
   ├─ TargetCodeGen (IR → RV64 汇编)
   └─ PeepholeOptimizer (窥孔优化)
   ↓
输出 (.s)
```

### 架构说明

- **无 AST 层** - 直接使用 ANTLR4 Visitor 从 ParseTree 生成 IR
- **无 SemanticAnalyzer** - 假设输入程序无语法/语义错误
- **简化流程** - ParseTree → IR → Assembly

---

## 二、目录结构

```
compiler/
├── include/
│   ├── Compiler.h          # 编译器主接口
│   ├── ir/
│   │   ├── IR.h            # 中间表示定义
│   │   └── IRBuilder.h     # ANTLR Visitor，ParseTree → IR
│   ├── backend/
│   │   ├── TargetCodeGen.h # IR → RV64 汇编
│   │   ├── RegisterAllocator.h
│   │   └── PeepholeOptimizer.h
│   └── utils/
│       ├── Error.h         # 错误处理
│       └── Logger.h        # 日志记录
├── src/
│   ├── main.cpp
│   ├── ir/
│   ├── backend/
│   └── utils/
├── grammar/
│   ├── SysY2022Lexer.g4    # 词法规则
│   └── SysY2022Parser.g4   # 语法规则
├── test/
├── CMakeLists.txt
└── DEVELOPMENT_GUIDE.md
```

---

## 三、开发步骤

### 阶段 1: 实现 IRBuilder.cpp
**目标**: 继承 `SysY2022ParserBaseVisitor`，直接从 ParseTree 生成 IR

**关键方法**:
- `visitCompilationUnit` - 处理整个编译单元
- `visitFuncDef` - 处理函数定义
- `visitBlock` - 处理语句块
- `visitStmt` - 处理语句
- `visitExp` / `visitAddExp` / etc. - 处理表达式，返回 Value*

### 阶段 2: 实现后端
**目标**: 从 IR 生成 RV64 汇编

### 阶段 3: 实现 Compiler.cpp
**目标**: 整合所有模块

---

## 四、技术学习路线图

### 前置知识
1. C++20
2. ANTLR4 Visitor 模式
3. LLVM IR 概念
4. RISC-V RV64GC 指令集

### 关键模块
1. **IRBuilder** - 核心模块，继承 `SysY2022ParserBaseVisitor`
2. **TargetCodeGen** - IR 到汇编转换

---

## 五、关键文件说明

### IRBuilder.h
- 继承 `SysY2022ParserBaseVisitor`
- 维护当前 Function、BasicBlock、符号表
- Visitor 方法返回 `std::any` (通常是 Value*)

### IR.h
- 定义 IR 结构：Module, Function, BasicBlock, Instruction, Value
