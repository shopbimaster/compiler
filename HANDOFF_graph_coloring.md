# 图着色 + GVN 优化线 交接文档

> 面向队友的集成说明。本线从 v3.1 分叉，与 main（v3.3 线性扫描线）并行，
> 已在平台验证。核心是**把后端从线性扫描换成图着色，并借此启用 GVN**。

## 一、成果与平台数据

| 版本 | 分支/tag | 平台成绩 | 说明 |
|------|----------|---------|------|
| v4.0.1 | tag `v4.0.1` | **799.50s AC/100**（开发板30） | call-aware 图着色替代线性扫描 |
| v4.1.0 | tag `v4.1.0` / `feat/graph-coloring-gvn` | **795.28s AC/100**（开发板126） | 图着色下启用 GVN |
| +调度 | `testbench`(7355e36) | 待测 | v4.1.0 叠加 post-RA 汇编调度 |

- 历史最优线性扫描 = 793.1s。图着色 v4.1.0 = 795.3s，**与最优线扫实质打平/微差**。
- 图着色的价值不在当下净胜，而在**解锁了 GVN**（线性扫描下 GVN 因长活跃区间抖动净亏 +1319ms，图着色的全局溢出决策消化了长区间）。

## 二、改动清单（相对 v3.1 基线）

### 1. 图着色寄存器分配器（`src/backend/RegisterAllocator.cpp`）
- `colorAllocate` / `colorRegClass`：Chaitin-Briggs 图着色，替代 `linearScan`。
- **复用同一份 `intervals`**（buildIntervals 的 liveness 不改——那是踩 SEGFAULT 换来的资产），只替换"分配决策"层。codegen 零改动。
- **call-aware 有偏着色**：按活跃区间是否跨 CALL 选染色偏好——跨调用值优先 callee-saved，不跨调用值优先 caller-saved。复刻线扫的 RA-CALL 策略，修复初版图着色 knapsack +22.8% 退化。
- 逃生开关：`RA_ALLOCATOR=linear` 切回线扫，`RA_COLOR_CALLAWARE=0` 复现初版。

### 2. GVN 启用（`src/opt/Optimizer.cpp`）
- GVN 从禁用转为默认启用。逃生开关 `OPT_DISABLE_GVN=1`。
- 静态实测（图着色下 GVN 开 vs 关）：总指令 -2.7%、循环加权 spill -264、零回退用例，正确性 60/60。

### 3. post-RA 汇编调度（`src/backend/PostRAScheduler.cpp`，仅 testbench）
- 最终汇编文本层的延迟感知列表调度，隐藏 load-use 延迟。`SCHED_OFF=1` 关闭。
- 正确 60/60，静态代理收益微弱（-1.1%），待平台裁决。

## 三、给集成的建议

**两条线都重写了寄存器分配器，不能直接 merge**。建议当面定用哪个分配器，再叠加优化。若采用图着色线，v3.3 里有两个互补优化值得合入：

1. **v3.3 的"前端把非逃逸标量 alloca 放 entry 块"**：正好解决图着色下 many_mat_cal 循环内变量栈驻留问题（实测是它 40% 时间的瓶颈）。⚠️ 注意：直接扩展 mem2reg 提升循环内 alloca 会撞上 `MAX_MEM2REG_PHI_NODES_PER_FUNCTION=14` 掩盖的 mem2reg 重命名固有 bug（crypto 多变量组合出错），前端放置更安全。
2. **v3.3 的 call-local caller-saved 偏好**：与图着色的 call-aware 同理念，可对照取优。

## 四、关键约束（踩过的坑）

- **qemu 对寄存器分配类优化结构性失效**：QEMU TCG 把 12 个 s 寄存器映射到 x86 仅 6 个 callee-saved，任何改变寄存器分配的优化在 qemu 上必显回归。图着色/GVN/调度**只能靠平台真机验证**，别信 qemu 墙钟。
- **决定性排序**：图着色的溢出选择、简化压栈都加了 `value->getName()` tiebreaker，保证编译结果确定（同 v3.3 陷阱 17 的教训）。
- **move coalescing 死路**：图节点合并式 move coalescing 在本后端结构性无效（PHI/incoming 区间被刻意放大到必然重叠），已验证 merged=0。现有 coalescePhis 事后补丁才是适配方案。

## 五、逃生开关总表

| 开关 | 作用 |
|------|------|
| `RA_ALLOCATOR=linear` | 切回线性扫描 |
| `RA_COLOR_CALLAWARE=0` | 关闭 call-aware 偏好（复现初版图着色） |
| `OPT_DISABLE_GVN=1` | 关闭 GVN |
| `SCHED_OFF=1` | 关闭 post-RA 调度 |
