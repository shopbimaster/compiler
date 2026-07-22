# 📊 SysY2022 编译器项目 - 已完成路径总结

> **历史快照**：本文记录截至 2026-05-29 的开发过程，不代表当前代码和测试状态。
> 当前构建、优化级别与严格测试流程以 `README.md`、`TEST_GUIDE.md` 和
> `DEVELOPMENT_PLAN.md` 顶部的 2026-07-20 状态校正为准；合规记录见
> `COMPLIANCE.md`。

## 2026-07-20 六个性能用例正确性修复

- 修复前官网成绩：Functional 100/100、H_Functional 40/40、Performance 54/60。
- `h-5-01/02/03`：限制 GEP 强度削弱仅处理单一指针递推，避免多条递推 PHI
  进入当前不能安全处理的 PHI lowering/寄存器分配路径（`9d0b542`）。
- `crypto-1/2/3`：Mem2Reg 仅提升入口块 alloca，并约束重复流水线运行产生的
  PHI 数量，避免动态 alloca 生命周期和 PHI 压力共同导致错误值（`63d1b72`）。
- 最新提交态已在 Ubuntu 24.04 下完成编译、RISC-V 静态链接与 QEMU 实际执行，
  六个目标用例 6/6 PASS；未运行完整 200 例，官网完整结果待确认。

## 项目信息
- **开始时间**: 2026-05
- **目标平台**: RISC-V RV64GC (medany)
- **开发语言**: C++17（测评平台标准）
- **目标操作系统**: Ubuntu 24.04 (WSL)
- **最后更新**: 2026-05-29（P3 高级优化完成）

---

## ✅ 已完成的工作

### 第 1 阶段: 项目初始化 & 架构设计
- [x] 确定项目目录结构
- [x] 选择 ANTLR4 作为前端工具
- [x] 确定无中间 AST 层的简化架构 (ParseTree → IR)
- [x] 确定无 SemanticAnalyzer（测试用例无误）

### 第 2 阶段: G4 语法文件开发
- [x] 编写 SysY2022Lexer.g4（词法分析器）
- [x] 编写 SysY2022Parser.g4（语法分析器）
- [x] Token 命名规范更新（全大写 + 下划线分隔）
- [x] 简化 Lexer 规则，移除嵌套 fragment

### 第 3 阶段: 环境部署 & 语法验证
- [x] WSL Ubuntu 26.04 LTS 环境配置
- [x] Java 17 + ANTLR4 JAR 安装
- [x] ANTLR4 C++ Runtime (libantlr4-runtime-dev) 安装
- [x] **G4 语法文件验证通过！**

### 第 4 阶段: IR 模块核心实现 ✅
- [x] **Type 系统**: VoidType, LabelType, IntegerType, FloatType, PointerType, ArrayType, FunctionType（全局单例 + 指针恒等比较）
- [x] **Value/User/Use**: Def-Use 链完整性（addOperand/setOperand/dropAllUses 自动维护 uses）
- [x] **VReg**: SSA 虚拟寄存器
- [x] **ConstantInt / ConstantFloat**: 常量值缓存
- [x] **Instruction**: 19 种 opcode + LLVM 风格工厂方法
- [x] **BasicBlock**: 指令容器，terminator 检测
- [x] **Function**: 基本块容器 + Argument 形参列表
- [x] **Module**: 顶层 IR 容器，dump() 输出 LLVM IR 格式

### 第 5 阶段: ANTLR4 C++ Visitor 集成 ✅
- [x] lib/ 目录放置 antlr4 JAR 包
- [x] 生成 C++ 版 Lexer/Parser/BaseVisitor 到 src/antlr/
- [x] IRBuilder 继承 SysY2022ParserBaseVisitor
- [x] 实现 visitCompilationUnit → Module
- [x] 实现 visitFuncDef → Function + BasicBlock
- [x] 实现 visitStmt（赋值/if-else/while/return）
- [x] 实现 visitExp → Value*（全部左递归表达式处理）
- [x] 实现 && / || 短路求值（branch + phi）
- [x] 编译器主入口 compiler (.sy → IR)
- [x] Compiler 类封装
- [x] BailErrorStrategy 语法错误检测
- [x] **25/25 全部测试通过（18 单元 + 7 集成）**

