# BOOM 双发射深度优化实施计划 v2

## 摘要

用户要求放弃"指令数下降"指标，改用 QEMU 模拟计时 + 反汇编层面验证双发射友好性，继续尝试五大方向：**超大循环展开、长依赖链消除、宏指令融合、内存访问对齐、软件流水**。本计划基于对代码库和测试用例的实证探索，对五项逐一给出"实现"或"实证不适用"的决策，并补充两项启用性优化（EarlyReturn→Select、多 BB 展开）以放大前两项的收益。所有代码变更提交到独立分支 `boom-dual-issue-opt`，由用户在 FPGA 上重测。

## 当前状态分析（Phase 1 探索结论）

### 测试用例的归约循环结构（实证）

| 用例 | 循环 | 结构 | trip count | 可展开性 |
|---|---|---|---|---|
| `matmul1.sy:86-90` | `sum = sum + c[i][j]` | **单 BB 纯加法归约** | 200 | ★ P1 分裂 + P2 展开 |
| `matmul1.sy:41-45` | `if(...) temp = temp + b[i][k]*a[k][j]` | 多 BB 条件归约 | 200 | P3 多 BB 展开 |
| `matmul1.sy:56-61` | `if(c[i][j]<temp) temp = c[i][j]` | 多 BB min 归约 | 200 | P3 多 BB 展开 |
| `h-4-03.sy:24-27` | `sum = (sum + f(x) + 1) % mod` | 单 BB 含 `%` 归约 | 动态 | P4 拆 max → P2 展开 |

`matmul1.sy:86` 的 `sum += c[i][j]` 是 **200 次串行 ADD 依赖链**，BOOM 16-entry ROB 无法并行 → P1 分裂为 4 路独立累加器可直接打破瓶颈。

### 现有基础设施核查

| 组件 | 位置 | 状态 |
|---|---|---|
| 部分展开 `LoopUnrolling.cpp` | candidates={8,6,4,3,2}, tc≤64, 仅单 BB, **无寄存器压力检查** | 需扩展 |
| 完全展开 `LoopFullUnroll.cpp:784-819` | 有 externalVals 寄存器压力检查 | 可复用框架 |
| 多 BB 完全展开 `LoopFullUnroll.cpp:360-746` `fullUnrollMultiBB` | 支持 tc≤64 多 BB 克隆 | 可复用克隆逻辑 |
| NaturalLoop API `LoopFind.cpp` | `getLoopsInnermostFirst` / `isBlockInLoop` / `analyzeLoopInduction` | 可直接调用 |
| PostRAScheduler `latencyOf` | load=3, mul=1, div/rem=5 | 已正确（E1 修复后） |
| PeepholeOptimizer 融合 | 20+ 模式（li+op/mv+op/addi 链/la+mem/sw+lw 等） | 已完备 |
| TargetCodeGen 分支发射 | 直接 `beq/bne/blt/bge`，无 `slt+beqz` | 已最优 |
| 循环头对齐 | `.p2align 4`（16B） | 可实验 32B |

### 五项用户要求逐一决策

| # | 方向 | 决策 | 依据 |
|---|---|---|---|
| A | 超大循环展开 | **实现（P2）** | matmul1 tc=200 远超当前 tc≤64 限制；BOOM ROB 小，展开暴露跨迭代 ILP 有价值 |
| B | 长依赖链消除 | **实现（P1）** | matmul1 `sum+=c[i][j]` 200 长依赖链是 BOOM 双发射最大瓶颈；4 路累加器让 OoO 并行 |
| C | 宏指令融合 | **不实现（已完备）** | PeepholeOptimizer 已有 20+ 融合；TargetCodeGen 已用直接分支；BOOM 解码器**无硬件宏融合**（RISC-V 标准 decode，非 x86 式 macro-fusion） |
| D | 内存访问对齐 | **小实验（P5）** | 循环头 `.p2align 4`→`.p2align 5`（32B）对齐实验；数组 cache-line 对齐对跨步访问无收益（matmul 列访问 stride 800B） |
| E | 软件流水 | **不实现（P1 等效）** | 真正 SMS 复杂度极高且寄存器压力是瓶颈；P1 多累加器已实现等效的跨迭代 ILP 暴露，且更安全；LICM 已外提不变 load |

