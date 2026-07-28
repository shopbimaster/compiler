# 编译器优化开发草纸（STATUS）

> **用途**：如同做题草纸，记录当前未完成任务的思路、进展、结论。**积极更新，旧内容及时清理**。
> **维护原则**：任务完成后将其摘要归入"历史日志"，从"当前焦点"清除；每轮开发前后更新"当前焦点"。

---

## 一、当前焦点

**分支**：`opt-research-scratch`（A 组已提交 `b24d9ab`，E 组待提交）
**当前方向**：**BOOM 双发射针对性优化**（E1/E2 完成，E3 评估完毕不实施）

> 调研文档：`Solutions/调研文档.md`；本次 BOOM 微架构优化调研报告见本节"3.1 调研结论"。

### 1.1 立项背景

BOOM 是双发射乱序执行软核，目标平台为 RV64GC + medany，跑在 FPGA BOOM 软核 + QEMU。前期 A/B 组优化（强度削减类）因 **BOOM mul 单周期全流水** 而净负收益。本轮转向**充分利用双发射槽位 + 前端带宽 + 分支预测**的优化方向，避开已被硬件覆盖的指令调度盲区。

### 1.2 BOOM 微架构关键参数（调研核实）

| 维度          | 参数                                  | 编译器含义                          |
| ------------- | ------------------------------------- | ----------------------------------- |
| 发射宽度      | 2-wide 乱序                           | 非对称发射口需配对                  |
| Port 0        | iALU + iMul + FMA                     | mul 在此口，单周期全流水            |
| Port 1        | iALU + iDiv + LSU                     | load/store 在此口                   |
| 双发可行组合  | ALU+LSU / ALU+MUL / ALU+ALU / MUL+LSU | 编译器配对收益有限（OoO 动态调度）  |
| 双发不可行    | 2×MUL / 2×LSU / 2×FP / DIV+LSU        | 同口争用，硬件串行化                |
| Load-use 延迟 | ~4 周期                               | 短延迟硬件已隐藏，长延迟编译器可补  |
| L1 I-cache    | 32KB，8 路组相联                      | 代码布局/压缩指令收益显著           |
| 取指带宽      | 16B/周期 = 8×c. 或 4×非c.             | **C 扩展直接翻倍取指带宽**          |
| ROB / Issue   | BOOMv2: 16/16/16 entries              | **ROB 较小，编译器暴露 ILP 有价值** |
| 误预测惩罚    | ~14 周期                              | **分支友好布局高收益**              |
| 工具链支持    | GCC/LLVM 均无专门 BOOM 调度模型       | 工程机会：自建模型                  |

### 1.3 关键判断：乱序 CPU 上编译器调度的收益边界

**共识**：对现代乱序超标量，编译器静态指令调度的边际收益有限但非零。BOOM 的特殊性使收益结构不同：

- **ROB 较小（16 entries）**——比商用 OoO（128-256 项）小得多，硬件调度窗口有限，**编译器暴露跨迭代/跨块 ILP 的边际收益相对更大**。
- **FPGA 主频低（~90MHz）但 I-cache miss 代价显著**——代码布局/压缩指令/预取的收益突出。
- **误预测惩罚大（~14 周期）**——分支友好布局是高收益区。
- **非对称发射口**——OoO 硬件会动态避开冲突，编译器精确配对收益有限。

**收益分区**：
| 收益区 | 优化类型 | 是否被 OoO 硬件覆盖 |
| ------ | -------- | ------------------- |
| **高收益** | C 扩展、代码布局、分支友好布局、减少 spill | 否（前端/缓存/分支，硬件不管） |
| **中收益** | 跨迭代 ILP 暴露（unroll+load 前移）、软件流水线、宏融合 | 部分（ROB 窗口外有收益） |
| **低/零收益** | 基本块内指令重排、load-use 短延迟填充、精确发射口配对 | 是（硬件已动态调度） |
| **负收益** | strength reduction（mul→shift+add）、过度循环展开 | — （A3/B2 已验证） |

---

## 二、基线信息

- **稳定版本 commit**：`017ab02 refactor(opt): 泛化基数排序转换条件`
- **分支创建**：`opt-research-scratch` @ 2026-07-28
- **功能测试**：95/100 通过（预存失败：62_percolation / 68_brainfk / 75_max_flow；预存超时 2 个）
- **性能测试**：60/60 通过，**基线静态指令数 20130**（A 组后，已复核）
- **最大用例**：huffman 697、sort 684、conv2d 639、crypto 614、fft 470、shuffle 383

