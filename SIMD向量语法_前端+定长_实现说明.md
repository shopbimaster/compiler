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
```

### 1.2 "前端 + 定长" 的含义

| 关键词 | 含义 |
|--------|------|
| **前端** | 只动 `grammar/*.g4` + `include/ir/IRBuilder.h` + `src/ir/IRBuilder.cpp`，不动 `src/opt/*` 优化器、不动 `src/backend/*` 代码生成、不动 `include/ir/IR.h` 类型系统 |
| **定长** | 向量长度在**编译期**确定：`vec a = {...}` 长度 = 列表元素数；`vec a;` 默认长度 4。运行时不可变。不支持变长 |
| **去语法糖** | `vec` 在 IR 中表示 `alloca [N x i32]`，所有向量运算符展开为 N 个标量 `load / op / store`，与普通 `int a[4]` 等价 |

### 1.3 为什么这么设计

- **后端零改动**：FPGA BOOM 软核只保证 RV64GC，没有 RVV；纯标量 IR 走现有 TargetCodeGen 即可。
- **优化器零改动**：标量 IR 走 Mem2Reg / SCCP / GVN / LoopUnroll 等已有 pass，这些 pass 本就支持 `[N x i32]` 数组。
- **类型系统零改动**：不新增 `VectorType`，复用 `ArrayType` + `PointerType`。
- **代价**：不享受 SIMD 寄存器打包的真并行。但决赛 FPGA 不确定支持 SIMD，这是稳妥选择。

---

## 二、源码改动清单（5 个文件，全部标 `《前端+定长》`）

> 检索命令：`grep -rn '《前端+定长》' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp`

### 2.1 `grammar/SysY2022Lexer.g4` — 加 `VEC` 关键字

```antlr
VEC: 'vec';   // 《前端+定长》 SIMD 向量关键字（SSE 风格定长，仅前端词法扩展）
```

### 2.2 `grammar/SysY2022Parser.g4` — 加 `vecDecl` / `vecInit` 规则

```antlr
// 《前端+定长》 在原 SysY 声明基础上新增 vecDecl 分支
decl: constDecl | varDecl | vecDecl;

// 《前端+定长》 SIMD 风格向量声明（仅前端语法扩展，编译期定长）：
//   vec name = { e1, e2, ... };   列表初始化（定长，长度=元素数）
//   vec name = exp;               表达式初始化（exp 须为向量，如 a + b）
//   vec name;                     默认长度 4（SSE 标准宽度），零初始化
vecDecl: VEC IDENTIFIER (ASSIGN (vecInit | exp))? SEMICOLON;

// 《前端+定长》 向量初值（列表形式）
vecInit: L_BRACE exp (COMMA exp)* R_BRACE;
```

**设计要点**：`vecInit | exp` 用 first-token 区分（`vecInit` 以 `L_BRACE` 起首，`exp` 以
`L_PAREN / IDENTIFIER / number / unaryOp` 起首），ANTLR LL(*) 无歧义，无需语义谓词。

### 2.3 `include/ir/IRBuilder.h` — 4 个辅助方法声明

```cpp
// 《前端+定长》 SIMD 向量声明访问器（仅前端扩展，编译期定长）
std::any visitVecDecl(SysY2022Parser::VecDeclContext* ctx) override;
std::any visitVecInit(SysY2022Parser::VecInitContext* ctx) override;

// ===== 《前端+定长》 SSE 向量运算辅助（仅前端 IR 生成扩展，编译期定长） =====
bool   isVecValue(Value* v);                                              // 判断是否为向量
Value* emitVecBinOp(Instruction::Opcode op, Value* left, Value* right);   // vec op vec
Value* emitVecScalarOp(Instruction::Opcode op, Value* scalar,
                       Value* vec, bool scalarOnLeft);                    // scalar op vec
void   emitVecCopy(Value* dst, Value* src);                               // vec = vec
```

### 2.4 `src/ir/IRBuilder.cpp` — 实现与接线（10 处标注）

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

### 2.5 `src/antlr/*` — ANTLR 4.13.1 重新生成（对齐云端 runtime）

自动生成，无需手工标注。校验：`SysY2022Parser.h` 含 `VecDeclContext`，
`SysY2022ParserVisitor.h` 含 `visitVecDecl`。

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

### 3.9 【设计约束】浮点向量不支持

本版 `isVecValue` 要求 `elementType->isInteger()`，浮点数组不会被识别为向量。
这是有意为之——`vecf` 需要新的 IR 类型或更复杂的标量展开，留给后续版本。

---

## 四、支持的语法形态总览

| 语法 | 语义 | IR 展开 | 实现位置 |
|------|------|---------|----------|
| `vec a;` | 默认长度 4，零初始化 | `alloca [4 x i32]` + 4 个 `store 0` | `visitVecDecl` Case 1 |
| `vec a = {1, 2, 3, 4};` | 列表初始化，长度 = 元素数 | `alloca [N x i32]` + N 个 `store` | `visitVecDecl` Case 2 |
| `vec c = a + b;` | 表达式初始化，rhs 须为向量 | 求 rhs → `alloca` 同长 lhs → `emitVecCopy` | `visitVecDecl` Case 3 |
| `c = a + b;`（声明后赋值） | 向量赋值 | 逐元素 `load / store` | `visitStmt` + `emitVecCopy` |
| `vec c = a * 2;` / `3 + a` | 标量广播 | 标量与每个元素做标量 op | `emitVecScalarOp` |
| `c[i]` | 向量索引 | 标准 GEP（与普通数组一致） | `visitLVal` 原逻辑 |
| `a + b` / `a - b` / `a * b` / `a / b` / `a % b` | 向量二元运算 | N 个标量 `load / op / store` | `visitAddExp` / `visitMulExp` + `emitVecBinOp` |

支持运算符：`+ - * / %`（`/` 和 `%` 是 `SDIV / SREM`，非 SIMD 求余）。

---

## 五、测试用例

新建 `test/functional/simd_vector/` 子目录，7 个用例覆盖所有路径：

| 用例 | 覆盖点 | 期望输出 |
|------|--------|----------|
| `01_vec_add.sy` | `vec + vec`（用户原例） | `5 5 5 5` |
| `02_vec_sub.sy` | `vec - vec` | `-3 -1 1 3` |
| `03_vec_mul.sy` | `vec * vec`（逐元素） | `4 6 6 4` |
| `04_vec_scalar.sy` | `vec * k`、`k + vec`、`vec * 10`、`k - vec` | 4 行广播结果 |
| `05_vec_assign.sy` | `vec c; c = a + b;`（visitStmt 路径） | `5 5 5 5` |
| `06_vec_default.sy` | `vec a;` 默认长度 4 零初始化 | `0 0 0 0` |
| `07_vec_chain.sy` | `a + b + c` 链式（result of emitVecBinOp 再做 +） | `8 8 8 8` |

每个 `.sy` 都配 `.out` 文件（**最后一行是返回码**，`run_tests.sh` 用 `head -n -1`
剥离，剩下的是期望 stdout）。

测试脚本：`scripts/run_simd_tests.sh`：

```bash
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_simd_tests.sh O0"
wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && bash scripts/run_simd_tests.sh O1"
```

**实测**：7/7 PASS @ O0，7/7 PASS @ O1（说明优化器对去语法糖后的标量 IR 完全友好）。
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

1. **真 SIMD 寄存器打包**：新增 `VectorType`（如 `<4 x i32>`），TargetCodeGen 把
   连续 4 个 `load + add + store` 合并成 `vle32.v + vadd.vv + vse32.v`（需 RVV）。
   这属于"后端"改动，**不在本版范围内**。
2. **变长向量**：之前 `feat-vector-dynamic` 分支已探索"数据指针 + 长度 + 静态堆"
   方案，可作为运行时库扩展。属于"运行时定长"或"运行时变长"，**不在本版范围内**。
3. **更丰富的元素类型**：当前只支持 `i32`，可扩展 `vecf`（float）、`veci64` 等。
4. **向量内建函数**：`dot(a, b)`、`sum(a)`、`shuffle(a, b, mask)` 等归约/重排操作。

本版保持后端/优化器零侵入，为上述扩展保留了干净的接入点：只要在 IRBuilder 把
新语法去语法糖为标量 IR，无需触碰其他层。

---

## 八、文件清单

| 文件 | 状态 | 说明 |
|------|------|------|
| `grammar/SysY2022Lexer.g4` | 修改 | 加 `VEC: 'vec';`（标 `《前端+定长》`） |
| `grammar/SysY2022Parser.g4` | 修改 | 加 `vecDecl` / `vecInit`（标 `《前端+定长》`） |
| `include/ir/IRBuilder.h` | 修改 | 加 4 个方法声明（标 `《前端+定长》`） |
| `src/ir/IRBuilder.cpp` | 修改 | 10 处实现与接线（均标 `《前端+定长》`） |
| `src/antlr/*` | 重新生成 | 4.13.1，对齐云端 runtime |
| `test/functional/simd_vector/01..07_*.sy` + `.out` | 新增 | 7 个测试用例 |
| `scripts/run_simd_tests.sh` | 新增 | SIMD 测试运行器 |
| `SIMD向量语法_前端+定长_实现说明.md` | 新增 | 本文档 |

---

## 九、定位微调点

需要微调本版实现时，用以下命令快速定位所有改动点：

```bash
# 列出所有源码改动
grep -rn '《前端+定长》' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp

# 只看 IRBuilder.cpp 中的改动
grep -n '《前端+定长》' src/ir/IRBuilder.cpp

# 统计改动点数
grep -r '《前端+定长》' grammar/ include/ir/IRBuilder.h src/ir/IRBuilder.cpp | wc -l
```

切换到其他版本（如"后端 + 真SIMD"或"前端 + 变长"）时，换一个标注名
（如 `《后端+SIMD》` 或 `《前端+变长》`）即可并存。