**补充启用性优化**：
- **P3 多 BB 部分展开**：扩展 P2 支持含 `if` 的循环（复用 `fullUnrollMultiBB` 克隆框架）
- **P4 EarlyReturn→Select**：h-4-03 的 `max` 函数 if-else-RET → SELECT，消除 COND_BR 使循环体变单 BB 可展开

## 实施步骤

### 步骤 1: P1 — 多累加器归约分裂（核心，对应"长依赖链消除"）

**文件**: 新建 `src/opt/ReductionSplitting.cpp`

**注册**:
- `include/opt/Optimizer.h`: 在 `loopUnrolling` 声明附近（约 L132）添加 `bool reductionSplitting(IR::Module* mod);`
- `src/opt/Optimizer.cpp` `runO3`: 在 `loopUnrolling` **之前**插入（分裂后仍可被展开）
- `CMakeLists.txt:133` 后添加 `src/opt/ReductionSplitting.cpp`

**算法**:
1. `getLoopsInnermostFirst(func)` 取最内层循环
2. 仅处理单 BB 循环体（`loop.body.size() == 2`，header + body）
3. 检测归约 PHI：`%sum = phi [init, preheader], [sum.next, body]`
4. 验证 `sum.next = ADD sum, expr`（仅整型 ADD；跳过 SDIV/SREM/浮点）
5. 验证 `expr` 不依赖 `sum`（`sum` 仅被 `sum.next` 使用）
6. 计算分裂因子 N：默认 4；寄存器压力约束 `externalVals + N ≤ 12`（留 4 个临时寄存器）
7. 要求 `tripCount % N == 0`（否则降级 N 为 tripCount 的最大因子 ≤4，或放弃）
8. 变换：
   - 创建 N 个独立累加器 PHI：`%sum0 = phi [0, preheader], [sum0.next, body]` ... `%sumN-1`
   - body 中 N 份克隆分别更新 `sumI.next = ADD sumI, expr_for_iteration_k`
   - 步长 ×N：归纳变量 `i` 每次迭代 `i += N*step`
   - 出口插入合并：`%final = ADD sum0, sum1; ... ; ADD final, sumN-1` 再 `ADD final, init`
9. 用 `PASS_CALL(reductionSplitting)` 宏支持 `OPT_DISABLE` 开关

**安全检查**（保守）:
- 仅整型 ADD/MUL（可结合可交换）；跳过 SDIV/SREM（h-4-03 的 `% mod` 不分裂）
- 跳过浮点（SysY 语义不允许 FP 重排）
- 跳过 `sum` 被 STORE 到内存的模式
- 溢出保护：若 `expr` 可能产生大值（如含 MUL 且操作数范围未知），跳过。matmul1 的 `c[i][j]` 是 int 数组元素，4 路累加最坏 4×2^31 溢出 → **需要用 64 位中间累加？不**：保持 int 语义，仅当循环次数 × 单次增量上界 < 2^31 时分裂。matmul1 sum 累加 200 个 int 元素，最坏 200×2^31 溢出。**因此 P1 对 matmul1 的 sum 循环也需谨慎**：改为分裂因子 N=4 后每路累加 50 次，仍可能溢出。**决策**：P1 仅对"加法且操作数为非负量"或"循环次数 × |单次增量| < 2^31"的安全场景生效。matmul1 的 sum 累加 int 数组，无法证明非负 → **P1 不会对 matmul1 生效**。

  **调整**：P1 主要目标改为 **`01_mm*.sy` / `many_mat_cal*.sy`** 中可证明无溢出的归约（如累加非负计数、或小范围值）。先实现框架，对 matmul1 类用例由 P2 超大展开覆盖。