### 第 6 阶段: 后端代码生成 ✅
- [x] 指令选择：全部 IR opcode → RISC-V 指令
- [x] 栈帧管理：函数序言/尾声、栈变量分配
- [x] 寄存器分配：线性扫描分配器
- [x] 调用约定：RV64 LP64 (a0-a7, t0-t6, s0-s11)
- [x] 全局变量：lui+addi (medany) 访问
- [x] 浮点指令：FADD/FSUB/FMUL/FDIV → fadd.s/fsub.s/fmul.s/fdiv.s
- [x] 11/11 端到端用例 QEMU 验证通过

### 第 7 阶段: 前端语言特性补全 ✅
- [x] break/continue、嵌套 if-else/while
- [x] 一维/多维数组、数组传参、表达式下标
- [x] 全局变量/数组、const 常量
- [x] float 语法/常量折叠、void 函数
- [x] 十六进制/八进制、一元运算符
- [x] 嵌套块作用域/变量遮蔽
- [x] I/O 运行时函数声明（12个）
- [x] 递归函数、多文件 return、语句表达式
- [x] 数组初始化、全局数组初始化、部分数组初始化
- [x] **52/52 全部测试通过（18 单元 + 23 集成 + 11 端到端）**

### 第 7.5 阶段: 全流程Bug修复 ✅ (2026-05-28)
- [x] **Bug #1: InlineExpansion ICMP条件名丢失**: cloneInstruction 使用 createBinOp+".i"后缀 → 改用 createCmp 保留原始条件名 → ifelse.sy 结果正确
- [x] **Bug #2: AlgebraicSimplification 恒等式消除死循环**: 6处恒等式消除后 replaceAllUsesWith+dropAllUses 但未 erase → while(changed) 从头重扫死循环 → 添加 `bb->erase` 修复
- [x] **Bug #3: BitOpPatternRecognition 相同死循环**: trySimplifyShiftAndMask 中 AND 冗余消除后未 erase → 添加 erase 逻辑修复
- [x] **Bug #4: LoopUnrolling ICMP常量错误更新**: cloneNonTermInst 跳过 LOAD/STORE 且未做操作数重映射，但 ICMP 常量被错误减半 → 注释掉 ICMP 常量更新，待完整克隆修复后恢复
- [x] **Bug #5: test_backend.sh ifelse 期望值错误**: 预期0实为1（之前被ICMP反向bug掩盖）→ 修正为1
- [x] **Bug #6: TailRecursionElimination SIGSEGV**: eliminateOnFunction range-for循环内 eliminateTailCall 修改BB指令列表（erase CALL/RET）导致迭代器失效 → 改为先收集尾调用到vector再逐个处理
- [x] **test_qemu_extra.sh 预期值修正**: recursive_mul(30→21), div_chain(3→2), mul_simple(20→21), break_test(3→201=1225%256)
- [x] **16/16 QEMU端到端、18单元、23集成 全部通过**

### 第 8 阶段: 优化 Pass ✅
- [x] **O1**: 常量折叠 + 死代码消除 + 窥孔优化
- [x] **O2**: 函数内联 + 局部CSE + LICM + 线性扫描寄存器分配
- [x] **O3**: 代数化简+强度削减 + 循环展开 + 尾递归消除
- [x] **P0 语义级优化**:
  - P0-1: 除/取模→移位/位与（AlgebraicSimplification）
  - P0-2: 位运算模式识别（BitOpPatternRecognition）
  - P0-3: 递归乘法→原生乘法（RecursiveMulToNative）
