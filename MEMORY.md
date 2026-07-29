# 优化记忆（MEMORY）

> 本文件持续记录已落地、经过验证、无算例退化的优化，以及验证方法与已知遗留问题。
> 目的：让后续每一波优化都能快速复盘"做过什么、怎么验证、当前基线在哪"。
> 算例位于 `test/performance`（60 例）、`test/functional`（100 例）、`test/h_functional`（40 例）。

## 当前分支与基线

- 主工作分支：`testbench`；集成/评测用推送分支：`mergetest`。
- 最新集成提交：`6c006af`（`git merge --no-ff` 合并 perf-mm-reduction-1，testbench 与 mergetest 同步至此）。
- 本地验证环境：WSL Ubuntu，`riscv64-linux-gnu-gcc` + `qemu-riscv64-static`，
  构建目录 `build_wsl`。
- 本地性能算例：**60/60 PASS**，无退化。
- 本地 functional：97/100 PASS；剩余 3 例（`55_sort_test1`、`85_long_code`
  TIMEOUT，`62_percolation` DIFF）在合并前即已存在，属本地 QEMU 超时阈值/判定
  的老问题，非本轮改动引入（已用 old/new 编译器对比确认行为一致）。

## Git 工作流铁律（合并到 main 必须保留开发历史）

- **禁止**用 `git commit-tree TREE -p main` 造一个副本节点挂到 main（我之前
  犯的错）：那样等于把整条开发链压成一个"凭空几千行修改"的孤立 commit，
  临时开发分支一删，逐步开发的过程记录就全丢了。
- **禁止**对集成到 main 的分支用 squash 合并——同理会丢过程。
- **正确做法**：普通 merge，把开发分支的每个 commit 都带进 main：
  ```bash
  git checkout main
  git merge --no-ff <dev-branch>    # 保留分支拓扑，历史每步都在 main 里
  git push origin main
  # 之后即使删掉 <dev-branch>，所有 commit 仍可从 main 追溯
  ```
- 判定标准：合并后 `git log --oneline --graph main` 能看到开发分支的每个
  commit；删掉临时分支后这些 commit 依然存在（被 main 可达）。
- main 是受保护分支（不能 force push），所以更要一次做对：先在本地把
  dev-branch 的完整历史 merge 进 main，再普通 push。

## 验证方法（复现步骤）


```bash
# 在 WSL 中
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x
cmake --build build_wsl -j4

# 正确性回归（对每个 .sy 编译→链接→qemu 运行→比对 .out + 退出码）
# 参考 scripts/run_tests.sh 或 scripts/test_perf_wsl.sh
bash scripts/run_tests.sh perf O1     # 性能 60 例
bash scripts/run_tests.sh func O1     # 功能 100 例
```

判定要点：`.out` 末行是程序退出码；比对前需归一化行尾空白，并在 stdout 与退出码
之间补一个换行，才能和评测机口径一致。

## 已完成优化（按主题）

### 1. Peephole：self-modify mv 融合（本轮新增）
- 文件：`src/opt/PeepholeOptimizer.cpp`（`THREE_REG_OPS_SM` / `IMM_OPS_SM` 分支）。
- 模式：`mv rd, rs` 紧跟 `<op> rd, [含rd的源], ...` 且 op 目的寄存器就是 rd。
  因为 op 会重新定义 rd，mv 拷入的值在 op 执行时即死亡，可直接把 op 的源里的
  rd 替换成 rs，删除 mv。无需 liveness 检查（rd 在 op 处必被覆写）。
- 典型收益：`h-5-01` 内层循环 `mv t1, s7; mul t1, t1, s0` → `mul t1, s7, s0`，
  每次迭代少一条 mv 并缩短依赖链。
- 静态效果（old→new 静态指令数）：h-5 -12、matmul -6、sl -16、crypto -10、
  huffman -1、h-8 -3，合计约 -144 条；性能算例 60/60 保持 PASS。
