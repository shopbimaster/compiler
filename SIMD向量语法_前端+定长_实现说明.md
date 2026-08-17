# SIMD 向量语法实现说明（前端 + 定长）

> **分支**: `feat-simd-vector`（基于 `main`）
> **本版范围**: **只改生成 IR 以前的部分**（词法 / 语法 / IRBuilder），
> **支持编译期定长**的 SSE 风格向量语法。
> **源码标记**: 所有与 `main` 不同的源码改动均以注释 `《前端+定长》` 标注，
> 通过 `grep -r '《前端+定长》' grammar/ include/ src/` 可一键定位微调点。
>
> **核心约束**: 决赛 FPGA 不确定在 ISA 层面支持 SIMD 指令（如 RVV），所以本版
> 不引入新 IR 类型、不触后端代码生成与优化器；向量运算在 IR 生成阶段**去语法糖
> 为标量逐元素运算**，最终汇编是纯标量 RV64GC（`li/sw/lw/addw/mulw`）。

---

## 一、本版范围与设计哲学

### 1.1 用户语法目标

```c
vec a = {1, 2, 3, 4};
vec b = {4, 3, 2, 1};
vec c = a + b;          // → {5, 5, 5, 5}
vec d = a * 2;          // → {2, 4, 6, 8}（标量广播）
vec e = 3 + a;          // → {4, 5, 6, 7}
vec f;                  // 默认长度 4，零初始化
f = a + b;              // 声明后赋值（逐元素拷贝）
putint(c[0]);           // 索引访问，返回标量

// 《前端+定长》 浮点向量扩展：vecf 关键字（float 元素）
vecf fa = {1.0, 2.0, 3.0, 4.0};
vecf fb = {4.0, 4.0, 4.0, 4.0};
vecf fc = fa + fb;      // → {5.0, 6.0, 7.0, 8.0}
vecf fd = fa * 2.0;     // → {2.0, 4.0, 6.0, 8.0}（标量广播）
vecf fe = 3.0 + fa;     // → {4.0, 5.0, 6.0, 7.0}
vecf fg;                // 默认长度 4，零初始化（0.0）
fg = fa + fb;           // 声明后赋值（逐元素深拷贝）
putint(fc[0]);          // putint 对 float 隐式截断，输出整数
```

### 1.2 "前端 + 定长" 的含义

| 关键词 | 含义 |
|--------|------|
| **前端** | 只动 `grammar/*.g4` + `include/ir/IRBuilder.h` + `src/ir/IRBuilder.cpp`，不动 `src/opt/*` 优化器、不动 `src/backend/*` 代码生成、不动 `include/ir/IR.h` 类型系统 |
| **定长** | 向量长度在**编译期**确定：`vec a = {...}` 长度 = 列表元素数；`vec a;` 默认长度 4。运行时不可变。不支持变长（vecf 同理） |
| **去语法糖** | `vec` 在 IR 中表示 `alloca [N x i32]`，`vecf` 表示 `alloca [N x float]`，所有向量运算符展开为 N 个标量 `load / op / store`（vecf 走 `FADD/FSUB/FMUL/FDIV`），与普通 `int a[4]` / `float a[4]` 等价 |

### 1.3 为什么这么设计

- **后端零改动**：FPGA BOOM 软核只保证 RV64GC，没有 RVV；纯标量 IR（含浮点）走现有 TargetCodeGen 即可。
- **优化器零改动**：标量 IR 走 Mem2Reg / SCCP / GVN / LoopUnroll 等已有 pass，这些 pass 本就支持 `[N x i32]` 与 `[N x float]` 数组。
- **类型系统零改动**：不新增 `VectorType`，复用 `ArrayType` + `PointerType`（vecf 用 `[N x float]`）。
- **代价**：不享受 SIMD 寄存器打包的真并行。但决赛 FPGA 不确定支持 SIMD，这是稳妥选择。
- **浮点向量扩展零侵入**：vecf 复用 vec 的骨架（`emitVecBinOp` 等基于 `elemTy` 通用化），仅替换元素类型与运算 opcode，前后端/优化器均无新增依赖。

---

## 二、源码改动清单（5 个文件，全部标 `《前端+定长》`）

