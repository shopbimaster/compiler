# 编译器优化开发草纸（STATUS）

> **用途**：如同做题草纸，记录当前未完成任务的思路、进展、结论。**积极更新，旧内容及时清理**。
> **维护原则**：任务完成后将其摘要归入"历史日志"，从"进行中"清除；每轮开发前后更新"当前焦点"。

---

## 一、当前焦点

**分支**：`opt-research-scratch`（A 组已提交 `b24d9ab`）
**当前状态**：B 组深度分析完毕，正在尝试 **GEPStrengthReduce 多候选放宽实验**（B2 类）。

### A 组实施结果（2026-07-28 完成，已提交 b24d9ab）

| 项  | 内容                                                                                   | 结果                                                                     |
| --- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| A1  | 支配树 DFS L/R 区间编码（O(1) 支配判断）+ GVN 全支配合并                               | **收益全部来源**。h-5 -18、many_mat_cal -12、fft/huffman/shuffle -10     |
| A2  | 纯函数识别（PureFuncDetection）+ LICM 提升纯 CALL + LoadElim 跨纯 CALL 保缓存          | 静态数零变化（纯函数形态在 60 性能用例中稀少），零回归，作为能力基建保留 |
| A3  | BFS 乘法 mul_plan                                                                      | **放弃**：BOOM 上 mul 单周期全流水，shift+add 链反而更慢（实测结论冲突） |
| A4  | PassManager 参数化（OPT_DISABLE / OPT_ENABLE 环境变量，含 GVN/LICM/SCCP/CSE/DSE 别名） | 基建，已用于本次归因隔离                                                 |

- **静态指令数**：20337 → 20130，净 -207（-1.02%）。功能 95/100、性能 60/60 同基线。
- **逃生开关**：`OPT_GVN_IDOM_ONLY=1`；`OPT_DISABLE=<pass名>` / `OPT_ENABLE=<pass名>`。

### B 组深度分析结论（2026-07-28，基于代码+汇编实证）

**基线静态指令数：20130（已复核）**。最大用例：huffman 697、sort 684、conv2d 639、crypto 614、fft 470、shuffle 383。

| #   | 候选               | 代码实证结论                                                                                                                                                                          | 裁决     |
| --- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- |
| B1  | LoopUnswitching    | 测试用例少有循环不变条件分支。conv2d `if(rr>=0&&rr<N_eff...)` 中 rr 随迭代变；01_mm1 `if(A[i][k]==1)` 中 A[i][k] 随 i 变；shuffle0 `if(k>100)` 在非紧计算循环内。无干净 unswitch 机会 | 跳过     |
| B2  | IndVarsSimplify    | SCEV 已识别 {start,+,step} PHI 链（SCEVAnalysis.cpp L159-200）；LSR/GEPStrengthReduce/LoopUnroll 均消费 SCEV。**IndVars 核心已被覆盖**。但发现 LSR 实质失效（见下）                   | 部分跳过 |
| B3  | LoopSimplify/LCSSA | LICM 已按需创建 preheader（仅当有非 header 不变量）；SimplifyCFG.foldSinglePredBlock（L423）会折叠空 preheader。规范链无直接静态收益，仅基建。LoopFind 已合并同 header 循环           | 跳过     |
| B4  | TRE 表达式重排     | RV64GC **无 Zba**（后端仅 slli/add/mul，无 sh3add），TRE 无融合目标                                                                                                                   | N/A 跳过 |
| B5  | LoopInit           | 收益小                                                                                                                                                                                | 暂缓     |
| B6  | LIVS for DSE       | 当前 DSE 启发式已可用，独立 LIVS 收益小                                                                                                                                               | 暂缓     |

#### 关键发现：LSR 完全失效 + GEPStrengthReduce 多候选限制

1. **LSR（LoopStrengthReduce.cpp）0/60 用例生效**：`findMulOfInductionVar`（L29-71）只匹配 `LOAD 自 info.var(alloca)`，但 mem2reg 在 LSR 之前运行（O2 阶段），此时 IV 已是 PHI。SCEV 的 `info.var` 是 PHI 节点，MUL 直接用 PHI 结果而非 LOAD → 永不匹配。汇编中 `lsr` 命名痕迹 = 0。
2. **但 A3 教训限制收益**：BOOM 上 mul 单周期全流水，LSR（mul→add）运行时收益有限。静态数可能略降（mul+add→addi）但运行时可能持平。
3. **GEPStrengthReduce 正常工作**（PHI 形式），但有两限制：
   - `candidates.size() != 1` → 整循环跳过（L256-261）。conv2d 内层有 In[]+K[] 两个 GEP、h-5 有多个 GEP，全被跳过。
   - 仅匹配 `GEP base, iv` 或 `GEP base, 0, iv`，不匹配 `GEP base, iv, const`（行主序 A[iv][const]）。
4. **conv2d 内层循环已优化良好**（slli 移位），mul 多在外层或算法计算。h-5 内层有未削减步长 mul `a5*5600`。

