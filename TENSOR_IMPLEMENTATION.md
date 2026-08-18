# SysY2026 张量语法实现记录

> 分支：`feat-tensor`（基于 `main`）
> 策略：**最小修改、小步提交**。张量在 IR 层复用现有 `ArrayType` 多维数组机制，
> 不新增 IR 类型，后端/优化器零侵入。
> 每个提交（V1~V5）只新增一个最小功能系列，并跑快速全量回归。

## 实现思路总览

`tensor int t[2][3]` 在 IR 中等价于现有 `int t[2][3]`（即 `[2 x [3 x i32]]`）。
张量运算（`+ - * / % @`、单目 `±`）在 IRBuilder 阶段**去语法糖为标量逐元素运算**，
最终汇编是纯标量 RV64GC（无 SIMD/RVV），FPGA BOOM 软核可直接运行。

---

## V1：词法 + 语法扩展（后向兼容，基线编译回归通过）

### 修改点

| 文件 | 修改 |
|------|------|
| `grammar/SysY2022Lexer.g4` | 关键字加 `TENSOR: 'tensor';`；运算符加 `MATMUL: '@';` |
| `grammar/SysY2022Parser.g4` | `bType` 加 `\| tensorType`；新增 `tensorType: TENSOR (INT \| FLOAT);`；`funcType` 加 `\| tensorType`；`mulExp` 加 `\| MATMUL` |
| `src/antlr/*` | 用 ANTLR 4.13.1 重新生成（WSL Ubuntu-24.04-eval + `/usr/local/lib/antlr-4.13.1-complete.jar`） |

### 语法规则（SysY2026 规范）

```
BType      → 'int' | 'float' | TensorType
TensorType → 'tensor' ('int' | 'float')
FuncType   → 'void' | 'int' | 'float' | TensorType
MulExp     → UnaryExp | MulExp ('*' | '/' | '%' | '@') UnaryExp
```

### 验证

- `TensorTypeContext` 在 Parser.h 出现（6 处），`MATMUL` token 生成。
- clang++ 编译通过；`run_tests.sh func O0` 99 OK / 1 DIFF（`68_brainfk` 为预存失败，基线同样失败，与本轮无关）。

### 踩坑

- **ANTLR `-o` 路径陷阱**：`java -jar ... -o src/antlr grammar/X.g4` 会把生成物放到
  `src/antlr/grammar/` 嵌套子目录（按源文件相对路径展开）。需 `cp` 归位并 `rm -rf src/antlr/grammar`，
  否则编译用旧的 `.h` 导致找不到 `TensorTypeContext`。
- 生成命令（单行）：
  ```bash
  wsl -d Ubuntu-24.04-eval -e bash -lc "cd /mnt/d/VSCodeProjects/compiler && \
    java -jar /usr/local/lib/antlr-4.13.1-complete.jar -Dlanguage=Cpp -visitor -o src/antlr grammar/SysY2022Lexer.g4 && \
    cp -f src/antlr/grammar/SysY2022Lexer.tokens grammar/ && \
    java -jar /usr/local/lib/antlr-4.13.1-complete.jar -Dlanguage=Cpp -visitor -o src/antlr grammar/SysY2022Parser.g4 && \
    rm -f src/antlr/SysY2022* && cp -f src/antlr/grammar/SysY2022* src/antlr/ && rm -rf src/antlr/grammar"
  ```

---

## V2：张量声明与初始化（IRBuilder 零改动，复用现有多维数组机制）

### 修改点

| 文件 | 修改 |
|------|------|
| `test/functional/tensor/01_tensor_decl_init.sy` + `.out` | 新增测试：覆盖规范 3 个示例的初始化 |

### 原理

`tensor int t1[4]={1,2};` 经 `visitVarDecl` 走普通数组路径：`toIRType("tensorint")`
fallback 到 I32，`t1` 类型为 `[4 x i32]`。`emitInitStoresVar` 递归处理嵌套花括号
并做**顺序填充 + 零填充**，正好是张量规范要求的语义。

- `tensor int t1[4]={1,2}` → `{1,2,0,0}`
- `tensor int t2[2][3]={{3,4},{5,6,7}}` → `{{3,4,0},{5,6,7}}`

### 验证

编译 + qemu 运行输出与 `.out` 一致。

### 踩坑

无需改动——现有数组初始化的零填充语义与张量规范完全一致。

---

## V3：张量逐元素运算 + 标量提升 + 单目 ±

### 修改点

| 文件 | 修改 |
|------|------|
| `include/ir/IRBuilder.h` | 加 `tensorVars` 成员（name→isFloat）+ 6 个辅助方法声明：`isTensorOperand`/`emitTensorElementWise`/`emitTensorScalarOp`/`emitTensorNeg`/`emitTensorCopy`/`emitTensorMatMul` |
| `src/ir/IRBuilder.cpp` | `visitVarDecl` 登记张量变量；`visitPrimaryExp` 张量裸名不衰减（保留 alloca 指针）；`visitAddExp`/`visitMulExp` 张量运算拦截；`visitUnaryExp` 张量 `-`/`+`；`visitStmt` 张量整体赋值走 `emitTensorCopy`；新增 6 个辅助方法实现（`emitTensorMatMul` 留 stub） |
| `test/functional/tensor/02_tensor_arith.sy` + `.out` | 新增测试：9 种运算全覆盖 |

### 原理