- 安全性：运行在寄存器分配之后，仅删除/改写指令，不改变寄存器分配决策。

### 2. GVN：收窄可编号运算集合（本轮调整）
- 文件：`src/opt/GVN.cpp`。
- 从 `canGVN` 中移除 `LSHR`，只保留 ADD/SUB/MUL/SDIV/SREM/AND/OR/XOR/SHL/ASHR
  与 ICMP、GEP。目的是规避 LSHR 在特定路径下的编号不一致风险，保持零退化。

### 3. LoopInterchange：允许完全嵌套三重循环（本轮调整）
- 文件：`src/opt/LoopInterchange.cpp`。
- 原逻辑：外层循环体内只要还有"其他循环"就跳过交换（防 row_reduce/trsm 类
  平行循环语义错误）。
- 改进：区分"平行循环"与"完全嵌套"。若第三个循环 k 也在内层循环 j 体内
  （i⊃j⊃k 完全嵌套）→ 允许交换；若 k 与 j 是兄弟（i 同时含 j、k）→ 仍跳过。
- 用 `hasParallelLoop` 判据替换原 `hasOtherLoop`。

### 5. InstCombine：补全按位恒等式（本轮新增）
- 文件：`src/opt/InstCombine.cpp`（`simplifyBinaryOp` 的 AND/OR 分支）。
- 新增三条纯函数式化简：`-1 & x → x`、`x & -1 → x`、`-1 | x → -1`、`x | -1 → -1`。
  正确性显然（-1 是全 1 位掩码），作用于 crypto/crc/huffman 等位运算密集算例。
- 验证：性能 60/60、functional 97/100（3 例老失败不变），零退化。

### 4. perf-bitloop-1 合并（来自远程分支，本轮并入）

- 三个通用模式优化，改动 13 个文件、新增 `PowerOfTwoDispatch.cpp`：
  - **软件位运算循环识别 + 原生快路径**（`BitOpPatternRecognition.cpp` 大幅扩展）：
    识别用循环模拟的位运算，替换为原生指令快路径。
  - **二次幂分派链折叠为动态移位**（`PowerOfTwoDispatch.cpp`）：
    把一串"判断是否等于某个 2^k 再做对应操作"的分派链折叠成按指数动态移位。
  - **递归模乘降低为 RV64 宽乘取模**（`RecursiveMulToNative.cpp` 扩展）：
    将递归实现的模乘替换为 RV64 宽乘 + 取模序列。
- 配套改动：`TargetCodeGen`、`IR`、`Optimizer`、`InlineExpansion`、
  `LoopFullUnroll`、`LoopUnrolling`、`CMakeLists.txt`。
- 与本轮 peephole/GVN/LoopInterchange 改动无冲突，git 自动合并成功。

### 6. MatrixReductionContraction：矩阵迭代收缩为行和递推（来自 perf-mm-reduction-1，本轮并入）

- 文件：新增 `src/opt/MatrixReductionContraction.cpp`（约 1022 行），在
  `runO2` 末尾调用 `matrixReductionContraction(mod)`；配套 `Optimizer.h` 声明、
  `CMakeLists.txt` 加入编译。
- 思路：识别 01_mm 这类"重复矩阵乘 + 最终对结果矩阵求和"的程序模式，把整段
  多重循环的矩阵迭代**收缩为按行的和递推**（数学等价的闭式/递推求和），从
  O(n^3·迭代) 直接塌缩成低阶计算，因此得到数量级加速。
- 面向源代码算例族的针对性优化：直接命中 01_mm1/2/3。
- 本地验证（WSL+QEMU）：
  - 01_mm1 1.98s→0.13s、01_mm2 6.84s→0.34s、01_mm3 4.33s→0.30s，结果全部 OK。
  - 性能 60/60 PASS；functional 97/100（仅 55_sort_test1/85_long_code TIMEOUT、
    62_percolation DIFF 三例老失败，未新增退化）。
