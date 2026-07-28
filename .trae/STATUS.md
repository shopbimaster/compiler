# 编译器优化开发草纸（STATUS）

> **用途**：如同做题草纸，记录当前未完成任务的思路、进展、结论。**积极更新，旧内容及时清理**。
> **维护原则**：任务完成后将其摘要归入"历史日志"，从"进行中"清除；每轮开发前后更新"当前焦点"。

---

## 一、当前焦点

**分支**：`opt-research-scratch`（基于稳定版本 `017ab02` 创建）
**当前状态**：A 组优化（A1/A2/A4）已实现并验证完毕，**待用户确认是否提交，并选择下一批目标（B 组）**。

### A 组实施结果（2026-07-28 完成）

| 项  | 内容                                                                                   | 结果                                                                     |
| --- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| A1  | 支配树 DFS L/R 区间编码（O(1) 支配判断）+ GVN 全支配合并                               | **收益全部来源**。h-5 -18、many_mat_cal -12、fft/huffman/shuffle -10     |
| A2  | 纯函数识别（PureFuncDetection）+ LICM 提升纯 CALL + LoadElim 跨纯 CALL 保缓存          | 静态数零变化（纯函数形态在 60 性能用例中稀少），零回归，作为能力基建保留 |
| A3  | BFS 乘法 mul_plan                                                                      | **放弃**：BOOM 上 mul 单周期全流水，shift+add 链反而更慢（实测结论冲突） |
| A4  | PassManager 参数化（OPT_DISABLE / OPT_ENABLE 环境变量，含 GVN/LICM/SCCP/CSE/DSE 别名） | 基建，已用于本次归因隔离                                                 |

- **验证**：功能 95/100（3 预存 DIFF + 2 预存超时，与基线一致）；性能 60/60 全过。
- **静态指令数**：20337 → 20130，**净 -207（-1.02%）**。30 用例改善、24 不变、6 微回归（crypto +1、sl +2，均来自 GVN 全支配合并，净收益为正故保留默认）。
- **逃生开关**：`OPT_GVN_IDOM_ONLY=1` 恢复 GVN 旧 idom-only 限制；`OPT_DISABLE=<pass名>` 黑名单 / `OPT_ENABLE=<pass名>` 白名单。
- 附带修复：`scripts/test_perf_wsl.sh` qemu 自动检测（`which qemu-riscv64-static || qemu-riscv64`）。

### 下一步候选（B 组，按推荐顺序）

| 排序 | 候选                                                  | 预估收益 | 风险  | 前置                                          |
| ---- | ----------------------------------------------------- | -------- | ----- | --------------------------------------------- |
| 1    | B3 LoopSimplify/LCSSA/LoopRotate 规范化链             | ★★       | 中    | LoopFind 已有；规范后 LSR/Unroll/IndVars 更稳 |
| 2    | B2 IndVarsSimplify（归纳变量 {0,+,1} + 重写退出条件） | ★★       | 中    | 依赖 B3 + SCEV（已有）                        |
| 3    | B1 LoopUnswitching（不变条件外提，循环复制两版本）    | ★★       | 中    | 依赖 B3                                       |
| 4    | B5 LoopInit / B4 TRE / B6 LIVS                        | ★        | 低-中 | 按用例瓶颈选做                                |

**待用户确认**：是否提交 A 组；下一批选 B3→B2→B1 顺序还是其他。

---

## 二、基线信息

- **稳定版本 commit**：`017ab02 refactor(opt): 泛化基数排序转换条件`
- **分支创建**：`opt-research-scratch` @ 2026-07-28
- **功能测试**：95/100 通过（预存失败：62_percolation / 68_brainfk / 75_max_flow；预存超时 2 个）
- **性能测试**：60/60 通过，静态指令数净 -1.02%（A 组后）
- **已启用关键优化**：GVN（全支配模式）、SCCP、SCEV、LSR、LoopFullUnroll、LICM（纯函数感知）、Mem2Reg、MagicDivision、PeepholeOptimizer 全套融合

### 硬约束（来自 project_memory，勿违反）

- 目标 RV64GC + medany，跑在 FPGA BOOM 软核 + QEMU
- **陷阱 14**：QEMU TCG 下寄存器分配变化常带来回归，当前字典序分配已实测最优，**不要碰寄存器分配器**（带权图着色禁用）
- CodeSink O(N⁴)，大 BB（size>200）须跳过
- Mem2Reg 快速路径须在保守检查之后，且 useBlocks ≤ 3，phiBlocks=0 跳过
- PeepholeOptimizer 在 ret/tail/jr 处必须清所有寄存器跟踪状态
- TargetCodeGen 必须支持 WIDE_SMOD_MUL（opcode 18），FFT 用例依赖

---

## 三、差距分析：调研报告 vs 现有实现

> 调研文档 `Solutions/调研文档.md` 最后更新 2026-07-25。A 组已完成并移出下表。

