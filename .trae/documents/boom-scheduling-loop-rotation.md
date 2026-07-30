# BOOM 双发射调度优化 — 循环旋转 (Loop Rotation, E4)

## 摘要

在无 bug 基线分支 `Peephole-test` @ `aaf20de`（已含 P1/P2/P4/P6/P8 + E1/E2，功能 95/100、性能 60/60）上，新增**一项**以调度为核心、不追求指令数下降的针对性优化：**循环旋转 (Loop Rotation)**。

**核心论点**：当前所有 `while` 循环的回边都是无条件 `j header`（已实证，01\_mm1 单文件 9 处）。该 `j` 每迭代占用 1 个取指槽位，且阻断 fall-through 流。旋转将 `while(cond){body}` 重构为 `guard(cond, 一次) + do{body}while(cond)`，把回边从「无条件 `j`」变为「latch 处的条件分支」，消除每迭代 1 条 `j`，同时让循环体成为连续的 fall-through 流。

**为什么这是"调度"而非"减指令"**：旋转的主要收益是**控制流结构改善**（取指流连续、回边可预测），`j` 的消除是结构改善的副产物，而非代数化简。这与 P6（纯指令消除）有本质区别，符合用户"不追求减少指令，而是用更科学的调度方法"的导向。

**严谨性保障**：保守实现（仅单 BB 体循环 + SSA IV + ICMP 条件），全套功能/性能对照测试，`LOOP_ROTATE_OFF=1` 环境变量 A/B 开关，静态汇编 diff 验证无代码膨胀。

***

## 当前状态分析（Phase 1 探索结论）

### 基线分支状态

* **分支**：`Peephole-test` @ `aaf20de`（`perf(opt): Peephole 优化 + 循环头对齐修正`）

* **已含优化**：P1 归约分裂、P2 超大展开、P4 EarlyReturn→Select、P6 宏融合、P8 LOAD 前移、E1 mul latency、E2 fall-through、LSR 禁用、Peephole 全套

* **测试状态**：功能 95/100（预存失败 62/68/75），性能 60/60，hfunc O0 40/40、O1 36/40（预存 4 失败）

* **无 bug**：用户已确认此版本为"真正没有 bug 的版本"

### 实证的回边模式（01\_mm1.S 生成汇编）

```
.Lmain_while_cond_18:        ; header（条件检查）
  bge     s7, s11, .Lmain_while_end_20
.Lmain_while_body_19:        ; body（单 BB，含 IV 更新）
  ...
  j       .Lmain_while_cond_18   ; ★ 回边：无条件 j（每迭代执行）
.Lmain_while_end_20:         ; exit
```

01\_mm1.S 中共有 **9 处** `j .L..._cond` 回边。每个回边在 N=1024 的循环中产生 1024 次无条件跳转。这是 BOOM 前端带宽的纯浪费——`j` 占用 1 个取指槽位，且打断 fall-through 流。

### 现有基础设施核查（实证）

