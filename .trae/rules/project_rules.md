# 项目架构与记忆文档

## 一、项目概述

SysY2022 语言编译器，将 SysY 源码编译为 RISC-V 64 (RV64GC) 汇编。

**编译管线**：`SysY源码 → ANTLR语法解析 → IRBuilder构建IR → 优化Pass管线 → TargetCodeGen生成汇编 → Peephole优化`

## 二、优化级别命令行映射（极其重要！）

**测评服务器仅支持 `-O1` 这一个大写优化选项**。

| 命令行参数 | 内部级别 | 包含的Pass                            | 用途               |
| ---------- | -------- | ------------------------------------- | ------------------ |
| `-O1`      | OALL     | O1 + O2 + O3 + P0                     | **测评服务器使用** |
| `-O0`      | O0       | 无优化                                | 评测基准           |
| `-o0`      | O0       | 无优化                                | 本地调试           |
| `-o1`      | O1       | CF + DCE + CSE                        | 本地逐级调试       |
| `-o2`      | O2       | O1 + 内联 + LICM + 额外CSE            | 本地逐级调试       |
| `-o3`      | O3       | O1+O2 + 代数化简/循环交换/展开/尾递归 | 本地逐级调试       |

**不可违反的规则**：

1. **永远不要修改大写 `-O1` 到 OALL 的映射关系** — 这是测评服务器唯一支持的优化选项
2. 小写 `-o1`/`-o2`/`-o3` 仅用于本地逐级调试
3. 所有面向测评服务器的测试命令应使用 `-O1`（大写）
4. 本地调试时使用小写 `-o1`/`-o2`/`-o3` 逐级定位问题
5. **不要给测评服务器添加 `-O2`、`-O3` 等大写选项** — 服务器不支持

## 三、目录结构

```
compiler/
├── src/
│   ├── main.cpp              # 入口，命令行解析
│   ├── Compiler.cpp          # 编译管线：parse → optimize → emit
│   ├── antlr/                # ANTLR4 自动生成的词法/语法解析器
│   ├── ir/
│   │   ├── IR.cpp            # IR 数据结构：Value/Instruction/BasicBlock/Function/Module
│   │   └── IRBuilder.cpp     # 遍历 AST 构建 IR（Visitor 模式）
│   ├── opt/
│   │   ├── Optimizer.cpp     # 优化 Pass 调度入口（runO1/O2/O3/P0/P3）
│   │   ├── ConstantFolding.cpp
│   │   ├── DeadCodeElimination.cpp
│   │   ├── CSE.cpp           # 公共子表达式消除
│   │   ├── LICM.cpp          # 循环不变量外提
│   │   ├── InlineExpansion.cpp
│   │   ├── AlgebraicSimplification.cpp  # 强度削减 + 恒等式消除
│   │   ├── LoopInterchange.cpp          # 循环交换（带多项安全检查）
│   │   ├── LoopUnrolling.cpp            # 循环展开（最大8×）
│   │   ├── TailRecursionElimination.cpp # 尾递归→循环转换
│   │   ├── InstructionScheduling.cpp    # 分段指令调度（P3，暂禁用）
│   │   ├── BitOpPatternRecognition.cpp  # 位运算模式识别（P0）
│   │   ├── RecursiveMulToNative.cpp     # 递归乘法→原生乘法（P0，暂禁用）
│   │   ├── DominatorAnalysis.cpp        # 支配树分析
│   │   └── PeepholeOptimizer.cpp        # 汇编级窥孔优化
│   ├── backend/
│   │   ├── TargetCodeGen.cpp   # IR → RISC-V 汇编
│   │   └── RegisterAllocator.cpp
│   └── utils/
│       └── Logger.cpp
├── include/                   # 头文件
├── test/
│   ├── functional/            # 100 个功能测试
│   ├── h_functional/          # 40 个隐藏功能测试
│   ├── performance/           # 60 个性能测试
│   └── *.sy                   # 临时测试文件（可清理）
├── Solutions/
│   ├── 调研文档.md             # 优秀编译器范例调研与对比分析
│   ├── Cpl1/                  # 范例1：Gnalc（三层IR，仿射分析，高级循环变换）
│   ├── Cpl2/                  # 范例2：LLVM风格IR（完整SSA，丰富后端优化）
│   └── Cpl3/                  # 范例3：老师菜菜捞捞队（循环森林+SCEV，GVN+GCM，别名分析+MemorySSA，后端SSA优化）
├── scripts/
│   ├── run_tests.sh           # 统一测试入口
│   ├── quick_test.sh          # 快速单用例 O0 vs O3 对比
│   ├── run_func_tests.sh      # 功能测试
│   ├── clean.sh               # 清理构建产物
│   ├── debug/                 # 调试辅助脚本
│   ├── grammar/               # 语法测试脚本
│   └── setup/                 # 环境安装脚本
├── grammar/                   # ANTLR 语法定义
├── SysYlib/                   # SysY 运行时库（sylib.c/h）
├── logs/                      # 测试日志
│   ├── test7.txt / test8.txt / test9.txt  # 近期测评结果
│   └── bisect_test9/          # 临时 bisect 文件（已清理）
├── build/                     # 构建目录（不在版本控制中）
├── CMakeLists.txt
└── .trae/rules/project_rules.md  # 本文档
```

## 四、优化 Pass 详细说明

### 4.1 Pass 调度顺序（借鉴 Cpl3 分阶段调度策略）