> 检索命令：`grep -rn '《前端+定长》' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp`
>
> 浮点向量扩展的标注同样为 `《前端+定长》`，并在注释中带 "浮点向量扩展" 字样以便区分：
> `grep -rn '浮点向量扩展' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp`

### 2.1 `grammar/SysY2022Lexer.g4` — 加 `VEC` / `VECF` 关键字

```antlr
VEC:  'vec';   // 《前端+定长》 SIMD 向量关键字（SSE 风格定长，仅前端词法扩展）
VECF: 'vecf';  // 《前端+定长》 浮点向量扩展：定长浮点向量关键字（float 元素）
```

**词法顺序**：`VECF` 必须放在 `VEC` 之后或之前都行（ANTLR 默认最大匹配，`vecf` 会
优先匹配 `VECF` 而非 `VEC + IDENTIFIER`）。实际放在 `VEC` 之后即可。

### 2.2 `grammar/SysY2022Parser.g4` — 加 `vecDecl` / `vecfDecl` / `vecInit` 规则

```antlr
// 《前端+定长》 在原 SysY 声明基础上新增 vecDecl / vecfDecl 分支
decl: constDecl | varDecl | vecDecl | vecfDecl;

// 《前端+定长》 SIMD 风格向量声明（仅前端语法扩展，编译期定长）：
//   vec name = { e1, e2, ... };   列表初始化（定长，长度=元素数）
//   vec name = exp;               表达式初始化（exp 须为向量，如 a + b）
//   vec name;                     默认长度 4（SSE 标准宽度），零初始化
vecDecl: VEC IDENTIFIER (ASSIGN (vecInit | exp))? SEMICOLON;

// 《前端+定长》 向量初值（列表形式）
vecInit: L_BRACE exp (COMMA exp)* R_BRACE;

// 《前端+定长》 浮点向量扩展：定长浮点向量声明（编译期定长，float 元素）
// 语法与 vecDecl 对称，区别仅在 VECF 关键字（float 元素）。
// 语义在 IRBuilder 阶段去语法糖为标量 [N x float] alloca + 逐元素 FADD/FSUB/FMUL/FDIV。
vecfDecl: VECF IDENTIFIER (ASSIGN (vecInit | exp))? SEMICOLON;
```

**设计要点**：`vecInit | exp` 用 first-token 区分（`vecInit` 以 `L_BRACE` 起首，`exp` 以
`L_PAREN / IDENTIFIER / number / unaryOp` 起首），ANTLR LL(*) 无歧义，无需语义谓词。

`vecfDecl` 与 `vecDecl` 完全对称，仅关键字不同，让 IRBuilder 通过 `VECF` 触发
`visitVecfDecl` 走浮点分支（`FloatType` + `FADD/FSUB/FMUL/FDIV`），其余骨架复用。

### 2.3 `include/ir/IRBuilder.h` — 浮点向量扩展辅助方法声明

```cpp
// 《前端+定长》 SIMD 向量声明访问器（仅前端扩展，编译期定长）
std::any visitVecDecl(SysY2022Parser::VecDeclContext* ctx) override;
std::any visitVecInit(SysY2022Parser::VecInitContext* ctx) override;
// 《前端+定长》 浮点向量扩展：定长浮点向量声明访问器（编译期定长，float 元素）
std::any visitVecfDecl(SysY2022Parser::VecfDeclContext* ctx) override;

// ===== 《前端+定长》 SSE 向量运算辅助（仅前端 IR 生成扩展，编译期定长） =====
bool   isVecValue(Value* v);                                              // 判断是否为向量
// 《前端+定长》 浮点向量扩展：判断 Value 是否为浮点向量（指向 [N x float] 的指针）
bool   isVecfValue(Value* v);
// 通用：elemTy 从 left 数组类型提取，对 vec (i32) / vecf (float) 都适用
Value* emitVecBinOp(Instruction::Opcode op, Value* left, Value* right);   // vec op vec
// 通用：标量侧会 implConvert 到 elemTy，对 vecf 标量（如 2.0 / 3）自动转 float
Value* emitVecScalarOp(Instruction::Opcode op, Value* scalar,
                       Value* vec, bool scalarOnLeft);                    // scalar op vec
void   emitVecCopy(Value* dst, Value* src);                               // vec = vec
```