| 组件                       | 位置                                                                                              | 状态                                | 旋转复用      |
| ------------------------ | ----------------------------------------------------------------------------------------------- | --------------------------------- | --------- |
| NaturalLoop API          | [Optimizer.h:84-100](file:///d:/VSCodeProjects/compiler/include/opt/Optimizer.h#L84)            | header/latch/body/exitBlocks 字段齐全 | ✅ 循环检测    |
| `getLoopsInnermostFirst` | [LoopFind.cpp](file:///d:/VSCodeProjects/compiler/src/opt/LoopFind.cpp)                         | 内层优先遍历                            | ✅ 遍历入口    |
| `analyzeLoopInduction`   | Optimizer.h:114                                                                                 | 返回 IV/start/step/tripCount        | ✅ IV 识别   |
| `cloneInstruction`       | [LoopFullUnroll.cpp:20-128](file:///d:/VSCodeProjects/compiler/src/opt/LoopFullUnroll.cpp#L20)  | 支持 ICMP/SELECT/PHI 映射等全 opcode    | ✅ 条件复制    |
| `fullUnrollSingleBB`     | [LoopFullUnroll.cpp:135](file:///d:/VSCodeProjects/compiler/src/opt/LoopFullUnroll.cpp#L135)    | 单 BB 体处理 + PHI 模式                 | ✅ 算法参考    |
| BasicBlockReordering     | [BasicBlockReordering.cpp](file:///d:/VSCodeProjects/compiler/src/opt/BasicBlockReordering.cpp) | 支配树 DFS + fall-through 启发         | 旋转后自动布局   |
| E2 fall-through          | TargetCodeGen `emitCondBr`                                                                      | 选最优 fall-through 目标               | 旋转后自动生效   |
| **Loop Rotation**        | —                                                                                               | **不存在**                           | **本计划新建** |

### 为什么不能用 block reordering 替代旋转

支配者约束（[BasicBlockReordering.cpp:6-12](file:///d:/VSCodeProjects/compiler/src/opt/BasicBlockReordering.cpp#L6)）：header 支配 body，故 header 必须排在 body 之前 → 回边 body→header 必为后向跳转，**任何块重排都无法让回边 fall-through**。必须通过 CFG 重构（旋转）把条件从 header 移到 latch。

***

## 微架构论证（严谨论证，对应"更科学的调度方法"）

### BOOM 关键参数（来自 project\_memory + STATUS.md 调研）

| 参数    | 值               | 含义               |
| ----- | --------------- | ---------------- |
| 取指带宽  | 16B/周期          | 每周期 4 条非压缩指令     |
| 误预测惩罚 | \~14 周期         | 分支预测错误的代价        |
| ROB   | 16 entries      | 较小，编译器暴露 ILP 有价值 |
| 分支预测器 | 2-bit 计数器 + BTB | 后向分支预测 taken     |

### 旋转前后的逐迭代分析

**旋转前**（当前 `while(cond){body}` 形态）：

```
header: cond_br (i>=n) → exit[taken,冷] / body[fall-through,热]
body:   ... ; j header              ; 无条件后向跳转[热,每迭代]
```

* 每迭代：1 条条件分支（header，fall-through=热，预测正确）+ 1 条无条件 `j`（后向，预测 taken 正确，但**占用 1 取指槽**）

* 误预测：仅退出时 1 次（header 条件翻转）

**旋转后**（`guard + do{body}while(cond)` 形态）：

```
guard:  cond_br (i>=n) → exit / body    ; 仅 1 次（入口守卫）
body:   ... ; cond_br (i.next>=n) → exit[fall-through,冷] / body[taken,后向,热]
```

* 每迭代：1 条条件分支（body 末尾，taken=body=后向=热，预测 taken 正确；fall-through=exit=冷）

* 误预测：仅退出时 1 次（body 条件翻转）

* **消除**：每迭代 1 条无条件 `j`

### 收益结构对比

| 维度       | 旋转前              | 旋转后                    | 差异                    |
| -------- | ---------------- | ---------------------- | --------------------- |
| 每迭代分支指令数 | 2（1 cond + 1 j）  | 1（1 cond）              | **-1**                |
| 每迭代取指槽位  | 多 1 个 `j` 槽      | 无 `j`                  | **省 1 槽/迭代**          |
| 回边预测     | `j` 后向 taken（正确） | cond 后向 taken（正确）      | 持平                    |
| 退出误预测    | 1 次              | 1 次                    | 持平                    |
| 静态代码大小   | header + body    | guard + body（条件复制）     | +2\~4 指令/循环（guard 条件） |
| 循环体连续性   | body 末尾被打断（j）    | body 末尾 fall-through 流 | **改善**                |

**关键判断**：旋转的收益是**取指带宽 + 控制流连续性**，不是指令数。`j` 的消除是结构改善的副产物。对 BOOM 16B/周期取指、N=1024 的紧循环，省 1 槽/迭代 = 省 1024 次取指重定向。

### 与 E2 fall-through 回归的关系（安全性论证）

project\_memory 记录：E2 fall-through 在 h-4-03 (+14.62%)、huffman (+4%) 回归，原因是**无 PGO 时 if-then 热路径猜错**。

**旋转不同**：回边"热"是**可靠静态启发**（循环迭代 N 次，回边执行 N-1 次，退出 1 次）。后向分支预测 taken 在 BOOM 2-bit 预测器下几乎总是正确。这与 if-then 的不可靠猜测有本质区别。因此旋转**不会重蹈 E2 的 if-then 回归**。

### 不做的优化（避免重蹈覆辙）

| 不做                  | 原因                               |
| ------------------- | -------------------------------- |
| 基本块内指令重排强化          | OoO 硬件已覆盖（STATUS.md 收益分区：低/零收益）  |
| P6 式纯指令消除           | 用户明确"不追求减少指令"                    |
| 多 BB 体循环旋转（本轮）      | CFG 重构复杂度高，风险大；留作下一轮验证单 BB 收益后再议 |
| 软件 pipelining (SMS) | 寄存器压力瓶颈，P8 已做轻量版（LOAD 前移）        |
| 精确发射口配对             | OoO 硬件绕过静态约束                     |

***

## 实施方案

### 步骤 1：新建循环旋转 pass

**文件**：新建 `src/opt/LoopRotation.cpp`

**注册**：

* `include/opt/Optimizer.h`：在 `loopUnrolling` 声明附近添加 `bool loopRotation(IR::Module* mod);`

* `src/opt/Optimizer.cpp` `runO3`：在 `loopFullUnroll` **之前**插入（旋转后更易完全展开；且在 `basicBlockReordering` 之前，让重排看到旋转后的 CFG）

  ```cpp
  // E4: LoopRotation — while(cond){body} → guard + do{body}while(cond)
  // 消除回边无条件 j，让循环体成为 fall-through 流。在展开前运行：
  // 旋转后的 do-while 形态更易被 LoopFullUnroll 识别为单 BB 体。
  if (PASS_CALL(loopRotation)) {
      simplifyCFG(mod);
      constantFolding(mod);
      deadCodeElimination(mod);
      o3Changed = true;
  }
  ```

* `CMakeLists.txt`：在 `src/opt/LoopFullUnroll.cpp` 附近添加 `src/opt/LoopRotation.cpp`

**环境变量开关**：复用 `PASS_CALL(loopRotation)` 宏，`OPT_DISABLE=loopRotation` 可关闭；额外支持 `LOOP_ROTATE_OFF=1` 别名（便于记忆）。

### 步骤 2：旋转算法（保守版，仅单 BB 体）

**适用条件**（全部满足才旋转）：

1. `getLoopsInnermostFirst(func)` 取最内层循环
2. 循环体 = `{header, body}`，body 是 latch（header→body→header），body 为单 BB
3. header 的 terminator 是 `COND_BR`，且仅有一条非 terminator 指令（ICMP）。header 无 PHI（IV PHI 应在 body，或 header 的 PHI 可移至 body）
4. IV 是 SSA PHI 形式（`%i = phi [init, preheader], [%i.next, body]`）。若 IV 仍是 alloca/LOAD 形式，**跳过**（保守）
5. ICMP 的操作数：一个是 IV（或 IV 的 PHI），另一个是循环不变量（常量或定义在循环外的值）
6. body 无 CALL / 无 STORE 到循环不变地址 / 无可能异常的指令
7. exit 块是 header 的直接后继之一

**算法**（对每个满足条件的循环）：

```
原始 CFG:
  preheader → header
  header: %cond = icmp %iv, %bound; cond_br %cond, exit, body
  body:   %iv = phi [init, preheader], [%iv.next, body]
          ... (%iv.next = add %iv, step; ...)
          br header
  exit:   ...

旋转后 CFG:
  preheader → guard
  guard:  %cond0 = icmp %init, %bound; cond_br %cond0, exit, body   ; 入口守卫（用初始 IV 值）
  body:   %iv = phi [init, guard], [%iv.next, body]                 ; PHI 改为从 guard 来
          ... (%iv.next = add %iv, step; ...)
          %cond = icmp %iv.next, %bound; cond_br %cond, exit, body  ; 回边条件（用更新后 IV 值）
  exit:   ...
```

**变换步骤**：

1. **创建 guard 块**：在 preheader 之后插入新 BB `guard`
2. **复制条件到 guard**：用 `cloneInstruction` 克隆 header 的 ICMP，但操作数 `%iv` 替换为 `%init`（PHI 的 preheader incoming value）。`%bound` 不变（循环不变量）
3. **guard 的 terminator**：`cond_br %cond0, exit, body`（与 header 同方向：true→exit, false→body）
4. **body 末尾追加条件**：克隆 header 的 ICMP，操作数 `%iv` 替换为 `%iv.next`（body 内更新的值）。新增 `cond_br %cond, exit, body` 替换原 `br header`
5. **更新 body 的 PHI**：`%iv = phi [init, guard], [%iv.next, body]`（incoming block 从 preheader 改为 guard，从 header 改为 body 自身——因为回边现在是 body→body 自循环）
6. **重连前驱**：preheader 的 terminator `br header` 改为 `br guard`
7. **删除 header 块**（其指令已迁移到 guard 和 body）
8. **更新后继/PHI**：exit 块若有 PHI 来自 header 的 incoming，改为来自 guard 和 body

**SSA 安全性**：

* `%init`（PHI 的 preheader incoming）在 guard 处可用（guard 是 preheader 的直接后继）

* `%iv.next` 在 body 末尾可用（body 内定义）

* `%bound` 循环不变量在 guard 和 body 均可用

* 不引入新的非 SSA 形式

**名称唯一性**：克隆的 ICMP 用 `.rot` 后缀（参考 `cloneInstruction` 的 `.c` 后缀机制）

### 步骤 3：保守边界与跳过条件

* body 指令数 > 60：跳过（避免过大循环旋转后代码膨胀）

* 循环嵌套深度 > 3：跳过（外层循环旋转收益小）

* header 含 PHI（除 IV 外有其他 PHI）：跳过（多 PHI 处理复杂）

* body 含 COND\_BR（内部有 if）：跳过（本轮仅单 BB 体，多 BB 留待下轮）

* trip count 已知且 ≤ 4：跳过（小循环旋转收益小，LoopFullUnroll 会直接展开）

### 步骤 4：与现有 pass 的协同

* **旋转在 LoopFullUnroll 之前**：旋转后 do-while 形态的循环，若 trip count 小，LoopFullUnroll 可直接展开（旋转后的单 BB 自循环更易展开）

* **旋转在 BasicBlockReordering 之前**：旋转改变 CFG 后，BasicBlockReordering 的支配树 DFS 会自然把 guard→body→exit 排成 fall-through 顺序（guard 支配 body 和 exit，DFS 先序即此顺序）

* **旋转在 InstructionScheduling 之前**：旋转后 body 变单 BB 自循环，InstructionScheduling 的分段调度可在 body 内提前 LOAD（P8 已支持）

* **E2 fall-through 自动生效**：旋转后 body 的 `cond_br %cond, exit, body`，E2 会选 body 为 taken（后向自循环）、exit 为 fall-through（前向冷路径），无需额外改动

***

## 测试流程（严谨测试，防止超时）

### 测试原则

1. **每步对照**：每个优化项配 A/B 开关，对照测量而非只看绝对值
2. **静态先行**：先验证汇编无意外变化、无代码膨胀，再跑动态测试
3. **区分真假 TLE**：project\_memory 已证 FPGA harness 有随机假 TLE。QEMU 计时 <0.5s 的用例若 FPGA 报 TLE≥300s，判定为假 TLE，需重跑 2-3 次
4. **mtime 陷阱防范**：改完代码后 `touch` 所有改动文件 + 干净重建

### 测试步骤

**T1. 构建验证**

```bash
# WSL 内，touch 改动文件避免 mtime 陷阱
touch src/opt/LoopRotation.cpp src/opt/Optimizer.cpp include/opt/Optimizer.h CMakeLists.txt
cmake --build build -j 2>&1 | tail -5
```

验证：编译无 warning/error，`build/compiler` 生成成功。

**T2. 功能测试（O1 + O0 + hfunc）**

```bash
bash scripts/run_tests.sh func O1    # 目标 ≥95/100（预存 3 失败）
bash scripts/run_tests.sh func O0    # 目标 ≥99/100
bash scripts/run_tests.sh hfunc O0   # 目标 40/40
bash scripts/run_tests.sh hfunc O1   # 目标 ≥36/40（预存 4 失败）
```

验证：无新增失败。特别关注 93\_nested\_calls、11\_BST（上轮 IfConversion 破坏的用例，确保旋转不重蹈）。

**T3. 性能测试（60/60 + QEMU 计时 A/B）**

```bash
# A: 旋转开启
bash scripts/run_tests.sh perf O1
# B: 旋转关闭（对照）
LOOP_ROTATE_OFF=1 bash scripts/run_tests.sh perf O1
# 计时对比（重点用例）
bash scripts/test_perf_wsl.sh   # 3 次取中位数
```

重点对比用例：01\_mm1/2/3、matmul1/2/3、conv2d-1/2/3、huffman-01/02/03、h-4-03、crypto-1/2/3、03\_sort1/2/3。
验证：60/60 全 pass；QEMU 计时无 >10% 回归（QEMU 不模拟双发射，主要看有无代码膨胀导致的翻译开销回归）。

**T4. 静态汇编分析（核心验证）**

```bash
# 生成 A/B 汇编对照
for c in 01_mm1 01_mm2 matmul1 conv2d-2 huffman-01 h-4-03 crypto-1; do
  ./build/compiler -S test/performance/$c.sy -o /tmp/rot_on.S -O1
  LOOP_ROTATE_OFF=1 ./build/compiler -S test/performance/$c.sy -o /tmp/rot_off.S -O1
  echo "=== $c ==="
  echo "回边 j 数: on=$(grep -c 'j .*cond\|j .*header' /tmp/rot_on.S) off=$(grep -c 'j .*cond\|j .*header' /tmp/rot_off.S)"
  echo "总行数: on=$(wc -l < /tmp/rot_on.S) off=$(wc -l < /tmp/rot_off.S)"
done
```

验证：

* 旋转开启后回边 `j` 数显著下降（01\_mm1 应从 9 降至 \~0 或少量未触发）

* 总行数（静态代码大小）增幅 < 3%（guard 条件复制的代价）

* 旋转后循环体出现 `bnez/beqz ... body` 自循环模式（回边为条件分支，非 `j`）

**T5. 关键用例反汇编确认**

```bash
riscv64-linux-gnu-objdump -d /tmp/mm1_rot_on | grep -A 20 '<mm>'
```

验证：循环体末尾是条件分支（`bge`/`blt` 到自身），无 `j` 回边；循环头有 guard 守卫。

**T6. 无 SEGFAULT 验证**
全 60 性能用例 + 100 功能用例编译期无 SEGFAULT；特别关注上轮出问题的 52\_scope、64\_calculator、shuffle0、crypto 系列。

### 超时防范专项

1. **代码大小监控**（T4）：若任一用例静态行数增幅 >5%，暂停并分析是否触发 I-cache 膨胀
2. **QEMU 计时回归阈值**（T3）：任一用例 QEMU 计时回归 >15% 视为可疑，需反汇编定位
3. **FPGA 假 TLE 判据**：QEMU <0.5s 但 FPGA TLE≥300s → 假 TLE（>1000× 量级不可能是代码问题），重跑 2-3 次确认
4. **不碰寄存器分配**（project\_memory 陷阱 14）：旋转不改变寄存器分配器输入语义，仅改变 CFG 结构

***

## 关键文件清单

| 文件                         | 改动类型                                            | 步骤     |
| -------------------------- | ----------------------------------------------- | ------ |
| `src/opt/LoopRotation.cpp` | 新建                                              | 步骤 1-3 |
| `include/opt/Optimizer.h`  | 修改：声明 `loopRotation`                            | 步骤 1   |
| `src/opt/Optimizer.cpp`    | 修改：`runO3` 注册 `loopRotation`（LoopFullUnroll 之前） | 步骤 1   |
| `CMakeLists.txt`           | 修改：添加 `src/opt/LoopRotation.cpp`                | 步骤 1   |

## 复用的现有基础设施

* `LoopFind.cpp` 的 `NaturalLoop` + `getLoopsInnermostFirst` — 循环检测

* `Optimizer.h:114` 的 `analyzeLoopInduction` — IV 识别（start/step/tripCount）

* `LoopFullUnroll.cpp:20-128` 的 `cloneInstruction` — 条件指令克隆（ICMP + valueMap）

* `LoopFullUnroll.cpp:135` 的 `fullUnrollSingleBB` — 单 BB 体处理参考

* `Optimizer.cpp:28` 的 `PASS_CALL` 宏 — 环境变量开关

* `BasicBlockReordering.cpp` — 旋转后自动 fall-through 布局

* `TargetCodeGen` E2 fall-through — 旋转后自动选最优分支方向

## 假设与决策

1. **仅单 BB 体循环**：本轮保守，仅处理 header + body（body 为 latch，单 BB）。多 BB 体（含 if-then）旋转留待下轮，验证单 BB 收益后再议。
2. **IV 必须是 SSA PHI**：若 IV 仍是 alloca/LOAD 形式（mem2reg 未提升），跳过。这排除了 crypto 等 useBlocks>3 的用例，但保证安全性。
3. **不改变寄存器分配**：旋转仅重构 CFG，不改变值语义，寄存器分配器输入语义不变（避免陷阱 14）。
4. **guard 条件复制代价可接受**：每循环 +2\~4 条静态指令（guard 的 ICMP + cond\_br），但循环内每迭代省 1 条 `j`。对 N≥10 的循环净收益为正。
5. **trip count ≤4 不旋转**：小循环直接被 LoopFullUnroll 展开，旋转无意义。
6. **A/B 开关必备**：`LOOP_ROTATE_OFF=1` 用于对照测试，任何回归可立即定位。

## 风险与缓解

| 风险                              | 缓解                                                                                    |
| ------------------------------- | ------------------------------------------------------------------------------------- |
| CFG 重构引入 SEGFAULT（PHI 处理错误）     | 保守约束（单 BB 体 + SSA IV + ICMP only）；T6 全用例 SEGFAULT 扫描；参考 fullUnrollSingleBB 的成熟 PHI 处理 |
| guard 条件复制导致代码膨胀 → I-cache 负收益  | T4 静态行数监控（<3%）；trip count≤4 跳过；body>60 跳过                                             |
| 旋转后 BasicBlockReordering 布局非最优  | 旋转在 Reordering 之前运行，支配树 DFS 自然排 guard→body→exit                                       |
| 与 LoopFullUnroll 交互（旋转后展开异常）    | 旋转在 FullUnroll 之前，旋转后的自循环形态更易展开；T2 功能测试覆盖                                             |
| 重蹈 IfConversion 的 93/11\_BST 覆辙 | 旋转不改 PHI 语义（仅改 incoming block），不改值定义；T2 专项验证 93/11\_BST                               |
| FPGA 假 TLE 干扰判断                 | T3 QEMU 计时对照；假 TLE 判据（>1000× 量级）；重跑 2-3 次                                             |

## 验证完成标准

* [ ] T1 构建通过

* [ ] T2 功能无新增失败（O1 ≥95、O0 ≥99、hfunc O0 40、hfunc O1 ≥36）

* [ ] T3 性能 60/60 + QEMU 无 >10% 回归

* [ ] T4 回边 `j` 数下降 + 静态行数增幅 <3%

* [ ] T6 无 SEGFAULT

* [ ] 提交到当前分支 `Peephole-test`（或用户指定的新分支），更新 STATUS.md

