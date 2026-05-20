# 🚀 SysY2022 编译器 - 开发规划

## 目标
将 SysY2022 语言编译为 RV64GC 汇编，支持 medany 内存模型。

---

## 📋 开发路线图

---

## 第 1 阶段: 中间表示 (IR) 设计与实现

**优先级: 🔴 高**

| 任务 | 文件 | 状态 |
|-----|------|------|
| 1.1 设计 IR 数据结构 | `include/ir/IR.h` | ✅ **完成** |
| 1.2 实现 IR 基本块 (BasicBlock) | `include/ir/IR.h` | ✅ **完成** |
| 1.3 实现 IR 指令 (Instruction) | `include/ir/IR.h` | ✅ **完成** |
| 1.4 实现 IR 函数 (Function) | `include/ir/IR.h` | ✅ **完成** |
| 1.5 IR 单元测试 | `test/test_ir.cpp` | ✅ **完成** |

---

## 第 2 阶段: IR 构建器 (IRBuilder) 实现

**优先级: 🔴 高**

| 任务 | 文件 | 状态 |
|-----|------|------|
| 2.1 IRBuilder 基础框架 | `include/ir/IRBuilder.h` | ✅ **完成** |
| 2.2 实现 Visitor 访问 ParseTree | `include/ir/IRBuilder.h` | ⏳ 待实现（需要 ANTLR4 集成） |
| 2.3 处理表达式构建 IR | `src/ir/IRBuilder.cpp` | ⏳ 待实现 |
| 2.4 处理语句构建 IR | `src/ir/IRBuilder.cpp` | ⏳ 待实现 |
| 2.5 处理函数构建 IR | `src/ir/IRBuilder.cpp` | ⏳ 待实现 |
| 2.6 IRBuilder 集成测试 | `test/` | ⏳ 待编写 |

---

## 第 3 阶段: 后端实现 (RISC-V 代码生成)

**优先级: 🔴 高**

| 任务 | 文件 | 状态 |
|-----|------|------|
| 3.1 寄存器分配器 | `include/backend/RegisterAllocator.h` | ⏳ 待实现 |
| 3.2 IR 到 RISC-V 指令映射 | `include/backend/TargetCodeGen.h` | ⏳ 待实现 |
| 3.3 窥孔优化 (Peephole) | `include/backend/PeepholeOptimizer.h` | ⏳ 待实现 |
| 3.4 支持 medany 内存模型 | `src/backend/` | ⏳ 待实现 |

---

## 第 4 阶段: 编译器集成

**优先级: 🟡 中**

| 任务 | 文件 | 状态 |
|-----|------|------|
| 4.1 完整编译器主入口 | `src/main.cpp` | ⏳ 待完善 |
| 4.2 Compiler 类集成 | `src/Compiler.cpp` | ⏳ 待完善 |
| 4.3 命令行参数解析 | `src/` | ⏳ 待实现 |
| 4.4 最终集成测试 | `test/` | ⏳ 待编写 |

---

## 第 5 阶段: 最终验证与优化

**优先级: 🟢 低**

| 任务 | 状态 |
|-----|
| 5.1 基准测试程序验证 | ⏳ |
| 5.2 可选优化 (可选) | ⏳ |
| 5.3 文档完善 | ⏳ |

---

## 🎯 近期任务（下一步工作)

### 最小可行路径（推荐顺序）：

1. **继续完善 IRBuilder**
   - [ ] 添加更多指令支持（ADD, SUB, 等）
   - [ ] 完善符号表和作用域管理
   - [ ] 添加更多 IR 打印功能

2. **开始后端开发**
   - [ ] 实现简单的 RISC-V 汇编输出（仅文本输出，暂不汇编）
   - [ ] 简单的寄存器分配（线性扫描）
   - [ ] 将 IR 翻译成 RISC-V 指令

3. **暂不追求完整编译环境，优先实现核心逻辑**

---

## 📂 新增文件清单（最近）

| 文件 | 描述 |
|-----|------|
| `test/test_ir.cpp` | IR 模块测试程序 |
| `test_ir_build/CMakeLists.txt` | IR 测试专用 CMake 配置 |

---

## 🛠️ 环境配置说明

### IR 模块测试（当前）

IR 模块可以**完全独立测试**（不需要 ANTLR4）。

测试步骤（手动执行）：
```bash
# 进入测试构建目录
cd test_ir_build
# 配置
cmake .
# 编译
make
# 运行
./test_ir
```

### 完整编译器

后续集成 ANTLR4 时再配置完整环境。

---

## 📝 备注

- **当前日期**: 2026-05
- **更新时间线**: 本文件将根据进度更新
- **最新状态**: ✅ IR 基础框架已搭建完成，测试通过！