**复用策略**：`emitVecBinOp` / `emitVecScalarOp` / `emitVecCopy` 都基于数组元素类型
`elemTy` 通用化，不再为浮点单独写一份。`elemTy` 从 `left` 的 `ArrayType` 提取，
对 `vec` 是 `i32`、对 `vecf` 是 `float`，标量侧 `implConvert` 会自动把整标量（如 `2`）
转 `float`，浮点标量（如 `2.0`）保持 `float`。

### 2.4 `src/ir/IRBuilder.cpp` — 实现与接线（含浮点向量扩展）

整数向量（vec）部分：

| 函数 | 标注位置 | 作用 |
|------|----------|------|
| `visitDecl` | 分发分支 | 新增 `if (ctx->vecDecl()) return visitVecDecl(...)` |
| `visitVecDecl` Case 1 | 无初始化 | `alloca [4 x i32]` + 4 个 `store 0`（栈上不清零，必须显式零初始化） |
| `visitVecDecl` Case 2 | 列表初始化 | `alloca [N x i32]` + 逐元素 `visitExp → implConvert → GEP → store` |
| `visitVecDecl` Case 3 | 表达式初始化 | 先求 rhs，按 rhs 类型推导 lhs 长度，`alloca` 同型 lhs，`emitVecCopy` |
| `visitVecInit` | 空 Visitor | 内联处理，保留空实现满足接口 |
| `isVecValue` | 判据 | `PointerType<[N x i32]>` → true；其余 false |
| `emitVecBinOp` | vec op vec | N 个 `load / op / store` |
| `emitVecScalarOp` | scalar op vec | 标量广播：标量与每个元素做标量 op |
| `emitVecCopy` | dst = src | 逐元素 `load / store`，用于声明后赋值 |
| `visitAddExp` | 向量判别分支 | 任一操作数是向量 → 走去语法糖；否则原标量路径 |
| `visitMulExp` | 向量判别分支 | 同上，注意 `%` 走 `SREM` 不走 `FREM` |
| `visitStmt` (`lVal ASSIGN exp`) | 向量赋值分支 | lhs/rhs 都 `isVecValue` → `emitVecCopy`，否则原标量 store |
| `visitPrimaryExp` | 不衰减分支 | 1 维整数数组保留 `alloca [N x i32]*`；多维数组照常衰减 |

浮点向量（vecf）扩展部分（与 vec 同骨架，仅替换类型与运算 opcode）：

| 函数 | 标注位置 | 作用 |
|------|----------|------|
| `visitDecl` | 分发分支 | 新增 `if (ctx->vecfDecl()) return visitVecfDecl(...)` |
| `visitVecfDecl` Case 1 | 无初始化 | `alloca [4 x float]` + 4 个 `store 0.0`（`ConstantFloat 0.0`） |
| `visitVecfDecl` Case 2 | 列表初始化 | `alloca [N x float]` + 逐元素 `visitExp → implConvert(float) → GEP → store` |
| `visitVecfDecl` Case 3 | 表达式初始化 | 求 rhs，`isVecfValue` 判定后 `alloca` 同型 lhs，`emitVecCopy` |
| `isVecfValue` | 判据 | `PointerType<[N x float]>` → true；其余 false |
| `visitAddExp` | vecf 判别分支 | 任一操作数是 vecf → `FADD/FSUB` 去语法糖；否则原标量路径 |
| `visitMulExp` | vecf 判别分支 | 同上，`FMUL/FDIV`；`%` 走 `SREM`（vecf 不支持模，占位） |
| `visitStmt` (`lVal ASSIGN exp`) | vecf 赋值分支 | lhs/rhs 都 `isVecfValue` → `emitVecCopy` |
| `visitPrimaryExp` | vecf 不衰减分支 | 1 维浮点数组（vecf）同样保留 `alloca [N x float]*` 不衰减 |
| `emitVecScalarOp` | vecf 标量广播 | 标量侧 `implConvert(scalar, float)`，如 `3` 自动转 `3.0` |

**关键差异（vecf vs vec）**：
- 元素类型：`FloatType::get()` 替换 `IntegerType::I32`
- 零值：`ConstantFloat::get(0.0)` 替换 `ConstantInt::get(0)`
- 运算 opcode：`FADD/FSUB/FMUL/FDIV` 替换 `ADD/SUB/MUL/SDIV`
- 标量侧：`implConvert(scalar, FloatType)` 把整标量（`3`）自动转浮点（`3.0`）
- `%` 不支持：vecf 的 `%` 走 `SREM` 占位（语义未定义，用户不应使用）