调度分为 6 个阶段，阶段 2/4 有迭代收敛（最多 2 次），阶段 4→2 有反馈通道。

**核心设计原则（基于 Cpl3 分析 + 实测验证）**：

1. **算术优化在值传播之前**：MagicDivision/AlgebraicSimplification 产生新常量后，SCCP 才能传播
2. **LICM 在内联之后**：内联后循环体中有新不变量，此时外提收益最大
3. **尾递归在内联之前**：借鉴 Cpl3 构造阶段策略，转循环后可被内联
4. **从最内层到最外层处理循环**：避免内层循环变换影响外层循环结构

```cpp
// O1: 基础优化（LICM 已移至 O2 阶段 5，在内联之后运行）
CF → DCE → CSE → CF → DCE

// O2: 中层优化管线（分阶段调度）
// 阶段 1: 结构化变换
treeShaking
→ bitOpPatternRecognition → CF → DCE
→ tailRecursionElimination → CF → DCE  // 在函数内联之前！转循环后可被内联
→ inlineExpansion → CF → DCE
→ globalVariablePromotion → CF → DCE

// 阶段 2: 指令级化简 + CFG 简化（迭代 2×）
// InstCombine(含Store-to-Load前推) → DSE → SimplifyCFG → 循环
iter ×2: instCombine → CF → DCE → deadStoreElimination → CF → DCE → simplifyCFG → CF → DCE

// 阶段 3: 算术优化（在值传播之前！产生新常量为 SCCP 喂料）
magicDivision → CF → DCE  // 常量除法→乘法+移位，产生新常量
→ algebraicSimplification → CF → DCE  // 强度削减+恒等式，产生新常量
→ reassociate → CF → DCE  // 表达式重结合，产生新常量
→ loadElimination → CF → DCE  // 消除冗余LOAD，简化后续分析

// 阶段 4: 值级别分析 + 传播（在算术优化之后！迭代 2×）
// SCCP → SimplifyCFG(折叠新常量分支) → CopyPropagation → 循环
iter ×2: SCCP → CF → DCE → simplifyCFG → CF → DCE → copyPropagation → CF → DCE

// 阶段 4→2 反馈：值传播后再次运行指令级化简
// SCCP 传播常量后，InstCombine 可能发现新的化简机会
instCombine → CF → DCE → deadStoreElimination → CF → DCE → simplifyCFG → CF → DCE

// 阶段 5: 循环优化（在结构稳定后、所有清理完成后运行）
// LICM 重新启用！使用 NaturalLoop 森林 + innermost-first + 安全检查
loopInvariantCodeMotion → CF → DCE
→ CSE(第二次) → CF → DCE  // LICM 外提代码 + 所有前置变换可能创建公共子表达式

// 阶段 6: 全局清理与最终优化
ifConversion → CF → DCE
→ adce → CF → DCE
→ codeSink → CF → DCE
→ basicBlockReordering

// O3: 循环特定变换（在 O2 通用优化之后运行）
loopInterchange → CF → DCE
→ loopStrengthReduce → CF → DCE
→ gepStrengthReduce → CF → DCE
→ loopFullUnroll → CF → DCE
→ loopUnrolling → CF → DCE

// P0: 特殊模式识别
recursiveMulToNative
→ bitOpPatternRecognition → CF → DCE

// P3: 指令调度
instructionScheduling → CF → DCE
```

### 4.2 当前启用的 Pass 及关键注意事项