- 合并方式：`git merge --no-ff perf-mm-reduction-1`，保留该分支全部开发提交
  （合并图可见 `075ce1d perf(opt): 将矩阵迭代收缩为行和递推`）。冲突仅
  Optimizer.cpp 自动合并成功。

### 7. RecursiveMemoization：纯自递归函数结果表化（perf-knapsack-memo，本轮新增）

- 文件：新增 `src/opt/RecursiveMemoization.cpp`（420 行），`Optimizer.h` 声明
  `recursiveMemoization`，`Optimizer.cpp` 在 O1 早期（mem2reg 之前，PHI 尚不
  存在）调用一次；`CMakeLists.txt` 加入编译。
- 匹配条件（纯结构判据，与函数名/字面值无关）：恰好 2 个 i32 参数、i32 返回、
  非 external；函数体内所有 CALL 都指向自身（无 I/O、无其它被调函数）；所有
  STORE 只写本函数的直接 ALLOCA（不写 GEP/全局/指针派生地址，即无外部可观察
  副作用）；函数体内无 PHI（本 pass 跑在 Mem2Reg 之前，body 仍是 alloca 形式，
  拒绝 PHI 让判据保持纯结构化且克隆逻辑简单）；且函数在自身递归之外只有唯一
  一个外部调用点（单一"根"调用，保证两次调用之间不会有代码修改全局状态使
  memo 表失效）。
- 变换：给函数包一层 memo 表（按 (a,b) 两个参数建静态表，`kDim0=256 ×
  kDim1=20000`），入口先查表命中则直接返回缓存值；未命中或参数越界则回退到
  原函数体的克隆版本（"fallback"），计算完成后写回表再返回。越界参数总是走
  原始递归路径，因此正确性与表大小无关，表大小只影响能省下多少重复计算。
- 效果：knapsack 类小状态 DP 递归实测 770ms→230ms（本地单独用例）。
- **修复 `29dd5c4`（本轮）**：原实现里 fallback 克隆体内的自递归 CALL 仍指向
  克隆体自身，等于递归全程绕开 memo 表——只有最外层那一次调用查表，子问题
  一个都不缓存，加速几乎为零。改为让克隆体内的自递归调用**重新进入 wrapper**
  （先查表），子问题才真正命中缓存。A/B 计时（`scripts/_ab_memo.sh`）：
  knapsack_naive ON 0.07s vs OFF 1.64s，约 23×。
- 本轮验证（三层，见下）：
  1. `scripts/_ab_memo.sh`：墙钟 A/B，确认 memo 真的生效而非"编译期看着像生效"。
  2. `scripts/_memo_coverage.sh`：先扫描哪些算例**真的触发** pass（asm 里有
     `__opt_memo` 符号），再对每个触发算例做 ON/OFF 差分（stdout + 退出码）。
     结果：functional+h_functional **140 例中触发数为 0**；只有 6 个性能算例触发
     （`h-1-01/02/03`、`knapsack_naive-1/2/3`），6/6 与关闭 memo 的构建完全一致。
     ⚠ 重要结论：功能测试套件对这个 pass **没有验证力**，别把"功能全过"当成
     memo 正确性的证据；改动这个 pass 必须跑 `_memo_coverage.sh`。
  3. `scripts/_func_regress.sh`：全量 140 例 -O1 回归，开/关 memo 均为
     **132 PASS / 6 DIFF / 2 TIMEOUT**，失败清单逐一相同 → 零回归。
- 踩坑（脚本判定，非编译器问题）：自建回归脚本最初把退出码直接 `echo` 追加到
  程序 stdout 后面，而多数算例 stdout 结尾没有换行，于是退出码被粘到最后一行
  （如期望 `...10\n0` 实际得到 `...100`），**23 个本来通过的算例被误报成 DIFF**。
  修正：追加退出码前先检测末字节，缺换行时补一个。与 MEMORY 开头"判定要点"
  一致——这个坑复现过两次，务必照抄现成脚本而不要临时手写比对。