**预期影响**:
- 对可证明无溢出的加法归约用例，4 路独立 ADD 链让 BOOM 双发射槽位并行执行
- 反汇编验证：归约循环出现 4 条独立 `add` 链（非串行 `add rd, rd, rs`）

### 步骤 2: P2 — 超大循环展开（对应"超大循环展开"）

**文件**: 修改 `src/opt/LoopUnrolling.cpp`

**改动**:
1. **L245** 候选列表：`{8,6,4,3,2}` → `{16,12,8,6,4,3,2}`
2. **L239** trip count 上限：`tc > 64` → `tc > 256`
3. **L109,116** `inferTripCount` 内的 `bound <= 64` / `bound < 64` 上界：放宽到 `256`
4. 在 factor 选择后（L256 附近）添加**寄存器压力检查**（移植自 `LoopFullUnroll.cpp:784-819`）：
   ```cpp
   // 统计循环体内使用的、定义在循环外的不同值数量
   std::unordered_set<IR::Value*> externalVals;
   for (auto* bb : loop.body) {
       for (auto& inst : bb->getInstructions()) {
           for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
               auto* op = inst->getOperand(i);
               if (!op || dynamic_cast<IR::Constant*>(op) ||
                   dynamic_cast<IR::BasicBlock*>(op) ||
                   dynamic_cast<IR::Function*>(op) ||
                   dynamic_cast<IR::GlobalVariable*>(op)) continue;
               auto* opInst = dynamic_cast<IR::Instruction*>(op);
               if (opInst && opInst->getParent() && loop.body.count(opInst->getParent())) continue;
               if (dynamic_cast<IR::Argument*>(op) || opInst) externalVals.insert(op);
           }
       }
   }
   // 外部值 × 展开次数 = 展开后同时活跃值（近似）；上限 14（留 2 个临时寄存器）
   if (externalVals.size() * factor > 14 || externalVals.size() > 10) {
       // 降级 factor：从候选中找最大的满足约束的
       for (unsigned f : {8, 6, 4, 3, 2}) {
           if (tc % f == 0 && tc >= f && externalVals.size() * f <= 14) { factor = f; break; }
       }
       if (externalVals.size() * factor > 14) return false;
   }
   ```
5. 限制 `factor=16` 仅在 `externalVals <= 4` 时允许（避免寄存器溢出）

**安全**: 保留 `isSimpleBody` 不变；保留 PHI→back-edge 映射逻辑（L275-347）

**预期影响**:
- matmul1 的 `sum += c[i][j]`（tc=200）可 8× 展开（200/8=25 次迭代，每次 8 个独立 ADD）
- 反汇编验证：循环体变长，循环开销占比下降，8 个独立 ADD 可被 OoO 并行发射

### 步骤 3: P3 — 多 BB 循环部分展开

**文件**: 扩展 `src/opt/LoopUnrolling.cpp`

**算法**（复用 `LoopFullUnroll.cpp:360-746` `fullUnrollMultiBB` 的克隆框架）:
1. 放宽 **L222** `loop.body.size() > 2` → `> 8`
2. 放宽 `isSimpleBody`（L127-141）：允许 COND_BR（但分支目标必须在 `loop.body` 内），仍拒绝 CALL
3. 多 BB 克隆 factor 份：跨迭代连接 latch→bodyEntry，维护 cross-iter valueMap
4. 寄存器压力：`externalVals * factor + loop.body.size() ≤ 14`（更保守，因为多 BB 展开后每个 BB 的外部值都需保持活跃）
5. **限制**：多 BB 模式下 factor 上限为 4（避免代码膨胀）

**目标**: matmul1 的条件归约循环（`if(...) temp += ...`）可 4× 展开

**风险**: 多 BB 克隆逻辑复杂，需充分测试。若实现困难，可降级为"仅当 factor=2 且 body.size()≤4"的保守模式。

### 步骤 4: P4 — EarlyReturn→Select 转换

**文件**: 新建 `src/opt/EarlyReturnToSelect.cpp`

