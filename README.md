# SysY2022 Compiler

一个将 SysY2022 语言编译为 RV64GC 汇编的完整编译器。

## 项目架构

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

### 架构特点
- ✅ **移除了独立的 AST 层** - 直接使用 ANTLR4 Visitor 模式从 ParseTree 生成 IR
- ✅ **无 SemanticAnalyzer** - 假设输入程序无语法/语义错误
- ✅ **简化的编译流程** - ParseTree → IR → Assembly

## 目录结构

```
compiler/
├── include/
│   ├── Compiler.h          # 编译器主接口
│   ├── ir/
│   │   ├── IR.h            # 中间表示定义
│   │   └── IRBuilder.h     # 从 ParseTree 生成 IR
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
│   ├── SysY2022Lexer.g4
│   └── SysY2022Parser.g4
├── test/
├── CMakeLists.txt
└── README.md
```

## 编译说明

```bash
mkdir build && cd build
cmake ..
make
```