### 当前实验：GEPStrengthReduce 多候选放宽（B2 类）— 已完成，负面

**假设**：寄存器分配器已升级为 call-aware 图着色，`candidates.size()!=1` 限制可能已可放宽。
**做法**：放宽为允许 ≤4 候选。
**结果**：**负面，已回退**。静态指令数 20130 → 21087（**+957，+4.75%**）。
**根因**：每个额外候选新增 lsr.init + lsr.ptr PHI + lsr.inc 三条指令，多指针 PHI 增加寄存器压力导致溢出。BOOM 上 mul 单周期，GEP 强度削减（mul+add→指针 PHI+inc）不省延迟反增代码体积。
**附带发现**：GEPStrengthReduce 单候选模式也是 0/60 生效——非平凡循环都有 2+ 数组访问（candidates ≥ 2 → 跳过），平凡循环的 GEP 已被 GEPFolding 折叠或模式不匹配。即整个强度削弱基建（LSR + GEPStrengthReduce）在当前用例集上实质未生效。

### B 组最终裁决（2026-07-28）

**B 组无可行优化**。核心原因：**BOOM 上 mul 单周期全流水**（A3 教训），所有 strength-reduction 类优化（B2 IndVars/LSR、GEPStrengthReduce 多候选）用多条指令换 1 条廉价 mul，净负收益。B1/B3/B4/B5/B6 因用例形态或架构限制无机会。

**下一步方向**（待用户选择）：

1. **C 组**：AliasAnalysis → GCM（GVN 完整配套），高复杂度高风险，但可能是最后的大收益来源
2. **目标特定调优**：函数对齐（icache line）、指令调度优化、减少 spill
3. **逐用例瓶颈分析**：挑 top-N 热点用例，针对性优化而非通用 pass
4. **接受现状**：编译器已为该目标架构充分优化，A 组 -1.02% 已是近期可达收益

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

- ~~第一批 A 组~~：A1 支配树 L/R + GVN 全支配、A2 纯函数识别、A4 PassManager 参数化（2026-07-28，净 -1.02%，已提交 b24d9ab）
- ~~第二批 B 组~~：**评估完毕，无可行优化**（2026-07-28）。B2 强度削减类在 BOOM（mul 单周期）上净负收益；B1/B3/B4/B5/B6 因用例形态或架构限制无机会。详见"当前焦点"。

### 下一批（待用户选择）

- **C 组**：AliasAnalysis → GCM（高复杂度高风险，可能是最后大收益来源）
- **目标特定调优**：函数对齐（icache）、指令调度、减少 spill
- **逐用例瓶颈分析**：针对性优化而非通用 pass

---

## 五、历史日志

| 日期       | 内容                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 2026-07-28 | 创建草纸。基于稳定版 `017ab02` 建 `opt-research-scratch` 分支。完成调研报告 vs 现有实现差距分析：核实调研文档"规划中"项中 GVN/GEPFolding/LICM may_alias/动态 preheader/icmp+br→beq/Peephole 融合套件 实际已实现；真实未实现项整理为 A/B/C/D 四组，给出推荐路线。                                                                                                                                                                                                                                                                                                                                                                            |
| 2026-07-28 | **A 组完成**。A1 支配树 L/R 编码 + GVN 全支配合并（新增 `computeDomTreeLR`/`dominatesLR`，GVN 默认放开全支配、`OPT_GVN_IDOM_ONLY=1` 逃生）；A2 纯函数识别（新文件 PureFuncDetection.cpp，乐观初始化+迭代降级不动点，LICM 纯 CALL 可外提/不阻塞 LOAD、LoadElim 跨纯 CALL 保缓存）；A4 PassManager 参数化（新文件 PassManager.cpp，OPT_DISABLE/OPT_ENABLE + 别名归一化，兼容旧 OPT_DISABLE_GVN）。A3 放弃（BOOM mul 单周期，shift+add 更慢）。验证：功能 95/100 同基线、性能 60/60、静态指令 20337→20130（-1.02%），收益全部来自 GVN 全支配，微回归 crypto +1/sl +2 可接受。附带修复 test_perf_wsl.sh qemu 自动检测。待提交 + 待选 B 组目标。 |
| 2026-07-28 | **B 组评估完毕，无可行优化**。深度代码+汇编实证：(1) B1 LoopUnswitching——测试用例无干净循环不变条件分支；(2) B2 IndVars——SCEV 已覆盖核心，LSR 虽 0/60 生效（PHI 形式不匹配）但 BOOM mul 单周期修复无收益；(3) B3 LoopSimplify——SimplifyCFG 折叠空 preheader，仅基建无静态收益；(4) B4 TRE——RV64GC 无 Zba，N/A。实验：GEPStrengthReduce 多候选放宽（≤4）→ 静态 20130→21087（+4.75%），已回退。GEPStrengthReduce 单候选亦 0/60 生效（非平凡循环均 2+ GEP 候选）。结论：BOOM mul 单周期使所有 strength-reduction 净负。临时脚本已清理。                                                                                                        |