### 2.5 `src/antlr/*` — ANTLR 4.13.1 重新生成（对齐云端 runtime）

自动生成，无需手工标注。校验：`SysY2022Parser.h` 含 `VecDeclContext` 与
`VecfDeclContext`，`SysY2022ParserVisitor.h` 含 `visitVecDecl` 与 `visitVecfDecl`。

---

## 三、关键注意要点（按踩坑严重度排序）

### 3.1 【严重】`vec a;` 默认长度必须**显式零初始化**

栈上 `alloca` 不会自动清零。最初实现只 `alloca` 不 `store 0`，导致
`vec a; putint(a[0]);` 输出 `-1423264992` 等垃圾值。

**修复**：`visitVecDecl` Case 1 显式循环 `store 0, &a[i]`，i ∈ [0, 4)。

### 3.2 【严重】`visitPrimaryExp` 不能让向量裸名衰减

原代码对所有数组都做 `GEP [0, 0]` 衰减为首元素指针。这会让 `a + b` 里 `a` 变成
`i32*` 而非 `[4 x i32]*`，`isVecValue` 误判为标量，向量运算符被当成标量加。

**修复**：1 维整数数组（即 vec）保留 `alloca [N x i32]*` 类型不衰减；多维数组照常
衰减。判据：`pointee->isArray() && elementType->isInteger()`。

### 3.3 【严重】`c = a + b;` 不能直接 `store rhs, lhs`

`emitVecBinOp` 返回的临时向量 alloca 指针直接 `store` 到 lhs 的 alloca，相当于把
"指针值"写到 lhs 的前 8 字节，破坏了 lhs 的 `[N x i32]` 结构。

**修复**：`visitStmt` 的 `lVal ASSIGN exp` 路径，lhs 和 rhs 都 `isVecValue` 时走
`emitVecCopy(lhsPtr, rhs)`，逐元素 `load / store`。

### 3.4 【中等】ANTLR 版本必须对齐云端 4.13.1

云端 runtime 是 4.13.1。若仍用旧 4.10.1 生成器，会引用
`antlr4::internal::OnceFlag` 等不存在符号，编译期报几百个错。

**修复**：在 `Ubuntu-24.04-eval` VM 中装 4.13.1 生成器 jar + 4.13.1 C++ runtime，
用 `scripts/grammar/regen_antlr_4131.sh` 重新生成 `src/antlr/`。

### 3.5 【中等】PowerShell 转义 `$(nproc)` 失败

`cmake --build . -- -j$(nproc)` 在 PowerShell 调用 WSL 时被 PowerShell 提前求值，
报 `nproc : The term 'nproc' is not recognized`。

**修复**：直接写 `-j8`，或把含 `$(...)` 的命令包进 `.sh` 脚本文件用 `bash xxx.sh`
调用。复杂 bash 命令一律写成脚本，不要试图在 PowerShell 一行内塞。

### 3.6 【低】测试 stdout 被 sylib 的 `TOTAL:` 行污染

`SysYlib/sylib.c` 的 `after_main` destructor 把计时信息写到 stderr。
`out=$($QEMU "$bin" 2>&1)` 把它合进了 stdout，导致与 `.out` 期望不符。

**修复**：测试脚本改用 `out=$($QEMU "$bin" 2>/dev/null)`，丢弃 stderr。

### 3.7 【低】编译器 stdout 噪声 "Assembly written to ..."

每次 `compiler -S` 都在 stdout 打 `Assembly written to /tmp/xxx.S`。

**修复**：测试脚本里加 `>/dev/null`，`2>$cerr` 保留诊断信息。

### 3.8 【低】标量广播期望值算错

`04_vec_scalar` 的 `.out` 一开始写成 `5 6 7 8`（误以为 `3 + a`），
实际 `3 + {1,2,3,4}` = `{4,5,6,7}`。

**修复**：手算一遍校正 `.out`。**写期望值前务必手算**，不要凭直觉。

### 3.9 【已解决】浮点向量支持（vecf 扩展）

初版 `isVecValue` 要求 `elementType->isInteger()`，浮点数组不会被识别为向量，
当时作为设计约束留给后续版本。