- [x] P0 优化流水线: recursiveMulToNative → bitOpPatternRecognition → constantFolding → deadCodeElimination
- [x] P1-3 **循环交换 (Loop Interchange)**: 二重嵌套循环变量交换（entry初始化→outerBody初始化→innerBody/outerLatch自增→ICMP条件），支持直接嵌套检测，单次 pass 避免振荡；21 单元测试 + 24 集成测试 + 10 QEMU端到端 全部通过
- [x] LoopUnrolling 克隆修复：LOAD/STORE/GEP 完整支持 + 操作数重映射 + valueMap 分离克隆与插入避免指针失效
- [x] **21/21 单元 + 24/24 集成 全部通过（45/45）**

---

## 📁 项目结构 (当前)

```
compiler/
├── grammar/
│   ├── SysY2022Lexer.g4          ✅ 词法
│   └── SysY2022Parser.g4         ✅ 语法
├── lib/
│   └── antlr-4.10.1.jar          ✅ ANTLR JAR
├── include/
│   ├── Compiler.h                ✅ Compiler 封装
│   ├── backend/
│   │   ├── TargetCodeGen.h
│   │   ├── RegisterAllocator.h
│   │   └── PeepholeOptimizer.h
│   ├── ir/
│   │   ├── IR.h                  ✅ 完整类型系统+指令+BB+Function+Module
│   │   └── IRBuilder.h           ✅ Visitor + 符号表+作用域栈
│   └── utils/
│       ├── Error.h
│       └── Logger.h
├── src/
│   ├── main.cpp                  ✅ 命令行入口
│   ├── Compiler.cpp              ✅ Compiler 实现
│   ├── antlr/                    ✅ ANTLR 生成文件（被 .gitignore）
│   │   ├── SysY2022Lexer.cpp/h
│   │   ├── SysY2022Parser.cpp/h
│   │   └── SysY2022ParserBaseVisitor.cpp/h
│   ├── ir/
│   │   ├── IR.cpp                ✅ 完整实现
│   │   └── IRBuilder.cpp         ✅ 完整 Visitor 实现
│   ├── opt/
│   │   ├── AlgebraicSimplification.cpp   ✅ O3/P0-1
│   │   ├── BitOpPatternRecognition.cpp   ✅ P0-2
│   │   ├── ConstantFolding.cpp           ✅ O1
│   │   ├── CSE.cpp                       ✅ O2
│   │   ├── DeadCodeElimination.cpp       ✅ O1
│   │   ├── InlineExpansion.cpp           ✅ O2
│   │   ├── InstructionScheduling.cpp     ✅ P3-3
│   │   ├── LICM.cpp                      ✅ O2
│   │   ├── LoopInterchange.cpp           ✅ P1-3
│   │   ├── LoopUnrolling.cpp             ✅ O3/P3-4
│   │   ├── Optimizer.cpp                 ✅ 优化流水线
│   │   ├── PeepholeOptimizer.cpp         ✅ O1
│   │   ├── RecursiveMulToNative.cpp      ✅ P0-3
│   │   └── TailRecursionElimination.cpp  ✅ O3
│   └── utils/
│       └── Logger.cpp
├── test/
│   ├── hello.sy                  ✅ 最小 main
│   ├── arithmetic.sy             ✅ 算术表达式
│   ├── variable.sy               ✅ 变量声明/赋值
│   ├── ifelse.sy                 ✅ 条件分支
│   ├── while_test.sy             ✅ while 循环
│   ├── func_call.sy              ✅ 函数调用
│   ├── bad.sy                    ✅ 语法错误测试
│   ├── float_test.sy
│   ├── nested_loop_test.sy       ✅ 嵌套循环测试
│   ├── loop_unroll_4x.sy         ✅ 循环展开 4× 测试
│   ├── instr_sched_basic.sy      ✅ 指令调度测试
│   ├── test_ir.cpp               ✅ IR 单元测试 (26/26)
│   └── test_integration.cpp      ✅ 集成测试 (25/25)
├── build_backend.sh               ✅ 后端构建
├── test_backend.sh                ✅ QEMU 端到端 (11/11)
├── test_qemu_extra.sh             ✅ QEMU 端到端 额外 (5/5)
├── test_qemu_unroll.sh            ✅ 循环展开 QEMU 测试
├── test_qemu_sched.sh             ✅ 指令调度 QEMU 测试
├── CMakeLists.txt                ✅ 3 库 + 3 可执行文件
├── DEVELOPMENT_PLAN.md
├── PROGRESS_SUMMARY.md
├── TEST_GUIDE.md
└── Token命名对照表.md
```

