# 📊 SysY2022 编译器项目 - 已完成路径总结

## 项目信息
- **开始时间**: 2026-05
- **目标平台**: RISC-V RV64GC (medany)
- **开发语言**: C++20
- **目标操作系统**: Ubuntu 24.04 (WSL)
- **最后更新**: 2026-05-21

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
- [x] 编译器主入口 sysyc (.sy → IR)
- [x] Compiler 类封装
- [x] BailErrorStrategy 语法错误检测
- [x] **25/25 全部测试通过（18 单元 + 7 集成）**

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
│   ├── test_ir.cpp               ✅ IR 单元测试 (18/18)
│   └── test_integration.cpp      ✅ 集成测试 (7/7)
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
| 2026-05-21 | sysyc: hello.sy | ✅ | IR 输出正确 |
| 2026-05-21 | sysyc: arithmetic.sy | ✅ | 表达式 IR 正确 |
| 2026-05-21 | sysyc: variable.sy | ✅ | alloca/store/load |
| 2026-05-21 | sysyc: ifelse.sy | ✅ | 条件分支 IR |
| 2026-05-21 | sysyc: while_test.sy | ✅ | 循环 IR |
| 2026-05-21 | sysyc: func_call.sy | ✅ | 函数调用 IR |
| 2026-05-21 | test_integration 7 项 | ✅ | 全部通过 |
| 2026-05-21 | test_ir 18 项 | ✅ | 全部通过 |

---

## 🛠️ 构建命令

```bash
cd /mnt/d/VSCodeProjects/compiler
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./test_ir             # IR 单元测试
./test_integration    # 集成测试
./sysyc ../test/hello.sy   # 编译 .sy → IR
./sysyc ../test/hello.sy -o output.ir   # 输出到文件
```

---

## 📝 当前状态

- **语法解析**: G4 文件完全正确，C++ 版 ANTLR 运行时已集成
- **IR 框架**: 类型系统 + SSA IR + Module/dump 完整可用
- **前端→IR 管线**: sysyc 可从 .sy 源文件自动生成 LLVM 风格 IR
- **已支持特性**: 变量/赋值、四则运算、比较、短路求值、if-else、while、函数+参数
- **待实现特性**: break/continue、数组、全局变量、float、void 函数、作用域、I/O 等
- **后端**: 代码生成器头文件已定义，待实现
- **测试用例**: Final_Test 目录包含 functional (100) + h_functional (40) + performance (~50) 共 ~190 条
- **下一步**: 前端补全 → 后端 O0 代码生成 (IR → RISC-V 汇编)