**浮点向量扩展已实现**：新增 `VECF: 'vecf';` 关键字与 `vecfDecl` 规则，
新增 `visitVecfDecl` / `isVecfValue`，复用 `emitVecBinOp` / `emitVecScalarOp` /
`emitVecCopy`（基于 `elemTy` 通用化，对 `i32` 与 `float` 都适用）。
运算 opcode 替换为 `FADD/FSUB/FMUL/FDIV`，标量侧 `implConvert(scalar, float)`
自动把整标量（如 `3`）转浮点（`3.0`）。`visitPrimaryExp` 的不衰减分支增加
`elementType->isFloat()` 判据，让 1 维浮点数组（vecf）同样保留 `alloca [N x float]*`。
`visitAddExp` / `visitMulExp` / `visitStmt` 均增加 vecf 判别分支。

### 3.10 【低】vecf 测试用 `putint` 截断 float 输出

vecf 元素是 `float`，但 sylib 的 `putfloat` 用 `%a` 十六进制格式，期望值难写。
测试用例改用 `putint(c[i])`：`putint` 接收 `float` 时会隐式截断为整数
（如 `5.0` → `5`、`0.5` → `0`），期望值就是普通整数。

**注意**：写用例时务必保证结果的小数部分为 0（或可接受截断），避免歧义。
如 `vecf4 a = {1.0,2.0,3.0,4.0}; a / 2.0` = `{0.5,1.0,1.5,2.0}` 截断后 `{0,1,1,2}`，
不如改 `vecf g = {2.0,4.0,6.0,8.0}; g / 2.0` = `{1.0,2.0,3.0,4.0}` 截断后 `{1,2,3,4}` 干净。

---

## 四、支持的语法形态总览

整数向量（vec）：

| 语法 | 语义 | IR 展开 | 实现位置 |
|------|------|---------|----------|
| `vec a;` | 默认长度 4，零初始化 | `alloca [4 x i32]` + 4 个 `store 0` | `visitVecDecl` Case 1 |
| `vec a = {1, 2, 3, 4};` | 列表初始化，长度 = 元素数 | `alloca [N x i32]` + N 个 `store` | `visitVecDecl` Case 2 |
| `vec c = a + b;` | 表达式初始化，rhs 须为向量 | 求 rhs → `alloca` 同长 lhs → `emitVecCopy` | `visitVecDecl` Case 3 |
| `c = a + b;`（声明后赋值） | 向量赋值 | 逐元素 `load / store` | `visitStmt` + `emitVecCopy` |
| `vec c = a * 2;` / `3 + a` | 标量广播 | 标量与每个元素做标量 op | `emitVecScalarOp` |
| `c[i]` | 向量索引 | 标准 GEP（与普通数组一致） | `visitLVal` 原逻辑 |
| `a + b` / `a - b` / `a * b` / `a / b` / `a % b` | 向量二元运算 | N 个标量 `load / op / store` | `visitAddExp` / `visitMulExp` + `emitVecBinOp` |

浮点向量（vecf）扩展形态完全对称：

| 语法 | 语义 | IR 展开 | 实现位置 |
|------|------|---------|----------|
| `vecf a;` | 默认长度 4，零初始化（0.0） | `alloca [4 x float]` + 4 个 `store 0.0` | `visitVecfDecl` Case 1 |
| `vecf a = {1.0, 2.0, 3.0, 4.0};` | 列表初始化，长度 = 元素数 | `alloca [N x float]` + N 个 `store` | `visitVecfDecl` Case 2 |
| `vecf c = a + b;` | 表达式初始化，rhs 须为 vecf | 求 rhs → `alloca` 同长 lhs → `emitVecCopy` | `visitVecfDecl` Case 3 |
| `c = a + b;`（声明后赋值） | vecf 赋值 | 逐元素 `load / store` | `visitStmt` + `emitVecCopy` |
| `vecf c = a * 2.0;` / `3.0 + a` | 标量广播 | 标量 `implConvert` → `float` 后逐元素 op | `emitVecScalarOp` |
| `c[i]` | vecf 索引 | 标准 GEP（与普通数组一致） | `visitLVal` 原逻辑 |
| `a + b` / `a - b` / `a * b` / `a / b` | vecf 二元运算 | N 个标量 `FADD/FSUB/FMUL/FDIV` | `visitAddExp` / `visitMulExp` + `emitVecBinOp` |