---

## 📊 类继承体系

```
Type (全局单例)
  ├── VoidType
  ├── LabelType
  ├── IntegerType (I1, I8, I32)     ← 程序启动时自动初始化
  ├── FloatType
  ├── PointerType (pointee)
  ├── ArrayType (elem × n)
  └── FunctionType (ret × params)

Value (name, type, uses)
  ├── VReg (SSA temp)
  ├── Argument (函数形参)
  ├── BasicBlock (指令容器)
  ├── Function (基本块容器 + 形参)
  ├── User (基类: 有 operands)
  │   ├── Constant
  │   │   ├── ConstantInt (全局缓存)
  │   │   ├── ConstantFloat
  │   │   └── GlobalVariable
  │   └── Instruction (19 opcodes)
  └── Module (顶层容器)

Use → { User*, operandNo }  // Def-Use 链
```

---

## 📊 验证记录

| 日期 | 验证项 | 结果 | 备注 |
|-----|--------|------|------|
| 2026-05-20 | G4 Lexer/Parser Java | ✅ | ANTLR4 TestRig 验证 |
| 2026-05-20 | IR Type System 9 项 | ✅ | 唯一性、toString |
| 2026-05-20 | Def-Use Chain 2 项 | ✅ | addUse/removeUse |
| 2026-05-20 | Constants 1 项 | ✅ | 缓存验证 |
| 2026-05-20 | Instructions 3 项 | ✅ | createRet/BinOp/Void |
| 2026-05-20 | Module → Function → BB 1 项 | ✅ | dump() 输出 |
| 2026-05-20 | IRBuilder end-to-end 2 项 | ✅ | main 返回 0 / 42 |
| 2026-05-21 | compiler: hello.sy | ✅ | IR 输出正确 |
| 2026-05-21 | compiler: arithmetic.sy | ✅ | 表达式 IR 正确 |
| 2026-05-21 | compiler: variable.sy | ✅ | alloca/store/load |
| 2026-05-21 | compiler: ifelse.sy | ✅ | 条件分支 IR |
| 2026-05-21 | compiler: while_test.sy | ✅ | 循环 IR |
| 2026-05-21 | compiler: func_call.sy | ✅ | 函数调用 IR |
| 2026-05-21 | test_integration 7 项 | ✅ | 全部通过 |
| 2026-05-21 | test_ir 18 项 | ✅ | 全部通过 |
| 2026-05-28 | P0-1 除/取模→移位/位与 | ✅ | div_chain.sy: sra 替代 div, QEMU exit 2 |
| 2026-05-28 | P0-2 位运算模式识别 | ✅ | 代码审查: 4条规则正确, 测试无回归 |
| 2026-05-28 | P0-3 递归乘法→原生乘法 | ✅ | recursive_mul.sy: mul 替代递归, QEMU exit 21 |
| 2026-05-28 | P0 全量回归 | ✅ | 18 单元 + 23 集成 全部通过 |
| 2026-05-28 | Bug #1-#6 修复后全量回归 | ✅ | 16 QEMU端到端 + 18单元 + 23集成 全部通过 (57/57) |
| 2026-05-28 | Bug #6 TailRecursionElimination SIGSEGV | ✅ | eliminateOnFunction 迭代器失效修复，recursive_mul_shift 不再崩溃 |
| 2026-05-28 | Bug #1 InlineExpansion ICMP 条件名 | ✅ | createBinOp→createCmp，ifelse 结果正确 |
| 2026-05-28 | Bug #2/#3 死循环修复 | ✅ | AlgebraicSimplification + BitOpPatternRecognition erase修复 |
| 2026-05-28 | LoopUnrolling 克隆修复 | ✅ | cloneNonTermInst 支持 LOAD/STORE/GEP + valueMap 操作数重映射，分离克隆创建与BB插入避免迭代器/指针失效；仅展开 tripCount%2==0 的循环；loop_unroll_test.sy QEMU exit=10 通过 |
| 2026-05-29 | P1-3 循环交换实现 + 单元测试 | ✅ | 21/21 单元 + 24/24 集成 + 10/10 QEMU端到端（O0+O3）全部通过；新增 3 个 LoopInterchange 单元测试（basic/noop/computation）和 1 个集成测试；修复振荡 bug（maxIters=1） |
| 2026-05-29 | P3-4 循环展开 4× 增强 | ✅ | LoopUnrolling 优先 4× 展开（tripCount%4==0），回退 2×；新增 3 个单元测试（basic/4×/fallback）+ 3 个集成测试用例 + QEMU 端到端验证（6/6） |
| 2026-05-29 | P3-3 指令调度 | ✅ | 基本块内列表调度：构建数据依赖 DAG，优先调度 LOAD 和多使用者指令，stable_sort 原地重排避免迭代器失效；新增 2 个单元测试（load_hoist/dep_chain）+ 1 个集成测试 + QEMU 端到端验证（2/2） |
| 2026-05-29 | P3 全量回归 | ✅ | 26 单元 + 25 集成 + 8 QEMU端到端（2 test suites）全部通过（59/59） |

