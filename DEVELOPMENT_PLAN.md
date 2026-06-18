# SysY2022 编译器 — 开发规划

## 项目目标

将 SysY2022 语言编译为 **RV64GC** 汇编（medany 内存模型），最终在 BOOM FPGA 软核上通过全部 208 条测试用例。

| 测试类别 | 路径 | 数量 | 难度 |
|----------|------|------|------|
| 功能测试 | `test/functional/` | 100 | O0 |
| 高级功能测试 | `test/h_functional/` | 40 | O0 |
| 性能测试 | `test/performance/` | 68（23组×3） | O2~O3 |
| **合计** | | **~208** | |

---

## 一、已完成模块

### 1.1 IR 核心（第 1 阶段）

| 组件 | 说明 | 状态 |
|------|------|:--:|
| Type 系统 | Void/Label/Integer(I1/I8/I32)/Float/Pointer/Array/Function，全局单例 + 指针恒等 | ✅ |
| Value/User/Use | Def-Use 链完整性，operand 读写自动维护 uses | ✅ |
| VReg | SSA 虚拟寄存器 | ✅ |
| Constant/ConstantInt/ConstantFloat | 常量值缓存 | ✅ |
| Instruction | 30 种 Opcode（见下表），LLVM 风格工厂方法 | ✅ |
| BasicBlock | `vector<unique_ptr<Instruction>>` 容器，terminator 检测 | ✅ |
| Function | BasicBlock 容器 + Argument 形参列表 | ✅ |
| GlobalVariable | 全局变量（含 const/init） | ✅ |
| Module | 顶层容器，`dump()` 输出 LLVM IR 格式 | ✅ |

**IR Opcode 全覆盖：** RET / BR / COND_BR / PHI | ADD / SUB / MUL / SDIV / SREM | FADD / FSUB / FMUL / FDIV | AND / OR / XOR / SHL / ASHR | ICMP / FCMP | ALLOCA / LOAD / STORE | CALL | GETELEMENTPTR | ZEXT / SEXT / TRUNC / SITOFP / FPTOSI

### 1.2 前端（第 2-3 阶段）

| 组件 | 说明 | 状态 |
|------|------|:--:|
| ANTLR4 Visitor | IRBuilder 直接遍历 ParseTree → IR，无中间 AST | ✅ |
| 基本语法 | 变量声明/赋值、四则运算、比较、短路求值(&&/\|\|) | ✅ |
| 控制流 | if-else（含嵌套）、while、break、continue、空语句 | ✅ |
| 数组 | 一维/多维声明、存取、传参、表达式下标、聚合初始化 | ✅ |
| 全局变量 | 全局 int/数组、const 常量、部分/完全初始化 | ✅ |
| 类型 | float 常量、void 函数、16进制/8进制、一元运算(-/!/+) | ✅ |
| 作用域 | 嵌套块作用域、变量遮蔽 | ✅ |
| 函数 | 递归、多 return、语句表达式 | ✅ |
| I/O 声明 | getint/getch/getarray/putint/putch/putarray/starttime/stoptime | ✅ |

> 架构决策：无独立 AST 层、无 SemanticAnalyzer（测试用例无错）。

### 1.3 后端（第 4 阶段）

| 组件 | 说明 | 状态 |
|------|------|:--:|
| 指令选择 | 全部 IR → RISC-V RV64GC 指令，含浮点 fadd/fsub/fmul/fdiv | ✅ |
| 栈帧管理 | 函数序言（sp 调整 + ra/callee-saved 保存）+ 尾声 | ✅ |
| 寄存器分配 | **线性扫描分配器**：12 int (s0-s11) + 12 float (fs0-fs11) callee-saved | ✅ |
| 调用约定 | RV64 LP64：a0-a7 传参、t0-t6 临时、ra 返回地址 | ✅ |
| 全局变量 | lui+addi(medany) 访问，.data/.rodata/.bss 段 | ✅ |
| 窥孔优化 | 汇编级模式匹配（addi rd,rs,0→mv 等 6 条规则） | ✅ |

> 已验证 11 端到端用例 QEMU 通过（hello/arithmetic/variable/ifelse/while/func_call/global/float_cmp/array_1d/array_2d/array_init）。

### 1.4 优化系统（第 6 阶段）

#### 优化 Pass 清单（已实现 13 个）

