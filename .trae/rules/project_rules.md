# 项目规则与约定

## 优化级别命令行映射（极其重要！）

**测评服务器仅支持 `-O1` 这一个优化选项**，因此编译器必须将大写 `-O1` 映射到内部最高优化级别（OALL = O1+O2+O3）：

| 命令行参数 | 内部级别 | 包含的Pass                            | 用途               |
| ---------- | -------- | ------------------------------------- | ------------------ |
| `-O1`      | OALL     | O1 + O2 + O3（不含P0/P3）             | **测评服务器使用** |
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

- **OALL**：O1+O2+O3，P0 和 P3 暂时禁用（已知会导致段错误）
- **P0**（recursiveMulToNative + bitOpPatternRecognition）：待修复后启用
- **P3**（instructionScheduling）：待修复后启用
