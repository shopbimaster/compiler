# SysY2022 编译器

将 SysY2022 语言编译为 RV64GC 汇编的完整编译器，目标平台为 FPGA BOOM CPU 软核（medany 内存模型）。编译器只启用依赖程序语义和 IR 数据流的通用优化，不得通过函数名、测试名、输入值或测评环境特征触发专用优化。

AI 辅助说明：项目开发使用 OpenAI Codex 辅助合规审计、通用优化实现、正确性诊断、测试和文档整理。所有 AI 生成结果均由参赛队成员理解、人工复核、修改并通过回归测试后纳入项目，参赛队对最终实现和正确性负责。

## 目录

- [宏观架构](#宏观架构)
- [模块依赖链](#模块依赖链)
- [函数功能列表](#函数功能列表)
  - [前端（Frontend）](#前端frontend)
  - [中间表示（IR）](#中间表示ir)
  - [优化器（Optimizer）](#优化器optimizer)
  - [后端（Backend）](#后端backend)
- [测试体系](#测试体系)
- [注释规范](#注释规范)
- [构建与测试命令](#构建与测试命令)

---

## 宏观架构

```
输入 (.sy)
   ↓
[ANTLR4 词法/语法分析]  ← grammar/SysY2022Lexer.g4 + SysY2022Parser.g4
   ↓
[IRBuilder — Visitor 模式直接从 ParseTree 生成 IR]  ← src/ir/IRBuilder.cpp
   ↓
[IR Module]  ← include/ir/IR.h (Value/BasicBlock/Function/Module/Instruction/Type)
   ↓
[优化管线]  ← src/opt/Optimizer.cpp 调度
   ├─ O1 基础安全优化（CF/DCE/CSE）
   ├─ O2 中层优化（内联/SSA/SCCP/LICM/CFG/算术/矩阵分块）
   ├─ O3 循环变换（交换/强度削减/旋转/展开/软件流水/归约分裂）
   ├─ P0 特殊模式识别（递归→原生/位运算模式）
   └─ P3 指令调度
   ↓
[后端代码生成]  ← src/backend/TargetCodeGen.cpp
   ├─ RegisterAllocator（图着色寄存器分配）
   ├─ TargetCodeGen（IR → RV64GC 汇编 + ALLOCA 提升 + PHI 边拷贝）
   ├─ PostRAScheduler（寄存器分配后指令调度）
   └─ PeepholeOptimizer（窥孔优化 + 压缩指令生成）
   ↓
输出 (.s)
```

### 设计原则

- **无独立 AST 层**：直接使用 ANTLR4 Visitor 模式从 ParseTree 生成 IR，简化编译流程（ParseTree → IR → Assembly）。
- **无 SemanticAnalyzer**：假设输入程序无语法/语义错误。
- **分层优化管线**：O1→O2→O3→P0→P3 分阶段调度，每阶段后运行 CF/DCE 清理；阶段间迭代收敛（最多 2 次）。
- **目标导向优化**：针对 BOOM 微架构（16 项 ROB、14 周期分支误预测、单周期全流水 mul、4 周期 load-use）定制软件流水、分支概率布局、归约分裂等硬件协同优化。

---

## 模块依赖链

```
include/ir/IR.h  ← 类型系统与 IR 数据结构（无外部依赖）
       ↑
include/ir/IRBuilder.h  ← 依赖 ANTLR4 运行时
       ↑
include/Compiler.h  ← 编译器门面，调度 IRBuilder + Opt + Backend
       ↑
include/opt/Optimizer.h  ← 所有优化 Pass 声明（依赖 ir/IR.h）
include/opt/LoopAnalysis.h  ← 循环分析合并头（依赖 ir/IR.h）
       ↑
include/backend/TargetCodeGen.h  ← 依赖 ir/IR.h + RegisterAllocator.h
include/backend/RegisterAllocator.h
include/backend/PostRAScheduler.h
include/backend/PeepholeOptimizer.h
       ↑
include/utils/Error.h, Logger.h  ← 通用工具
```

**库依赖关系**（CMakeLists.txt 定义）：

| 库 | 源文件 | 依赖 |
|----|--------|------|
| `sysy_ir_core` | `src/ir/IR.cpp` | 无 |
| `sysy_ir_builder` | `src/ir/IRBuilder.cpp` | `sysy_ir_core` + ANTLR4 |
| `sysy_compiler` | `src/Compiler.cpp` | `sysy_ir_builder` |
| `sysy_opt` | `src/opt/*.cpp`（64 个 Pass + 基础设施） | `sysy_ir_core` |
| `sysy_backend` | `src/backend/*.cpp`（3 个） | `sysy_ir_core` |

`compiler` 可执行文件链接：`sysy_compiler` + `sysy_backend` + `sysy_opt` + ANTLR4。

---

## 函数功能列表

### 前端（Frontend）

| 文件 | 入口函数 | 职责 | 单元测试 | 系统测试 |
|------|----------|------|----------|----------|
| `grammar/SysY2022Lexer.g4` | —（ANTLR4 生成） | SysY2022 词法规则 | — | functional/hfunc/perf 全集 |
| `grammar/SysY2022Parser.g4` | —（ANTLR4 生成） | SysY2022 语法规则 | — | functional/hfunc/perf 全集 |
| `src/ir/IRBuilder.cpp` | `IRBuilder::compile(sourcePath)` | Visitor 模式从 ParseTree 直接生成 IR Module | `test_ir` | functional 全集 |
| `src/main.cpp` | `main()` | 命令行解析 + 调度编译流程 | — | 手动验证 |
| `src/Compiler.cpp` | `Compiler::emitAsmToFile()` | 编译门面：compile → runOptPasses → TargetCodeGen → peephole | `test_integration` | functional 全集 |

### 中间表示（IR）

| 文件 | 核心类型/函数 | 职责 | 单元测试 | 系统测试 |
|------|--------------|------|----------|----------|
| `include/ir/IR.h` / `src/ir/IR.cpp` | `Value`, `Instruction`, `BasicBlock`, `Function`, `Module`, `Type`/`IntegerType`/`FloatType`/`ArrayType`/`FunctionType`, `Constant`/`ConstantInt`/`ConstantFloat`, `Argument`, `GlobalVariable` | IR 类型系统与数据结构 | `test_ir` | 全集 |
| `include/ir/IRBuilder.h` | `IRBuilder` | IR 构造器（Visitor 直生 IR） | `test_ir` | 全集 |

### 优化器（Optimizer）

优化 Pass 按管线阶段分组。每个 Pass 返回 `bool`（是否修改 IR），管线在每步后运行 `constantFolding` + `deadCodeElimination` 清理。开关机制：`OPT_DISABLE`/`OPT_ENABLE` 环境变量控制（见 [构建与测试命令](#构建与测试命令)）。

#### O1 基础安全优化（`runO1`，总是有益、无依赖）

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `constantFolding` | `ConstantFolding.cpp` | 常量折叠（编译期计算） | `test_ir` | functional O0+ |
| `deadCodeElimination` | `DeadCodeElimination.cpp` | 死代码消除（无 use 指令删除） | `test_ir` | functional O0+ |
| `commonSubexpressionElimination` | `CSE.cpp` | 公共子表达式消除（局部 CSE） | `test_ir` | functional O1+ |

#### O2 中层优化（`runO2`，分阶段调度）

**阶段 1 — 结构化变换：**

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `treeShaking` | `TreeShaking.cpp` | 死函数消除（useCount=0 函数删除） | — | functional |
| `modAddRecurrenceStrengthReduce` | `NativeLowering.cpp` | 模加递推循环 → 原生加法+模运算 | — | perf |
| `radixSortLowering` | `RadixSortLowering.cpp` | 基数排序模式 → 原生位提取 | — | perf |
| `redundantIterationElimination` | `RedundantIterationElimination.cpp` | 可证冗余的循环迭代消除 | — | perf |
| `dynamicIdempotentLoopElimination` | `DynamicIdempotentLoopElimination.cpp` | 动态幂等循环迭代消除 | — | perf |
| `recursiveMemoization` | `RecursiveOpt.cpp` | 纯自递归函数 → 记忆化查表 | — | perf |
| `repeatedDivRemToNative` | `NativeLowering.cpp` | 重复除余提取 → 原生位运算 | — | perf |
| `recursiveModularMulToNative` | `NativeLowering.cpp` | 递归模乘 → WIDE_SMOD_MUL 指令 | — | perf (crypto) |
| `bitOpPatternRecognition` | `BitOpPatternRecognition.cpp` | 位运算函数调用 → 原生位指令 | — | perf |
| `powerOfTwoDispatchSimplification` | `PowerOfTwoDispatch.cpp` | 2 的幂次 switch 分派化简 | — | perf |
| `tailRecursionElimination` | `TailRecursionElimination.cpp` | 尾递归 → 循环 | — | functional |
| `earlyReturnToSelect` | `EarlyReturnToSelect.cpp` | if-else-RET → SELECT+RET（单 BB 化） | — | perf |
| `inlineExpansion` | `InlineExpansion.cpp` | 函数内联（热点循环内联 + 多 BB 克隆） | `test_integration` | functional |
| `mem2reg` / `mem2regLocal` | `Mem2Reg.cpp` | SSA 构造（alloca/load/store → PHI）/ 局部 SSA 提升 | `test_ir` | functional |
| `globalVariablePromotion` | `GlobalVariablePromotion.cpp` | 标量全局变量 → 局部 ALLOCA | — | perf |
| `globalConstantPropagation` | `GlobalConstantPropagation.cpp` | 只读全局常量 → 常量替换 | — | perf |
| `deadGlobalStoreElimination` | `DeadGlobalStoreElimination.cpp` | 无读/无逃逸全局的 STORE 消除 | — | perf |
| `inplaceMatrixBlocking` | `InPlaceMatrixBlocking.cpp` | 原地矩阵乘缓存局部性分块 | `test_matrix_blocking` | perf |

**阶段 2 — 指令级化简 + CFG 简化（迭代 2 次收敛）：**

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `instCombine` | `InstCombine.cpp` | 指令合并（代数恒等式 + Store-to-Load 前推） | — | functional |
| `deadStoreElimination` | `DeadStoreElimination.cpp` | 死存储消除 | — | functional |
| `simplifyCFG` | `SimplifyCFG.cpp` | CFG 简化（常量分支折叠 + 不可达块删除 + 空块消除） | — | functional |
| `jumpThreading` | `JumpThreading.cpp` | 跳转线程化（冗余跳转链消除） | — | functional |
| `triangularCopyOptimization` | `TriangularCopyOptimization.cpp` | 下三角拷贝范围裁剪 | — | perf |
| `conditionalMatrixBlocking` | `ConditionalMatrixBlocking.cpp` | 条件矩阵归约列融合 | `test_matrix_blocking` | perf |

**阶段 3 — 算术优化（在值传播之前，产生新常量）：**

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `magicDivision` | `MagicDivision.cpp` | 常量除法 → 乘法 + 移位序列 | — | perf |
| `algebraicSimplification` | `AlgebraicSimplification.cpp` | 代数化简（强度削减 sdiv→ashr + 恒等式消除） | — | functional |
| `reassociate` | `Reassociate.cpp` | 表达式重结合（优化常量折叠机会） | — | perf |
| `loadElimination` | `LoadElimination.cpp` | 冗余 LOAD 消除 | — | functional |

**阶段 4 — 值级别分析 + 传播（迭代 2 次收敛）：**

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `sparseConditionalConstantPropagation` | `SCCP.cpp` | 稀疏条件常量传播（结合分支条件） | — | functional |
| `copyPropagation` | `CopyPropagation.cpp` | 复制传播 | — | functional |

**阶段 5 — 循环优化：**

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `loopInvariantCodeMotion` | `LICM.cpp` | 循环不变量外提（NaturalLoop + innermost-first） | — | perf |

**阶段 6 — 全局清理与最终优化：**

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `ifConversion` | `IfConversion.cpp` | 条件转换（empty-else 菱形 → select） | — | functional |
| `adce` | `ADCE.cpp` | 激进死代码消除（基于后支配者） | — | functional |
| `codeSink` | `CodeSink.cpp` | 代码下沉（减少寄存器压力） | — | perf |
| `basicBlockReordering` | `BasicBlockReordering.cpp` | 基本块重排（支配树 DFS + 分支概率引导布局） | — | perf |
| `globalValueNumbering` | `GVN.cpp` | 全局值编号（支配树跨 BB CSE） | — | perf |
| `stencilInteriorSpecialization` | `StencilInteriorSpecialization.cpp` | 模板内部边界特化 | — | perf |
| `matrixReductionContraction` | `MatrixReductionContraction.cpp` | 矩阵归约收缩 | — | perf |

#### O3 循环特定变换（`runO3`，在 O2 通用优化之后）

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `loopInterchange` | `LoopInterchange.cpp` | 循环交换（优化访存局部性） | — | perf |
| `loopStrengthReduce` | `LoopStrengthReduce.cpp` | 循环强度削弱（MUL → 累加） | — | perf |
| `loopRotation` | `LoopRotation.cpp` | 循环旋转（while → guard + do-while，回边 fall-through 化） | — | perf |
| `loopFullUnroll` | `LoopFullUnroll.cpp` | 循环完全展开（tc ≤ 64） | — | functional |
| `softwarePipelining` | `SoftwarePipelining.cpp` | 软件流水（跨迭代 LOAD 预取，隐藏 load-use 延迟） | — | perf |
| `reductionSplitting` | `ReductionSplitting.cpp` | 归约分裂（多累加器，消除长依赖链） | — | perf |
| `loopUnrolling` | `LoopUnrolling.cpp` | 循环部分展开（最大 16×，tc ≤ 256） | — | perf |
| `gepStrengthReduce` | `GEPStrengthReduce.cpp` | GEP 地址计算强度削弱 | — | perf |

#### P0 特殊模式识别（`runP0`，语义级优化）

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `recursiveMulToNative` | `NativeLowering.cpp` | 递归乘法 → 原生 MUL | — | perf |
| `bitOpPatternRecognition` | `BitOpPatternRecognition.cpp` | 位运算模式识别 | — | perf |
| `hoistRecursiveCallGuards` | `RecursiveOpt.cpp` | 递归调用守卫提升（base-case 短路） | — | perf |

#### P3 指令调度（`runP3`）

| 入口函数 | 文件 | 职责 | 单元测试 | 系统测试 |
|----------|------|------|----------|----------|
| `instructionScheduling` | `InstructionScheduling.cpp` | 指令调度（暴露 ILP + 分支友好布局） | — | perf |

#### 优化基础设施（供其他 Pass 调用）

| 入口函数 | 文件 | 职责 |
|----------|------|------|
| `buildPredecessors`/`buildSuccessors`/`computeDominators`/`computePostDominators`/`computeImmediateDominators`/`computeDominanceFrontier` | `DominatorAnalysis.cpp` | 支配者分析基础设施 |
| `computeDomTreeLR`/`dominatesLR` | `DominatorAnalysis.cpp` | 支配树 DFS L/R 区间编码（O(1) 支配查询） |
| `findNaturalLoops`/`getLoopsInnermostFirst` | `LoopFind.cpp` | 自然循环森林检测 |
| `analyzeLoopInduction` | `SCEVAnalysis.cpp` | 标量演化分析（归纳变量 CR 链） |
| `passEnabled` | `PassManager.cpp` | 参数化 Pass 开关（黑/白名单） |
| `detectPureFunctions`/`isPureFunction` | `PureFuncDetection.cpp` | 纯函数识别（迭代至不动点） |
| `readOnlyGlobalAnalysis` | `ReadOnlyGlobal.cpp` | 只读全局变量分析 |
| `phiLowering` | `PhiLowering.cpp` | PHI 降级（PHI → ALLOCA + STORE + LOAD） |
| `buildAllocaArgumentMap`/`collectPointerAccess`/`analyzeAffineRecurrence`/`analyzeAllocaScalarReduction`/`analyzeCanonicalCountedLoop` | `LoopAnalysis.cpp` | 循环分析合并模块（访存分解 + 仿射递推 + 标量归约 + 计数循环模式） |

### 后端（Backend）

| 文件 | 入口函数/类 | 职责 | 单元测试 | 系统测试 |
|------|------------|------|----------|----------|
| `src/backend/TargetCodeGen.cpp` | `TargetCodeGen::generate(module)` | IR → RV64GC 汇编（ALLOCA 寄存器提升 + GEP 融合 + ICMP-CondBr 融合 + PHI 边拷贝 + 全局地址缓存 + 循环头对齐 + WIDE_SMOD_MUL） | `test_peephole` | functional 全集 |
| `src/backend/RegisterAllocator.cpp` | `RegisterAllocator` | 图着色寄存器分配（callee-saved 管理 + 溢出决策） | `test_register_allocator`（可选） | functional 全集 |
| `src/backend/PostRAScheduler.cpp` | `PostRAScheduler` | 寄存器分配后指令调度（mul 单周期延迟建模） | — | perf |
| `src/opt/PeepholeOptimizer.cpp` | `peepholeOptimize(asmCode)` | 窥孔优化（mv+branch copy propagation + 压缩指令生成 c.li/c.addi/c.mv/c.ld/c.sd/c.j/c.beqz） | `test_peephole` | functional 全集 |

---

## 测试体系

### 单元测试（`tests/`，CMake/CTest 驱动）

| 测试可执行文件 | 源文件 | 覆盖范围 | 运行命令 |
|---------------|--------|----------|----------|
| `test_ir` | `tests/test_ir.cppx` | IR 数据结构 + IRBuilder + Mem2Reg + 基础优化 | `ctest --test-dir build -R ir_unit` |
| `test_integration` | `tests/test_integration.cppx` | 端到端编译流程（源码 → IR → 汇编） | `ctest --test-dir build -R compiler_integration` |
| `test_peephole` | `tests/test_peephole.cppx` | 后端窥孔优化 + TargetCodeGen 安全性 | `ctest --test-dir build -R peephole_unit` |
| `test_matrix_blocking` | `tests/test_matrix_blocking.cppx` | 矩阵分块合法性 + 触发条件 | `ctest --test-dir build -R matrix_blocking_unit` |
| `test_register_allocator` | `tests/test_register_allocator.cpp`（可选） | 寄存器分配回归 | `ctest --test-dir build -R register_allocator_unit` |

```bash
# 构建并运行全部单元测试
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### 系统测试（QEMU 模拟执行）

| 套件 | 目录 | 用例数 | 运行脚本 | 期望基线（-O1/OALL） |
|------|------|--------|----------|---------------------|
| 功能测试 | `test/functional/` | 100 | `bash scripts/run_tests.sh func O1` | 96 OK / 3 DIFF / 1 TO¹ |
| 硬件功能测试 | `test/h_functional/` | 40 | `bash scripts/run_tests.sh hfunc O1` | 36 OK / 4 DIFF² |
| 性能测试 | `test/performance/` | 60 | `bash scripts/test_perf_wsl.sh` | 60/60 PASS |

> ¹ 62_percolation / 68_brainfk / 75_max_flow 在合并基线即失败（非本轮引入），1 个超时为 FPGA 抖动。
> ² 12_DSU / 21_union_find / 30_many_dimensions / 35_math 在合并基线即失败。

**测试运行前准备：**

```bash
# 构建 sylib 运行时库（QEMU 链接所需）
bash scripts/build_sylib.sh
```

**本地逐级调试（小写 -o 精确控制优化级别）：**

```bash
bash scripts/run_tests.sh func o0   # 无优化基准
bash scripts/run_tests.sh func o1   # 仅 O1
bash scripts/run_tests.sh func o2   # O1 + O2
bash scripts/run_tests.sh func o3   # O1 + O2 + O3
bash scripts/run_tests.sh all  o0   # 全部套件 O0
```

---

## 注释规范

### 文件头块（统一格式）

每个源文件（`.cpp`/`.h`）以统一格式的注释头块开头：

```cpp
// ================================================================
// <文件路径> — <一句话职责>
// ----------------------------------------------------------------
// 所属模块：<frontend / ir / opt / backend / utils>
// 关键依赖：<被依赖的头文件 / 被调用的基础设施 pass>
// 环境开关：<OPT_DISABLE / XXX_OFF 等控制项，若无则省略此行>
// 注意事项：<正确性约束 / 已知陷阱，若无则省略此行>
// ================================================================
```

### inline 注释

- **单行注释为主**：使用 `//`（不使用 `/* */` 单行）。
- **`//` 后加一个空格**：`// 注释内容`，而非 `//注释内容`。
- **多行块注释**：使用 `//` 逐行或 `/* */` 块（仅用于大段说明）。
- **领域逻辑用中文**：与现有代码一致，优化策略、微架构约束、正确性陷阱用中文注释。
- **删除被注释掉的死代码**：不保留注释掉的实现。
- **注释与代码间一个空格**：`code; // 注释`。

### 合并文件结构

合并多个同族 Pass 的 `.cpp` 文件时，每节保留独立注释分隔块，标注原文件名与合并说明。由于 C++ 匿名命名空间在同文件内合并，跨节重名的 helper 函数需删除重复定义（保留首个定义，其余节以注释标注"已由第 N 节提供"）。

---

## 构建与测试命令

### 环境要求

- Ubuntu 24.04 x86（WSL 或原生）
- ANTLR4 C++ 运行时 4.13.1（`scripts/setup/setup-ubuntu.sh` 安装）
- `qemu-riscv64`（QEMU 用户态模拟）
- RISC-V 交叉编译工具链（`scripts/install_riscv.sh`，若存在）

### 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 优化级别

测评服务器仅支持 `-O1`，编译器将其映射为最高优化级别（OALL = O1+O2+O3+P0+P3）：

| 命令行参数 | 内部级别 | 包含的优化 | 用途 |
|-----------|---------|-----------|------|
| `-O1` | OALL | O1 + O2 + O3 + P0 + P3 | **测评服务器使用** |
| `-O0` | O0 | 无优化 | 评测基准 |
| `-o0` | O0 | 无优化 | 本地调试 |
| `-o1` | O1 | CF + DCE + CSE | 本地逐级调试 |
| `-o2` | O2 | O1 + 内联 + SSA + SCCP + LICM + CFG + 矩阵分块 | 本地逐级调试 |
| `-o3` | O3 | O1+O2 + 循环交换/强度削减/旋转/展开/软件流水 | 本地逐级调试 |

> 大写 `-O1` 对应全部优化（映射到 OALL），小写 `-o1` 对应仅 O1。设计原因：测评服务器只支持 `-O1`，必须在此选项下输出最佳性能。

### 使用示例

```bash
# 编译为汇编（测评服务器调用方式）
./build/compiler -S -o output.s input.sy -O1

# 输出 IR（调试用）
./build/compiler -o output.ir input.sy -o3
```

### Pass 开关（环境变量）

| 环境变量 | 作用 | 示例 |
|---------|------|------|
| `OPT_DISABLE` | 黑名单：禁用指定 Pass | `OPT_DISABLE="gvn,licm" ./build/compiler -S -o out.s in.sy -O1` |
| `OPT_ENABLE` | 白名单：只运行指定 Pass | `OPT_ENABLE="mem2reg,sccp" ./build/compiler -S -o out.s in.sy -O1` |
| `P6_OFF=1` | 禁用宏指令融合 | `P6_OFF=1 ./build/compiler ...` |
| `P8_OFF=1` | 禁用软件流水（旧） | — |
| `P9_OFF=1` | 禁用跨迭代 LOAD 预取（软件流水） | — |
| `LAYOUT_PROB_OFF=1` | 禁用分支概率引导布局 | — |
| `ROT_ALLOCA_OFF=1` | 禁用 alloca-IV 循环旋转路径 | — |
| `NO_PEEPHOLE=1` | 禁用窥孔优化 | — |
| `DEBUG_LOWER_PHI=1` | 启用 PHI 降级（调试） | — |
| `DUMP_PEEPHOLE_PRE=1` | 输出窥孔优化前汇编到 `/tmp/peep_pre.S` | — |
| `DUMP_PEEPHOLE_POST=1` | 输出窥孔优化后汇编到 `/tmp/peep_post.S` | — |

### 调试脚本

| 脚本 | 用途 |
|------|------|
| `scripts/run_tests.sh <func\|hfunc\|all> <O1\|o0\|o1\|o2\|o3>` | 运行功能/硬件功能测试 |
| `scripts/test_perf_wsl.sh` | 运行性能测试（自动检测 qemu 路径） |
| `scripts/quick_test.sh` | 快速冒烟测试 |
| `scripts/differential_test.sh` | 差分测试（对比不同优化级别输出） |
| `scripts/exp_gvn_ab.sh` | GVN A/B 实验脚本 |
| `scripts/build_sylib.sh` | 构建 sylib 运行时库 |
| `scripts/clean.sh` | 清理构建产物 |