| Pass                     | 级别   | 状态 | 关键注意事项                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ------------------------ | ------ | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ConstantFolding          | O1     | 启用 | `sitofp` 折叠成 `ConstantFloat` 后，IR 打印和代码生成均需正确处理 `ConstantFloat`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |
| DeadCodeElimination      | O1     | 启用 | 无已知问题                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| CSE                      | O1/O2  | 启用 | 使用 `replaceAllUsesWith` 后 `erase`，安全                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| LICM                     | O2     | 启用 | 在内联之后运行以捕获更多不变量；使用 NaturalLoop 森林 + innermost-first 处理 + 多项安全检查（isLoopValid、terminator 引用验证）；从 O1 移入 O2 阶段 5                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| InlineExpansion          | O2     | 启用 | 单 BB 小函数（指令数 <20）和多 BB 函数（≤8 BB、≤60 指令）均可内联；多 BB 函数在同一 caller 中被调用超过 2 次跳过内联，避免代码膨胀导致性能下降                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| AlgebraicSimplification  | O2     | 启用 | 强度削减：`sdiv/srem` 除以 2 的幂需要检查左操作数非负才能替换为 `ashr`/`and`；从 O3 移入 O2 阶段 3，在值传播之前运行以产生新常量                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| LoopInterchange          | O3     | 启用 | **三项安全检查缺一不可**：`icmpUsesVar`、`isUsedOutsideBBSet`、`sameLoopBounds`；**外加 `hasOtherLoop` 检查**（外层循环体不能有除当前内层外其他循环）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| LoopUnrolling            | O3     | 启用 | 仅展开迭代次数 ≤64 的简单 while 循环；`cloneNonTermInst` 中 STORE 指针操作数必须通过 `lookup` 查找                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| TailRecursionElimination | O2     | 启用 | 在函数内联之前运行，将尾递归转为循环后可被内联；`findBodyBlock` 将 entry block 拆分为 init 和 body；**幂等设计**：拆分后再次调用直接返回 BR 目标                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| BitOpPatternRecognition  | P0/O2  | 启用 | 模式匹配位运算优化；O2 中提前运行以消除自定义位运算函数，使 read_bits 等函数可被内联                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| GlobalVariablePromotion  | O2     | 启用 | 将标量全局变量提升为局部 ALLOCA；**跳过 const 全局变量**（.rodata 只读段）；**在遍历 BB 前先收集 RET 指令**，避免插入时迭代器失效；init 指令插入到 entry block 开头（ALLOCA 之后）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| RecursiveMulToNative     | P0     | 启用 | 将递归乘法转换为原生 MUL 指令；**修复**：转换后清除空的非 entry BB，避免 CFG 中出现空 BB 导致段错误                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| InstructionScheduling    | P3     | 启用 | **修复**：`scheduleBB` 中 vector::insert 导致迭代器失效的 bug — 先移除 terminator，清空 BB 后按新顺序 pushBack，最后恢复 terminator                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| LoadElimination          | O2     | 启用 | 消除冗余 LOAD 指令；**安全检查**：跳过全局变量（`involvesGlobal`）、跳过跨 BB 被 STORE 的 ALLOCA、仅替换同 BB 内 uses、使用严格指针相等（`a == b`）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| CodeSink                 | O2     | 启用 | 将指令下沉到更靠近使用者的位置；**安全约束**：LOAD 指令不可下沉（可能越过 STORE/CALL 改变内存读写顺序）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| DeadStoreElimination     | O2     | 启用 | 消除无用的 STORE 指令                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| InstCombine              | O2     | 启用 | 代数恒等式简化 + Store-to-Load 转发                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| SimplifyCFG              | O2     | 启用 | 常量分支折叠 + 不可达块删除 + 块合并 + 空块消除                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| CopyPropagation          | O2     | 启用 | 数据流复制传播                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| MagicDivision            | O2     | 启用 | 常量除法转换为乘法+移位序列                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| Reassociate              | O2     | 启用 | 表达式重结合，优化常量折叠机会                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| IfConversion             | O2     | 启用 | 条件转换                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ADCE                     | O2     | 启用 | 激进死代码消除                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| BasicBlockReordering     | O2     | 启用 | 基于支配树的拓扑排序基本块重排，优化 fall-through，确保定义在使用之前                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| TreeShaking              | O2     | 启用 | 移除未使用的函数和全局变量                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| PeepholeOptimizer        | 汇编级 | 启用 | 寄存器分配后运行；包含：零寄存器替换（li rd,0 + sw → sw x0）、sw x0 对合并为 sd x0、mv 链合并、冗余 lw+sw 同址消除、BB 内冗余 li 消除（reg→imm 映射）、**BB 内局部值编号（LVN）**、死 trampoline 清理、j fall-through 优化；**LVN 增强**：①纯 fall-through 标签（未被任何跳转指令引用）保留跟踪，消除跨 fall-through 边的冗余指令（如 shuffle1 beq 后 endif 标签处的冗余 slli）；②`regChangedSince` 用 `>` 而非 `>=` 判断（prevIdx 处的写入是定义点，不算"之后被修改"）；③源寄存器 == prevRd 时认为被修改（rd==rs 指令如 `seqz s2,s2` 改变源寄存器值）；LVN 可通过 `PEEPHOLE_NO_LVN=1` 禁用；`PEEPHOLE_MAX_ITER=N` 控制迭代次数（默认 3）；`DUMP_PEEPHOLE_PRE=1` dump 优化前汇编；`NO_PEEPHOLE=1` 完全禁用 |

### 4.3 关键设计决策

**为什么 LICM 在 O2 阶段 5（内联之后）而非 O1？**
内联后循环体中出现新的不变量（来自被内联函数），此时外提收益最大。修复：使用 NaturalLoop 森林替代 ad-hoc 回边检测，从最内层到最外层处理循环，添加 isLoopValid 和 terminator 引用验证等安全检查防止段错误。

**为什么算术优化（阶段 3）在值传播（阶段 4）之前？**
MagicDivision 产生魔数常量、AlgebraicSimplification 产生移位常量、Reassociate 产生合并常量——这些新常量必须在 SCCP 运行之前产生，SCCP 才能将其传播到分支条件（使 SimplifyCFG 折叠更多分支）和使用点（使 InstCombine 进一步化简）。如果顺序颠倒，SCCP 会错过这些常量。

**为什么 LoopInterchange 需要 `hasOtherLoop` 检查？**
row_reduce（conv2d）的 r 循环体中有两个 c 循环，交换后仅处理第一个内层循环，第二个仍使用原变量导致语义错误。

**为什么 TailRecursionElimination 需要拆分 entry block？**
尾递归的 BR 需要跳转到包含 base case 检查的代码，而非直接跳到返回块。拆分为 init（allocas+参数存储）和 body（base case 检查及后续逻辑）确保尾递归跳转后重新执行条件判断。

**为什么 `sameLoopBounds` 对于非方阵至关重要？**
`array[20][100]` 中 `i<20, j<100` 交换后 `j` 可达 99 → 越界 → SEGFAULT。

**为什么 GlobalVariablePromotion 要跳过 const 全局变量？**
const 全局变量放在 `.rodata` 只读段，退出时 STORE 回写会导致 SEGFAULT。const 全局变量值不变，无需回写。