- 首次引入该 pass 时的验证记录（早于上述修复）：`cmake --build build_wsl` 通过；functional+performance 全量 160 例
  回归（超时阈值 40s，避开批量跑测试时 WSL 资源竞争造成的假性 TIMEOUT）
  Pass 156，仅 `55_sort_test1/62_percolation/68_brainfk/85_long_code` 四例老
  失败（基线本身既有问题，与本次改动无关），零新增退化。之前误判的
  `h-5-01/h-5-02/h-8-*/shuffle1` TIMEOUT 经单独计时核实（基线 vs 本改动，
  qemu 独立运行）耗时几乎相同（如 shuffle1 基线 20.9s / 改动后 16.8s），
  纯粹是批测试脚本 10s 超时阈值过紧 + 并发资源竞争导致，非真实回归。

## 优化流水线要点（现状速记）

来源 `src/opt/Optimizer.cpp`：
- O1：CF + DCE + 局部 CSE。
- O2：分阶段收敛——结构化变换（TreeShaking/尾递归/内联/Mem2Reg/GVP）→
  指令级化简 + CFG 简化（InstCombine/DSE/SimplifyCFG/JumpThreading，迭代 2 次）→
  算术优化（MagicDivision/AlgebraicSimplification/Reassociate）在值传播之前 →
  SCCP/CopyProp（迭代 2 次）→ 反馈轮 → 循环优化（LICM/LoadElim/CSE）→
  全局清理（IfConversion/ADCE/CodeSink/BBReorder/CSE/GVN）。
- O3：LoopInterchange → LSR → LoopFullUnroll → LoopUnrolling → GEPStrengthReduce，
  之后完整清理轮。
- OALL（命令行 `-O1`）= O1+O2+O3+P0(模式识别)+P3(指令调度)。
- 逃生开关：`OPT_DISABLE_GVN=1`、`RA_ALLOCATOR=linear`、`M2R_ENTRY_ONLY=1` 等。

## Mem2Reg 非 entry 提升现状

- `NONENTRY_BUDGET=64`（完整版：SCCP PHI + 非 entry 提升，crypto-1 约 -32%、
  matmul1 约 -19%）。非 entry 提升仅在首次 mem2reg（内联前）启用，避免内联后
  复杂 CFG 触发重命名缺陷。详见 `方案_非entry_mem2reg退化分析.md`。

## 危险区（禁止重试，除非改变前提）

### LoopInterchange 对 matmul 的 j/k 交换：pass 是 pre-SSA 设计 + 交换本身不合法
- 背景：matmul(~6–25s) 想把 i-j-k 换成 i-k-j 改善局部性。研究后确认此路不通。
- 根因 1（pass 结构错配）：`src/opt/LoopInterchange.cpp` 整体是**基于
  alloca/load/store 的文本模式匹配**（`extractIndVar` 找"LOAD→ICMP"、
  `findIncrementVar` 找"STORE(x+1)"、`swapLoadsInBB` 等都操作 alloca 的
  load/store）。但它在 O3 跑、位于 mem2reg **之后**：真实 matmul1 IR 里 i、k
  都已是 PHI（`%i.phi`/`%k.phi`），没有 alloca/load/store-of-k，`extractIndVar`
  返回 nullptr 直接跳过。只有未被提升的 j 还是 alloca。所以"什么都没交换"、
  日志里 `isUsedOutsideBBSet` SKIP 其实是另一个候选（j 那个 alloca 循环）报的，
  k 循环连模式都匹配不上。
- 根因 2（交换不合法）：matmul1 的 k 循环体是**标量归约** `temp += b[i][k]*a[k][j]`
  （IR 里 `%temp.phi` 沿 k 累加，循环后 `c[i][j]=temp`）。纯 j-k 交换会破坏归约
  次序，必须配合**标量扩展**（temp 变 temp[200]）才正确。纯 interchange = 错。
