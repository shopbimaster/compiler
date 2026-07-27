# 计划：增强 IR 层指令调度器（feat/instr-scheduling）

## 背景与定位
- 基线：main = v3.3（队友线）。本工作在 `feat/instr-scheduling` 分支，与图着色线(feat/graph-coloring-gvn)、mem2reg 改动**零文件冲突**。
- 只改 `src/opt/InstructionScheduling.cpp`（两条线都没碰过）。
- 现有调度器（217 行）的致命弱点：把 LOAD/STORE 当**不可移动边界**（isMovable 排除它们），等于放弃调度的核心价值——隐藏 RISC-V 的 load-use 延迟。

## 目标
让 LOAD 能在**保证无别名冲突**的前提下前移，拉开 load 与其 use 的距离，隐藏 load-use 停顿；并把优先级从"LOAD+100+useCount"升级为**延迟感知的关键路径**启发式。保持 STORE/CALL 顺序语义不变（安全第一）。

## 设计

### 1. 依赖建模（段内 + 内存依赖）
当前段边界过粗。改为：在一个更大的调度窗口（BB 内、以 CALL/terminator 为硬边界）内构建依赖 DAG，节点含 LOAD：
- **数据依赖**：inst 用到 seg 内某 inst 的结果 → 边（已有）。
- **内存依赖**（新增，保守）：
  - LOAD 依赖它之前的所有 STORE（可能读到该 store 写的值）——除非能证明不同地址。第一版保守：LOAD 不能跨越它之前的任何 STORE。
  - STORE 依赖之前所有 LOAD 和 STORE（保序）。
  - 即：STORE 仍是"半边界"（其前的 load/store 都对它有依赖边，其后的对它有反依赖），但 LOAD **之间**可自由重排、且可在无 STORE 间隔时相互提前。
- CALL 仍是硬边界（副作用不可知）。

### 2. 延迟感知优先级（关键路径）
- 给每类指令一个近似延迟：LOAD=3（load-use）、MUL/DIV=4、其余=1。
- 自底向上算每个节点的**关键路径高度** height(n) = latency(n) + max(height(succ))。
- ready 集合按 height 降序选择（关键路径优先），tie-break 用原始顺序保持确定性。
- 效果：LOAD 因高延迟被优先发射、且其 use 被推后，自然拉开距离。

### 3. 安全边界（正确性第一）
- CALL / terminator / PHI / ALLOCA：硬边界，绝不跨越。
- STORE 前后的内存依赖边保证 load/store 不乱序。
- volatile/带副作用指令（若有标记）：视为硬边界。
- 决定性：所有 tie-break 用指令原始索引，消除非确定性。

## 实施步骤
1. 重写 `scheduleSegment`：纳入 LOAD，加内存依赖边，改延迟感知优先级。
2. 调整 `isMovable` / 段收集：STORE/CALL 仍是边界，但 LOAD 进入可调度集。
3. 保留"顺序未变则不动"的快速返回（避免无谓改动）。
4. 全量正确性回归（60/60，权威脚本带 exit-code）。
5. 静态指标：看 load-use 相邻对是否减少（load 与首个 use 的平均间距）。
6. 若正确且有改善 → 推 feat/instr-scheduling 分支，交给团队集成。

## 验证口径
- **正确性**：native WSL，60 perf，riscv64-gcc + qemu-static，权威 exit-code 比对，必须 60/60。
- **收益代理**：静态统计 "load 紧跟其 use"（相邻 load-use）的数量下降；因 qemu 摊薄延迟，最终真机收益以平台为准。

## 风险
- 内存别名保守 → 可能错失部分重排，但不会错排（安全）。
- IR 层调度可能被后续 regalloc/codegen 部分抵消——第一版接受，用静态代理评估，平台数据定论。
- 不碰寄存器分配器/mem2reg，与队友和图着色线均无冲突。