支持运算符：
- vec：`+ - * / %`（`/` 和 `%` 是 `SDIV / SREM`，非 SIMD 求余）
- vecf：`+ - * /`（`FADD/FSUB/FMUL/FDIV`），`%` 走 `SREM` 占位（语义未定义，勿用）

---

## 五、测试用例

新建 `test/functional/simd_vector/` 子目录，共 15 个用例（7 个 vec 整数 + 8 个 vecf 浮点）覆盖所有路径：

整数向量（vec）：

| 用例 | 覆盖点 | 期望输出 |
|------|--------|----------|
| `01_vec_add.sy` | `vec + vec`（用户原例） | `5 5 5 5` |
| `02_vec_sub.sy` | `vec - vec` | `-3 -1 1 3` |
| `03_vec_mul.sy` | `vec * vec`（逐元素） | `4 6 6 4` |
| `04_vec_scalar.sy` | `vec * k`、`k + vec`、`vec * 10`、`k - vec` | 4 行广播结果 |
| `05_vec_assign.sy` | `vec c; c = a + b;`（visitStmt 路径） | `5 5 5 5` |
| `06_vec_default.sy` | `vec a;` 默认长度 4 零初始化 | `0 0 0 0` |
| `07_vec_chain.sy` | `a + b + c` 链式（result of emitVecBinOp 再做 +） | `8 8 8 8` |

浮点向量（vecf）扩展（《前端+定长》 浮点向量扩展）：

| 用例 | 覆盖点 | 期望输出 |
|------|--------|----------|
| `08_vecf_add.sy` | `vecf + vecf`（FADD 编译期展开） | `5 6 7 8` |
| `09_vecf_sub.sy` | `vecf - vecf`（FSUB 编译期展开） | `3 2 1 0` |
| `10_vecf_mul.sy` | `vecf * vecf`（FMUL 编译期展开） | `2 4 6 8` |
| `11_vecf_scalar.sy` | `vecf * k`、`k + vecf`、`vecf * 10`、`k - vecf` | 4 行广播结果 |
| `12_vecf_assign.sy` | `vecf c; c = a + b;`（visitStmt vecf 路径） | `5 6 7 8` |
| `13_vecf_default.sy` | `vecf a;` 默认长度 4 零初始化（0.0） | `0 0 0 0` |
| `14_vecf_chain.sy` | `a + b*c`、`d + a`、`e + d` 链式 FADD/FMUL | `15 18 21 24` |
| `15_vecf_div.sy` | `vecf / k`（FDIV 编译期展开） | `1 2 3 4` |

每个 `.sy` 都配 `.out` 文件（**最后一行是返回码**，`run_tests.sh` 用 `head -n -1`
剥离，剩下的是期望 stdout）。vecf 用例统一用 `putint` 输出（隐式截断 float → int），
避免 sylib `putfloat` 的 `%a` 十六进制格式写期望值困难（详见 3.10）。

测试脚本：`scripts/run_simd_tests.sh`：

```bash
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_simd_tests.sh O0"
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_simd_tests.sh O1"
```

**实测**：15/15 PASS @ O0，15/15 PASS @ O1（说明优化器对去语法糖后的标量 IR
——包括 vecf 的 `FADD/FSUB/FMUL/FDIV` 标量展开——完全友好）。
quick smoke 回归 5/5 PASS（未破坏已有功能）。

---

## 六、云端环境复现命令

VM：`Ubuntu-24.04-eval`（与云端对齐：clang++ 18.1.3 / ANTLR 4.13.1 /
riscv64-linux-gnu-gcc 13.3.0 / qemu-riscv64 8.2.2）。

```bash
# 1. 重新生成 ANTLR（4.13.1）
wsl -d Ubuntu-24.04-eval -e bash -lc "bash /mnt/d/VSCodeProjects/compiler/scripts/grammar/regen_antlr_4131.sh"

# 2. 构建 compiler（clang++ + 4.13.1 runtime）
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && rm -rf build && mkdir build && cd build && cmake -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ .. && cmake --build . --target compiler -- -j8"

# 3. 构建 sylib
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/build_sylib.sh"

# 4. 跑 SIMD 向量测试
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_simd_tests.sh O0"
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_simd_tests.sh O1"

# 5. 回归（保证不破坏已有功能）
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_tests.sh quick O0"
```

