# 📊 SysY2022 编译器项目 - 已完成路径总结

## 项目信息
- **开始时间**: 2026-05
- **目标平台**: RISC-V RV64GC (medany)
- **开发语言**: C++20
- **目标操作系统**: Ubuntu 24.04 (WSL)
- **最后更新**: 2026-05-20

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
- [x] Java 17 安装
- [x] ANTLR4 4.13.1 安装和配置
- [x] **G4 语法文件验证通过！**

### 第 4 阶段: IR 模块核心实现 ✅ [NEW]
- [x] **Type 系统**: VoidType, LabelType, IntegerType, FloatType, PointerType, ArrayType, FunctionType（全局单例 + 指针恒等比较）
- [x] **Value 基类**: name, type, uses 列表
- [x] **Use/User**: Def-Use 链完整性（addOperand/setOperand/dropAllUses 自动维护 uses）
- [x] **VReg**: SSA 虚拟寄存器
- [x] **ConstantInt / ConstantFloat**: 常量值缓存
- [x] **Instruction**: 19 种 opcode + LLVM 风格工厂方法（createRet, createBr, createBinOp, createAlloca, createLoad, createStore, createCall, createGEP, createCmp, createCast, createPhi）
- [x] **BasicBlock**: 指令容器，terminator 检测
- [x] **Function**: 基本块容器 + Argument 形参列表
- [x] **Module**: 顶层 IR 容器，dump() 输出 LLVM IR 格式
- [x] **IRBuilder**: buildSimpleMain() 端到端构造并输出 `define i32 @main() { ret i32 0 }`
- [x] **18/18 单元测试全部通过**

---

## 📁 项目结构 (当前)

```
compiler/
├── grammar/
│   ├── SysY2022Lexer.g4     ✅ 词法
│   └── SysY2022Parser.g4    ✅ 语法
├── include/
│   ├── Compiler.h
│   ├── backend/
│   │   ├── TargetCodeGen.h
│   │   ├── RegisterAllocator.h
│   │   └── PeepholeOptimizer.h
│   ├── ir/
│   │   ├── IR.h              ✅ 完整类型系统+指令+BB+Function+Module
│   │   └── IRBuilder.h       ✅ 符号表+作用域栈+构建辅助
│   └── utils/
│       ├── Error.h
│       └── Logger.h
├── src/
│   ├── main.cpp
│   ├── Compiler.cpp
│   ├── ir/
│   │   ├── IR.cpp            ✅ 完整实现
│   │   └── IRBuilder.cpp     ✅ 完整实现 + buildSimpleMain()
│   ├── utils/
│   │   └── Logger.cpp
│   └── test-grammar.cpp
├── test/
│   ├── hello.sy
│   ├── float_test.sy
│   └── test_ir.cpp           ✅ IR 框架 18 项单元测试
├── CMakeLists.txt             ✅ 独立编译 sysy_ir 库
├── DEVELOPMENT_PLAN.md
├── PROGRESS_SUMMARY.md
├── Token命名对照表.md
└── [旧文档留档]
```

---

## 📊 类继承体系

```
Type (全局单例)
  ├── VoidType
  ├── LabelType
  ├── IntegerType (I1, I8, I32)
  ├── FloatType
  ├── PointerType (pointee)
  ├── ArrayType (elem × n)
  └── FunctionType (ret × params)

Value (name, type, uses)
  ├── VReg (SSA temp, 无 operands)
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
| 2026-05-20 | G4 Lexer/Parser | ✅ 成功 | ANTLR4 Java 版本生成 |
| 2026-05-20 | 测试用例 hello.sy / float_test.sy | ✅ 成功 | ParseTree 解析通过 |
| 2026-05-20 | IR Type System 9 项 | ✅ 全部通过 | 唯一性、toString |
| 2026-05-20 | Def-Use Chain 2 项 | ✅ 全部通过 | addUse/removeUse |
| 2026-05-20 | Constants 1 项 | ✅ 通过 | 缓存验证 |
| 2026-05-20 | Instructions 3 项 | ✅ 全部通过 | createRet/BinOp/Void |
| 2026-05-20 | Module → Function → BB 1 项 | ✅ 通过 | dump() 输出 |
| 2026-05-20 | IRBuilder end-to-end 2 项 | ✅ 全部通过 | main 返回 0 / 42 |

---

## 🛠️ 构建命令

```bash
cd /mnt/d/VSCodeProjects/compiler
mkdir build && cd build
cmake ..
make -j4
./test_ir    # 运行 IR 单元测试
```

---

## 📝 当前状态

- **语法解析**: G4 文件完全正确，已验证
- **IR 框架**: 类型系统 + SSA IR + Module/dump 完整可用
- **后端**: 代码生成器头文件已定义，待实现
- **下一步**: 集成 ANTLR4 C++ Runtime → 实现 IRBuilder visitor → 后端代码生成