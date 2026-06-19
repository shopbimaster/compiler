# 项目规则与约定

## 优化级别命令行映射（极其重要！）

**测评服务器仅支持 `-O1` 这一个优化选项**，因此编译器必须将大写 `-O1` 映射到内部最高优化级别（OALL = O1+O2+O3）：

| 命令行参数 | 内部级别 | 包含的Pass                            | 用途               |
| ---------- | -------- | ------------------------------------- | ------------------ |
| `-O1`      | OALL     | O1 + O2 + O3 + P0 + P3                | **测评服务器使用** |
| `-O0`      | O0       | 无优化                                | 评测基准           |
| `-o0`      | O0       | 无优化                                | 本地调试           |
| `-o1`      | O1       | CF + DCE + CSE + LICM                 | 本地逐级调试       |
| `-o2`      | O2       | O1 + 内联 + 额外CSE/LICM              | 本地逐级调试       |
| `-o3`      | O3       | O1+O2 + 代数化简/循环交换/展开/尾递归 | 本地逐级调试       |

**不可违反的规则**：

1. **永远不要修改大写 `-O1` 到 OALL 的映射关系** — 这是测评服务器唯一支持的优化选项
2. 小写 `-o1`/`-o2`/`-o3` 仅用于本地逐级调试，分别对应内部的 O1/O2/O3 级别
3. 所有面向测评服务器的测试命令应使用 `-O1`（大写）
4. 本地调试某个优化 Pass 的问题时，使用小写 `-o1`/`-o2`/`-o3` 逐级定位
5. **不要给测评服务器添加 `-O2`、`-O3` 等大写选项** — 服务器不支持，只会被忽略

## 编译与测试命令

```bash
# 编译
cd build && make -j$(nproc)

# 运行测试（全部优化 = 测评服务器级别）
bash scripts/run_tests.sh func O1
bash scripts/run_tests.sh all O1

# 本地逐级调试
bash scripts/run_tests.sh func o1   # 仅 O1 优化
bash scripts/run_tests.sh func o2   # O1 + O2
bash scripts/run_tests.sh func o3   # O1 + O2 + O3
```

## 当前优化Pass状态

- **OALL**：O1+O2+O3+P0+P3，所有优化已全部上线
- **P0**：bitOpPatternRecognition 已启用；recursiveMulToNative 已启用（修复了 entry block 缺少 BR terminator 导致 crypto 编译段错误的问题）
- **P3**（instructionScheduling）：已启用。重写为分段调度（Segmented Scheduling），将基本块划分为连续可移动指令段和非可移动指令边界，仅在段内重排指令，避免跨段 use-before-def 问题
- **loopInterchange**（O3）：已启用。修复了 `swapICmpConstants`（交换 ICMP 常量/全局变量 LOAD 边界）和 `moveAllocaToEntry`（避免 use-before-def），非方阵越界问题已解决
- **loopUnrolling**（O3）：已启用。修复了 `cloneNonTermInst` 中 STORE 指令指针操作数未通过 `lookup` 查找的问题（导致 19_search 段错误）

## 性能分析结论（第七次测试 — 全部优化上线）

### 耗时 TOP 用例（O1 全部优化）

| 用例                   | 第六次  | 第七次  | 提升   | 共性原因                                        |
| ---------------------- | ------- | ------- | ------ | ----------------------------------------------- |
| shuffle1               | 2013ms  | 1510ms  | -25.0% | 函数调用开销，受益于指令调度+位运算优化         |
| knapsack_naive-(1,2,3) | ~1600ms | ~1612ms | 持平   | 递归 O(2^n) 算法，编译器无法优化                |
| conv2d-1               | 1412ms  | 1410ms  | 持平   | 5层嵌套循环 + 多维数组访问                      |
| h-5-(01,02,03)         | ~1013ms | ~911ms  | -10.1% | 3层嵌套循环（LU分解），受益于循环交换/展开      |
| h-8-(01,02,03)         | ~911ms  | ~809ms  | -11.2% | 3层嵌套循环（Nussinov DP），受益于循环交换/展开 |
| sl2                    | 813ms   | 710ms   | -12.7% | 3层嵌套循环（3D stencil），受益于循环交换/展开  |
| transpose2             | 812ms   | 709ms   | -12.7% | transpose 函数频繁调用，受益于指令调度          |
| h-4-03                 | 810ms   | 809ms   | 持平   | f(x)/max 函数调用在紧循环中，多BB函数无法内联   |
| many_mat_cal-(1,2,3)   | ~711ms  | ~710ms  | 持平   | 多重矩阵运算，3层嵌套循环                       |

### 结论

- 嵌套循环类用例（h-5, h-8, sl2, transpose2）受益于循环交换+循环展开+指令调度，平均提升 10-13%
- shuffle1 受益于指令调度和位运算模式识别，提升 25%
- 递归算法（knapsack_naive）和多BB函数内联受限的用例（h-4-03, conv2d-1）无明显提升
- 所有优化 Pass 已全部上线，0 SEGFAULT，0 OUTPUT DIFF