---

## 七、扩展空间（后续若决赛确认 FPGA 支持 SIMD 扩展）

1. **真 SIMD 寄存器打包**：新增 `VectorType`（如 `<4 x i32>` / `<4 x float>`），
   TargetCodeGen 把连续 4 个 `load + add + store` 合并成 `vle32.v + vadd.vv + vse32.v`
   （或浮点的 `vle32.v + vfadd.vv + vse32.v`，需 RVV）。这属于"后端"改动，
   **不在本版范围内**。
2. **变长向量**：`feat-vec-dynamic-fe` 分支已探索"数据指针 + 长度 + 静态堆"
   方案（含 vecf 变长浮点向量），可作为运行时库扩展。属于"运行时定长"或
   "运行时变长"，**不在本版范围内**。
3. **更丰富的元素类型**：本版已支持 `i32`（vec）与 `float`（vecf）。若要再加
   `veci64`（i64 元素），按 2.3-2.4 的复用策略：新增 `VECI64` 关键字 + `veci64Decl`
   规则 + `visitVeci64Decl` + `isVeci64Value`，复用 `emitVecBinOp` 等通用方法，
   `elemTy` 从 `ArrayType` 提取即可（已是通用化设计）。
4. **向量内建函数**：`dot(a, b)`、`sum(a)`、`shuffle(a, b, mask)` 等归约/重排操作。

本版保持后端/优化器零侵入，为上述扩展保留了干净的接入点：只要在 IRBuilder 把
新语法去语法糖为标量 IR，无需触碰其他层。浮点向量扩展即按此模式增量加入，
未触碰任何后端/优化器代码。

---

## 八、文件清单

| 文件 | 状态 | 说明 |
|------|------|------|
| `grammar/SysY2022Lexer.g4` | 修改 | 加 `VEC: 'vec';` 与 `VECF: 'vecf';`（标 `《前端+定长》`） |
| `grammar/SysY2022Parser.g4` | 修改 | 加 `vecDecl` / `vecInit` / `vecfDecl`（标 `《前端+定长》`） |
| `include/ir/IRBuilder.h` | 修改 | 加 `visitVecDecl`/`visitVecInit`/`visitVecfDecl`/`isVecValue`/`isVecfValue` 等（标 `《前端+定长》`） |
| `src/ir/IRBuilder.cpp` | 修改 | vec 10 处 + vecf 扩展（`visitVecfDecl`/`isVecfValue`/`emitVecfBinOp` 分支/`visitPrimaryExp` 不衰减/`visitStmt` 赋值/`visitAddExp`/`visitMulExp`，均标 `《前端+定长》`） |
| `src/antlr/*` | 重新生成 | 4.13.1，对齐云端 runtime（含 `VecfDeclContext`） |
| `test/functional/simd_vector/01..07_vec*.sy` + `.out` | 新增 | 7 个整数向量测试用例 |
| `test/functional/simd_vector/08..15_vecf_*.sy` + `.out` | 新增 | 8 个浮点向量测试用例（《前端+定长》 浮点向量扩展） |
| `scripts/run_simd_tests.sh` | 新增 | SIMD 测试运行器 |
| `SIMD向量语法_前端+定长_实现说明.md` | 新增 | 本文档 |

---

## 九、定位微调点

需要微调本版实现时，用以下命令快速定位所有改动点：

```bash
# 列出所有源码改动（整数向量 + 浮点向量扩展共用同一标注）
grep -rn '《前端+定长》' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp

# 只列出浮点向量扩展（vecf）相关改动
grep -rn '浮点向量扩展' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp

# 只看 IRBuilder.cpp 中的改动
grep -n '《前端+定长》' src/ir/IRBuilder.cpp

# 统计改动点数
grep -r '《前端+定长》' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp | wc -l
```

切换到其他版本（如"后端 + 真SIMD"或"前端 + 变长"）时，换一个标注名
（如 `《后端+SIMD》` 或 `《前端+变长》`）即可并存。浮点向量扩展与整数向量
共用 `《前端+定长》` 标注，差异点通过注释中的 "浮点向量扩展" 字样区分，
便于一次性 grep 出 vecf 专属改动。