**注册**:
- `include/opt/Optimizer.h`: 添加 `bool earlyReturnToSelect(IR::Module* mod);`
- `src/opt/Optimizer.cpp` `runO2` 阶段 1：在 `inlineExpansion`（L88, L110）之后插入
- `CMakeLists.txt`: 添加源文件

**算法**:
1. 扫描函数内模式：`if (cond) { ...; return X; } else { ...; return Y; }` 或 `if (cond) return X; ... return Y;`
2. 安全检查：then/else 分支指令数 ≤ 4，无 CALL/STORE/LOAD（仅纯计算），无嵌套 if
3. 转换为 `return select(cond, X, Y)`（参考 `IfConversion.cpp` 的 SELECT 创建）
4. 转换后函数体变为单 BB，后续 LoopUnrolling 可展开含该函数调用的循环

**目标**: h-4-03 的 `max` 函数内联后 3 个 if-else-RET → 3 个 SELECT → `f(x)` 体变单 BB → `loop_test` 循环可展开

**预期影响**: h-4-03 从 +14.62% 回归转为可展开 → 预期改进

### 步骤 5: P5 — 循环头 32B 对齐实验（对应"内存访问对齐"）

**文件**: 修改 `src/backend/TargetCodeGen.cpp:932-935`

**改动**:
```cpp
void TargetCodeGen::emitBasicBlock(IR::BasicBlock& bb) {
    currentBB = &bb;
    // 循环头对齐：BOOM 取指带宽 16B/周期，32B 对齐确保循环头不跨取指块边界
    if (loopHeaders.count(&bb)) {
        emitter.emitText("  .p2align 5");  // 原 4 → 5（32B 对齐）
    }
    ...
}
```

**注意**: 这会增加代码体积（最多 +31B/循环头）。用 `BOOM_ALIGN32_OFF=1` 环境变量保留回退开关：
```cpp
if (loopHeaders.count(&bb)) {
    const char* a = std::getenv("BOOM_ALIGN32_OFF");
    emitter.emitText(a && std::string(a)=="1" ? "  .p2align 4" : "  .p2align 5");
}
```

**数据对齐**: 不做。matmul1 的 `int a[200][200]` 是跨步访问（列访问 stride 800B），cache-line 对齐起始地址无收益。

### 步骤 6: 验证与提交

1. **构建**: `cmake --build build -j`
2. **功能测试**: `bash scripts/run_tests.sh all O1`，目标功能 95+/100、性能 60/60（不引入新回归）
3. **QEMU 计时**: 对比 matmul1/2/3、many_mat_cal1/2/3、conv2d-1/2/3、h-4-03 的执行时间（用 `scripts/test_perf_wsl.sh`）
4. **反汇编分析**: `riscv64-linux-gnu-objdump -d` 检查：
   - P1: 归约循环出现 N 条独立 `add` 链
   - P2: 展开后循环体变长，循环开销占比下降
   - P4: h-4-03 的 `max` 调用变为 SELECT（无 COND_BR）
5. **提交到独立分支**:
   ```bash
   git checkout -b boom-dual-issue-opt
   git add src/opt/ReductionSplitting.cpp src/opt/EarlyReturnToSelect.cpp \
           src/opt/LoopUnrolling.cpp src/opt/Optimizer.cpp \
           include/opt/Optimizer.h src/backend/TargetCodeGen.cpp CMakeLists.txt
   git commit -m "perf(opt): BOOM 双发射深度优化 P1-P5"
   git push gitlab boom-dual-issue-opt
   ```

## 关键文件清单

| 文件 | 改动类型 | 步骤 |
|---|---|---|
| `src/opt/ReductionSplitting.cpp` | 新建 | P1 |
| `src/opt/EarlyReturnToSelect.cpp` | 新建 | P4 |
| `src/opt/LoopUnrolling.cpp` | 修改 | P2 + P3 |
| `src/opt/Optimizer.cpp` | 修改：注册 P1/P4，调整 runO3 顺序 | P1 + P4 |
| `include/opt/Optimizer.h` | 修改：声明 P1/P4 | P1 + P4 |
| `src/backend/TargetCodeGen.cpp` | 修改：循环头对齐 4→5 | P5 |
| `CMakeLists.txt` | 修改：注册新源文件 | P1 + P4 |

