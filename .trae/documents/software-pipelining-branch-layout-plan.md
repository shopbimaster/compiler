# 软件流水（IR 层跨迭代 LOAD 预取）+ 分支概率引导布局

## 概述

本轮在 `Peephole-test` 分支上新增两项硬件导向优化：

1. **软件流水（SWP）**：新建 `SoftwarePipelining.cpp`，在 IR 层实现跨迭代 LOAD 预取，隐藏 BOOM 4 周期 load-use 延迟。采用 body-split + 守卫预取算法，保守识别单 BB 自循环 + 单 IV 依赖 LOAD 的循环。
2. **分支概率引导布局**：增强 `BasicBlockReordering.cpp`，引入静态分支概率启发式（循环退出冷、早返回冷、后支配者热），让热路径 fall-through、冷路径 out-of-line。规避 E2 无 PGO 回归陷阱（模糊 if-then-else 不动）。

前置已完成：本地与远端 `gitlab/Peephole-test` 完全同步（0 ahead / 0 behind），工作树干净，WSL `build/compiler` 存在。

---

## 一、当前状态分析（基于 Phase 1 探索）

### 1.1 已有基础设施

| 组件 | 文件 | 现状 | 与本轮关系 |
| --- | --- | --- | --- |
| 自然循环森林 | [LoopFind.cpp](file:///d:/VSCodeProjects/compiler/src/opt/LoopFind.cpp) | `findNaturalLoops` / `getLoopsInnermostFirst` / `NaturalLoop{header,latch,body,exitBlocks,exitingBlocks}` | SWP 复用 |
| SCEV 归纳分析 | [SCEVAnalysis.cpp](file:///d:/VSCodeProjects/compiler/src/opt/SCEVAnalysis.cpp) | `analyzeLoopInduction` 返回 `InductionInfo{var,start,step,end,tripCount,cmpKind}`，支持 PHI/LOAD/ADD 三种 IV 形态 | SWP 复用 |
| 循环旋转 | [LoopRotation.cpp](file:///VSCodeProjects/compiler/src/opt/LoopRotation.cpp) | `while(cond){body}` → `guard + do{body}while(cond)`，body 成为单 BB 自循环 | SWP 候选形态来源 |
| 指令调度 P8 | [InstructionScheduling.cpp](file:///d:/VSCodeProjects/compiler/src/opt/InstructionScheduling.cpp) | LOAD 段内可移动（intra-BB），**无跨迭代预取** | SWP 补足跨迭代 |
| 后 RA 调度 | [PostRAScheduler.cpp](file:///d:/VSCodeProjects/compiler/src/backend/PostRAScheduler.cpp) | 汇编层 DAG 列表调度，LOAD 延迟=3，mul=1，div=5 | SWP 后协同 |
| BB 重排 | [BasicBlockReordering.cpp](file:///d:/VSCodeProjects/compiler/src/opt/BasicBlockReordering.cpp) | 支配树 DFS 先序，子节点排序仅按 "CFG 后继优先 + 名字"，**无概率意识** | 布局改造目标 |
| cond_br 发射 | [TargetCodeGen.cpp:1515-1680](file:///d:/VSCodeProjects/compiler/src/backend/TargetCodeGen.cpp#L1515) | 已有 fall-through 优化：elseBB 物理相邻则真跳走/假 fall-through；thenBB 相邻则反转条件。**布局决定哪条边 fall-through** | 布局改造的下游消费者 |
| Pass 开关 | [PassManager.cpp](file:///d:/VSCodeProjects/compiler/src/opt/PassManager.cpp) | `OPT_DISABLE`/`OPT_ENABLE`/`OPT_FORCE_ENABLE` + `builtinDisable` | SWP 接入 |

### 1.2 已确认的缺口

- **无跨迭代 LOAD 预取**：P8 仅在段内提前 LOAD，下迭代 LOAD 仍在本迭代工作完成后才发射，4 周期 load-use 延迟未被隐藏。
- **无压缩指令显式生成**：本轮不做（用户未选）。
- **布局无概率意识**：循环退出边（冷）可能 fall-through，循环继续边（热）反而跳转——方向反了。
- **E2 教训**：无 PGO 时 if-then 全局 fall-through 有回归（h-4-03 +14.62%）。本轮规避：模糊 if-then-else 不动，仅高置信度启发。

### 1.3 BOOM 微架构动机（来自 project_memory）

- Load-use ~4 周期；ROB=16（小，编译器暴露跨迭代 ILP 边际收益大）；误预测惩罚 ~14 周期；取指 16B/周期。
- mul 单周期全流水（已确认，不再做强度削减）。
- QEMU TCG 不模拟延迟 → 本地只能验证功能正确性 + 静态指标，真实收益待 FPGA。

---

## 二、Part 1：软件流水（IR 层跨迭代 LOAD 预取）

### 2.1 算法：body-split + 守卫预取

**目标形态**（post-LoopRotation 自循环）：

```
原:
guard:    if (!(i<N)) goto exit
body:     t = load a[i]          // LOAD（地址依赖 i）
          ... use t ...           // ALU 工作
          i = i + 1
          if (i<N) goto body      // 自环
          goto exit
exit:

变换后:
guard:    if (!(i<N)) goto exit
          t_pref = load a[i]      // ★ 首迭代预取（安全：guard 已检查 i<N）
body.use: t = phi(t_pref from guard, t_pref_next from body.prefetch)
          ... use t ...           // 原工作（LOAD 已移除）
          i = i + 1
          if (i<N) goto body.prefetch   // ★ 继续则预取
          goto exit                // 退出不预取
body.prefetch: t_pref_next = load a[i]  // ★ 下迭代预取（i 已自增，安全：i<N 已检查）
               goto body.use
exit:
```

### 2.2 候选识别（保守）

在 `getLoopsInnermostFirst(func)` 返回的循环中找候选，**任一条件不满足即跳过**：

| # | 条件 | 理由 |
| --- | --- | --- |
| 1 | `loop.body.size() == 1`（自循环，header==latch==body） | post-rotation 形态；或 `size()==2`（header+body 双 BB，pre-rotation）— 本轮先只支持自循环形态 |
| 2 | body 起始是 PHI（IV），末尾是 `ADD(PHI, const) → ... → COND_BR` | 标准 counted loop |
| 3 | body 内**恰好 1 个 LOAD**，其地址操作数链含 IV（直接或经 GEP） | 单 LOAD 保守，多 LOAD 后续扩展 |
| 4 | body 无 `CALL`、无 `COND_BR`（除末尾 latch）、无 `PHI`（除起始 IV） | 纯直线体 |
| 5 | body 的 STORE（若有）不别名 LOAD 基址 | 简单判断：LOAD 与 STORE 的基址 GlobalVariable 不同即安全；任一非 GlobalVariable 基址 → 跳过 |
| 6 | IV step 是 `ConstantInt` | 简化下迭代地址计算（直接复用原 GEP，移到 body.prefetch 即可，因 i 已自增） |
| 7 | body 指令数 ≤ 40 | 避免代码膨胀（拆分后约 +10 指令） |
| 8 | 有 preheader（或 guard 块可作为预取插入点） | 首迭代预取需要落点 |
| 9 | LOAD 的所有 use 都在 body 内 | 跨块 use 处理复杂，本轮跳过 |

### 2.3 变换实现步骤

**入口**：`bool softwarePipelining(IR::Module* mod)`
- `SWP_OFF=1` 或 `OPT_DISABLE=softwarePipelining` → 直接返回 false
- 遍历每个非 external 函数，`getLoopsInnermostFirst`，对每个循环调 `tryPipelineLoop`

**`tryPipelineLoop(func, loop)`**：

1. **结构校验**：确认是自循环（body = {header}, header == latch）。`header->getTerminator()` 是 `COND_BR`，且其某一后继是 header 自身（自环），另一后继是 exit。
2. **IV 识别**：header 起始 PHI（IV），记录 `ivPhi`、`initVal`（来自 preheader/guard）、`backVal`（来自自环）。`backVal` 应为 `ADD(ivPhi, constStep)` 形态。
3. **LOAD 识别**：扫描 body 指令，找恰好一个 `LOAD`。记录 `loadInst`、其地址操作数 `loadAddrPtr`（应为 GEP 指令或 ALLOCA/GlobalVariable）。
4. **安全检查**（2.2 表全部条件）。
5. **创建 body.prefetch BB**：`func->insertBlock(header->getName() + ".swp_prefetch", header)`（插在 header 之后，保证 fall-through：body.use → body.prefetch → body.use）。
6. **guard/preheader 插入首迭代预取**：在 preheader 末尾（其 terminator 之前）克隆 `loadInst` 及其地址计算依赖（GEP 等），得 `tPref`。若 preheader 不存在但 guard 块存在（post-rotation），在 guard 块的 COND_BR 之前插入。
7. **body.use 起始插入 PHI**：`tPhi = createPhi(loadInst->getType(), "t.swp", 4)`，incoming：`[tPref, guard/preheader]`, `[tPrefNext, body.prefetch]`（后者先占位，步骤 8 填）。插在 header 起始 PHI 之后。
8. **移动 LOAD 到 body.prefetch**：将 `loadInst` 及其专属地址计算指令（仅被 loadInst 使用的 GEP）从 header 移到 body.prefetch。重命名其结果为 `tPrefNext`。在 body.prefetch 末尾追加 `BR header`（回 body.use）。
9. **替换 use**：`loadInst->replaceAllUsesWith(tPhi)`（在移动之前执行，避免悬空）。
10. **改写 header 末尾 COND_BR**：原 `cond_br cond, header(self), exit` → `cond_br cond, body.prefetch, exit`（继续则进 prefetch，退出仍到 exit）。
11. **补全 PHI incoming**：`tPhi` 的第二个 incoming 填 `[tPrefNext, body.prefetch]`。

**正确性要点**：

- **预取安全性**：guard 的 `tPref` 在 `if (!(i<N)) goto exit` 之后插入，故 i<N 已成立；body.prefetch 的 `tPrefNext` 在 body.use 的 `if (i<N) goto body.prefetch` 之后执行，故 i<N 已成立。无越界 LOAD。
- **地址正确性**：LOAD 用原 GEP（`a[i]`）。在 body.prefetch 中执行时，`i` 已被 body.use 末尾的自增更新为下迭代值，故 `a[i]` 即下迭代地址。✓
- **PHI 语义**：body.use 起始的 `tPhi` 首迭代取 guard 的 `tPref`，后续迭代取 body.prefetch 的 `tPrefNext`。✓
- **exit 路径**：body.use 的 COND_BR 退出分支直接到 exit，不进 body.prefetch，无多余 LOAD。✓
- **IV 自增不变**：IV 的 ADD 仍在 body.use 内（在 COND_BR 之前），PHI 更新逻辑不变。✓

### 2.4 文件改动清单

| 文件 | 改动 |
| --- | --- |
| `src/opt/SoftwarePipelining.cpp` | **新建**：实现 `softwarePipelining(mod)` 及内部 `tryPipelineLoop` |
| `include/opt/Optimizer.h` | 在 O3 Pass 区段追加 `bool softwarePipelining(IR::Module* mod);` 声明 |
| `src/opt/Optimizer.cpp` `runO3` | 在 `loopUnrolling` + `gepStrengthReduce` 之后、O3 清理轮之前插入：`if (PASS_CALL(softwarePipelining)) { simplifyCFG(mod); constantFolding(mod); deadCodeElimination(mod); o3Changed = true; }` |
| `src/opt/PassManager.cpp` | **无需改**：`passEnabled` 已支持 `OPT_DISABLE`；`builtinDisable` 不加 SWP（默认启用）。`SWP_OFF` 在 SWP 内部读 |

### 2.5 管线位置论证

放在 `loopUnrolling` 之后：被展开器展开（body 变多 LOAD）的循环会因"LOAD 数≠1"被 SWP 跳过；未被展开的大 tc 循环保持单 LOAD，SWP 接手。`gepStrengthReduce` 之后：地址计算已化简为最简 GEP 形态，LOAD 地址识别更稳。在 O3 清理轮之前：SWP 拆出的新 BB 需要 SimplifyCFG + DCE 收敛。

### 2.6 开关与回退

- `SWP_OFF=1`：SWP 内部 `loopSwpDisabled()` 读此变量，返回 false。
- `OPT_DISABLE=softwarePipelining`：通过 `PASS_CALL` 宏短路。
- 二分对照：`SWP_OFF=1` vs ON，确认功能零回归 + 静态指标变化。

---

## 三、Part 2：分支概率引导布局

### 3.1 当前布局算法问题

[BasicBlockReordering.cpp:110-127](file:///d:/VSCodeProjects/compiler/src/opt/BasicBlockReordering.cpp#L110) 的 dom-children 排序：
```cpp
if (aIsSucc != bIsSucc) return aIsSucc;   // CFG 后继优先
return a->getName() < b->getName();        // 否则按名字
```
**问题**：同为 CFG 后继时不区分热/冷。例：循环 header 的两个后继——body（热，继续循环）与 exit（冷，退出）——按名字排序，可能让 exit fall-through 而 body 跳转，方向反了。

### 3.2 改造方案：静态概率启发式

在 dom-children 排序的比较函数中，引入"热后继优先"规则。**仅对高置信度启发应用**，模糊情况保持现状（规避 E2 回归）。

#### 3.2.1 概率估计规则（对当前 BB `cur` 的每个 CFG 后继 `s` 打分 `hotness[s]`）

| 启发 | 判定 | hotness |
| --- | --- | --- |
| **循环回边** | `s` 是某 NaturalLoop 的 header，且 `cur` 在该 loop.body 内（即 `cur→s` 是 back-edge） | **+100**（热，taken） |
| **循环退出** | `cur` 是某 NaturalLoop 的 exitingBlock，`s` 不在该 loop.body 内（即 `cur→s` 是 exit-edge） | **-100**（冷，not-taken） |
| **早返回** | `s` 内首条非 PHI 指令是 `RET`，或 `s` 仅含 `BR → func_exit`（epilogue） | **-50**（冷） |
| **后支配者** | `s` 后支配 `cur` 的另一后继 `s'`（即 `s` 在 `s'` 必经之路上） | **+20**（热，if-then-else 的 else 常更热） |
| **默认** | 不匹配以上 | **0**（不动） |

#### 3.2.2 排序规则修订

```cpp
// 1. 都不是 CFG 后继：按名字（保持现状）
// 2. 一方是 CFG 后继：CFG 后继优先（保持现状）
// 3. 都是 CFG 后继：热者优先（新）
//    hotness 高 → 先入栈 → 后出栈 → 排在 newOrder 前面 → 物理上 fall-through
//    hotness 相同 → 保持名字序（确定性 + 规避 E2）
```

**关键**：仅当 `hotness[a] != hotness[b]` 时介入；相等则按名字（现状）。这保证模糊 if-then-else（两者 hotness 都为 0 或都 +20）不被误动。

#### 3.2.3 实现要点

- 在 `reorderBlocks` 开头计算 `findNaturalLoops(func)` 一次，构建 `backEdges`/`exitEdges` 集合。
- 后支配用现有 `computePostDominators`（已在 Optimizer.h 声明）。
- 早返回识别：扫 `s` 指令，若首条非 PHI 是 RET，或 `s` 仅含 `BR .Lfunc_exit`（需识别 exit 块——可用后支配：被所有块后支配的块即 exit）。
- **作用域**：仅当函数有循环或显式早返回时才可能改变布局；无循环的纯直线代码不受影响。

### 3.3 文件改动清单

| 文件 | 改动 |
| --- | --- |
| `src/opt/BasicBlockReordering.cpp` | 在 `reorderBlocks` 内增加 `NaturalLoop` + 后支配分析，重写 dom-children 比较函数引入 hotness |
| 其他 | 无（TargetCodeGen 的 emitCondBr 已是 fall-through 感知，布局变即自动消费） |

### 3.4 风险控制与回退

- **E2 规避**：模糊 if-then-else（hotness 相等）不动，仅循环边/早返回/明确后支配者介入。
- **开关**：`LAYOUT_PROB_OFF=1` → 跳过概率分析，回退原比较函数。
- **确定性**：所有 tie-break 仍按名字序，保证可复现。
- **对照**：`LAYOUT_PROB_OFF=1` vs ON，看功能 + 静态 j 数变化。

---

## 四、验证计划

### 4.1 构建

```bash
wsl -d Ubuntu -e bash -c "cd /mnt/d/VSCodeProjects/compiler && make -j\$(nproc) 2>&1 | tail -20"
```

### 4.2 功能正确性（零回归门槛）

```bash
# 性能套件（60 用例，主战场）
wsl -d Ubuntu -e bash -c "cd /mnt/d/VSCodeProjects/compiler && bash scripts/test_perf_wsl.sh 2>&1 | tail -30"
# h_functional（40 用例）
wsl -d Ubuntu -e bash -c "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_tests.sh hfunc 2>&1 | tail -20"
# functional（100 用例）
wsl -d Ubuntu -e bash -c "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_tests.sh func 2>&1 | tail -20"
```

**通过标准**：新增失败 = 0。预存失败（62_percolation/68_brainfk/75_max_flow/12_DSU/21_union_find/30_many_dimensions/35_math）允许保留。

### 4.3 二分对照实验

```bash
# SWP 对照
SWP_OFF=1 bash scripts/test_perf_wsl.sh > /tmp/swp_off.log 2>&1
bash scripts/test_perf_wsl.sh > /tmp/swp_on.log 2>&1
# 布局对照
LAYOUT_PROB_OFF=1 bash scripts/test_perf_wsl.sh > /tmp/lay_off.log 2>&1
bash scripts/test_perf_wsl.sh > /tmp/lay_on.log 2>&1
```

### 4.4 静态指标

对 60 perf 用例汇编统计：
- 代码行数（期望：SWP 略增 +10/用例，布局不变或略降）
- 无条件 `j` 数（期望：布局优化后下降）
- `load-use 相邻对`（LOAD 紧跟其 use，间隔 0-3 条指令——PostRAScheduler 已有的弱代理指标；期望 SWP 后下降）

```bash
wsl -d Ubuntu -e bash -c "cd /mnt/d/VSCodeProjects/compiler && for f in test/performance/*.sy; do n=\$(basename \$f .sy); ./build/compiler -S -o /tmp/\$n.s \$f -O1; done && python3 scripts/asm_stats.py /tmp/*.s 2>/dev/null || echo 'manual check'"
```

### 4.5 WSL/git mtime 陷阱（来自 project_memory）

改完代码后若行为异常，先 `touch` 所有改动文件 + 重建确认，避免 stash/checkout 后 mtime 未变漏编译。

---

## 五、假设与决策

| 项 | 决策 | 理由 |
| --- | --- | --- |
| SWP 仅支持 post-rotation 自循环形态 | 是 | LoopRotation 默认启用，已把热循环转成自循环；pre-rotation 形态后续扩展 |
| SWP 仅单 LOAD | 是 | 保守，多 LOAD 交互复杂；多 LOAD 循环交给现有 unroll+PostRA |
| SWP 不处理 CALL body | 是 | CALL 副作用 + 跨 CALL 移动 LOAD 不安全 |
| SWP step 必须常量 | 是 | 非常量 step 下迭代地址不可静态计算 |
| 布局仅高置信度启发 | 是 | E2 教训：无 PGO 时模糊 if-then 全局 fall-through 有回归 |
| 压缩指令本轮不做 | 是 | 用户未选；后续单独迭代 |
| 不碰寄存器分配器 | 是 | project_memory 硬约束（陷阱 14） |
| 不做 mul 强度削减 | 是 | project_memory：BOOM mul 单周期，净负收益 |

---

## 六、执行顺序

1. **先做 SWP**（Part 1）：新建 `SoftwarePipelining.cpp` + 注册 + 管线接入 → 构建 → 功能测试 → 二分对照。
2. **再做布局**（Part 2）：改 `BasicBlockReordering.cpp` → 构建 → 功能测试 → 二分对照。
3. **联合测试**：两项同时 ON，跑完整套件，确认无交互回归。
4. **更新 STATUS.md**：把本轮成果摘要写入"历史日志"，清理"当前焦点"。
5. **不提交、不推送**：本轮仅本地实现 + 测试，待用户确认后再决定提交（遵循 git 安全协议）。

---

## 七、本轮不做（明确边界）

- 压缩指令显式生成（c.* 助记符）——用户未选
- 真 PGO / Profile 引导——本项目无 profile 采集设施
- 多 LOAD SWP / modulo scheduling II>1——后续迭代
- 函数对齐 / 热 BB 对齐调整——用户未选
- 任何 strength reduction / LSR 触碰——硬约束
- 寄存器分配器改动——硬约束
