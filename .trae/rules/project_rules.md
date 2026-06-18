# 项目规则与约定

## 优化级别命令行映射（极其重要！）

**测评服务器仅支持 `-O1` 这一个优化选项**，因此编译器必须将大写 `-O1` 映射到内部最高优化级别（OALL = O1+O2+O3）：

| 命令行参数 | 内部级别 | 包含的Pass                            | 用途               |
| ---------- | -------- | ------------------------------------- | ------------------ |
| `-O1`      | OALL     | O1 + O2 + O3 + P0（不含P3）           | **测评服务器使用** |
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

- **OALL**：O1+O2+O3+P0，P3 暂时禁用（已知会导致段错误）
- **P0**：bitOpPatternRecognition 已启用；recursiveMulToNative 暂禁用（crypto 编译段错误）
- **P3**（instructionScheduling）：暂禁用（导致大量段错误）
- **loopInterchange**（O3）：已启用。修复了 `swapICmpConstants`（交换 ICMP 常量/全局变量 LOAD 边界）和 `moveAllocaToEntry`（避免 use-before-def），非方阵越界问题已解决
- **loopUnrolling**（O3）：已启用。修复了 `cloneNonTermInst` 中 STORE 指令指针操作数未通过 `lookup` 查找的问题（导致 19_search 段错误）

## 性能分析结论（第六次测试）

### 耗时 TOP 用例

| 用例                   | 耗时    | 共性原因                                     |
| ---------------------- | ------- | -------------------------------------------- |
| shuffle1               | 2013ms  | 函数调用开销（hash/insert/reduce）           |
| knapsack_naive-(1,2,3) | ~1600ms | 递归 O(2^n) 算法，编译器无法优化             |
| conv2d-1               | 1412ms  | 5层嵌套循环 + 多维数组访问                   |
| h-5-(01,02,03)         | ~1013ms | 3层嵌套循环（LU分解），数组访问局部性差      |
| h-8-(01,02,03)         | ~911ms  | 3层嵌套循环（Nussinov DP），数组访问局部性差 |
| sl2                    | 813ms   | 3层嵌套循环（3D stencil），数组访问局部性差  |
| transpose2             | 812ms   | transpose 函数频繁调用，多BB函数无法内联     |
| h-4-03                 | 810ms   | f(x)/max 函数调用在紧循环中                  |
| many_mat_cal-(1,2,3)   | ~711ms  | 多重矩阵运算，3层嵌套循环                    |

### 三大共性瓶颈

1. **函数调用在紧循环中**：内联仅支持单BB函数（≤20条指令），多BB函数（transpose、insert、reduce）无法内联
2. **多重嵌套循环 + 多维数组访问**：循环交换和循环展开已修复启用；指令调度导致段错误禁用
3. **递归算法**：knapsack_naive 为 O(2^n)，尾递归消除不适用，recursiveMulToNative 不适用
