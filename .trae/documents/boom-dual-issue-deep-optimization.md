# BOOM 双发射深度优化实施计划

## Context

前期 E1（mul latency 修复）+ E2（fall-through 优化）的几何平均加速比仅 +0.35%，且有 h-4-03 +14.62% 回归。根本原因：**仅做了布局优化，未触及计算核心的依赖链瓶颈**。BOOM 的 16-entry ROB 在串行归约循环（`sum = sum + expr`）中无法发掘足够 ILP。本轮转向**计算层面的深度变换**：多累加器、超大展开、多 BB 展开、EarlyReturn→Select，直接打破依赖链、暴露跨迭代并行性。

用户明确要求：不再以指令数为指标，改用 QEMU 模拟计时 + 反汇编层面验证双发射友好性。所有变更提交到独立分支供用户在 FPGA 上重测。

## 已确认无需做的项

| 项 | 原因 |
|---|---|
| 宏指令融合 | `TargetCodeGen.cpp:265-271` 已实现 GEP+LOAD/STORE 和 ICMP+COND_BR 融合 |
| 内存访问对齐 | `TargetCodeGen.cpp:127,141,147` 已有 `.align 2`，RISC-V 对齐隐含在地址中 |
| 软件流水（本轮） | 多累加器（P1）已实现等效效果（独立依赖链 → OoO 自动乱序）；若 P1-P4 后仍有空间再评估 |

## 实施步骤

### 步骤 1: P2 — 提升部分展开因子到 16

**文件**: `src/opt/LoopUnrolling.cpp`

**改动**:
1. `:245` 候选列表 `{8,6,4,3,2}` → `{16,12,8,6,4,3,2}`
2. `:239` trip count 上限 `64` → `256`（允许大循环部分展开）
3. 在 factor 选择后添加寄存器压力检查：`externalVals + factor ≤ 14`，不满足则降级 factor
4. 仅当 `factor ≤ 8` 或 `externalVals ≤ 4` 时允许 `factor=16`

**安全**: 保留 `isSimpleBody` 不变；保留 PHI→back-edge 映射逻辑

### 步骤 2: P4 — EarlyReturn→Select 转换

**文件**: 新建 `src/opt/EarlyReturnToSelect.cpp`，注册到 `CMakeLists.txt:103` 的 `sysy_opt` 库

**插入位置**: `Optimizer.cpp::runO2` 阶段 1，`inlineExpansion` 之后（内联后 max/min 函数变成 if-else-RET）

**算法**:
1. 识别模式：`if (cond) { ...; return X; } else { ...; return Y; }` 或 `if (cond) return X; return Y;`
2. 安全检查：then/else 分支指令数 ≤ 4，无 CALL/STORE/LOAD，无嵌套 if
3. 转换为 `return select(cond, X, Y)`，消除 COND_BR
4. 转换后函数体变为单 BB，后续 LoopUnrolling 可展开

**目标**: h-4-03 的 `max` 函数内联后 3 个 if-else-RET → 3 个 SELECT → 循环体变单 BB → 可展开

### 步骤 3: P1 — 多累加器归约分裂（核心优化）

**文件**: 新建 `src/opt/ReductionSplitting.cpp`，注册到 `CMakeLists.txt` 和 `Optimizer.h:132` 附近

**插入位置**: `Optimizer.cpp::runO3` 在 `loopUnrolling` **之前**（分裂后仍可被进一步展开）

**算法**:
1. 遍历最内层循环，检测归约 PHI：`%sum = phi [init, preheader], [sum.next, body]` 且 `sum.next = ADD sum, expr`
2. 验证 `expr` 不依赖 `sum`（`sum` 仅被 `sum.next` 使用）
3. 计算分裂因子 N（默认 4，寄存器压力约束：`externalVals + N ≤ 14`）
4. 变换：创建 N 个独立累加器 PHI，循环步长 ×N，body 中 N 份克隆分别更新不同累加器
5. 循环出口插入合并：`sum.final = sum0 + sum1 + ... + sum(N-1)`

**安全检查**:
- 仅整型 ADD/MUL（可结合可交换）；跳过 SDIV/SREM
- 跳过浮点（SysY 不允许 FP 重排）
- 跳过 `sum` 被 STORE 到内存的模式
- trip count 必须能被 N 整除（否则降级 N 或放弃）
- 仅单 BB 循环体（与现有 LoopUnrolling 一致）

**预期影响**: matmul×3 / many_mat_cal×3 / conv2d×3 / 01_mm×3 的内层归约循环，4 路独立 ADD 链让 OoO 并行执行

### 步骤 4: P3 — 多 BB 循环部分展开

**文件**: 扩展 `src/opt/LoopUnrolling.cpp`

**算法**（复用 `LoopFullUnroll.cpp:360-746` 的多 BB 克隆框架）:
1. 放宽 `body.size() > 2` → `> 8`
2. 放宽 `isSimpleBody`：允许 COND_BR（但分支目标必须在 loop.body 内），仍拒绝 CALL
3. 多 BB 克隆 factor 份：跨迭代连接 latch→bodyEntry，维护 cross-iter valueMap
4. 寄存器压力：`externalVals + factor × bodyBBs ≤ 14`

**目标**: h-4-03 即使 P4 漏掉某些模式也能展开；h-5/h-8/h-9/h-10 的带 if 循环

### 步骤 5: 验证与提交

1. 构建编译器，运行 `scripts/run_tests.sh all O1` 确认无回归
2. 运行性能测试，对比 QEMU 计时
3. 反汇编检查热点循环：`riscv64-linux-gnu-objdump -d` 验证多累加器产生独立 ADD 链
4. 创建独立分支 `boom-dual-issue-opt`，提交所有变更

## 关键文件清单

| 文件 | 改动类型 |
|---|---|
| `src/opt/LoopUnrolling.cpp` | 修改：P2 提升展开因子 + P3 多 BB 展开 |
| `src/opt/ReductionSplitting.cpp` | 新建：P1 多累加器归约分裂 |
| `src/opt/EarlyReturnToSelect.cpp` | 新建：P4 if-else-RET → SELECT |
| `src/opt/Optimizer.cpp` | 修改：注册新 pass，调整 runO3 顺序 |
| `include/opt/Optimizer.h` | 修改：声明新 pass 函数 |
| `CMakeLists.txt` | 修改：注册新源文件 |

## 复用的现有基础设施

- `LoopFind.cpp` 的 `NaturalLoop` + `getLoopsInnermostFirst` — 循环检测
- `LoopFullUnroll.cpp:360-746` 的多 BB 克隆框架 — P3 复用
- `LoopFullUnroll.cpp:787-819` 的 externalVals 寄存器压力检查 — P1/P2/P3 复用
- `IfConversion.cpp` 的 SELECT 创建模式 — P4 参考
- `Optimizer.cpp:28` 的 `PASS_CALL(fn)` 宏 — 所有新 pass 支持环境变量开关

## 验证方法

1. **正确性**: `scripts/run_tests.sh all O1`，功能测试 95+/100，性能测试 60/60
2. **QEMU 计时**: 对比 matmul×3 / many_mat_cal×3 / conv2d×3 / h-4-03 的执行时间
3. **反汇编分析**: `riscv64-linux-gnu-objdump -d` 检查：
   - P1: 归约循环出现 N 条独立 ADD 链（非串行依赖）
   - P2: 展开后循环体变长，循环开销占比下降
   - P4: h-4-03 的 max 调用变为 SELECT（无 COND_BR）
4. **提交到独立分支** `boom-dual-issue-opt`，用户在 FPGA 上重测
