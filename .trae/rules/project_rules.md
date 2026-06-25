# 项目架构与记忆文档

## 一、项目概述

SysY2022 语言编译器，将 SysY 源码编译为 RISC-V 64 (RV64GC) 汇编。

**编译管线**：`SysY源码 → ANTLR语法解析 → IRBuilder构建IR → 优化Pass管线 → TargetCodeGen生成汇编 → Peephole优化`

## 二、优化级别命令行映射（极其重要！）

**测评服务器仅支持 `-O1` 这一个大写优化选项**。

| 命令行参数 | 内部级别 | 包含的Pass                            | 用途               |
| ---------- | -------- | ------------------------------------- | ------------------ |
| `-O1`      | OALL     | O1 + O2 + O3 + P0                     | **测评服务器使用** |
| `-O0`      | O0       | 无优化                                | 评测基准           |
| `-o0`      | O0       | 无优化                                | 本地调试           |
| `-o1`      | O1       | CF + DCE + CSE + LICM                 | 本地逐级调试       |
| `-o2`      | O2       | O1 + 内联 + 额外CSE                   | 本地逐级调试       |
| `-o3`      | O3       | O1+O2 + 代数化简/循环交换/展开/尾递归 | 本地逐级调试       |

**不可违反的规则**：

1. **永远不要修改大写 `-O1` 到 OALL 的映射关系** — 这是测评服务器唯一支持的优化选项
2. 小写 `-o1`/`-o2`/`-o3` 仅用于本地逐级调试
3. 所有面向测评服务器的测试命令应使用 `-O1`（大写）
4. 本地调试时使用小写 `-o1`/`-o2`/`-o3` 逐级定位问题
5. **不要给测评服务器添加 `-O2`、`-O3` 等大写选项** — 服务器不支持

## 三、目录结构

```
compiler/
├── src/
│   ├── main.cpp              # 入口，命令行解析
│   ├── Compiler.cpp          # 编译管线：parse → optimize → emit
│   ├── antlr/                # ANTLR4 自动生成的词法/语法解析器
│   ├── ir/
│   │   ├── IR.cpp            # IR 数据结构：Value/Instruction/BasicBlock/Function/Module
│   │   └── IRBuilder.cpp     # 遍历 AST 构建 IR（Visitor 模式）
│   ├── opt/
│   │   ├── Optimizer.cpp     # 优化 Pass 调度入口（runO1/O2/O3/P0/P3）
│   │   ├── ConstantFolding.cpp
│   │   ├── DeadCodeElimination.cpp
│   │   ├── CSE.cpp           # 公共子表达式消除
│   │   ├── LICM.cpp          # 循环不变量外提
│   │   ├── InlineExpansion.cpp
│   │   ├── AlgebraicSimplification.cpp  # 强度削减 + 恒等式消除
│   │   ├── LoopInterchange.cpp          # 循环交换（带多项安全检查）
│   │   ├── LoopUnrolling.cpp            # 循环展开（最大8×）
│   │   ├── TailRecursionElimination.cpp # 尾递归→循环转换
│   │   ├── InstructionScheduling.cpp    # 分段指令调度（P3，暂禁用）
│   │   ├── BitOpPatternRecognition.cpp  # 位运算模式识别（P0）
│   │   ├── RecursiveMulToNative.cpp     # 递归乘法→原生乘法（P0，暂禁用）
│   │   ├── DominatorAnalysis.cpp        # 支配树分析
│   │   └── PeepholeOptimizer.cpp        # 汇编级窥孔优化
│   ├── backend/
│   │   ├── TargetCodeGen.cpp   # IR → RISC-V 汇编
│   │   └── RegisterAllocator.cpp
│   └── utils/
│       └── Logger.cpp
├── include/                   # 头文件
├── test/
│   ├── functional/            # 100 个功能测试
│   ├── h_functional/          # 40 个隐藏功能测试
│   ├── performance/           # 60 个性能测试
│   └── *.sy                   # 临时测试文件（可清理）
├── Solutions/
│   ├── 调研文档.md             # 优秀编译器范例调研与对比分析
│   ├── Cpl1/                  # 范例1：Gnalc（三层IR，仿射分析，高级循环变换）
│   └── Cpl2/                  # 范例2：LLVM风格IR（完整SSA，丰富后端优化）
├── scripts/
│   ├── run_tests.sh           # 统一测试入口
│   ├── quick_test.sh          # 快速单用例 O0 vs O3 对比
│   ├── run_func_tests.sh      # 功能测试
│   ├── clean.sh               # 清理构建产物
│   ├── debug/                 # 调试辅助脚本
│   ├── grammar/               # 语法测试脚本
│   └── setup/                 # 环境安装脚本
├── grammar/                   # ANTLR 语法定义
├── SysYlib/                   # SysY 运行时库（sylib.c/h）
├── logs/                      # 测试日志
│   ├── test7.txt / test8.txt / test9.txt  # 近期测评结果
│   └── bisect_test9/          # 临时 bisect 文件（已清理）
├── build/                     # 构建目录（不在版本控制中）
├── CMakeLists.txt
└── .trae/rules/project_rules.md  # 本文档
```

