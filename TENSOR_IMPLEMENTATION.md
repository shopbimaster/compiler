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

## V3：张量逐元素运算 + 标量提升 + 单目 ±（待实现）

## V4：矩阵乘法 @（待实现）

## V5：张量拷贝赋值（待实现）
