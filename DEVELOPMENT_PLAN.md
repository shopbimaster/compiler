# 🚀 SysY2022 编译器 - 开发规划

## 目标
将 SysY2022 语言编译为 RV64GC 汇编，支持 medany 内存模型。

---

## 📋 开发路线图

---

## ✅ 第 1 阶段: 中间表示 (IR) 设计（已完成）

**完成日期**: 2026-05-20 | **测试**: 18/18 全部通过

| 文件 | 内容 |
|-----|------|
| `include/ir/IR.h` | Type 系统 (7 种)、Value/User/Use、VReg、ConstantInt/Float、Instruction (19 opcodes)、BasicBlock、Function、Module |
| `src/ir/IR.cpp` | 完整实现：全局单例 Type 缓存、Def-Use 链、工厂方法、dump() |
| `include/ir/IRBuilder.h` | 符号表、作用域栈、构建辅助 |
| `src/ir/IRBuilder.cpp` | buildSimpleMain() 端到端 |
| `test/test_ir.cpp` | 18 项单元测试 |

---

## ✅ 第 2 阶段: IRBuilder Visitor 集成 ANTLR4（已完成）

**完成日期**: 2026-05-21 | **测试**: 25/25 全部通过 (18 单元 + 7 集成)

| 任务 | 文件 | 状态 |
|-----|------|------|
| 2.1 安装 ANTLR4 C++ Runtime | 环境 | ✅ |
| 2.2 生成 C++ 版 Lexer/Parser/Visitor | `src/antlr/` | ✅ |
| 2.3 IRBuilder 继承 SysY2022ParserBaseVisitor | `IRBuilder.h` | ✅ |
| 2.4 实现 visitCompUnit → Module | `IRBuilder.cpp` | ✅ |
| 2.5 实现 visitFuncDef → Function + BB + 形参 | `IRBuilder.cpp` | ✅ |
| 2.6 实现 visitExp → Value*（左递归表达式/短路求值） | `IRBuilder.cpp` | ✅ |
| 2.7 实现 visitStmt（赋值/if-else/while/return） | `IRBuilder.cpp` | ✅ |
| 2.8 Compiler 封装 + 命令行入口 sysyc | `Compiler.h/cpp`, `main.cpp` | ✅ |
| 2.9 集成测试：7 项编译用例 | `test/test_integration.cpp` | ✅ 7/7 通过 |

**已支持的语言特性**:
- 变量声明与赋值 (int/float)
- 算术表达式 (+ - * / %)
- 比较表达式 (< > <= >= == !=)
- 逻辑表达式 (&& || 短路求值)
- 条件分支 (if-else)
- 循环 (while)
- 函数定义与调用（含形参传递）

---

## 第 3 阶段: 后端代码生成（下一步）

**优先级: 🔴 高** | **状态: ⏳ 待开始**

| 任务 | 文件 | 状态 |
|-----|------|------|
| 3.1 IR → RV64 指令映射 | `TargetCodeGen.h` | ⏳ |
| 3.2 函数序言/尾声生成 | `TargetCodeGen.cpp` | ⏳ |
| 3.3 寄存器分配器 (线性扫描) | `RegisterAllocator.h` | ⏳ |
| 3.4 窥孔优化 | `PeepholeOptimizer.h` | ⏳ |
| 3.5 medany 内存模型支持 (.LCPI0_0 等) | `backend/` | ⏳ |

---

## 第 4 阶段: 编译器集成与验证

**优先级: 🟡 中** | **状态: ⏳ 待开始**

| 任务 | 状态 |
|-----|------|
| 4.1 后端整合到 sysyc 管线 | ⏳ |
| 4.2 在 RISC-V FPGA (BOOM) 上运行 | ⏳ |
| 4.3 基准测试验证 | ⏳ |

---

## 🎯 近期目标

1. **实现后端代码生成**（IR → RISC-V RV64GC 汇编）
2. **寄存器分配**（虚拟寄存器 → 物理寄存器映射）
3. **FPGA 上板验证**

---

## 🛠️ 技术学习需求

| 主题 | 优先级 | 用途 |
|-----|--------|------|
| RISC-V RV64GC 指令集 | 🔴 高 | 后端代码生成 |
| RISC-V 调用约定 | 🔴 高 | 函数序言/尾声 |
| 寄存器分配 (线性扫描) | 🟡 中 | 虚拟寄存器 → 物理寄存器 |
| LLVM IR 语法 | 🟢 低 | 参考对照 |

---

## 📝 备注

- **当前日期**: 2026-05-21
- **IR 框架**: 18 项测试通过，完整可用
- **前端→IR 管线**: 7 项集成测试通过，sysyc 可编译 .sy 到 IR
- **build-* 和 src/antlr/**: 已配置 .gitignore 自动忽略