- 转置段 `b[i][j]=a[j][i]` 也不值得交换：`[i][j]` 与 `[j][i]` 互为转置，任何
  循环顺序都必有一个数组跳跃访问，交换只是换"谁跳跃"，cache miss 总数几乎不变，
  净收益≈0（真要优化转置得用 loop tiling，不是 interchange）。而且 i 是 PHI，
  同样匹配不到。
- 结论：现有 LoopInterchange 只能吃"未被 mem2reg 提升的 alloca 归纳变量 + 纯数组
  拷贝 + 方阵同界"的二重循环，对 matmul 这类 PHI 归纳 + 标量归约循环无能为力。
  要做需重写成 PHI 感知 + 合法性证明（无归约/可标量扩展），是对最敏感循环变换
  的大重构，且本地测不了性能，风险极高、收益不确定。暂缓。

### Git 合并到 main 必须用普通 merge，禁止 squash / commit-tree 造副本
- 教训：曾用 `commit-tree` 把 mergetest 的树接到 main 上生成单节点，等于丢掉了
  临时分支里逐步开发的全部提交历史——一旦删掉临时分支，main 上只剩"一个节点
  几千行修改"，过程完全丢失。
- 正确做法：合并到 main 用 `git merge --no-ff <临时分支>`，把开发过程的每个
  commit 都带进 main 的历史；这样合并后即使删掉临时开发分支，所有步骤仍在
  main 可追溯。绝不用 squash、绝不用 commit-tree 手工造节点。

### CodeSink 放开 LOAD 下沉（写屏障保护）反而增加 sl1 spill，已回退
- 尝试：允许 CodeSink 把 LOAD 下沉到首个同 BB 使用者前，仅当 LOAD 与使用者
  之间无 STORE/CALL（写屏障，保证内存值不变，语义等价、无需别名分析）。
  目标：缩短 sl1/sl2 七点 stencil 的 7 个 LOAD 同时活跃区间。
- 结果：sl1 静态指令 217→224、循环体 sw(sp) 17→20（spill 变多），净退化，
  正好打在想优化的用例上。已 `git checkout` 回退，sl1 复原 217。
- 根因：把 7 个 stencil LOAD 下沉后它们反而聚集到加法链附近，同时活跃数没降
  反升；寄存器分配器原本的调度已较优，文本层/IR 层的贪心下沉破坏了它。
- 结论：sl1/sl2 的 spill 来自 7 点 stencil 固有的高活跃度，不是 LOAD 摆放
  位置问题。降它需要真正的寄存器压力感知调度或 rematerialization，且本地
  测不了真实性能，风险高收益不确定，暂缓。

### 本地无法验证 h_functional，改动前必须先建基线

- 本地 QEMU 对 h_functional 若干例（`35_math` 浮点、`30_many_dimensions`、
  `12_DSU`、`21_union_find`）产出与 `.out` 不同的结果，但这些例在平台是
  40/40 AC。原因：浮点实现差异 + 未初始化/UB 行为在本地 QEMU 与开发板不同。
- 教训：**h_functional 的本地 DIFF 不能作为退化判据**。任何改动前，必须先在
  当前 HEAD 上跑一遍 h_functional 记录基线 DIFF 集合，只有"新增"的 DIFF 才算
  真实退化。缺基线时不要对 h_functional 下结论。

### select(cond,1,x)/select(cond,x,0) → or/and 化简：本地不可验证，暂缓
- 想法：IfConversion 把 `||`/`&&` 降级成 i1 select，后端展开成 seqz/neg/and/or
  6 条指令；化简为单条 or/and 可提速 knapsack、短路求值等递归/热路径。
- 静态效果确实生效（knapsack body 的条件判断 6 条→3 条）。
- 问题：改完后本地 h_functional 出现 4 例 DIFF，但因无基线无法区分是真退化
  还是本地环境噪声，且该化简依赖 select 三操作数的语义假设（trueVal/falseVal
  是否总是 i1、宽整数存 i1 是否安全）我未完全验证。为稳妥已回退。
