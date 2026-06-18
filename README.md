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

## 优化级别

测评服务器仅支持 `-O1` 优化选项，因此编译器将其映射为最高优化级别：

| 命令行参数 | 优化级别 | 包含的优化Pass | 用途 |
|-----------|---------|---------------|------|
| `-O1` | OALL | O1 + O2 + O3（不含P0/P3） | **测评服务器使用** |
| `-O0` | O0 | 无优化 | 评测基准 |
| `-o0` | O0 | 无优化 | 本地调试 |
| `-o1` | O1 | CF + DCE + CSE + LICM | 本地逐级调试 |
| `-o2` | O2 | O1 + 内联 + 额外CSE/LICM | 本地逐级调试 |
| `-o3` | O3 | O1+O2 + 代数化简/循环交换/展开/尾递归 | 本地逐级调试 |

> **注意**：大写 `-O1` 对应全部优化（映射到内部 OALL 级别），小写 `-o1` 对应仅 O1 优化。
> 这一设计是因为测评服务器只支持 `-O1` 选项，我们必须在此选项下输出最佳性能。

## 测试

```bash
# 运行全部优化（测评服务器级别）
./scripts/run_tests.sh func O1

# 本地逐级调试
./scripts/run_tests.sh func o1
./scripts/run_tests.sh func o2
./scripts/run_tests.sh all o0
```