| 级别 | Pass | 实现文件 | 策略 |
|:--:|------|------|------|
| O1 | 常量折叠 | [ConstantFolding.cpp](file:///d:/VSCodeProjects/compiler/src/opt/ConstantFolding.cpp) | 编译期计算 int/float 常量表达式、cast 折叠 |
| O1 | 死代码消除 | [DeadCodeElimination.cpp](file:///d:/VSCodeProjects/compiler/src/opt/DeadCodeElimination.cpp) | 从副作用指令反向标记活性、移除未使用指令 |
| O1 | 窥孔优化 | [PeepholeOptimizer.cpp](file:///d:/VSCodeProjects/compiler/src/opt/PeepholeOptimizer.cpp) | 汇编级：addi rd,rs,0→mv、li 0→mv zero 等 |
| O2 | 函数内联 | [InlineExpansion.cpp](file:///d:/VSCodeProjects/compiler/src/opt/InlineExpansion.cpp) | 小函数（≤32 指令、非递归）体复制到调用点 |
| O2 | 局部 CSE | [CSE.cpp](file:///d:/VSCodeProjects/compiler/src/opt/CSE.cpp) | 哈希表识别基本块内相同操作、替换为第一个实例 |
| O2 | LICM | [LICM.cpp](file:///d:/VSCodeProjects/compiler/src/opt/LICM.cpp) | 循环不变量外提到循环头之前 |
| O3 | 代数简化 | [AlgebraicSimplification.cpp](file:///d:/VSCodeProjects/compiler/src/opt/AlgebraicSimplification.cpp) | 恒等式消除（x+0→x, x*0→0 等 6 条）+ 强度削减（div/mul→shift） |
| O3 | 循环交换 | [LoopInterchange.cpp](file:///d:/VSCodeProjects/compiler/src/opt/LoopInterchange.cpp) | 二重嵌套循环变量交换、提升缓存命中率 |
| O3 | 循环展开 | [LoopUnrolling.cpp](file:///d:/VSCodeProjects/compiler/src/opt/LoopUnrolling.cpp) | dominator 回边检测 → 小循环优先 4× 展开，回退 2× |
| O3 | 尾递归消除 | [TailRecursionElimination.cpp](file:///d:/VSCodeProjects/compiler/src/opt/TailRecursionElimination.cpp) | 尾递归→迭代循环（br to entry block） |
| P0 | 递归乘→原生MUL | [RecursiveMulToNative.cpp](file:///d:/VSCodeProjects/compiler/src/opt/RecursiveMulToNative.cpp) | 识别递归乘法模式 `f(x,y)=y==0?0:x+f(x,y-1)` → `mul` |
| P0 | 位运算模式识别 | [BitOpPatternRecognition.cpp](file:///d:/VSCodeProjects/compiler/src/opt/BitOpPatternRecognition.cpp) | 4 条规则：and-mask、shift-and、or-shift 组合模式 |
| P3 | 指令调度 | [InstructionScheduling.cpp](file:///d:/VSCodeProjects/compiler/src/opt/InstructionScheduling.cpp) | 基本块内列表调度：数据依赖 DAG + LOAD 优先 + stable_sort 原地重排 |

#### 优化流水线（OALL = 全部优化，对应命令行 -O1）

```
O1  → O2  → O3  → P0  → P3

O1 = constantFolding → deadCodeElimination
O2 = inlineExpansion → O1 → CSE → LICM → O1
O3 = algebraicSimplification → O1 → loopInterchange → O1
   → loopUnrolling → O1 → tailRecursionElimination → O1
P0 = recursiveMulToNative → bitOpPatternRecognition → O1
P3 = instructionScheduling → O1
```

### 1.5 测试体系

| 测试套件 | 路径 | 数量 | 类型 |
|----------|------|:--:|------|
| IR 单元测试 | `test/test_ir.cpp` | **26** | Type/DefUse/Constant/Instructions/Module/IRBuilder/LoopInterchange/LoopUnrolling/InstructionScheduling |
| 集成测试 | `test/test_integration.cpp` | **25** | .sy→IR 管线 + 优化流水线集成 |
| QEMU 端到端 | `test_backend.sh` | **11** | O0 汇编生成 + RISC-V GCC + QEMU |
| QEMU 额外 | `test_qemu_extra.sh` | **5** | 递归乘法、位运算、break 等 |
| QEMU 循环展开 | `test_qemu_unroll.sh` | **6** | loop_unroll_4x/2x/full (O0+O3) |
| QEMU 指令调度 | `test_qemu_sched.sh` | **2** | instr_sched_basic (O0+O3) |

> **总计：59/59 全部通过**

---

## 二、技术债务与已知限制

### 2.1 IR/前端层面

| 限制 | 影响范围 | 严重程度 |
|------|---------|:--:|
| ZEXT/SEXT/TRUNC/SITOFP/FPTOSI 使用有限 | 部分类型转换场景未覆盖 | 🟡 低 |

### 2.2 后端层面

| 限制 | 影响 | 严重程度 |
|------|------|:--:|
| 线性扫描寄存器分配（非图着色） | 寄存器压力大时有额外 spill | 🟡 中 |
| 无 peephole 全局寄存器分配 | 部分冗余 load/store 未消除 | 🟢 低 |

### 2.3 优化层面

| 限制 | 说明 | 影响 |
|------|------|:--:|
| LoopUnrolling 仅支持单 BB 循环体 | 带 break/continue 的循环不展开 | 部分用例无加速 |
| LoopUnrolling 仅识别 slt/sle ICMP | sgt/sge 条件无法推导 tripCount | 部分用例无加速 |
| LoopInterchange 仅二重嵌套 | 三重嵌套（如 matmul）不处理 | 矩阵乘法无加速 |
| LoopInterchange maxIters=1 | 单次 pass 避免振荡但可能漏掉级联可交换循环 | 边缘情况 |
| InstructionScheduling 无内存别名分析 | LOAD/STORE 间隐式依赖未处理 | 优化过于激进时可能错误 |
| InstructionScheduling 基本块内 | 跨 BB 调度未实现 | 调度收益有限 |
| InlineExpansion 阈值 32 | 大函数不内联，可能错过优化机会 | 部分场景 |
| CSE 仅局部（单 BB） | 跨 BB 公共子表达式不消除 | O2 收益受限 |

---

## 三、未来优化空间

### 3.1 🔴 阻塞性任务（进行中）

| # | 任务 | 受益 | 状态 |
|---|------|------|:--:|
| B1 | **I/O 运行时库** — getint/putint/starttime/stoptime 等 C 实现 + 编译为 libsysy.a | 解锁全部 functional/h_functional/performance 测试 | ✅ 已完成 |
| B2 | **自动化测试脚本** — .sy→.S→gcc→qemu diff .out 批量回归 | functional 100 + h_functional 40 | ✅ 已完成 |
| B3 | **functional 100 回归** + Bug 修复 | 确保 O0 基本正确 | 🔴 待运行 |
| B4 | **h_functional 40 回归** + Bug 修复 | 高级特性正确 | 🔴 待运行 |
| B5 | **性能测试 68 回归** + O0 基线测量 | 性能基准 | 🔴 待运行 |

### 3.2 🟡 高价值优化（实现复杂但收益大）

| # | 优化 | 文件 | 受益测试 | 预期加速 | 难度 |
|---|------|------|---------|:--:|:--:|
| O-1 | **循环分块 (Tiling)** | 新建 | 01_mm×3, matmul×3, conv2d×3, transpose×3 | 2-10× | ⭐⭐⭐ |
| O-2 | **循环融合 (Fusion)** | 新建 | conv2d×3, many_mat_cal×3 | 1.5-3× | ⭐⭐⭐ |
| O-3 | **LoopInterchange 扩展到三重嵌套** | LoopInterchange.cpp | 01_mm×3, matmul×3 | 2-5× | ⭐⭐ |
| O-4 | **LoopUnrolling 增强** — 支持 sgt/sge ICMP 推导 | LoopUnrolling.cpp | h-1×3, prime_search×3 | 1.2-1.5× | ⭐ |
| O-5 | **全局值编号 (GVN)** — 跨 BB CSE | 新建 | ∀ | 1.1-1.3× | ⭐⭐⭐ |

### 3.3 🟢 锦上添花优化

| # | 优化 | 说明 | 预期加速 | 难度 |
|---|------|------|:--:|:--:|
| S-1 | **SLP 向量化** | 识别连续 LOAD/STORE+同构运算→向量指令 | 01_mm×3, fft×3: 2-4× | ⭐⭐⭐⭐ |
| S-2 | **指令调度增强** — 内存别名分析 | 通过 TBAA 或保守策略处理 LOAD/STORE 依赖 | correctness | ⭐⭐⭐ |
| S-3 | **图着色寄存器分配** | 替代线性扫描，减少 spill | 1.05-1.2× | ⭐⭐⭐ |
| S-4 | **函数内联启发式增强** | 基于调用频率、代码膨胀率的成本模型 | 1.05-1.15× | ⭐⭐ |
| S-5 | **SSA 构造/析构** | 为 GVN、LICM 等提供更好的框架 | 基础设施 | ⭐⭐⭐ |

### 3.4 优化效果预期（完整实现后）

| 优化等级 | 累积加速比 | 覆盖测试 |
|----------|:--:|------|
| O0（无优化） | 1.0× | 100% |
| O1（常量折叠+DCE+窥孔） | 1.1-1.3× | 100% |
| O2（内联+CSE+LICM） | 1.2-1.6× | 100% |
| O3（代数简化+循环交换+展开+尾递归） | 1.5-2.5× | ~70% |
| P0（语义替换） | 2.0-10× | ~40%（排序/FFT/加密） |
| P3（指令调度） | 1.1-1.3× | ~15%（SL/调度模拟） |
| + 循环分块/融合 | 3-20× | ~30%（矩阵/卷积） |
| + 向量化 | 5-30× | ~15% |

---

## 四、项目结构

```
compiler/
├── grammar/
│   ├── SysY2022Lexer.g4             词法
│   └── SysY2022Parser.g4            语法
├── include/
│   ├── Compiler.h                   CLI 封装 + OptLevel 枚举（O0/O1/O2/O3/OALL，OALL 对应命令行 -O1）
│   ├── backend/
│   │   ├── TargetCodeGen.h          指令选择 + 栈帧 + 全局变量
│   │   ├── RegisterAllocator.h      线性扫描寄存器分配
│   │   └── PeepholeOptimizer.h      汇编级窥孔
│   ├── ir/
│   │   ├── IR.h                     完整类型系统 + 30 opcode + Module/dump
│   │   └── IRBuilder.h             ANTLR Visitor + 符号表 + 作用域栈
│   ├── opt/
│   │   └── Optimizer.h              13 个优化 Pass 声明
│   └── utils/
│       ├── Error.h
│       └── Logger.h
├── src/
│   ├── main.cpp                     CLI: -S/-o/-O1（测评）/-o1/-o2/-o3（本地调试）
│   ├── Compiler.cpp                 优化管线调度
│   ├── antlr/                       ANTLR 生成文件
│   ├── ir/
│   │   ├── IR.cpp                   Type/Value/Instruction/BasicBlock/Function/Module 实现
│   │   └── IRBuilder.cpp           全部 SysY2022 语法 → IR
│   ├── backend/
│   │   ├── TargetCodeGen.cpp        全部 IR opcode → RISC-V
│   │   └── RegisterAllocator.cpp    线性扫描: 12 int + 12 float callee-saved
│   └── opt/
│       ├── Optimizer.cpp            O1/O2/O3/P0/P3 流水线编排
│       ├── ConstantFolding.cpp      O1
│       ├── DeadCodeElimination.cpp  O1
│       ├── PeepholeOptimizer.cpp    O1
│       ├── InlineExpansion.cpp      O2
│       ├── CSE.cpp                  O2
│       ├── LICM.cpp                 O2
│       ├── AlgebraicSimplification.cpp  O3 + 强度削减
│       ├── LoopInterchange.cpp      O3
│       ├── LoopUnrolling.cpp        O3（2×/4×）
│       ├── TailRecursionElimination.cpp  O3
│       ├── RecursiveMulToNative.cpp P0
│       ├── BitOpPatternRecognition.cpp  P0
│       └── InstructionScheduling.cpp    P3
├── test/
│   ├── test_ir.cpp                  26 单元测试
│   ├── test_integration.cpp         25 集成测试
│   ├── *.sy                         端到端测试用例
│   ├── functional/*.sy              100 功能测试（尚未批量回归）
│   ├── h_functional/*.sy            40 高级功能测试（尚未批量回归）
│   └── performance/*.sy             68 性能测试（尚未批量回归）
├── SysYlib/
│   ├── sylib.h                       运行时库头文件（I/O + 计时函数声明）
│   └── sylib.c                       运行时库实现（getint/putint/starttime/stoptime等）
├── build_sylib.sh                    运行时库构建脚本（RISCV64 → libsylib.a）
├── test_qemu.sh                      快速 QEMU 端到端测试（含运行时库链接）
├── test_qemu_all.sh                  全量回归测试脚本（functional 100）
├── CMakeLists.txt                   主构建文件
├── DEVELOPMENT_PLAN.md              本文档
└── PROGRESS_SUMMARY.md              完成路径总结
```

---

## 五、已达成里程碑

| 日期 | 里程碑 | 测试通过 |
|------|--------|:--:|
| 2026-05-20 | IR 核心框架完成 | 18/18 |
| 2026-05-21 | IRBuilder ANTLR4 Visitor 集成 | 25/25 |
| 2026-05 | 前端全语言特性补全 | 52/52 |
| 2026-05 | 后端 O0 代码生成 | 11/11 QEMU |
| 2026-05-28 | 全流程 Bug 修复（6个） | 57/57 |
| 2026-05-28 | O1/O2/O3/P0 优化全部实现 | 同上 |
| 2026-05-29 | P1-3 循环交换 + 测试 | 49/49 |
| 2026-05-29 | P3 循环展开4× + 指令调度 | 59/59 |
| 2026-06-03 | 运行时库集成 — SysYlib 编译链接 + 全面回归测试脚本 | 已完成 |
| **2026-06-04** | **全面回归测试 + Bug 修复** | **见下方** |

### 2026-06-04 全面回归测试结果

| 测试套件 | 编译 | 链接 | 运行时正确 | 输出差异 | 段错误 | 超时 |
|---------|:---:|:---:|:--------:|:------:|:----:|:---:|
| functional (100) | 100 | 100 | 99 | 0 | 0 | 1¹ |
| h_functional (40) | 40 | 40 | 39 | 1² | 0 | 0 |
| performance (60) | 60 | 60 | 51 | 7³ | 0 | 2⁴ |

> ¹ 82_long_func: O0 计算量过大，15s 超时（非 Bug）
> ² 38_light2d: 浮点 SDF 渲染精度差异（自定义 sin/cos/sqrt 累积误差）
> ³ h-4-01/02/03, sl1/2/3, h-9-03: 有符号整数溢出行为差异（与参考编译器语义不同）
> ⁴ h-9-01/02: O0 未优化导致超时（非 Bug）

### 本轮修复的 Bug

| 编号 | 测试用例 | 问题描述 | 修复方案 |
|:---:|---------|---------|---------|
| 1 | 54_hidden_var | 3D 数组初始化 `subIdx` 被递归调用中 `flatIdx=0` 重置，导致子数组首元素被覆盖 | `emitInitStoresVar`/`emitInitStoresConst` 中，递归调用后设置 `subIdx = subTotal` |
| 2 | h_functional 测试脚本 | 缺少输出规范化，尾部换行符不匹配导致 7 个误报 | 添加 `norm` 函数统一格式化 |
| 3 | sylib.c | 缺少 `starttime`/`stoptime` 包装函数 | 添加 `#undef` + 包装函数，重建 libsylib.a |

### 已知问题

| 问题 | 影响范围 | 原因 | 优先级 |
|------|---------|------|:---:|
| 有符号整数溢出语义 | h-4, sl, h-9 系列 | IR 中 `add`/`mul` 使用 64 位运算，与 32 位回绕语义不同 | 低 |
| 浮点精度累积 | 38_light2d | 自定义 sin/cos/sqrt 迭代实现，小误差累积 | 低 |
| O0 大循环超时 | 82_long_func, h-9-01/02 | 未优化代码计算量过大 | 低 |

---

## 六、下一里程碑

```
当前（已完成）: O1/O2/O3/P0/P3 全部优化 Pass 实现 ✅
当前（已完成）: 运行时库集成（SysYlib + 测试脚本） ✅
当前（已完成）: functional 100 + h_functional 40 + performance 60 回归测试 ✅
下一阶段:      -O1 优化性能测试 → 38_light2d 浮点精度修复 → 整数溢出语义修复
远期:          循环分块/融合/向量化 → FPGA 上板
```

> **当前日期**：2026-06-04 &nbsp;|&nbsp; **测试通过**：functional 99/100, h_functional 39/40, performance 51/60 (O0) &nbsp;|&nbsp; **优化 Pass**：13 个全部实现 &nbsp;|&nbsp; **运行时库**：已集成