- **识别**：`tensorVars` 记录 `tensor` 声明的变量名；`isTensorOperand(v)` 反查符号表确认 v 是张量 alloca。
- **运算展开**：`emitTensorElementWise` 对 lhs/rhs 逐元素 `load → op → store`；`emitTensorScalarOp` 把标量隐式转换到元素类型后与每元素运算（`scalarOnLeft` 控制操作数顺序）。
- **单目**：`-t` → 逐元素 `0 - elem`；`+t` → 原样返回（SysY2026 规范）。
- **结果**：返回新的临时张量 alloca（`[N x ...]` 指针），可继续参与链式运算或被 `t = ...` 拷贝。

### 验证

- 9 种运算输出全对（O0 + O1）。
- 功能回归 99/100（仅预存 68_brainfk DIFF）。

### 踩坑

- **临时结果不在 tensorVars**：`emitTensorElementWise` 返回的临时 alloca 没有变量名，
  `isTensorOperand` 对它会返回 false。因此 `visitStmt` 的张量赋值判断**不能只靠
  `isTensorOperand(rhs)`**，而是判断 rhs 是"指向数组的指针"即可（lhs 仍需是张量）。
- **多维数组扁平索引**：`emitTensorElementWise` 用 `[0, i]` 扁平 GEP 遍历（行优先展开），
  与张量"按赋值表达式顺序填充"的线性内存布局一致。

---

## V4：矩阵乘法 @

### 修改点

| 文件 | 修改 |
|------|------|
| `src/ir/IRBuilder.cpp` | 补全 `emitTensorMatMul`（三重循环标量展开 M×L×N）；修正 `emitTensorElementWise`/`emitTensorScalarOp`/`emitTensorNeg`/`emitTensorCopy` 的扁平索引为多维 GEP 下标（新增 `collectDims`/`flatToGepIndices` 辅助函数） |
| `test/functional/tensor/03_tensor_matmul.sy` + `.out` | 2×2 @ 2×2 验证 |
| `test/functional/tensor/04_tensor_2d.sy` + `.out` | 2D 逐元素运算验证 |

### 踩坑

- **扁平索引不适用于多维数组**：V3 用 `[0, i]` 扁平索引对 1D 张量正确，但对 2D 张量
  `[2 x [3 x i32]]` 的 GEP `[0, 5]` 无效（第一维最大索引为 1）。修复：用 `flatToGepIndices`
  把线性索引按行优先分解为 `[0, i, j, ...]` 多级下标。
- 这同时说明**V3 的测试只覆盖了 1D**，多维测试（04_tensor_2d）必须在 V4 补上。

### 验证

- matmul: `{{1,2},{3,4}} @ {{5,6},{7,8}} = {{19,22},{43,50}}` ✓
- 2D 逐元素 + 与 * 输出正确 ✓（O0+O1）。
- 功能回归 99/100。

---

## V5：张量拷贝赋值 `t1 = t2`

### 修改点

| 文件 | 修改 |
|------|------|
| `src/ir/IRBuilder.cpp` | `visitStmt` 的 `lVal = exp` 分支：lhs 是张量 alloca 且 rhs 是"指向数组的指针"（张量变量或张量运算临时结果）时，走 `emitTensorCopy` 逐元素拷贝，不再生成单个 store |
| `test/functional/tensor/05_tensor_copy.sy` + `.out` | 新增测试：1D 拷贝、深拷贝验证（改 dst 不影响 src）、2D 拷贝 |
| `scripts/run_tensor_tests.sh` | 新增 tensor 套件一键回归脚本（编译→链接→qemu→比对 .out，支持 O0/O1） |

### 原理

- 张量变量在表达式中以**裸名**出现时（`visitPrimaryExp`），保留 alloca 指针不衰减，
  因此 `a = b` 的 rhs 直接拿到 src 张量的基址。
- `emitTensorCopy(dst, src)` 按元素总数（`getTotalElements`）行优先展开，
  每个元素 `load src → store dst`，多维下标由 `flatToGepIndices` 分解——天然深拷贝。
- 该接线同时覆盖 `t = a + b`、`t = a @ b` 等"运算结果赋回张量"的情形
  （rhs 是 `emitTensorElementWise`/`emitTensorMatMul` 返回的临时张量 alloca）。

### 验证

- 05_tensor_copy：`a=b` 后 a={10,20,30,40}；改 a[0]=99 后 b[0] 仍为 10（深拷贝）；2D `m2=m1` 正确 ✓
- tensor 套件 O0/O1 全 5 通过；func O0 99 OK / 1 DIFF（68_brainfk 预存）、
  func O1 97 OK / 3 DIFF（62_percolation/68_brainfk/71_full_conn 均为基线预存失败）；quick 5/5。

### 踩坑

- **判断条件不能只靠 `isTensorOperand(rhs)`**：张量运算的临时结果 alloca 没有变量名，
  不在 `tensorVars` 中。故条件为"lhs 是张量 && rhs 是指向数组的指针"，
  普通数组不会触发（lhs 必须过 `isTensorOperand`）。

---

## 当前状态小结

V1~V5 全部完成：词法/语法 → 声明初始化 → 逐元素运算+标量提升+单目 → 矩阵乘法 → 拷贝赋值。
张量在 IR 层即多维数组，后端/优化器零改动；tensor 套件 5/5（O0+O1），全量回归无新增失败。
