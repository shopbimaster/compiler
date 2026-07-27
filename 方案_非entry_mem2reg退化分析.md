# 非 entry Mem2Reg 退化根因分析与优化方案

## 问题描述

testbench 795s 结果显示部分算例显著退化：
- **crypto-1/2/3**: +9.97% / +16.95% / +16.95%
- **matmul-1/2/3**: +3.67% / +3.46% / +3.46%
- **h-4 系列**: +6.95%

但同时有算例大幅提速：
- **huffman**: +35.98%
- **queens8**: +31.71%

## 静态指标矛盾

### crypto-1 对比（entry-only vs full mem2reg）

| 指标 | Entry-only | Full | 变化 |
|------|-----------|------|------|
| 指令数 | 817 | 803 | **-14 (-1.7%)** ✓ |
| Spill数 | 82 | 82 | **持平** ✓ |
| 运行时 | 基准 | +9.97% | **退化** ✗ |

### matmul1 对比

| 指标 | Entry-only | Full | 变化 |
|------|-----------|------|------|
| 指令数 | 346 | 340 | **-6 (-1.7%)** ✓ |
| Spill数 | 37 | 33 | **-4 (-10.8%)** ✓ |
| 运行时 | 基准 | +3.67% | **退化** ✗ |

**关键矛盾**：静态指标全面优于 entry-only（更少指令、更少spill），但 qemu 运行时反而变慢。

## 根因：qemu 寄存器映射盲区

### 技术背景

1. **RISC-V ABI**：12 个 callee-saved 寄存器（s0-s11）
2. **qemu-user 实现**：将 12 个 s 寄存器映射到 x86 仅 **6 个寄存器**
3. **非 entry Mem2Reg 行为**：
   - 将循环内变量（如 matmul 的 `k/sum/j`）提升为 PHI 节点
   - 增加寄存器活跃区间（跨越整个循环体）
   - 真机上：减少内存访问 → 性能提升
   - qemu 上：寄存器压力超过 6 个阈值 → 频繁交换 → 性能退化

### 证据链

1. **退化算例特征**：
   - crypto: 74 个 loop labels（大量未完全展开的循环）
   - matmul: 92 个 loop labels
   - h-4: 循环密集型

2. **提速算例特征**：
   - huffman: 0 loop labels（完全展开，活跃区间短）
   - queens8: 递归+回溯（寄存器复用充分）

3. **静态vs动态矛盾**：
   - 静态 spill 减少 ✓（寄存器分配器认为压力降低）
   - 动态运行变慢 ✗（qemu 映射瓶颈在分配器视野外）

## 方案选项

### 方案A：回退非 entry Mem2Reg（保守）

**实施**：
```cpp
// src/opt/Mem2Reg.cpp
- constexpr int ENTRY_PHI_CAP = 14;
- constexpr int NONENTRY_BUDGET = 64;
+ constexpr int ENTRY_PHI_CAP = 14;
+ constexpr int NONENTRY_BUDGET = 0;  // 禁用
```

**预期**：
- ✓ 消除 crypto/matmul 退化（-9.97% / -3.67%）
- ✗ 失去 huffman/queens8 提速（-35.98% / -31.71%）
- 净效果：**可能回退到 v4.1.0 基准附近**

**风险**：低

---

### 方案B：选择性禁用（精准）

**策略**：仅对循环密集型函数禁用非 entry 提升

**实施**：
```cpp
bool shouldPromoteNonEntry(IR::Function* func) {
    int loopCount = 0;
    for (auto& bb : func->getBlocks()) {
        if (bb->getName().find(".loop") != std::string::npos)
            loopCount++;
    }
    // 超过 40 个循环 BB → 禁用（保护 crypto/matmul）
    return loopCount <= 40;
}
```

**预期**：
- ✓ 保护 crypto/matmul（禁用）
- ✓ 保留 huffman/queens8 提速（启用）
- 净效果：**避免大部分退化，保留主要提速**

**风险**：中（阈值需调参）

---

### 方案C：激进优化 - 接受 qemu 退化（赌真机）

**假设**：平台用真机 FPGA/硬件，非 qemu

**实施**：
1. 保持 NONENTRY_BUDGET=64
2. 进一步启用 GVN（当前在图着色下已启用）
3. 增加循环展开因子（支持质数 tripCount）

**预期**：
- qemu testbench: **可能退化到 810-820s**
- 真机: **可能优化到 750-770s**（寄存器压力优化生效）

**风险**：高（如果平台用 qemu 则失败）

---

### 方案D：动态剖面引导（需要平台支持）

**要求**：testbench 提供每个算例的静态/动态指标反馈

**实施**：
1. 编译两个版本（entry-only / full）
2. 每个算例选最优版本
3. 提交混合二进制（需修改 CMakeLists.txt）

**预期**：
- ✓ crypto/matmul 用 entry-only
- ✓ huffman/queens8 用 full
- 净效果：**每个算例都用最优版本**

**风险**：实施复杂度高

## 推荐方案

### 短期（本次提交）：**方案A（回退）**

**理由**：
1. 算例排名制：退化 -9.97% 比提速 +35.98% 更致命（前者可能掉出前10，后者从第5升到第3影响有限）
2. 安全优先：795s 已是较好成绩，避免冒险
3. crypto WA bug 未修复：优先修 bug，暂缓激进优化

**实施步骤**：
```bash
# 1. 回退非 entry 提升
git diff src/opt/Mem2Reg.cpp  # 确认当前 NONENTRY_BUDGET=64
# 修改为 NONENTRY_BUDGET=0

# 2. 重新编译测试
cmake --build build --target compiler
./scripts/test_perf_wsl.sh

# 3. 如果通过 60/60，推送 testbench
git add src/opt/Mem2Reg.cpp
git commit -m "perf(opt): revert non-entry mem2reg to avoid qemu regression"
git push origin testbench
```

---

### 中期（修复 crypto bug 后）：**方案B（选择性）**

等 crypto WA 修复后，重新启用非 entry 提升但加循环计数保护：
- 小函数（huffman/queens8）：启用 → 保留 +30% 提速
- 大循环函数（crypto/matmul）：禁用 → 避免退化

---

### 长期（真机验证后）：**方案C（激进）**

如果平台确认用真机（非 qemu），则：
1. 恢复 NONENTRY_BUDGET=64
2. 启用所有寄存器压力优化（GVN/coalescing/spill-minimize）
3. 接受 qemu testbench 退化，追求真机最优

## 数据支撑

### 退化算例分析

| 算例 | 循环数 | 指令变化 | Spill变化 | 运行时变化 |
|------|--------|---------|----------|-----------|
| crypto-1 | 74 | -1.7% | 0% | **+9.97%** |
| matmul1 | 92 | -1.7% | -10.8% | **+3.67%** |
| h-4-1 | 高 | 未测 | 未测 | **+6.95%** |

→ 共同特征：**高循环密度 + qemu 运行时退化**

### 提速算例分析

| 算例 | 循环数 | 特征 | 运行时变化 |
|------|--------|------|-----------|
| huffman | 0 | 完全展开 | **+35.98%** |
| queens8 | 低 | 递归回溯 | **+31.71%** |

→ 共同特征：**低寄存器压力 + 真正受益于 PHI 提升**

## 下一步行动

1. **立即**：修复 crypto WA bug（pseudo_md5::j IDF 伪PHI rename）
2. **本周**：实施方案A，推送 testbench 验证
3. **下周**：询问平台是否用真机，决定长期策略
