# SysY2022 编译器项目总结

## ✅ 架构简化 (2026-05-15)

### 移除的部分

- ❌ **AST 层** - 不再需要单独的 AST 定义和构建
- ❌ **SemanticAnalyzer** - 假设输入程序无语法/语义错误

### 新架构的优势

- 编译流程更简洁
- 减少中间表示转换的开销
- 开发效率更高

---

## 项目架构

```
输入 (.sy)
   ↓
[ANTLR4 词法/语法分析]
   ↓
[IRBuilder - 直接从 ParseTree 生成 IR]
   ↓
[IR - 中间表示]
   ↓
[后端]
   ├─ RegisterAllocator (寄存器分配)
   ├─ TargetCodeGen (IR → RV64 汇编)
   └─ PeepholeOptimizer (窥孔优化)
   ↓
输出 (.s)
```

---

## 目录结构

```
compiler/
├── include/
│   ├── Compiler.h          # 编译器主接口
│   ├── ir/
│   │   ├── IR.h            # 中间表示定义
│   │   └── IRBuilder.h     # 从 ParseTree 生成 IR (核心!)
│   ├── backend/
│   │   ├── TargetCodeGen.h
│   │   ├── RegisterAllocator.h
│   │   └── PeepholeOptimizer.h
│   └── utils/
│       ├── Error.h
│       └── Logger.h
├── src/
│   ├── main.cpp
│   ├── ir/
│   │   └── IRBuilder.cpp   # 待实现
│   ├── backend/
│   └── utils/
├── grammar/
│   ├── SysY2022Lexer.g4
│   └── SysY2022Parser.g4
└── test/
```

---

## 关键文件设计

### IRBuilder.h

- 继承 `SysY2022ParserBaseVisitor`
- Visitor 方法直接访问 ParseTree 的 context
- 直接生成 IR，无需中间 AST 层

---

## 后续开发计划

### 优先级 1: IRBuilder.cpp

实现所有 Visitor 方法，从 ParseTree 生成 IR

### 优先级 2: 后端实现

实现 TargetCodeGen 等

### 优先级 3: Compiler.cpp

整合所有模块