### 硬约束（来自 project_memory，勿违反）

- 目标 RV64GC + medany，跑在 FPGA BOOM 软核 + QEMU
- **陷阱 14**：QEMU TCG 下寄存器分配变化常带来回归，当前字典序分配已实测最优，**不要碰寄存器分配器**（带权图着色禁用）
- CodeSink O(N⁴)，大 BB（size>200）须跳过
- Mem2Reg 快速路径须在保守检查之后，且 useBlocks ≤ 3，phiBlocks=0 跳过
- PeepholeOptimizer 在 ret/tail/jr 处必须清所有寄存器跟踪状态
- TargetCodeGen 必须支持 WIDE_SMOD_MUL（opcode 18），FFT 用例依赖
- **BOOM mul 单周期全流水**：所有 strength-reduction 类优化净负收益（A3/B2 已验证）

### 已启用关键优化（无需再做）

GVN（全支配模式）、SCCP、SCEV、LSR、LoopFullUnroll、LICM（纯函数感知）、Mem2Reg、MagicDivision、PeepholeOptimizer 全套融合、LoopInterchange、MatrixReductionContraction、BasicBlockReordering（支配树 DFS）、InstructionScheduling（BB 内分段列表调度）、PostRAScheduler（汇编层延迟感知调度）。

---

## 三、差距分析：BOOM 双发射优化 vs 现有实现

### 3.1 调研结论（2026-07-28，含 LLVM/GCC/学术界来源）

详见调研报告（本会话生成）。核心结论：

1. **LLVM/GCC 均无专门 BOOM 调度模型**——BOOM 用户通常退化为 `rocket`（顺序核心！）或 `generic-ooo`。这是工程机会也是现实约束：我们不能依赖工具链，必须自建。
2. **Jeff Law（Ventana，RISC-V Summit 2025）关键建议**：从动态指令数转向 PMU 采样；重点处理分支误预测；小心 uarch quirks；精确 uarch 建模对性能至关重要（Spacemit X60 clmul 延迟修正案例 +3.7%）。
3. **BOLT/Codestitcher 实证**：代码布局优化在大型代码体上 I-cache miss -10~30%、提速 3-25%。BOOM 的 32KB I-cache 中等规模，对中等代码体收益可观。
4. **C 扩展实证**：RVC 减少 25-30% 取指位数、I-cache miss -20-25%，"约等于 I-cache 容量翻倍"。对 BOOM 16B/周期取指带宽，压缩指令直接翻倍等效 fetch 带宽。
5. **OoO 上编译器调度的实证收益**：LLVM MachineScheduler 在 SPEC CPU 2017 上 7.3%(int)/9.1%(fp)——但主要来自**寄存器压力管理**而非 ILP 调度本身。Kouveli 等人随机调度研究证实 OoO 上指令重排收益微乎其微。
6. **宏操作融合**：Celio 等人 2016 研究表明 RISC-V 宏融合可减少 5.4% 动态指令数（平均）。

### 3.2 现有后端实现核查（实证，非文档）