## 四、优化 Pass 详细说明

### 4.1 Pass 调度顺序

```cpp
// O1: 基础优化
CF → DCE → CSE → LICM → CF → DCE

// O2: 内联 + 全局变量提升 + 寄存器压力优化 + 新Pass
treeShaking
→ bitOpPatternRecognition → CF → DCE
→ inlineExpansion → CF → DCE
→ globalVariablePromotion → CF → DCE
→ deadStoreElimination → CF → DCE
→ instCombine → CF → DCE
→ simplifyCFG → CF → DCE
→ copyPropagation → CF → DCE
→ magicDivision → CF → DCE
→ loadElimination → CF → DCE
→ reassociate → CF → DCE
→ ifConversion → CF → DCE
→ CSE → CF → DCE
→ adce → CF → DCE
→ codeSink → CF → DCE
→ basicBlockReordering

// O3: 高级循环/递归优化
algebraicSimplification → CF → DCE
→ loopInterchange → CF → DCE
→ loopUnrolling → CF → DCE
→ tailRecursionElimination → CF → DCE

// P0: 特殊模式识别
recursiveMulToNative
→ bitOpPatternRecognition → CF → DCE

// P3: 指令调度
instructionScheduling → CF → DCE
```

### 4.2 当前启用的 Pass 及关键注意事项

| Pass                     | 级别  | 状态 | 关键注意事项                                                                                                                                                                       |
| ------------------------ | ----- | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ConstantFolding          | O1    | 启用 | `sitofp` 折叠成 `ConstantFloat` 后，IR 打印和代码生成均需正确处理 `ConstantFloat`                                                                                                  |
| DeadCodeElimination      | O1    | 启用 | 无已知问题                                                                                                                                                                         |
| CSE                      | O1/O2 | 启用 | 使用 `replaceAllUsesWith` 后 `erase`，安全                                                                                                                                         |
| LICM                     | O1    | 启用 | 仅 O1 运行一次；O2 不再次运行（避免与内联修改的 CFG 交互导致段错误）                                                                                                               |
| InlineExpansion          | O2    | 启用 | 单 BB 小函数（指令数 <20）和多 BB 函数（≤8 BB、≤60 指令）均可内联；多 BB 函数在同一 caller 中被调用超过 2 次跳过内联，避免代码膨胀导致性能下降                                     |
| AlgebraicSimplification  | O3    | 启用 | 强度削减：`sdiv/srem` 除以 2 的幂需要检查左操作数非负才能替换为 `ashr`/`and`                                                                                                       |
| LoopInterchange          | O3    | 启用 | **三项安全检查缺一不可**：`icmpUsesVar`、`isUsedOutsideBBSet`、`sameLoopBounds`；**外加 `hasOtherLoop` 检查**（外层循环体不能有除当前内层外其他循环）                              |
| LoopUnrolling            | O3    | 启用 | 仅展开迭代次数 ≤64 的简单 while 循环；`cloneNonTermInst` 中 STORE 指针操作数必须通过 `lookup` 查找                                                                                 |
| TailRecursionElimination | O3    | 启用 | `findBodyBlock` 将 entry block 拆分为 init 和 body；**幂等设计**：拆分后再次调用直接返回 BR 目标                                                                                   |
| BitOpPatternRecognition  | P0/O2 | 启用 | 模式匹配位运算优化；O2 中提前运行以消除自定义位运算函数，使 read_bits 等函数可被内联                                                                                               |
| GlobalVariablePromotion  | O2    | 启用 | 将标量全局变量提升为局部 ALLOCA；**跳过 const 全局变量**（.rodata 只读段）；**在遍历 BB 前先收集 RET 指令**，避免插入时迭代器失效；init 指令插入到 entry block 开头（ALLOCA 之后） |
| RecursiveMulToNative     | P0    | 启用 | 将递归乘法转换为原生 MUL 指令；**修复**：转换后清除空的非 entry BB，避免 CFG 中出现空 BB 导致段错误                                                                                |
| InstructionScheduling    | P3    | 启用 | **修复**：`scheduleBB` 中 vector::insert 导致迭代器失效的 bug — 先移除 terminator，清空 BB 后按新顺序 pushBack，最后恢复 terminator                                                |
| LoadElimination          | O2    | 启用 | 消除冗余 LOAD 指令；**安全检查**：跳过全局变量（`involvesGlobal`）、跳过跨 BB 被 STORE 的 ALLOCA、仅替换同 BB 内 uses、使用严格指针相等（`a == b`）                                |
| CodeSink                 | O2    | 启用 | 将指令下沉到更靠近使用者的位置；**安全约束**：LOAD 指令不可下沉（可能越过 STORE/CALL 改变内存读写顺序）                                                                            |
| DeadStoreElimination     | O2    | 启用 | 消除无用的 STORE 指令                                                                                                                                                              |
| InstCombine              | O2    | 启用 | 代数恒等式简化 + Store-to-Load 转发                                                                                                                                                |
| SimplifyCFG              | O2    | 启用 | 常量分支折叠 + 不可达块删除 + 块合并 + 空块消除                                                                                                                                    |
| CopyPropagation          | O2    | 启用 | 数据流复制传播                                                                                                                                                                     |
| MagicDivision            | O2    | 启用 | 常量除法转换为乘法+移位序列                                                                                                                                                        |
| Reassociate              | O2    | 启用 | 表达式重结合，优化常量折叠机会                                                                                                                                                     |
| IfConversion             | O2    | 启用 | 条件转换                                                                                                                                                                           |
| ADCE                     | O2    | 启用 | 激进死代码消除                                                                                                                                                                     |
| BasicBlockReordering     | O2    | 启用 | 基于支配树的拓扑排序基本块重排，优化 fall-through，确保定义在使用之前                                                                                                              |
| TreeShaking              | O2    | 启用 | 移除未使用的函数和全局变量                                                                                                                                                         |

