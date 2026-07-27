# 计划：汇编层 post-RA 局部指令调度（feat/instr-scheduling）

## 定位
- 基于 main = v3.3。只新增一个后端后处理步骤，作用于 TargetCodeGen 生成的**最终汇编文本**。
- 与图着色线、mem2reg、寄存器分配器**零耦合**（纯文本后处理），team 可独立集成。

## 为什么在汇编层
IR 层调度已实测无效（load-use 停顿 Δ+3，被 regalloc/codegen 抵消）。汇编层看到**真实物理寄存器**和**真实相邻关系**，重排不会被下游抵消——这是唯一能真正隐藏 load-use 延迟的层次。

## 设计（保守、正确性第一）
1. **解析**：把 text section 按行切分。识别三类行：标签(`.L…:`)、指令(`  op ops`)、指示符(`.p2align`等)。
2. **分块**：以标签、`call`、`j`/`b*`/`ret`（控制流）、指示符为**硬边界**，其间连续指令为一个可调度基本块窗口。
3. **def/use 建模**（每个助记符，约 40 个）：
   - load(lw/ld/lh/flw/fld…): def=rd, use=base 寄存器
   - store(sw/sd/fsw…): use=rs, use=base（无 def）
   - ALU 三址(add/sub/mul/and…): def=rd, use=rs1,rs2
   - ALU 立即(addi/slli/srai/andi…): def=rd, use=rs1
   - li/la: def=rd（无寄存器 use）
   - mv/neg/seqz/fmv…: def=rd, use=rs
   - 未识别助记符 → **整块放弃调度**（安全兜底，绝不猜）
4. **依赖 DAG**：RAW/WAR/WAW 三种寄存器依赖 + 内存依赖（load 不跨越前面的 store；store 保序）→ 与 IR 版同构但基于物理寄存器。
5. **延迟感知列表调度**：load=3、mul=3、div=5、其余=1；关键路径高度优先；tie 用原始行序（决定性）。
6. **重排回写**：块内按新序重排指令行，标签/边界/指示符位置不动。

## 安全边界（防 miscompile）
- **未识别助记符 → 该块原样保留**，不调度。
- `sp`/`ra`/`gp`/`tp` 相关、`call` 前后、栈帧 prologue/epilogue → 视为硬边界或整块跳过。
- 任何 def/use 不确定 → 保守假设它读写所有寄存器（等于不动）。
- 内存依赖保守：不做别名分析，load/store 严格保序（除 load-load 可换）。

## 关键风险（必须先让用户知情）
1. **验证盲区**：qemu-user 是功能模拟器，**不模拟流水线延迟**——本地测不出真实收益，只能用"load-use 相邻对"这个弱代理。真实收益只有平台能验证。
2. **miscompile 风险**：def/use 模型只要一个助记符建错 → 错误重排 → 平台 WA。这是把正确性押在建模完整性上。缓解：未识别一律放弃调度 + 全量 60/60 回归 + 保守内存序。
3. **收益不确定**：即便建模全对，qemu 下 load-use 相邻只是弱信号；平台是否净赚未知。

## 实施步骤
1. 新增 `postRASchedule(std::string asmText) -> std::string`，在 TargetCodeGen 输出后调用。
2. 逐块解析 + def/use + DAG + 调度 + 回写。
3. 全量正确性 60/60（权威 exit-code 脚本）——**任何一个 WRONG 都必须先修或回退**。
4. 静态代理：load-use 相邻对总数下降。
5. 若正确且代理改善 → 推 feat/instr-scheduling，标注"收益需平台验证"，交团队。

## 验证口径
- 正确性：native WSL 60 perf，qemu-static，权威比对，硬性 60/60。
- 收益：仅有 load-use 相邻弱代理；诚实标注平台待验证。
