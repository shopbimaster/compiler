# 计划：矩阵乘内层地址强度削减（攻 many_mat_cal 40% 大头）

## 根因（可信二进制 md5=3d6fbd 确认）

many_mat_cal 的 O(T³) 矩阵乘内层 `sum += C[i][k]*A[k][j]`，内层 k 循环每轮：
- `A[k][j]` 地址 = `A + k*4096 + j*4`，其中 `k*4096`（`gep A,0,k`）**每轮完整重算**（一次 slli k,12）
- `k`/`sum` 是循环体内声明的 alloca，全程栈驻留

**为什么现有 GEPStrengthReduce 对内层失效**：`src/opt/GEPStrengthReduce.cpp:112-113`
要求归纳变量 IV 必须是 PHI 节点。但 `k` 声明在循环体内 → mem2reg 不提升（只提 entry alloca）
→ `k` 是 alloca/load 形式，不是 PHI → GEPStrengthReduce 直接 return false → 内层所有 GEP 强度削减失效。

**关键发现**：`SCEVAnalysis.cpp:76-77` 的 `analyzeLoopInduction` **已经支持 ALLOCA/LOAD 形式**
的归纳变量（不止 PHI）。是 GEPStrengthReduce 自己额外加了 "IV 必须是 PHI" 的限制。

## 方案：扩展 GEPStrengthReduce 支持 ALLOCA/LOAD 归纳变量

不碰 mem2reg（避开那个 PHI-cap 重命名 bug），不移动 alloca（避开拉长区间负优化）。
只放宽 GEPStrengthReduce，让它处理 SCEV 已能分析的栈形式 IV。

### 变换（以 `A[k][j]` 为例，k 是栈上 IV，step=1，stride=4096）
```
before (内层每轮):                 after:
  %k = load %k.alloca               preheader: %ptr.init = gep A, 0, k_start  (k_start=0)
  %g = gep A, 0, %k                 header:    %ptr = phi [%ptr.init, pre], [%ptr.inc, latch]
  ... use %g ...                    body:      ... use %ptr 代替 %g ...
                                    latch:     %ptr.inc = gep %ptr, 4096  (step*stride)
```
消除每轮的 `slli k,12`，换成 latch 处一次指针递增（本就要执行的归纳更新）。

### 关键实现点
1. **IV 识别放宽**（GEPStrengthReduce.cpp:112-113）：允许 `info.var` 是 alloca（LOAD 形式），
   不再强制 PHI。SCEV 的 `info.start`/`info.step` 已提供。
2. **指针 PHI 仍需创建**：即使 IV 是栈变量，强度削减产生的指针必须是 PHI（在 header），
   因为它的值跨迭代传递。这不依赖 IV 是否 PHI——是新建的独立指针归纳。
3. **latch 处更新指针**：在 IV 的 STORE（`store k+1, %k.alloca`）位置附近插入 `%ptr.inc = gep %ptr, step*stride`。
4. **复用现有安全检查**：单 preheader、单 latch、base 循环不变、所有 use 在循环内、
   跳过多回边、跳过递减外层（GEPStrengthReduce.cpp:125-165 全部保留）。
5. **保守门槛**：仅当 SCEV 成功（tripCount>=0 或至少 start/step 已知）且 step 是正常量时启用。

### 逃生开关
`GEPSR_STACKIV=0` 关闭栈 IV 扩展，回退到仅 PHI（现有行为）。

## 分阶段验证（每步用可信二进制，md5 确认）
1. 实现扩展 → 构建，确认 md5 变化
2. 看 many_mat_cal 矩阵乘内层汇编：`slli k,12` 是否消失、换成指针递增
3. **全量正确性 60/60**（重点：矩阵类 + crypto + 递归类）——任何 WRONG 立即回退
4. 静态：矩阵乘内层指令数下降；循环深度加权 spill **不增**（关键：这次是替换乘法为
   已有的归纳更新，不新增长区间变量，理论上不增压力）
5. 若正确且指令降 → 推 testbench，平台真机裁决

## 风险与预案
- **风险 A：栈 IV 的指针 PHI 与栈变量并存，寄存器压力**。预案：指针 PHI 替换掉 `slli+add`
  地址计算，净寄存器占用可能持平（少了地址临时值）。用循环深度加权 spill 验证。
- **风险 B：多个 GEP 共享同一 IV（C[i][k] 和 A[k][j] 都用 k）**。现有代码逐个 GEP 处理，
  应能各自削减。需确认不冲突。
- **风险 C：本后端 CFG/PHI 脆弱**。预案：完全复用现有 GEPStrengthReduce 的 CFG 操作路径
  （已验证安全的 preheader/latch/PHI 插入），只放宽 IV 类型判断，不新增 CFG 操作。
- 任何阶段 WRONG 或加权 spill 显著上升 → `git checkout` 回退，记录 memory。

## 为什么这次不同于之前的失败
- move coalescing：区间强制重叠（结构性）——本方案不涉及 coalescing
- alloca 提 entry：拉长区间增 spill——本方案不移动 alloca，只削减地址计算
- post-RA 调度：qemu 测不出——本方案减指令，qemu 也可见
- 部分展开：热点多 BB——本方案不展开，逐 GEP 处理，与循环体 BB 数无关
- 核心区别：**减少内层每轮的地址重算指令，且不新增长区间变量**，是纯计算量削减
