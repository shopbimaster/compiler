# SysY2022 Compiler

一个将 SysY2022 语言编译为 RV64GC 汇编的完整编译器。

当前初赛目标为 SysY2022。编译器只启用依赖程序语义和 IR 数据流的通用优化，
不得通过函数名、测试名、输入值或测评环境特征触发专用优化。

AI 辅助说明：项目开发使用 OpenAI Codex 辅助合规审计、通用优化实现、正确性
诊断、测试和文档整理。AI 生成内容涉及部分优化器与后端修复、
`RedundantIterationElimination`、`DynamicIdempotentLoopElimination` 及相关
测试；所有生成结果均由参赛队成员理解、人工复核、修改并通过回归测试后纳入项目，
参赛队对最终实现和正确性负责。

## 当前评测状态（2026-07-20）

- 官网最近一次完整结果（六例修复前）：Functional 100/100、H_Functional 40/40、Performance 54/60。
- `h-5-01/02/03` 与 `crypto-1/2/3` 的通用正确性修复已提交；当前提交态已在
  Ubuntu 24.04 下通过 RISC-V 静态链接和 QEMU 执行，本地目标用例 6/6 PASS。
- 尚未运行修改后的官网完整回归，不能将本地 6/6 记为 Performance 60/60；最终成绩以官网为准。

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
| `-O1` | OALL | O1 + O2 + O3 + 通用模式优化 + 指令调度 | **测评服务器使用** |
| `-O0` | O0 | 无优化 | 评测基准 |
| `-o0` | O0 | 无优化 | 本地调试 |
| `-o1` | O1 | 常量折叠 + DCE + 局部 CSE | 本地逐级调试 |
| `-o2` | O2 | O1 + SSA/内联/SCCP/LICM/CFG 等中层优化 | 本地逐级调试 |
| `-o3` | O3 | O1+O2 + 循环交换/强度削减/展开 | 本地逐级调试 |

> **注意**：大写 `-O1` 对应全部通用优化（映射到内部 OALL 级别），小写 `-o1` 对应仅 O1 优化。
> 这一设计是因为测评服务器只支持 `-O1` 选项，我们必须在此选项下输出最佳性能。

## 测试

```bash
# 构建并运行 C++ 单元/集成测试
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# 构建本地 QEMU 回归所需的运行时库
bash scripts/build_sylib.sh

# 运行全部优化（测评服务器级别）
bash scripts/run_tests.sh func O1

# 本地逐级调试
bash scripts/run_tests.sh func o1
bash scripts/run_tests.sh func o2
bash scripts/run_tests.sh all o0
```

批量回归默认执行严格校验：每个用例必须存在对应 `.out`，并同时比较标准输出
和 `main` 的退出值；缺少期望输出或发生超时都会使测试失败。测试期望文件可通过
`.gitignore` 中的例外规则纳入版本控制。
