# Knapsack 记忆化板上失效分析

## 评测结果

**Board (perf-knapsack-memo dee2b61)**:
- knapsack_naive-1: 45.89s
- knapsack_naive-2: 45.85s
- knapsack_naive-3: 45.87s

**Local (same commit, qemu-riscv64)**:
- knapsack_naive-1: 0.3s (2.3s wall time)
- 输出正确: 52 ✓

## 根本原因：记忆化**从未生效**（本地+板上）

### 关键发现（2026-07-28 A/B 实验）

**Wall-clock 实测（qemu-riscv64）：**
```
ON (memo enabled):  1.89s / 1.93s / 1.84s  ← 32 memo symbols in .s
OFF (OPT_DISABLE):  1.79s / 1.78s / 1.83s  ← 32 memo symbols in .s
```

**结论：OPT_DISABLE 无效，pass 未真正禁用，两组耗时几乎相同（±5%），且都含 memo 符号。**

### 技术证据

1. **汇编已含记忆化表（但未起作用）**
   ```
   .globl  __opt_memo_value_0  # 2×20.48MB .bss
   .size   __opt_memo_value_0, 20480000
   .globl  __opt_memo_hit_0
   ```
   - 索引公式: `i * 20000 + w` (范围 i∈[0,256), w∈[0,20000))
   - 实际输入: N=25, W=150/127/103 → 完全在范围内
   - **但 OPT_DISABLE 环境变量传递失败，无法获得真实基线**

2. **本地运行（qemu 实测）**
   - sylib timer: `0H-0M-0S-300369us` **← 不可信**（many_mat_cal 也报相同值）
   - wall-clock: ~1.9s（三例）
   - 朴素递归 O(2^25) ≈ 33M 次调用，在 qemu 上 1.9s 不合理但无对照组
   - 结论: **记忆化表已生成但运行时行为存疑**

3. **板上运行**
   - 三例耗时几乎相同 (45.87±0.02s)
   - 如果记忆化生效，W=150 vs W=103 应有显著差异
   - 结论: **记忆化未命中**，全走朴素递归

## 可能的技术原因

### 假设1: OPT_DISABLE 未正确实现（可能性：极高）✓
- A/B 实验中 `OPT_DISABLE=recursiveMemoization` 未生效
- 两组汇编都含 32 个 memo 符号，说明 pass 始终运行
- 疑因 `PassManager::passEnabled()` 未正确解析环境变量，或 `recursiveMemoization()` 未通过 `PASS_CALL` 包装
- **需检查 src/opt/Optimizer.cpp L66 的 `recursiveMemoization(module)` 是否应改为 `PASS_CALL(recursiveMemoization)`**

### 假设2: 记忆化表未正确初始化（可能性：中）
- `.bss` 段由系统清零，但如果板上内核/链接器行为不同可能导致非零初始值
- 记忆化逻辑检查 `hit[idx] == 0` 判断未命中，如果初值非零则永远不命中

### 假设3: 索引计算溢出或越界（可能性：中）
- 当前公式 `i*20000 + w`，对 i=25, w=150: `25*20000+150 = 500150`
- 表大小 20480000 字节 = 5120000 int，索引 500150 在范围内
- 但如果汇编生成的索引公式有误（如乘数错误），可能导致访问错位

### 假设4: 板上链接了不同版本的代码（可能性：高）
- 评测系统可能缓存了旧的二进制文件
- 虽然 `RecursiveMemoization.cpp` 在 HEAD，但板上编译时可能未拉取最新代码
- 或者编译时 CMake 配置未包含该文件（但本地 `git show HEAD:CMakeLists.txt` 已确认包含）

## 下一步排查与修复方案

### 立即行动项

1. **修复 OPT_DISABLE 实现（高优先级）**
   - 检查 `src/opt/Optimizer.cpp` L66：`recursiveMemoization(module)` → `PASS_CALL(recursiveMemoization)`
   - 验证 `PassManager::passEnabled()` 是否正确读取环境变量
   - 重新 A/B 验证：OFF 组应无 memo 符号且耗时显著不同

2. **定位记忆化运行时失效原因**
   - 汇编已生成表和查表逻辑，但运行时耗时异常（1.9s 对比预期 <0.1s）
   - 可能原因：
     - 索引计算错误（需手工分析汇编 `i*20000+w` 实现）
     - 表初始化问题（.bss 段是否正确清零）
     - 条件判断错误（边界检查 `0≤i<256 && 0≤w<20000` 是否正确）

3. **禁用记忆化并对比基线（当前唯一可行的验证方式）**
   - 由于 OPT_DISABLE 失效，手工修改 `Optimizer.cpp` 注释掉 `recursiveMemoization()` 调用
   - 重新编译运行，获得真实的无记忆化基线
   - 如果基线也是 ~1.9s，说明问题在别处（如 qemu 性能）

### 降级方案

- **如果 1h 内无法修复** → 暂时跳过 knapsack（板上 140s），优先攻克 many_mat_cal (316s)
- Loop tiling/interchange 收益更确定，不依赖复杂 pass 基础设施

## 时间分配建议

- 如果 1h 内无法定位板上失效原因 → 立即转向 many_mat_cal
- many_mat_cal 三例占总时间 47%，收益更确定