## 复用的现有基础设施

- `LoopFind.cpp` 的 `NaturalLoop` + `getLoopsInnermostFirst` — 循环检测
- `LoopFullUnroll.cpp:360-746` 的 `fullUnrollMultiBB` 多 BB 克隆框架 — P3 复用
- `LoopFullUnroll.cpp:784-819` 的 externalVals 寄存器压力检查 — P1/P2/P3 复用
- `LoopFullUnroll.cpp:754` 的 `analyzeLoopInduction` — trip count 推导
- `IfConversion.cpp` 的 SELECT 创建模式 — P4 参考
- `Optimizer.cpp:28` 的 `PASS_CALL(fn)` 宏 — 所有新 pass 支持环境变量开关

## 假设与决策

1. **分支名**：`boom-dual-issue-opt`（独立分支，用户重测后决定是否合并）
2. **P1 溢出保护**：仅对可证明无溢出的加法归约生效。matmul1 的 `sum += c[i][j]` 因无法证明非负**不会被 P1 分裂**，改由 P2 超大展开覆盖。P1 主要受益用例为 `01_mm*` / `many_mat_cal*` 中可证明无溢出的归约。
3. **P3 保守模式**：多 BB 展开限制 factor ≤ 4，body.size() ≤ 8，避免代码膨胀和寄存器溢出。
4. **P5 可回退**：32B 对齐用 `BOOM_ALIGN32_OFF=1` 环境变量保留回退，便于 A/B 对比。
5. **宏指令融合不实现**：实证 PeepholeOptimizer 已完备，BOOM 无硬件宏融合，无新机会。
6. **软件流水不实现**：P1 多累加器等效暴露跨迭代 ILP，且更安全；真正 SMS 复杂度过高。
7. **优化顺序**：P4 在 O2 内联后 → P1 在 O3 展开前 → P2/P3 在 O3 → P5 在后端代码生成。所有新 pass 用 `PASS_CALL` 支持环境变量开关，便于定位回归。

## 验证方法

1. **正确性**：`bash scripts/run_tests.sh all O1`，功能 ≥95/100，性能 60/60
2. **QEMU 计时**：对比目标用例（matmul×3 / many_mat_cal×3 / conv2d×3 / h-4-03）的执行时间
3. **反汇编分析**：
   - P1: `objdump -d` 检查归约循环出现 N 条独立 `add` 链
   - P2: 展开后循环体变长，循环开销占比下降
   - P4: h-4-03 的 `max` 调用变为 `select`/`mv`+条件传送（无 `beqz`/`bnez`）
   - P5: 循环头前出现 `.p2align 5`
4. **对照开关**：每项 pass 配 `OPT_DISABLE` 环境变量，P5 配 `BOOM_ALIGN32_OFF`，可独立关闭定位回归
5. **提交到独立分支** `boom-dual-issue-opt`，用户在 FPGA 上重测真实双发射效果

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| P2 超大展开导致寄存器溢出（matmul1 的 200×8 展开） | externalVals 寄存器压力检查 + factor 降级机制 |
| P3 多 BB 克隆逻辑复杂引入 SEGFAULT | 保守模式（factor≤4, body≤8），充分功能测试 |
| P4 误转非纯函数的 early return | 严格安全检查（无 CALL/STORE/LOAD，指令数≤4） |
| P5 32B 对齐增加代码体积导致 icache 负收益 | `BOOM_ALIGN32_OFF` 回退开关，A/B 对比 |
| 整体优化在 QEMU 上不体现（QEMU 不模拟双发射） | 主要看反汇编层面的双发射友好性（独立 ADD 链、无串行依赖）；真实收益由用户 FPGA 重测 |