| #   | 项                             | 状态                                              | 证据                                                                                                      | BOOM 双发射收益      |
| --- | ------------------------------ | ------------------------------------------------- | --------------------------------------------------------------------------------------------------------- | -------------------- |
| 1   | **压缩指令生成**               | **未实现**（靠汇编器 relaxation）                 | [TargetCodeGen.cpp](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp) 全文无 `c.*` 助记符 | **高**               |
| 2   | 函数对齐                       | 已实现（固定 `.p2align 4`=16B）                   | [TargetCodeGen.cpp:321](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L321)            | 已做（可调 32B）     |
| 3   | 循环头对齐                     | 已实现（回边启发，无 dominance 校验）             | [TargetCodeGen.cpp:919-922](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L919)        | 已做                 |
| 4   | 热基本块对齐                   | 未实现（非循环头不对齐）                          | 同上                                                                                                      | 中                   |
| 5   | **分支概率引导布局**           | **未实现**（固定 false fall-through / true 跳转） | [TargetCodeGen.cpp:1531-1557](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L1531)     | **高**               |
| 6   | 全局地址缓存复用               | 已实现（数组型全局→callee-saved 寄存器）          | [TargetCodeGen.cpp:3082-3115](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L3082)     | 已做                 |
| 7   | **gp relaxation**              | **未实现**（`la` 伪指令，靠链接器）               | [TargetCodeGen.cpp:1454](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L1454)          | 中                   |
| 8   | load/store offset(base)        | 已实现（超 imm12 退化 li+add+ld）                 | [TargetCodeGen.cpp:466-489](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L466)        | 已做                 |
| 9   | PostRAScheduler 延迟感知       | 已实现（关键路径高度 + latencyOf）                | [PostRAScheduler.cpp:183-251](file:///d:/VSCodeProjects/compiler/src/backend/PostRAScheduler.cpp#L183)    | 已做（有 bug，见下） |
| 10  | BasicBlockReordering           | 已实现（支配树 DFS + fall-through 启发）          | [BasicBlockReordering.cpp](file:///d:/VSCodeProjects/compiler/src/opt/BasicBlockReordering.cpp)           | 已做（无频率信息）   |
| 11  | InstructionScheduling（IR 层） | 已实现（BB 内分段列表调度）                       | [InstructionScheduling.cpp](file:///d:/VSCodeProjects/compiler/src/opt/InstructionScheduling.cpp)         | 低（OoO 覆盖）       |

### 3.3 已发现的 Bug / 待修正项

#### Bug-1：PostRAScheduler `latencyOf` 与 BOOM mul 单周期矛盾【已修复 E1】

[PostRAScheduler.cpp:172-179](file:///d:/VSCodeProjects/compiler/src/backend/PostRAScheduler.cpp#L172) `latencyOf` 把 `mul/mulw/mulh/smulh` 设为 **3 周期**：

```cpp
if (a.op == "mul" || a.op == "mulw" || a.op == "mulh" || a.op == "smulh") return 3;
```

但 project_memory 明确：**BOOM 上 mul 单周期全流水**。这会让调度器误以为 mul 是长延迟指令，为其插入不必要的填充指令，可能拉长关键路径或增加代码体积。

**修复**（E1）：mul 系列改为 `return 1`。零风险，已验证静态指令数不变。

#### Bug-2：PostRAScheduler 不识别压缩指令助记符

[PostRAScheduler.cpp:107-128](file:///d:/VSCodeProjects/compiler/src/backend/PostRAScheduler.cpp#L107) 的助记符表只认标准形式（`lw/ld/addi/mv/li` 等），不识别 `c.*`。一旦显式生成压缩指令，调度器会把含 c.\* 的块整块放弃调度（`schedulable=false`）。**前置依赖**：若做显式压缩指令生成，必须同步扩展此表。

#### 文档不一致：medany 描述

`DEVELOPMENT_PLAN.md:67` 与 `PROGRESS_SUMMARY.md:76` 把 medany 描述为 `lui+addi`，应为 `auipc+addi`。实际代码用 `la` 伪指令在 medany 下展开正确。仅文档错误，非代码 bug。

---

## 四、有价值优化清单（按收益/成本排序）

### 第一梯队：高收益 + 低/中复杂度（立即推进）

| #      | 优化                                 | 收益   | 复杂度 | 依据                                                                            | 风险                                       |
| ------ | ------------------------------------ | ------ | ------ | ------------------------------------------------------------------------------- | ------------------------------------------ |
| **E1** | **修复 mul latency bug**             | 中     | 极低   | Bug-1，调度器不再为 mul 插填充                                                  | 零（SCHED_OFF 对照即可）                   |
| **E2** | **分支预测友好的 fall-through 布局** | **高** | 中     | BOOM 误预测 ~14 周期；当前固定 true 跳转/false fall-through，循环回边必然 taken | 中（需块频率启发式）                       |
| ~~E3~~ | ~~显式压缩指令生成~~                 | ~~高~~ | ~~中~~ | ~~C 扩展翻倍取指带宽~~                                                          | **评估完毕不实施**：汇编器已自动压缩 39.8% |
| **E4** | **循环回边 fall-through 化**         | **高** | 中     | E2 子项；循环 latch→header 的 taken 分支变 fall-through 可减少回边误预测        | 中（需 loop rotation，支配序约束）         |

**E2/E4 启发式**（无 PGO，基于静态特征）：

- 循环回边：让 latch fall-through 到 header（回边 taken → fall-through）
- `if (cond) {少量} else {大量}`：少量分支跳走，大量 fall-through
- `if (!cond) continue/return`：反转条件让正常路径 fall-through
- 退出条件 `if (i < n)`：退出分支跳走，循环体 fall-through

### 第二梯队：中收益 + 中复杂度（视第一梯队效果推进）

| #      | 优化                                  | 收益  | 复杂度 | 依据                                                                                            |
| ------ | ------------------------------------- | ----- | ------ | ----------------------------------------------------------------------------------------------- |
| **F1** | **gp relaxation**                     | 中    | 中     | 小全局 `la+ld`(3条) → `ld rd, %gprel(sym)(gp)`(1条)                                             |
| **F2** | **热基本块对齐**                      | 中    | 低     | 当前仅循环头对齐，扩展到 if-then 热分支（.p2align 4）                                           |
| **F3** | **函数对齐参数化**                    | 低-中 | 极低   | 测试 `.p2align 5`(32B) vs `.p2align 4`(16B)，BOOM v3 取指块可能 32B                             |
| **F4** | **Loop unrolling + 跨迭代 load 前移** | 中    | 中     | BOOM ROB 仅 16 entries，软件暴露跨迭代 ILP 有价值（当前 LoopFullUnroll 已有，但未做 load 前移） |
| **F5** | **宏操作融合（compare+branch）**      | 中    | 中     | RISC-V 无 cmp+branch 单指令，但 `slt+beqz` 等序列可被 BOOM decode 融合                          |

### 第三梯队：中收益 + 高复杂度（谨慎，单独立项）

| #   | 优化                                           | 收益  | 复杂度 | 风险                                          |
| --- | ---------------------------------------------- | ----- | ------ | --------------------------------------------- |
| G1  | 软件流水线（Swing Modulo Scheduling）          | 中-高 | 高     | 寄存器压力是瓶颈；BOOM ROB 小可能反而限制收益 |
| G2  | 软件预取（Zicbop）                             | 中    | 低     | 需确认 BOOM 配置是否启用 Zicbop               |
| G3  | 后端 SSA 机器指令层优化（RV64CSE/BlockLayout） | 中    | 高     | 需重构后端为 SSA 机器指令层                   |

### 明确不做（已验证或被硬件覆盖）

| 项                                             | 原因                                                               |
| ---------------------------------------------- | ------------------------------------------------------------------ |
| 基本块内指令重排（强化 InstructionScheduling） | OoO 硬件已动态调度，收益微乎其微                                   |
| 精确建模非对称发射口配对                       | OoO 硬件绕过静态约束                                               |
| strength reduction（mul→shift+add）            | A3/B2 已验证净负收益                                               |
| 过度循环展开                                   | 损害 I-cache，OoO 硬件自身动态展开                                 |
| 带权图着色寄存器分配                           | 陷阱 14：QEMU TCG 下 RA 变化常带回归                               |
| AliasAnalysis / GCM / MemorySSA（原 C 组）     | 高复杂度高风险，且收益方向（提升 LICM/DSE 精度）与双发射目标弱相关 |

---

## 五、推荐实施路线

### 第一批（立即推进，预期 1-2 项即可见效）

1. **E1 修复 mul latency bug**（10 分钟，零风险，先验证 PostRAScheduler 现状收益）
2. **E2+E4 分支友好布局**（核心项，静态启发式 fall-through 重排）
   - 在 BasicBlockReordering 或 TargetCodeGen 中实现
   - 循环回边 fall-through 优先 + if-then 热路径 fall-through
   - 用 SCHED_OFF / 独立开关对照测试
3. **E3 显式压缩指令生成**（高收益，但需谨慎）
   - 在 TargetCodeGen 选指令时优先 c.\* 形式（c.li/c.addi/c.mv/c.ld/c.sd/c.j/c.beqz/c.ret）
   - 同步扩展 PostRAScheduler 助记符表（Bug-2）
   - 风险：压缩指令有寄存器约束（c.ld/c.sd 仅 x8-x15），需回退到标准形式

### 第二批（视第一批效果）

4. F1 gp relaxation / F2 热块对齐 / F3 函数对齐参数化（快速验证项）
5. F4 loop unrolling + load 前移（基于现有 LoopFullUnroll 扩展）

### 验证策略

- **静态指标**：静态指令数（汇编行数）、压缩指令占比（c.\* 计数）、taken 分支数
- **动态指标**：QEMU 性能测试（注意 QEMU 不模拟延迟，主要看 icache/取指相关收益是否体现）
- **真实硬件**：FPGA BOOM 实测（若可用），重点看 IPC 和 I-cache miss
- **对照开关**：每项优化配 `OPT_*` / `SCHED_OFF` 环境变量逃生

### 已完成

- ~~A 组~~：A1 支配树 L/R + GVN 全支配、A2 纯函数识别、A4 PassManager 参数化（2026-07-28，净 -1.02%，已提交 b24d9ab）
- ~~B 组~~：评估完毕无可行优化（2026-07-28）。BOOM mul 单周期使 strength reduction 净负；B1/B3/B4/B5/B6 因用例形态或架构限制无机会。
- ~~E1 mul latency 修复~~：PostRAScheduler `latencyOf` 中 mul 系列从 3 周期改为 1 周期（BOOM 单周期全流水）。零风险，静态指令数不变（20130）。
- ~~E2 fall-through 优化~~：TargetCodeGen `emitCondBr`/`emitBr`/`emitRet` 支持 fall-through，省冗余 `j`。配套修复 PeepholeOptimizer 死 trampoline 清理的 fall-through 前驱检测。静态 20130→19602（**-528, -2.62%**），功能 95/100，性能 60/60。
- ~~E3 显式压缩指令~~：评估完毕**不实施**。汇编器已自动产生 39.8% 压缩指令（9576/24048），所有可压缩模式均已覆盖。显式生成不改变最终二进制，PostRAScheduler 基于延迟调度不受益于指令大小信息。

---

## 六、历史日志

| 日期       | 内容                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 2026-07-28 | 创建草纸。基于稳定版 `017ab02` 建 `opt-research-scratch` 分支。完成调研报告 vs 现有实现差距分析：A/B/C/D 四组路线。                                                                                                                                                                                                                                                                                                                                                                                                                        |
| 2026-07-28 | **A 组完成**。A1 支配树 L/R 编码 + GVN 全支配合并；A2 纯函数识别；A4 PassManager 参数化。A3 放弃（BOOM mul 单周期）。静态 20337→20130（-1.02%），收益全部来自 GVN 全支配。已提交 b24d9ab。                                                                                                                                                                                                                                                                                                                                                 |
| 2026-07-28 | **B 组评估完毕，无可行优化**。B1 无干净 unswitch 机会；B2 IndVars 被 SCEV 覆盖且 mul 廉价；B3 SimplifyCFG 折叠空 preheader；B4 RV64GC 无 Zba。实验：GEPStrengthReduce 多候选放宽 → +4.75% 回归，已回退。LSR 0/60 生效（PHI 形式不匹配）。结论：BOOM mul 单周期使所有 strength-reduction 净负。                                                                                                                                                                                                                                             |
| 2026-07-28 | **BOOM 双发射优化调研立项**。调研 LLVM/GCC/学术界对双发射乱序 CPU 的优化方法，核查后端实现。关键结论：(1) BOOM ROB 仅 16 entries，编译器暴露跨迭代 ILP 有价值；(2) C 扩展翻倍取指带宽，当前未显式生成；(3) 分支误预测 ~14 周期，当前无概率引导布局；(4) 发现 PostRAScheduler mul latency bug（设 3 周期，应 1 周期）；(5) OoO 硬件已覆盖基本块内指令重排，精确发射口配对收益有限。整理 E/F/G 三梯队路线，E1/E2/E3/E4 为第一批。                                                                                                            |
| 2026-07-28 | **E 组第一批完成**。E1: PostRAScheduler mul latency 3→1（Bug-1 修复）。E2: TargetCodeGen fall-through 优化——emitCondBr/emitBr/emitRet 省冗余 j；修复两个 bug：(a) away 边有 phi moves 时禁用 fall-through + 条件分支直接跳 awayBB 标签避免 edgeLabel 拦截；(b) PeepholeOptimizer 死 trampoline 清理增加 fall-through 前驱检测避免误删。静态 20130→19602（-528, -2.62%），功能 95/100 基线一致，性能 60/60。E3 评估不实施：汇编器已自动压缩 39.8%，显式生成无额外收益。剩余 j 分析：549 回边(需 loop rotation)、273 前向跳转、84 函数退出。 |