### 3.1 已实现（核实确认，无需再做）

| 优化                                                | 证据                                                                                                                             |
| --------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| GVN（跨 BB CSE + 代数化简 + **全支配合并**）        | [GVN.cpp](file:///d:/VSCodeProjects/compiler/src/opt/GVN.cpp)，支配树 L/R 编码 O(1) 判断，`OPT_GVN_IDOM_ONLY=1` 可退回 idom-only |
| 支配树 L/R 区间编码                                 | [DominatorAnalysis.cpp](file:///d:/VSCodeProjects/compiler/src/opt/DominatorAnalysis.cpp) `computeDomTreeLR` / `dominatesLR`     |
| 纯函数识别                                          | [PureFuncDetection.cpp](file:///d:/VSCodeProjects/compiler/src/opt/PureFuncDetection.cpp)，LICM/LoadElim 已接入                  |
| PassManager 参数化                                  | [PassManager.cpp](file:///d:/VSCodeProjects/compiler/src/opt/PassManager.cpp)，`OPT_DISABLE`/`OPT_ENABLE` 环境变量               |
| GEPFolding（嵌套 GEP 合并）                         | [GEPStrengthReduce.cpp](file:///d:/VSCodeProjects/compiler/src/opt/GEPStrengthReduce.cpp)                                        |
| LICM 内置 may_alias + 动态 preheader                | [LICM.cpp](file:///d:/VSCodeProjects/compiler/src/opt/LICM.cpp)                                                                  |
| icmp+br → beq/bne                                   | [PeepholeOptimizer.cpp](file:///d:/VSCodeProjects/compiler/src/opt/PeepholeOptimizer.cpp#L884-L1269)                             |
| Peephole 融合套件                                   | self-modify 融合、li+op swap、addi+MEM 偏移融合、li 复用                                                                         |
| SCCP / SCEV / LSR / LoopFullUnroll / ReadOnlyGlobal | 均在 O2/O3 管线                                                                                                                  |
| MagicDivision                                       | 含 SREM 魔数除法                                                                                                                 |
| LoopInterchange / MatrixReductionContraction        | 已实现                                                                                                                           |

### 3.2 未实现（真实差距）

#### B 组：中等复杂度循环优化（无需 IR 重构）— 下一批

| #   | 优化                                  | 来源      | 价值 | 复杂度 | 说明 / 前置                                                                                                                                              |
| --- | ------------------------------------- | --------- | ---- | ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| B1  | **LoopUnswitching**                   | Cpl6      | ★★   | 中     | 循环内不变条件提到循环外，转两版本循环。当前 LICM 不处理条件分支。依赖循环森林（已有 LoopFind）                                                          |
| B2  | **IndVarsSimplify**                   | Cpl3      | ★★   | 中     | 归纳变量规范化为 `{0,+,1}`，重写退出条件。SCEV 已有，需利用 CR 链重写                                                                                    |
| B3  | **LoopSimplify / LCSSA / LoopRotate** | Cpl3      | ★★   | 中     | 循环规范化前置链（单 preheader / 单 latch / exit 被 header 支配 / LCSSA 闭 def-use）。当前 LoopFind 已识别循环但未规范化。规范后 LSR/Unroll/IndVars 更稳 |
| B4  | **TRE 表达式树重排序**                | Cpl4,Cpl8 | ★    | 中     | IR 表达式重排为后端 sh3add/Zba 融合创造机会。当前 Reassociate 仅结合律重排                                                                               |
| B5  | **LoopInit 循环初始化优化**           | Cpl4      | ★    | 低     | 单独优化循环初始化路径（合并到 preheader、消冗余初始化）                                                                                                 |
| B6  | **LIVS 用于优化阶段**                 | Cpl4      | ★    | 中     | 活跃变量信息显式用于 DSE。当前 DSE 基于启发式                                                                                                            |

#### C 组：高价值但需大改 / 高风险（谨慎，单独立项）

| #   | 优化                                                                   | 来源      | 价值 | 复杂度 | 风险 / 前置                                                                                                           |
| --- | ---------------------------------------------------------------------- | --------- | ---- | ------ | --------------------------------------------------------------------------------------------------------------------- |
| C1  | **GCM 全局代码移动**                                                   | Cpl3,Cpl6 | ★★★  | 高     | GVN 配套，移指令到循环浅/支配深位置。需 PHI + 别名分析 + EDefUse + CDG。GVN 已有但缺 GCM 配套，长活跃区间靠图着色消化 |
| C2  | **AliasAnalysis 别名分析**                                             | Cpl3      | ★★★  | 高     | MemLocation 指针传播 + 跨函数 ModRef 摘要。支撑 LICM 提升 LOAD、DSE 精确消除、GVN 跨 BB 内存 CSE。C1/C3 前置          |
| C3  | **MemorySSA**                                                          | Cpl2,Cpl3 | ★★   | 高     | 内存 SSA 形式，使 GVN/GCM 处理内存操作。依赖 PHI                                                                      |
| C4  | **值域分析 rabai**                                                     | Cpl6      | ★★   | 高     | 识别变量值域，精确分支预测 + 强度削弱。当前 SCCP 仅常量传播                                                           |
| C5  | **后端 SSA 机器指令层优化（RV64CSE / BlockLayout / MoveElimination）** | Cpl3      | ★★   | 高     | 在 SSA 机器指令层做 CSE/块布局/move 消除，比汇编文本窥孔精确。当前 PeepholeOptimizer 部分覆盖但非 SSA 框架            |
| C6  | **LoopFusion / LoopTiling**                                            | Cpl1      | ★★   | 高     | 相邻循环融合 / 循环分块。需仿射分析（Omega Test），复杂度极高                                                         |
| C7  | **ComptimePass 编译时求值**                                            | Cpl2      | ★    | 极高   | 模拟执行函数将结果常量化。初始化密集型代码受益                                                                        |
| C8  | **SCCF 强连通分量识别循环**                                            | Cpl8      | ★    | 中     | SCC 天然处理多回边合并，或可替代 NaturalLoop 简化陷阱 10。需评估是否值得替换现有 LoopFind                             |
| C9  | **SCFG 静态控制流图优化**                                              | Cpl8      | ★    | 中     | CFG 高级分析，循环归约/分支链合并                                                                                     |

#### D 组：明确放弃 / 暂不碰

| #   | 优化                           | 原因                                                            |
| --- | ------------------------------ | --------------------------------------------------------------- |
| D1  | **带权图着色寄存器分配**       | 陷阱 14 警告：QEMU TCG 下 RA 变化常带回归，当前字典序已最优     |
| D2  | **Alloca 延迟到首次使用**      | 核实不确定，且与 Mem2Reg/PhiLowering 交互复杂，收益不明         |
| D3  | **冗余参数 alloca/store 移除** | Mem2Reg 已覆盖大部分，专门优化收益小                            |
| D4  | **PHI 简化独立 Pass**          | 当前 PHI 处理分散在 SimplifyCFG/Mem2Reg/PhiLowering，拆分收益小 |
| D5  | **A3 BFS 乘法 mul_plan**       | BOOM 实测 mul 单周期全流水，shift+add 链更慢，负优化            |

---

## 四、推荐实施路线

### 已完成

- ~~第一批 A 组~~：A1 支配树 L/R + GVN 全支配、A2 纯函数识别、A4 PassManager 参数化（2026-07-28，净 -1.02%）

### 第二批（中等复杂度循环）

- **B3 LoopSimplify 前置链** → **B2 IndVarsSimplify**：规范循环后，现有 LSR/Unroll 更稳，IndVars 简化退出条件
- **B1 LoopUnswitching**：在循环规范后实施

### 第三批（独立收益）

- **B5 LoopInit / B4 TRE / B6 LIVS**：小优化，按用例瓶颈选做

### 第四批（大改，单独立项，需充分验证）

- **C2 AliasAnalysis** → **C1 GCM**：GVN 的完整配套，性能上限提升关键
- **C3 MemorySSA**：在 C2 之上
- **C4 值域分析 / C5 后端 SSA / C6 LoopFusion**：长期规划

---

## 五、历史日志

| 日期       | 内容                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-07-28 | 创建草纸。基于稳定版 `017ab02` 建 `opt-research-scratch` 分支。完成调研报告 vs 现有实现差距分析：核实调研文档"规划中"项中 GVN/GEPFolding/LICM may_alias/动态 preheader/icmp+br→beq/Peephole 融合套件 实际已实现；真实未实现项整理为 A/B/C/D 四组，给出推荐路线。                                                                                                                                                                                                                                                                                                                                                                            |
| 2026-07-28 | **A 组完成**。A1 支配树 L/R 编码 + GVN 全支配合并（新增 `computeDomTreeLR`/`dominatesLR`，GVN 默认放开全支配、`OPT_GVN_IDOM_ONLY=1` 逃生）；A2 纯函数识别（新文件 PureFuncDetection.cpp，乐观初始化+迭代降级不动点，LICM 纯 CALL 可外提/不阻塞 LOAD、LoadElim 跨纯 CALL 保缓存）；A4 PassManager 参数化（新文件 PassManager.cpp，OPT_DISABLE/OPT_ENABLE + 别名归一化，兼容旧 OPT_DISABLE_GVN）。A3 放弃（BOOM mul 单周期，shift+add 更慢）。验证：功能 95/100 同基线、性能 60/60、静态指令 20337→20130（-1.02%），收益全部来自 GVN 全支配，微回归 crypto +1/sl +2 可接受。附带修复 test_perf_wsl.sh qemu 自动检测。待提交 + 待选 B 组目标。 |