**为什么 GlobalVariablePromotion 的 init 指令要插入到 entry block 开头？**
原始 LOAD 指令可能在 entry block 中，如果 init 指令插入到 terminator 之前，ALLOCA 定义会在 USE 之后。必须插入到所有 ALLOCA 之后、第一个非 ALLOCA 指令之前。

**为什么 GlobalVariablePromotion 要先收集 RET 再插入？**
在遍历指令列表时调用 `insertBefore` 修改列表会导致迭代器失效，exit LOAD 可能未被正确插入，导致 `%n.exit` 未定义。必须先收集所有 RET 指令，再批量插入。

**为什么 InstructionScheduling 的 scheduleBB 要先移除 terminator？**
指令列表是 `std::vector`，`vector::insert` 会使迭代器失效。原代码保存 terminator 迭代器后多次 insert，第一次 insert 后迭代器即失效导致 UB。修复：先移除 terminator → 清空 BB → 按序 pushBack → 恢复 terminator。

**为什么 CodeSink 不能下沉 LOAD 指令？**
LOAD 指令下沉可能越过 STORE/CALL 指令，改变内存读写顺序。例如：`load %ptr` → `store %val, %ptr` → `use %load`，若将 LOAD 下沉到 STORE 之后，LOAD 会读取到 STORE 写入的新值，而非原始值。

**为什么 RecursiveMulToNative 要清除空的非 entry BB？**
转换后非 entry BB 的指令被清空，但空 BB 仍留在 CFG 中，后续 Pass（如寄存器分配）遍历时遇到空 BB 可能段错误。必须清除这些空 BB。

**为什么 `computeDominators` 要跳过不可达前驱？**
inlineExpansion 可能产生无前驱的孤立块（如 `merge_41`），该块 `br` 到可达块（如 `merge_38`）。若将不可达前驱纳入 dom 交集计算：`dom[merge_41]={merge_41}` ∩ `dom[inline_cont]={12 blocks}` = 空集（因为 `merge_41` 不在那 12 个块中），导致 `dom[merge_38]={merge_38}`（不含 entry），被误判为不可达。这级联影响后继块（`merge_33→merge_28→merge_25`）的 `idom=null`，Mem2Reg rename 跳过这些块，遗留未替换的 LOAD → SEGFAULT（11_BST 根因）。修复：计算前驱 dom 交集时跳过不可达前驱（dom 不含 entry 的前驱），仅计算可达前驱的交集。

**为什么 LoopFind 和 RegisterAllocator 都必须合并相同 header 的自然循环？**
多回边循环（如 `while` 含 `continue`）的每条回边独立产生一个 NaturalLoop，body 仅含从该 latch 可达的块。不合并会导致两个严重 bug：

1. **LICM 误重定向**（19_search 无限循环根因）：LICM 处理 NaturalLoop1 时，NaturalLoop2 的 latch 不在 body1 中，被误判为"循环外前驱"并重定向到 preheader，破坏循环计数器 PHI → 无限循环。LoopFind 的合并使 LICM 看到完整 body。
2. **寄存器分配活跃区间不完整**（01_mm1 SEGFAULT 根因）：RegisterAllocator 的循环感知扩展为每条回边独立构建循环体。LICM 外提的不变量 `%t47` 定义在 preheader、使用在 `while_body_16`（仅在 body2 中）。扩展只将 lastSeen 扩到 body2 的 maxLoopId（while_end_17），未覆盖 `then_12`（body1 的 latch，ID 更大）。寄存器分配器误认为 `%t47` 在 `then_12` 不活跃，将 `%t24` 也分配到 s3 → 覆写 `%t47` → SEGFAULT。修复：RegisterAllocator 的循环体构建也必须合并相同 header 的循环。

## 五、编译与测试命令

```bash
# 编译（在 WSL 中）
cd build && make -j$(nproc)

# 全量测试（测评服务器级别）
bash scripts/run_tests.sh all O1

# 分套测试
bash scripts/run_tests.sh func O1    # 100 个功能测试
bash scripts/run_tests.sh hfunc O1   # 40 个隐藏功能测试
bash scripts/run_tests.sh perf O1    # 60 个性能测试

# 快速冒烟测试
bash scripts/run_tests.sh quick

# 本地逐级调试
bash scripts/run_tests.sh func o1    # 仅 O1
bash scripts/run_tests.sh func o2    # O1 + O2
bash scripts/run_tests.sh func o3    # O1 + O2 + O3

# 单用例快速对比 O0 vs O3
bash scripts/quick_test.sh <case-name>

# 生成 IR 或汇编
./build/compiler -o3 input.sy -o output.ir          # 生成 IR
./build/compiler -S -o3 input.sy -o output.S        # 生成汇编
```

## 六、调试工作流

1. **定位错误级别**：用 `-o1`/`-o2`/`-o3` 逐级运行，对比 O0 输出哈希找到引入错误的级别
2. **定位 Pass**：在 `Optimizer.cpp` 中二分注释/取消注释 Pass
3. **对比 IR**：`diff <(./compiler -o2 in.sy) <(./compiler -o3 in.sy)` 查看 O3 增加的变换
4. **QEMU 运行**：`bash scripts/run_tests.sh perf O1` 自动编译+链接+运行+对比

## 七、测试结果历史