### 4.3 关键设计决策

**为什么 O2 不二次运行 LICM？**
内联 Expansion 会修改 CFG（合并被调用函数的 BB），二次 LICM 会与修改后的 CFG 交互导致段错误。

**为什么 LoopInterchange 需要 `hasOtherLoop` 检查？**
row_reduce（conv2d）的 r 循环体中有两个 c 循环，交换后仅处理第一个内层循环，第二个仍使用原变量导致语义错误。

**为什么 TailRecursionElimination 需要拆分 entry block？**
尾递归的 BR 需要跳转到包含 base case 检查的代码，而非直接跳到返回块。拆分为 init（allocas+参数存储）和 body（base case 检查及后续逻辑）确保尾递归跳转后重新执行条件判断。

**为什么 `sameLoopBounds` 对于非方阵至关重要？**
`array[20][100]` 中 `i<20, j<100` 交换后 `j` 可达 99 → 越界 → SEGFAULT。

**为什么 GlobalVariablePromotion 要跳过 const 全局变量？**
const 全局变量放在 `.rodata` 只读段，退出时 STORE 回写会导致 SEGFAULT。const 全局变量值不变，无需回写。

**为什么 GlobalVariablePromotion 的 init 指令要插入到 entry block 开头？**
原始 LOAD 指令可能在 entry block 中，如果 init 指令插入到 terminator 之前，ALLOCA 定义会在 USE 之后。必须插入到所有 ALLOCA 之后、第一个非 ALLOCA 指令之前。

**为什么 GlobalVariablePromotion 要先收集 RET 再插入？**
在遍历指令列表时调用 `insertBefore` 修改列表会导致迭代器失效，exit LOAD 可能未被正确插入，导致 `%n.exit` 未定义。必须先收集所有 RET 指令，再批量插入。

**为什么 InstructionScheduling 的 scheduleBB 要先移除 terminator？**
指令列表是 `std::vector`，`vector::insert` 会使迭代器失效。原代码保存 terminator 迭代器后多次 insert，第一次 insert 后迭代器即失效导致 UB。修复：先移除 terminator → 清空 BB → 按序 pushBack → 恢复 terminator。

**为什么 CodeSink 不能下沉 LOAD 指令？**
LOAD 指令下沉可能越过 STORE/CALL 指令，改变内存读写顺序。例如：`load %ptr` → `store %val, %ptr` → `use %load`，若将 LOAD 下沉到 STORE 之后，LOAD 会读取到 STORE 写入的新值，而非原始值。

**为什么 RecursiveMulToNative 要清除空的非 entry BB？**
转换后非 entry BB 的指令被清空，但空 BB 仍留在 CFG 中，后续 Pass（如寄存器分配）遍历时遇到空 BB 可能段错误。必须清除这些空 BB。

## 五、编译与测试命令

