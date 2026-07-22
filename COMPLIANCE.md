# 编译器实现与优化合规记录

本文用于记录第三方技术借鉴、AI 辅助范围和优化合规审计。提交比赛前应由全体
成员复核，并补全所有外部资料的准确链接、许可证和实际使用范围。

## 官方方案基线（2026-07-18 复核）

本次复核以仓库中的以下官方文件为准：

- `2026年全国大学生计算机系统能力大赛-编译系统设计赛-编译系统实现赛道技术方案.pdf`
- `关于编译优化合理性及相关违规行为认定的说明.pdf`

需要持续满足的工程约束如下：

- 提交源码在 AMD64、8GB、Ubuntu 24.04 Docker 环境中使用 LLVM/Clang 18 构建；
  C++ 使用 C++17，官方编译选项为 `clang++ --std=c++17 -O2 -lm`。
- RISC-V 目标为 BOOM v3、RV64GC；目标汇编使用 GCC 13.3.0 汇编和链接，按技术
  方案使用 `-march=rv64gc`，并保证代码可在 medany 较大地址空间模型下运行。
- 编译器可执行文件统一命名为 `compiler`，功能测试调用为
  `compiler -S -o testcase.s testcase.sy`，性能测试在末尾额外传入 `-O1`。
- 官方技术方案写明 SysY2026；当前初赛平台与本仓库测试仍按 SysY2022 执行。
  在平台正式更新语言规范或用例前不得自行猜测差异，更新后必须做语法和语义差分审计。
- ANTLR 等通用词法/语法生成工具允许使用；不得直接使用或裁剪 GCC、LLVM 等现有
  开源编译器及其框架源码。GCC/QEMU 仅用于本地汇编、链接、执行和差分验证，不能成为
  参赛编译器运行时的一部分。

## 通用优化原则

测评使用的 `-O1` 只允许基于语言语义、IR 数据流、控制流和目标硬件通用特性
进行优化。不得根据以下信息触发转换：

- 用户函数名、变量名或特定字符串；
- 已知测试名称、测试目录或测试程序结构指纹；
- 特定输入值、输出值或测评环境特征；
- 无法对所有合法输入证明等价的猜测性条件。

2026-07-18 审计中，已从 `BitOpPatternRecognition.cpp` 删除通过 `_and`、
`_xor`、`_or`、`rotr8`、`rotlN`、`rotrN` 等函数名触发的调用替换，并增加
“用户函数同名时不得被替换”的集成测试。其余保留规则只检查 IR opcode、常量
和 def-use 关系。

## AI 辅助使用记录

| 日期 | 工具 | 辅助范围 | 人工复核状态 |
|---|---|---|---|
| 2026-07-18 | OpenAI Codex | 仓库合规审计；删除函数名触发的优化；增加回归用例；修复测试脚本、运行时库构建、CTest 与相关文档；定位并修复通用优化器/后端正确性问题；核对官方 PDF 并调整部署与合规说明 | 合并前由小组成员逐项复核 |
| 2026-07-20 | OpenAI Codex | 隔离并修复六个性能用例中的通用 GEP/Mem2Reg 正确性问题；使用 RISC-V GCC、静态运行库和 QEMU 验证目标用例；同步官网参考输出与项目状态文档 | 完整官网回归与合并前复核待小组成员完成 |
| 2026-07-21 | Anthropic Claude | RA-CALL-1: 实现调用感知寄存器偏好，基于 crossesCall 属性为 call-local 值优先分配 caller-saved 寄存器，减少递归/调用密集型函数的 prologue/epilogue 访存开销；添加 ra_call_regression.sy 回归用例；更新 PERFORMANCE_OPTIMIZATION_PLAN.md 记录实测结果 | 待官网 BOOM 测评确认，本地 QEMU knapsack_naive/huffman/h-5/crypto 通过 |
| 2026-07-22 | OpenAI Codex | GEP-LSR-2: 实现受嵌套区域压力预算约束的多链仿射 GEP 递推；修复寄存器与栈混合位置之间的并行 PHI 搬运；根据官网结果增加“外层递推必须在嵌套循环内使用”的通用盈利性约束；使用 Ubuntu 24.04、RISC-V GCC 和 QEMU 验证固定快速集及代表性性能用例 | `d939f92` 官网 100 分、793.6786s；shuffle 盈利性修正本地通过，完整官网复测待小组成员确认 |

AI 辅助内容不会自动视为正确或合规。负责合并的成员需要理解每项修改、运行测试，
并在提交记录或评审记录中确认人工修改情况。

## 已标注的技术借鉴

源码注释目前提到 Cpl1、Cpl2、Cpl3，以及 Hacker's Delight 和 Granlund &
Montgomery 的常数除法算法。涉及文件包括但不限于：

- `AlgebraicSimplification.cpp`、`ADCE.cpp`、`CodeSink.cpp`、
  `DeadStoreElimination.cpp`、`IfConversion.cpp`、`LoadElimination.cpp`、
  `Reassociate.cpp`、`TreeShaking.cpp`；
- `Mem2Reg.cpp`、`Optimizer.h`；
- `LoopFind.cpp`、`LoopFullUnroll.cpp`、`LoopStrengthReduce.cpp`、
  `ReadOnlyGlobal.cpp`、`SCCP.cpp`、`SCEVAnalysis.cpp`、`Optimizer.cpp`；
- `MagicDivision.cpp`。

提交前必须补充：Cpl1/Cpl2/Cpl3 分别对应的项目名称与 URL、许可证、借鉴的是
算法思想还是具体代码、与本项目实现的主要差异，以及负责成员能否独立解释。
在这些信息补齐前，不应把本文件视为最终合规声明。