| 测评   | 日期    | 结果                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ------ | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| test7  | 2026-06 | 27 WA，LoopInterchange 首版引入                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| test8  | 2026-06 | 21 WA + 5 TLE，LoopInterchange 修复未编译进二进制                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| test9  | 2026-06 | 9 WA，旧二进制测试；本地全量 200/200 通过                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| test10 | 2026-06 | 本地全量 200/200 通过（含 GlobalVariablePromotion）；conv2d/many_mat_cal/knapsack_naive 从超时→通过；huffman 仍超时（~96s）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| test12 | 2026-06 | 旧二进制测试（huffman TLE, conv2d 116s 等）；当前二进制全量 200/200 通过，LICM 修复后 huffman 107ms，conv2d-1 807ms                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| test13 | 2026-07 | 修复 DominatorAnalysis 不可达前驱 bug（11_BST SEGFAULT）；重启 P3 InstructionScheduling；本地全量 199/200 通过（1 TIMEOUT=04_break_continue 预存问题）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| test14 | 2026-07 | 修复 Module 析构 heap-use-after-free（82_long_func）；修复 LoopFind 循环合并（19_search 无限循环）；修复 RegisterAllocator 循环体合并（01_mm1/2/3 SEGFAULT）；本地全量 199/200 通过（1 TIMEOUT=62_percolation 预存问题）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| test15 | 2026-07 | 62_percolation TIMEOUT 被 RegisterAllocator 循环体合并修复一并解决（根因相同：多回边循环 live interval 未覆盖所有 latch）；**本地全量 200/200 通过**（func 100, hfunc 40, perf 60，0 DIFF/SEGFAULT/TIMEOUT）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           |
| test16 | 2026-07 | 修复 04_break_continue TIMEOUT（DCE 清理 BB 中第一条 terminator 后的死代码 br）；修复 62_percolation PHI 活跃区间 bug（PHI firstSeen 设为 blockMinId）；03_sort2 性能 5217ms→108ms（小函数豁免内联限制）；**本地全量 200/200 通过**                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    |
| test17 | 2026-07 | 汇编级优化：emitSelect 分支预测失败转化为无分支掩码算术（36/36 SELECT 转换为 neg/seqz+and/or）；PeepholeOptimizer 新增 `li rd,0; sw/sd rd` → `sw/sd x0`（使用硬连线零寄存器）；**本地全量 200/200 通过**；性能与基线持平（QEMU TCG 噪声 ±100ms 主导）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  |
| test18 | 2026-07 | PeepholeOptimizer 大幅强化：①sw x0 对合并为 sd x0（676 次，数组清零优化）；②sh x0 对合并为 sw x0；③mv 链合并（mv rd,rs; mv rd2,rd → mv rd2,rs）；④冗余 lw+sw 同址消除；⑤BB 内冗余 li 消除（维护 reg→imm 映射，消除 MagicDivision 魔数重复加载，819 条）；⑥死 trampoline 清理（删除重定向后的死跳板，2611 个）；⑦j fall-through 扫描跳过空 label；**总指令数 288,565→282,300（-6,265，-2.17%）**；**本地全量 200/200 通过**                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| test19 | 2026-07 | ①PeepholeOptimizer 新增 BB 内局部值编号（LVN）：追踪 (opcode\|源操作数) → (行索引, 目的寄存器)，相同指令且操作数未变时消除（rd 相同删除，rd 不同替换为 mv）；CACHEABLE 覆盖算术/逻辑/移位/比较/LOAD 指令；STORE 清 LOAD 缓存、CALL 清 LOAD 缓存并杀 caller-saved (t*, a*, ra)、标签清空所有跟踪；指令数 283,755→283,641（-114）。②**修复编译过程非确定性**：RegisterAllocator `unordered_map<IR::Value*, int> firstSeen` 迭代顺序基于指针地址哈希（ASLR 跨运行不同），`std::sort` 仅按 `start` 排序导致相同 `start` 的 interval 相对顺序非确定性 → 寄存器分配每次运行结果不同 → 汇编每次运行不同。修复：比较函数添加 `end` 和 `value->getName()` 作为 tiebreaker。验证：5 个测试用例两次运行 diff 均为 0，性能测试 ±1ms 稳定。③**TargetCodeGen emitBinOp imm12 立即数优化扩展**：原仅 ADD/SUB 有 imm12 优化（addiw），扩展到 AND/OR/XOR（andi/ori/xori）、SHL/ASHR（slliw/sraiw，移位量 0-31）、MUL 2 的幂（slliw）；操作数 0 的交换情况也同步扩展（ADD/AND/OR/XOR/MUL）；emitIcmp 的 eq/ne 与非零 imm12 常量优化（addi+seqz/snez 代替 li+sub+seqz/snez）；**总指令数 283,641→280,738（-2,903，-1.02%）**；shuffle1 性能 1609ms→1510ms（-99ms，mul→slliw 在热点循环）；其他用例持平；**本地全量 200/200 通过**                                                                                                                                                                                                                                                         |
| test20 | 2026-07 | **PeepholeOptimizer LVN 三项关键修复与增强**：①**修复 LVN 长期 bug**：`regChangedSince` 用 `>=` 导致 `regChangedSince(prevRd, prevIdx)` 总是返回 true（因为 `regLastWritten[prevRd] == prevIdx`），LVN 对被缓存指令的 rd 冗余检测从未生效。改为 `>` 后 shuffle1 slli 50→32（-18，-36%），热点循环 9 条→8 条。②**纯 fall-through 标签保留跟踪**：预处理统计被跳转指令（j/beq/bne/beqz 等）引用的标签，引用计数为 0 的标签是纯 fall-through（只有一个前驱，寄存器值与前一条指令执行后一样），LVN 遇到此类标签不清空 lastSeen/regLastWritten，可消除跨 fall-through 边的冗余指令。典型场景：shuffle1 中 `beq` 后 fall-through 到 endif 标签，slli t1,t4,2 在 beq 前后重复（t1/t4 未变），保留跟踪后可消除。③**源寄存器 == prevRd 安全检查**：rd==rs 指令（如 `seqz s2, s2` 即 `s2 = (s2==0)`）改变源寄存器值，后续相同 key 指令（如 `seqz t0, s2`）使用的源操作数不同，不是冗余。缺少此检查导致 03_sort1/2/3 全部 TIMEOUT（`seqz t0,s2` 被错误替换为 `mv t0,s2`，改变条件判断语义 → 无限循环）。④**lw+mv "j 后死亡"优化不安全（已禁用）**：`j label` 跳转的目标 BB 可能使用 ldRd，PeepholeOptimizer 无法做全局活跃变量分析。10_DFS SEGFAULT 根因。永久禁用，注释保留。**清理实验脚本**：删除 33 个 scripts/ 顶层实验脚本、53 个 scripts/debug/ 临时调试脚本、22 个 test/ 根目录临时 IR/S 文件、34 个 logs/ 调试日志（保留 test7-test14 官方结果）、60+ 个根目录临时脚本和 IR/S 文件；移除 PeepholeOptimizer.cpp 中未使用的 `#include <cstdio>`；**本地全量 200/200 通过** |