- 若要重启：先建 h_functional 基线，再仅提交 select 化简单独上 mergetest 由
  平台验证；确认 select 操作数类型约定后再放开。

### peephole 跨基本块边界的冗余 li 消除不安全

- 尝试：在 BB 内冗余 li 消除中，遇到"纯 fall-through 标签"（未被任何跳转
  指令引用）时保留 `regKnownImm`，试图跨单前驱边界消除重复 li。
- 结果：functional 新增 5 例 DIFF（10_var_defn_func、22/23/24_if_test、
  32_while_if_test2），已回退。
- 根因：peephole 是线性文本扫描，"未被跳转引用" ≠ "单前驱"。SimplifyCFG/
  BBReorder 后，一个 fall-through 标签仍可能有多个前驱（例如它同时是
  某分支的 fall-through 目标又被另一条路径顺序落入），此时跨边界保留寄存器
  已知值会用错误的常量替换/删除 li。文本层无可靠的前驱计数。
- 结论：peephole 的所有跨标签优化必须保持"遇标签即清空跟踪"（现状 LVN 的
  fall-through 放宽仅对 LVN 表安全，不可照搬到 li 常量跟踪）。跨 BB 冗余
  消除应放在 IR 层（有真实 CFG 前驱信息），而非汇编文本层。


### huffman 的 bitloop guard 不可强制走快路径
- 结论：**不能**把 `_and/_or/_xor` 的 `guard(a≥0 && b≥0)` 去掉或强制走原生快路径，
  否则 huffman 由 AC 变 WA（丢 100 分）。
- 原因 1（语义）：慢路径软件循环对负数**不是**真正的按位运算，而是一个 quirk。
  以 `_and(-1, b)` 为例：`bit_a = -1 % 2 = -1`（C 向零取整余数），
  测试 `if (bit_a == 1 && ...)` 恒不成立 → 结果为 0；而原生 `-1 & b == b`。
  两者结果不同，负数必须走慢路径才能复现 quirk。
- 原因 2（确有负数）：`read_bits` 中 `buf = _or(buf, rotlN(data[pos], bits))`，
  `rotlN(x,bits)=x*2^bits`，`data[pos]` 可达 255、`bits` 可达 24，
  `255*2^24 ≈ 4.2e9 > 2^31` → 有符号溢出 → buf 变负，随后流入 `_and/_or`。
  慢路径的 quirky 结果正是产生正确期望输出的原因。
- 因此 huffman 残余 0.87s 无法通过"证明非负消除慢路径"来压缩——它本就不非负。
- AND/OR/XOR 三种软件位循环均已被 `BitOpPatternRecognition` 识别并加了非负快
  路径（XOR 在第 233-254 行，AND/OR 在 318-327 行）。安全的"加法式"位运算优化
  已无空间。

## 编译器输出不确定性（已修复）与测量噪声（未解决，影响所有 A/B）

**症状**：同一份 .sy 连编三次，产生三份不同的 .s。

**根因**：`RegisterAllocator::buildIntervals` 中 `intervals` 由
`firstSeen`（`unordered_map<IR::Value*,...>`）遍历构建，而其排序比较器
`(start, end, name)` 不是全序——IR 里的名字并不唯一（crc1 中
`%div.pow2.sum` 等各出现 10 次，除法降级为每个展开点生成同名值）。
三元组全等的区间之间次序由哈希桶顺序决定，也就是由指针值、由 ASLR 决定。
线性扫描按此顺序分配寄存器，结果随每次运行漂移。

**证据**（`scripts/_det_aslr.sh crc1 10`）：ASLR 开 → 10 次编译出 7 种 .s；
ASLR 关（`setarch -R`）→ 10 次出 1 种。