---

## 🛠️ 构建命令

```bash
cd /mnt/d/VSCodeProjects/compiler
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./test_ir             # IR 单元测试
./test_integration    # 集成测试
./compiler ../test/hello.sy   # 编译 .sy → IR
./compiler ../test/hello.sy -o output.ir   # 输出到文件
```

---

## 📝 当前状态

- **语法解析**: G4 文件完全正确，C++ 版 ANTLR 运行时已集成
- **IR 框架**: 类型系统 + SSA IR + Module/dump 完整可用
- **前端→IR 管线**: compiler 可从 .sy 源文件自动生成 LLVM 风格 IR
- **已支持特性**: 全部 SysY2022 语言特性（数组、全局变量、float、void、作用域、I/O 等）
- **后端**: 代码生成器完整实现（指令选择 + 栈帧 + 线性扫描寄存器分配）
- **优化**: O1/O2/O3/P0/P3 全部实现，优化流水线完整（含循环交换、循环展开4×、指令调度）
- **测试用例**: test/ 目录包含 functional (100) + h_functional (40) + performance (60) 共 200 条官方源程序
- **当前稳定基线**: `Commit-Version3.3`；实现提交 `14493f5` 已获官网 100 分，Functional 100/100、H_Functional 40/40、Performance 60/60，总运行时间 793.1347s
- **相对 Version3.2**: 同为开发板 120，794.8685s → 793.1347s，减少 1.7338s（约 0.22%）；全部 200 个官方用例保持 AC
- **主要性能结果**: GEP-LSR-2 使 matmul 三例在上一轮官网结果中合计提升约 2.07%；盈利性修正使 shuffle 三例由 5.446539s 恢复至 4.936243s，较修正前提升约 9.37%
- **已知约束**: GEP-LSR-2 限制嵌套自然循环内最多 3 链、含调用循环不启用多链；多链由外层循环携带时，每条链必须在嵌套循环内使用，以避免地址递推增加寄存器压力和 spill/reload
- **下一步**: 以 Version3.3 为稳定基线选择下一项独立优化；优先依据热点汇编和目标用例小范围验证，不直接合入 `perf-optimize` 的历史实验提交