## 八、调研文档规则

**`Solutions/调研文档.md`** 是项目的重要参考文档，记录了所有已调研的优秀编译器范例、横向对比分析和优化建议。

**必须遵守的规则**：

1. **每次引入新范例（Solutions/CplN）后**，必须更新调研文档，新增该范例的详细分析
2. **每次参照范例改进项目后**，必须更新调研文档的"更新记录"和对应章节，标记已实施的优化
3. **调研文档的结构**：
   - 范例概览表（一）
   - 各范例详细分析（二）
   - 横向对比：公共基础设施（三）
   - 横向对比：优化思路（四）
   - 建议实施路线（五）
   - 更新记录（六）
4. 横向对比部分应保持四列（Cpl1 / Cpl2 / Cpl3 / 我们）或更多列，清晰展示差异
5. 建议实施路线按优先级分阶段，每个优化标注来源范例
6. **阶段性的调研成果（如具体范例分析、横评对比、实施建议）只应写入调研文档，不应写入本规则文件**，避免稀释全局规则
7. 当前已调研的范例：Cpl1（Gnalc）、Cpl2、Cpl3（老师菜菜捞捞队），详见 `Solutions/调研文档.md`

## 九、需要注意的陷阱

1. **IR 打印**：`ConstantFloat` 在 `Module::dump()` 的通用操作数打印路径（非 `loadToReg` 路径）中需要特殊处理，否则显示为 `%` 而非实际值
2. **STORE 指令**：`cloneNonTermInst` 中 STORE 的指针操作数必须通过 `lookup` 映射查找，否则指向旧 BB 的 ALLOCA
3. **BR vs COND_BR**：entry block 拆分后 terminator 变为无条件 BR，`findBodyBlock` 通过此特性实现幂等
4. **循环交换前提**：必须同时满足 4 项检查（`icmpUsesVar` + `isUsedOutsideBBSet` + `sameLoopBounds` + `hasOtherLoop`），缺一不可
5. **代数化简**：`sdiv/srem` 强度削减为 `ashr`/`and` 时，必须确保左操作数非负（有符号数右移除法和取模语义不同）
6. **测评服务器**：仅支持 `-O1`，所有优化必须通过此选项触发
7. **测试脚本**：run_tests.sh 中的 `SYLIB_A` 路径指向 `${BUILD_DIR}/libsylib.a`，由 CMake 构建
8. **QEMU 超时**：functional 测试超时 5s，h_functional/perf 超时 15s。早期 93_nested_calls / 04_break_continue / 62_percolation 曾 TIMEOUT，均已修复（根因分别是寄存器分配和循环体合并）
9. **支配者分析不可达前驱**：`computeDominators` 计算前驱 dom 交集时必须跳过不可达前驱（dom 不含 entry 的前驱）。inlineExpansion 可能产生无前驱的孤立块，该块 br 到可达块时，若纳入交集计算会导致交集为空，使可达块被误判为不可达，级联影响后继块的 idom=null，Mem2Reg rename 跳过这些块，遗留未替换的 LOAD → SEGFAULT（11_BST 根因）
10. **多回边循环必须合并**：`while` 含 `continue` 会产生多条回边，每条回边独立构建的 NaturalLoop body 不完整。**LoopFind 和 RegisterAllocator 两处都必须合并相同 header 的循环体**。不合并会导致：(a) LICM 将其他回边的 latch 误判为 outsidePred 并重定向 → 无限循环（19_search）；(b) 寄存器分配器活跃区间扩展不完整 → LICM 外提的不变量被循环内指令覆写 → SEGFAULT（01_mm1）
11. **Module 析构顺序**：`Module::~Module()` 必须在 `functions` 向量销毁之前清空所有指令操作数。CALL 指令引用 Function 对象（callee），BR/COND_BR/PHI 引用 BasicBlock 对象。若不清空，functions 向量逆序销毁时，先释放的 Function 的 use-list 被后销毁的 CALL 访问 → heap-use-after-free（82_long_func 根因）
12. **BB 中多条 terminator**：IRBuilder 在 break/continue 后不创建新 BB，导致同一 BB 中出现多条 br 指令（如 `break;continue;` 生成两条 br）。`getTerminator()` 返回 `insts.back()`，会错误地选择最后一条 br 作为 terminator，导致 `buildPredecessors` 构建错误的 CFG，进而使 mem2reg 在循环 header 创建自引用 PHI → 无限循环（04_break_continue 根因）。修复：DCE 在删除死代码前先遍历每个 BB，删除第一条 terminator（BR/COND_BR/RET）之后的所有指令
13. **PHI 活跃区间计算**：寄存器分配器基于指令 ID 计算活跃区间，但 PHI 在 BB 入口处"并行"执行，语义上在所有非 PHI 指令之前。如果 PHI 在 IR 中不在 BB 开头（可能在 mul/add 之后），基于指令 ID 的活跃区间会错误地认为 PHI 和前面的指令不重叠，导致它们被分配到同一寄存器，mul 覆盖 PHI 的值 → 无限循环（62_percolation 根因）。修复：PHI 的 firstSeen 设为 blockMinId（BB 入口）
14. **QEMU TCG 寄存器映射约束（极其重要！）**：QEMU TCG 将 RISC-V 的 12 个 callee-saved（s0-s11）映射到 x86 仅有的 6 个 callee-saved（rbx, rbp, r12-r15）。**任何改变寄存器分配的优化都会导致 QEMU 性能回归**，即使该变化"更正确"。例如修复 `std::set<std::string>` 字典序 bug（导致 s10/s11 在 s2-s9 之前分配）改为按声明顺序优先使用 s0-s5，会使 shuffle1 +101ms、h-5 系列 +102ms 回归。**字典序分配顺序（s0,s1,s10,s11,s2,...,s9,t3-t6）经实测验证为 QEMU 最优状态，不要修改**。安全方向：只使用 t0/t1/t2 暂存寄存器（不在 INT_REGS 池中）的汇编级优化，或 PeepholeOptimizer（在寄存器分配之后运行）
15. **emitSelect 无分支优化**：SELECT 指令（来自 IfConversion）的 cond 总是 0/1（来自 ICMP/SEQZ/SNEZ）。整型 SELECT 若某操作数为常量，可用掩码算术（neg/seqz + and/or）替代分支。模式 A/C（var,0/0,var）：cond 在 neg/seqz 后死亡，2 个暂存寄存器足够。模式 B/D（1,var/var,1）：cond 需存活到最终 or/xori，需 3 个暂存寄存器；当 cond 和 var 都需加载时（仅 2 个暂存可用），退化为分支模式
16. **PeepholeOptimizer 的零寄存器优化**：RISC-V 的 x0 是硬连线零寄存器，读取总是 0，写入被丢弃。`li rd, 0; sw/sd/sh/sb rd, offset(base)` 可优化为 `sw/sd/sh/sb x0, offset(base)`，省略 li 指令。QEMU 安全（运行在寄存器分配之后）
17. **编译过程非确定性（极其重要！）**：RegisterAllocator 中 `unordered_map<IR::Value*, int> firstSeen` 的迭代顺序基于指针地址哈希（ASLR 跨运行不同）。若 `std::sort` 比较函数仅有 `start` 字段，相同 `start` 的 interval 相对顺序非确定性 → `linearScan` 按此顺序分配寄存器 → 每次编译产生的汇编不同。**修复**：比较函数添加 `end` 和 `value->getName()` 作为 tiebreaker，确保排序完全确定。此 bug 影响所有性能测试的可复现性（同一二进制两次运行汇编 diff 可能 >100 行）。排查方法：`for i in 1 2; do ./build/compiler -O1 input.sy -o /tmp/a$i.S; done; diff /tmp/a1.S /tmp/a2.S`。注意：IR 本身是确定性的（两次 IR diff 为 0），非确定性仅来自寄存器分配器
18. **PeepholeOptimizer LVN 的 CALL/STORE 失效规则**：LVN 缓存 LOAD 结果时，遇到 STORE 必须清空 LOAD 缓存（STORE 可能改写内存地址，缓存值不再有效）；遇到 CALL 必须清空 LOAD 缓存并杀死 caller-saved 寄存器（t*, a*, ra）中的所有缓存项（CALL 可能改写内存并破坏 caller-saved 寄存器）；遇到标签必须清空所有跟踪（控制流汇合点）。若不清空会导致使用过期值 → 语义错误
19. **QEMU 性能测量必须多次运行取最小值**：QEMU TCG 首次运行会有翻译缓存冷启动开销（可能 +100-200ms）。例如 shuffle1 首次运行 1711ms，后续稳定在 1510ms。测量性能时至少运行 3 次取最小值，避免冷启动噪声。对比优化前后性能时，必须在相同条件下（同一会话、相同系统负载）测量，避免跨会话比较（系统状态变化可能导致 ±100ms 波动）
20. **TargetCodeGen imm12 立即数优化的语义正确性**：AND/OR/XOR 用 64 位立即数变体（andi/ori/xori）与 64 位寄存器形式（and/or/xor）+ li 语义一致——imm12 符号扩展到 64 位，与先 li（符号扩展）再 64 位 and/or/xor 结果相同。SHL/ASHR 用 32 位立即数变体（slliw/sraiw）与 32 位寄存器形式（sllw/sraw）+ li 语义一致。MUL 2 的幂用 slliw 与 mulw 语义一致（32 位乘法取低 32 位符号扩展）。注意 SHL/ASHR 移位量必须 0-31（imm12 可能 > 31，需检查），MUL 必须是 2 的幂（imm > 0 且只有一个 bit）
21. **PeepholeOptimizer LVN `regChangedSince` 的 `>` vs `>=`（极其重要！）**：`regChangedSince(reg, idx)` 判断 reg 自 idx 以来是否被修改。`idx` 是 prevIdx（之前缓存该指令的位置），`regLastWritten[reg] == prevIdx` 表示 reg 在 prevIdx 处被写入——这是指令自身的定义点，是预期的，不算"之后被修改"。**只有 `regLastWritten[reg] > prevIdx` 才说明 reg 在 prevIdx 之后被其他指令覆写**。错误使用 `>=` 会导致 `regChangedSince(prevRd, prevIdx)` 总是返回 true（因为缓存指令时 `regLastWritten[prevRd] = prevIdx`），LVN 的冗余检测对被缓存指令的 rd 从未生效——所有缓存指令都被误判为"rd 已被修改"而无法消除冗余。排查方法：设置 `PEEPHOLE_DEBUG_LVN=1` 查看 LVN 决策日志，若所有冗余查找都显示 `regChanged(prevRd)=1` 则是此 bug
22. **PeepholeOptimizer LVN 的 rd==rs 安全问题**：当指令的目的寄存器和源寄存器相同（如 `seqz s2, s2` 即 `s2 = (s2==0)`、`addi t0, t0, 1` 即 `t0 = t0+1`），指令执行后源寄存器值被改变。后续相同 key 的指令（如 `seqz t0, s2`）使用的源操作数是修改后的值，不是冗余的。**仅靠 `regChangedSince` 检查不够**：因为 `regLastWritten[s2] == prevIdx`（被缓存的 `seqz s2,s2` 在 prevIdx 处写入 s2），`>` 判断认为 s2 自 prevIdx 以来未被修改（因为没有其他指令在 prevIdx 之后写 s2）。必须在源寄存器检查中显式添加 `if (r == prevRd) { srcChanged = true; break; }`。典型故障：03_sort1/2/3 全部 TIMEOUT，`seqz t0,s2` 被错误替换为 `mv t0,s2`（使用了 `seqz s2,s2` 执行前的 s2 值），改变了条件判断语义 → 无限循环
23. **PeepholeOptimizer LVN 纯 fall-through 标签的安全识别**：标签分为两类——**被跳转指令引用的标签**（j/beq/bne/beqz/bnez/blt/bge/bltu/bgeu/blez/bgez/bltz/bgtz 的目标）和**纯 fall-through 标签**（引用计数为 0，只有一个前驱即上一条指令的 fall-through）。前者是控制流汇合点，可能从其他路径跳来，必须清空所有 LVN 跟踪；后者寄存器值和前一条指令执行后一样，lastSeen/regLastWritten 仍有效，可保留跟踪以消除跨 fall-through 边的冗余指令。**预处理必须扫描所有跳转指令的目标**构建 `referencedLabels` 集合，LVN 遇到标签时检查是否在集合中。漏掉任何跳转指令类型（如漏掉 `bnez`）会导致误将"被引用的标签"当作"纯 fall-through"保留跟踪 → 使用过期值 → 语义错误
24. **PeepholeOptimizer 不能做"j 后死亡"优化（极其重要！）**：`lw t3,0(t0); mv t4,t3; j .Lcond` 中 t3 在 j 之后看似死亡（j 不读取 t3 且不 fall-through），但 **`j .Lcond` 跳转的目标 BB 可能使用 t3**（PeepholeOptimizer 无法做全局活跃变量分析）。10_DFS SEGFAULT 根因：j 目标 BB 使用了 ldRd，但 PeepholeOptimizer 误以为 ldRd 在 j 后死亡，删除了 lw 指令 → 后续读取未初始化寄存器 → SEGFAULT。**结论**：PeepholeOptimizer 是局部优化，只能基于 BB 内的信息做决策；任何涉及跨 BB 活跃性判断的优化（如"j 后死亡"）必须先做全局活跃变量分析或保持保守（认为 ldRd 在 j 后存活）。永久禁用此优化，注释保留作为前车之鉴
25. **CSE 禁用 GEP 的根因（QEMU 寄存器分配约束的延伸）**：CSE 合并 GEP 会改变寄存器分配（lsr.ptr 减少导致其他值分配到不同寄存器），即使汇编指令数减少 32%（728→497）、内存操作减少，QEMU 性能仍回归 +200ms。根因同陷阱 14：QEMU TCG 将 RISC-V 的 12 个 s 寄存器映射到 x86 仅 6 个 callee-saved，寄存器分配变化导致 host 寄存器映射变差。**GEPStrengthReduce 内部已有 lsrCache 对相同 base/iv 的 GEP 去重**，CSE 对 LSR 无额外收益。结论：不改变寄存器分配的优化才安全，CSE 对 GEP 必须返回 false（CSE.cpp line 67）