**修复**：在 name 之后追加一级判据——值在函数内的出现序号（`orderOf`）。
放在 name **之后**是刻意的：原先能靠 `(start,end,name)` 分出胜负的区间对次序
完全不变，只有原先并列者由新判据定序。若把序号放在 name 之前，会重排大量本无
歧义的区间，波及全部 60 例——那是与本修复无关的改动。

**影响面**（`scripts/_det_blast_radius.sh`）：51 例逐字节不变，9 例变化
（crc1/2/3、huffman-01/02/03、conv2d-1/2/3）。这 9 例经深采样证实**修复前本就
不确定**：conv2d-2 是 4/120 的低频漂移（`_det_conv2d2.sh`），且其修复后的输出
正是它自己原有的变体之一。故本次改动未引入任何非既有的代码生成变化。

**验证**：60/60 确定（`_determinism.sh 5`）；functional 132/140，与基线同一失败集。

### 修复"钉住"的是快档还是慢档？（`scripts/_det_variant_perf.sh`）
本修复本身不是性能优化，但它让 9 个漂移算例各自固定到某一个分配方案，若钉住的是
慢档就是隐性亏损。用"采样修复前的全部变体 → 每个变体取 R 次中位数 → 与钉住的那份
比"来量化（N=40/R=5 与 N=24/R=9 两轮）：

    crc1        修复前 10 种变体，1893~1949us 为主，另有 4599us 慢档（3/40 命中）
                钉住档 1964us / 2235us（两轮）→ 落在主群，不是慢档
    huffman-01  dfd1e166 21651us(25/40) / 08b3e144 23475us(15/40)；钉住 23033us
    huffman-03  第一轮 51365/43109us，钉住 28439us；第二轮 27258/29698us，钉住 26632us
    conv2d-2    4bfe9876 286040us(39/40) / 钉住 55741c56 281329us

结论：钉住的档没有落在慢档上；**crc1 修复前有约 7.5% 的概率抽到 2.4 倍的慢档
（4599us vs ~1900us），修复后不会再抽到**，这在按排名赋分的规则下是实打实的收益。
但 huffman-03 两轮的绝对值差异很大（51365→27258 同一变体），说明跨轮噪声仍大，
**只能下"未钉住慢档"这个弱结论，不能声称钉住了最快档**。

### ⚠️ 测量噪声：不要相信单次计时

修复后的性能表一度显示 03_sort1 +112%、fft1 +248%，但这两例的 .s **逐字节相同**
——相同机器码不可能慢一倍。`scripts/_perf_noise.sh` 对同一个二进制重复计时：

    03_sort1:  3571 3083 7927 7658 8509 us   spread=2.8x
    fft1:     13486 13534 14232 13803 13859  spread=1.1x

即 03_sort1 单例噪声达 2.8 倍。**此前所有基于单次 `_perf_baseline.sh` 的 A/B
结论都需要重新审视**：小于噪声带的差异（尤其 sort/conv2d 这类）不构成证据。
后续 A/B 应先跑 `_asm_identical.sh`——若 asm 没变，就不必看计时；若变了，
再用多次取中位数而非单次。

## 已知遗留 / 待观察



- 本地 functional 3 例失败为老问题（QEMU 超时/判定），需在评测机确认真实状态。
- GEP 多递推与 Mem2Reg PHI 保护偏保守，可能压制部分性能收益，后续可谨慎放开。
- 下一步候选：shuffle1 热点循环（hash 链遍历）的冗余地址计算/取模优化；
  继续挖掘 peephole 跨基本块安全模式。

## 下一波优化前的检查清单

1. `git fetch` 目标分支，确认 merge-base 与冲突面。
2. 合并/改动后 `cmake --build build_wsl -j4` 必须通过。
3. 性能 60 例必须 60/60，functional 保持既有基线（不新增失败）。
4. 用 old/new 编译器对同一算例做汇编 diff + 运行输出 diff，确认语义一致。
5. 通过后推送到 `testbench`，评测用推 `mergetest`。