```bash
# 编译（在 WSL 中）
cd build && make -j$(nproc)

# 全量测试（测评服务器级别）
bash scripts/run_tests.sh all O1

# 分套测试
bash scripts/run_tests.sh func O1    # 100 个功能测试
bash scripts/run_tests.sh hfunc O1   # 40 个隐藏功能测试
bash scripts/run_tests.sh perf O1    # 60 个性能测试

# 快速冒烟测试
bash scripts/run_tests.sh quick

# 本地逐级调试
bash scripts/run_tests.sh func o1    # 仅 O1
bash scripts/run_tests.sh func o2    # O1 + O2
bash scripts/run_tests.sh func o3    # O1 + O2 + O3

# 单用例快速对比 O0 vs O3
bash scripts/quick_test.sh <case-name>

# 生成 IR 或汇编
./build/compiler -o3 input.sy -o output.ir          # 生成 IR
./build/compiler -S -o3 input.sy -o output.S        # 生成汇编
```

## 六、调试工作流

1. **定位错误级别**：用 `-o1`/`-o2`/`-o3` 逐级运行，对比 O0 输出哈希找到引入错误的级别
2. **定位 Pass**：在 `Optimizer.cpp` 中二分注释/取消注释 Pass
3. **对比 IR**：`diff <(./compiler -o2 in.sy) <(./compiler -o3 in.sy)` 查看 O3 增加的变换
4. **QEMU 运行**：`bash scripts/run_tests.sh perf O1` 自动编译+链接+运行+对比

## 七、测试结果历史

| 测评   | 日期    | 结果                                                                                                                        |
| ------ | ------- | --------------------------------------------------------------------------------------------------------------------------- |
| test7  | 2026-06 | 27 WA，LoopInterchange 首版引入                                                                                             |
| test8  | 2026-06 | 21 WA + 5 TLE，LoopInterchange 修复未编译进二进制                                                                           |
| test9  | 2026-06 | 9 WA，旧二进制测试；本地全量 200/200 通过                                                                                   |
| test10 | 2026-06 | 本地全量 200/200 通过（含 GlobalVariablePromotion）；conv2d/many_mat_cal/knapsack_naive 从超时→通过；huffman 仍超时（~96s） |
| test12 | 2026-06 | 旧二进制测试（huffman TLE, conv2d 116s 等）；当前二进制全量 200/200 通过，LICM 修复后 huffman 107ms，conv2d-1 807ms         |

## 八、调研文档规则

**`Solutions/调研文档.md`** 是项目的重要参考文档，记录了所有已调研的优秀编译器范例、横向对比分析和优化建议。

**必须遵守的规则**：

1. **每次引入新范例（Solutions/CplN）后**，必须更新调研文档，新增该范例的详细分析
2. **每次参照范例改进项目后**，必须更新调研文档的"更新记录"和对应章节，标记已实施的优化
3. **调研文档的结构**：
   - 范例概览表（一）
   - 各范例详细分析（二）
   - 横向对比：公共基础设施（三）
   - 横向对比：优化思路（四）
   - 建议实施路线（五）
   - 更新记录（六）
4. 横向对比部分应保持三列（Cpl1 / Cpl2 / 我们）或更多列，清晰展示差异
5. 建议实施路线按优先级分阶段，每个优化标注来源范例

## 九、需要注意的陷阱

1. **IR 打印**：`ConstantFloat` 在 `Module::dump()` 的通用操作数打印路径（非 `loadToReg` 路径）中需要特殊处理，否则显示为 `%` 而非实际值
2. **STORE 指令**：`cloneNonTermInst` 中 STORE 的指针操作数必须通过 `lookup` 映射查找，否则指向旧 BB 的 ALLOCA
3. **BR vs COND_BR**：entry block 拆分后 terminator 变为无条件 BR，`findBodyBlock` 通过此特性实现幂等
4. **循环交换前提**：必须同时满足 4 项检查（`icmpUsesVar` + `isUsedOutsideBBSet` + `sameLoopBounds` + `hasOtherLoop`），缺一不可
5. **代数化简**：`sdiv/srem` 强度削减为 `ashr`/`and` 时，必须确保左操作数非负（有符号数右移除法和取模语义不同）
6. **测评服务器**：仅支持 `-O1`，所有优化必须通过此选项触发
7. **测试脚本**：run_tests.sh 中的 `SYLIB_A` 路径指向 `${BUILD_DIR}/libsylib.a`，由 CMake 构建
8. **QEMU 超时**：functional 测试超时 5s，h_functional/perf 超时 15s；93_nested_calls 在 QEMU 下运行较慢（约 6-7s），functional 测试中会出现 TIMEOUT，这是预存问题
