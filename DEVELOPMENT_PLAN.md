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

## 第 2 阶段: IRBuilder Visitor 集成 ANTLR4（下一步）

**优先级: 🔴 高** | **状态: ⏳ 待开始**

| 任务 | 文件 | 状态 |
|-----|------|------|
| 2.1 安装 ANTLR4 C++ Runtime | 环境 | ⏳ |
| 2.2 生成 C++ 版 Lexer/Parser | `build/generated/` | ⏳ |
| 2.3 IRBuilder 继承 SysY2022ParserBaseVisitor | `IRBuilder.h` | ⏳ |
| 2.4 实现 visitCompUnit → Module | `IRBuilder.cpp` | ⏳ |
| 2.5 实现 visitFuncDef → Function + BB | `IRBuilder.cpp` | ⏳ |
| 2.6 实现 visitExp → Value* (表达式翻译) | `IRBuilder.cpp` | ⏳ |
| 2.7 实现 visitStmt → (语句翻译) | `IRBuilder.cpp` | ⏳ |
| 2.8 集成测试：编译 hello.sy | `test/` | ⏳ |

---

## 第 3 阶段: 后端代码生成

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
| 4.1 Compiler 类集成 | ⏳ |
| 4.2 命令行参数解析 | ⏳ |
| 4.3 基准测试验证 | ⏳ |

---

## 🎯 近期目标

1. **安装 ANTLR4 C++ Runtime**（解决网络问题后用 apt 或手动编译）
2. **完成 IRBuilder Visitor**（继承 BaseVisitor，遍历 ParseTree 生成 IR）
3. **后端代码生成**（先做最简单的 ret/常量，逐步扩展）

---

## 🛠️ 技术学习需求

| 主题 | 优先级 | 用途 |
|-----|--------|------|
| ANTLR4 C++ Visitor 模式 | 🔴 高 | IRBuilder 实现 |
| RISC-V RV64GC 指令集 | 🔴 高 | 后端代码生成 |
| RISC-V 调用约定 | 🔴 高 | 函数序言/尾声 |
| 寄存器分配 (线性扫描) | 🟡 中 | 虚拟寄存器 → 物理寄存器 |
| LLVM IR 语法 | 🟢 低 | 参考对照 |

---

## 📝 备注

- **当前日期**: 2026-05-20
- **IR 框架**: 18 项测试通过，完整可用
- **build-* 和临时文件**: 已配置 .gitignore 自动忽略