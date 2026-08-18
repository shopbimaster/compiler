# SysY2026 张量语法——实现位置与后端接口检查

> 分支：`feat-tensor`
> 本文按文件逐一标注所有修改位置（附源代码），并列出后端接口发现的问题。

---

## 一、修改位置总览

| 文件 | 修改性质 |
|------|----------|
| `grammar/SysY2022Lexer.g4` | 词法新增 TENSOR / MATMUL |
| `grammar/SysY2022Parser.g4` | 语法新增 tensorType、扩展 bType / funcType / mulExp |
| `include/ir/IRBuilder.h` | 新增 6 个张量辅助方法声明 + tensorVars 成员 |
| `src/ir/IRBuilder.cpp` | 核心：7 个 visitor 接线点 + 7 个辅助函数实现 |
| `src/antlr/*` | ANTLR 4.13.1 重新生成（自动产物） |
| `test/functional/tensor/*.sy + *.out` | 5 组测试 |
| `scripts/run_tensor_tests.sh` | tensor 回归脚本 |

---

## 二、词法层

### 文件：`grammar/SysY2022Lexer.g4`

**位置 1：关键字区（第 13 行，CONST 之后）**

```antlr
TENSOR:   'tensor';
```

**位置 2：算术运算符区（第 39 行，MOD 之后）**

```antlr
MATMUL: '@';
```

---

## 三、语法层

### 文件：`grammar/SysY2022Parser.g4`

**位置 1：bType 规则（第 17 行）**

```antlr
// 基本类型（SysY2026 新增：张量类型 'tensor' ('int' | 'float')）
bType: INT | FLOAT | tensorType;
```

**位置 2：新增 tensorType 规则（第 20 行）**

```antlr
// 张量类型：各维度大小由声明时方括号显式确定，如 tensor int t[2][3]
tensorType: TENSOR (INT | FLOAT);
```

**位置 3：funcType 规则（第 47 行）**

```antlr
// 函数类型（SysY2026 新增：张量返回类型）
funcType: VOID | INT | FLOAT | tensorType;
```

**位置 4：mulExp 规则（第 100-102 行）**

```antlr
// 乘除模表达式（SysY2026 新增：'@' 矩阵乘法，与乘除同优先级）
mulExp:
    unaryExp
    | mulExp (STAR | DIV | MOD | MATMUL) unaryExp;
```

---

## 四、IRBuilder 头文件

### 文件：`include/ir/IRBuilder.h`

**位置：第 89-106 行（private 区，工具方法之后）**

```cpp
    // ===== SysY2026 张量辅助 =====
    // 记录张量变量名（由 'tensor' 关键字声明），区分普通数组。
    std::unordered_map<std::string, bool> tensorVars;   // name → isFloat
    // 判断 Value 是否为张量操作数（指向数组的 alloca 指针，且变量名在 tensorVars 中）
    // 注：表达式中的张量通过 lVal 的裸名进入，先由 visitPrimaryExp 保留 alloca 指针。
    bool   isTensorOperand(Value* v);
    // 张量逐元素标量运算：lhs/rhs 均为同形张量 alloca，生成结果张量 alloca。
    Value* emitTensorElementWise(Instruction::Opcode intOp, Instruction::Opcode floatOp,
                                 Value* lhs, Value* rhs);
    // 张量与标量：标量提升到同型张量（每个分量都等于该标量），再逐元素运算。
    Value* emitTensorScalarOp(Instruction::Opcode intOp, Instruction::Opcode floatOp,
                              Value* tensorVal, Value* scalarVal, bool scalarOnLeft);
    // 单目取负：-tensor → 逐元素取负（0 - elem 或 0.0 - elem）。
    Value* emitTensorNeg(Value* tensorVal);
    // 张量拷贝赋值：dst = src（同形张量逐元素拷贝）。
    void   emitTensorCopy(Value* dst, Value* src);
    // 矩阵乘法 @：lhs[M x N] @ rhs[N x L] → result[M x L]。
    Value* emitTensorMatMul(Value* lhs, Value* rhs);
```

---

## 五、IRBuilder 实现

### 文件：`src/ir/IRBuilder.cpp`

以下按 visitor 调用顺序和辅助函数位置逐一标注。

---

### 接线点 1：`visitVarDecl`（第 162-241 行）

**第 164-166 行**：识别张量声明 + 获取元素类型

```cpp
    // SysY2026：记录该声明是否为张量（以及元素是否为 float）
    bool isTensorDecl = (ctx->bType()->tensorType() != nullptr);
    bool isFloatElem  = isTensorDecl && (ctx->bType()->tensorType()->FLOAT() != nullptr);
```

**第 240-241 行**：在 `declare(name, alloca)` 之后登记到 `tensorVars`

```cpp
        // SysY2026：登记张量变量名（全局与局部统一）
        if (isTensorDecl) tensorVars[name] = isFloatElem;
```

> **注意**：`visitBType` 在第 155-157 行调用 `toIRType(ctx->getText())`，对 `tensor float` 会返回 I32（见后端问题 1）。

---

### 接线点 2：`visitStmt`（第 420-443 行）

**第 424-434 行**：张量整体赋值拦截

```cpp
            // SysY2026：张量整体赋值（t = tensor_expr）→ 逐元素拷贝
            // lhs 是张量 alloca，rhs 是指向数组的指针（张量运算结果或另一张量）
            bool lhsIsTensorArr = lhsPtr->getType()->isPointer() &&
                static_cast<PointerType*>(lhsPtr->getType())->getPointeeType()->isArray() &&
                isTensorOperand(lhsPtr);
            bool rhsIsArrPtr = rhs->getType()->isPointer() &&
                static_cast<PointerType*>(rhs->getType())->getPointeeType()->isArray();
            if (lhsIsTensorArr && rhsIsArrPtr) {
                emitTensorCopy(lhsPtr, rhs);
                return {};
            }
```

---

### 接线点 3：`visitPrimaryExp`（第 641-672 行）

**第 650-654 行**：张量裸名保持 alloca 指针不衰减

```cpp
            if (pointee->isArray()) {
                // SysY2026 张量：裸名保持 alloca 指针，让表达式层识别为张量操作数
                if (isTensorOperand(ptr)) {
                    return std::any(static_cast<Value*>(ptr));
                }
                // 普通数组：衰减为首元素指针
                std::vector<Value*> indices;
                indices.push_back(ConstantInt::get(IntegerType::I32, 0));
                indices.push_back(ConstantInt::get(IntegerType::I32, 0));
                auto* gep = Instruction::createGetElementPtr(pointee, ptr, indices, newTempName());
                currentBB->pushBack(gep);
                return std::any(static_cast<Value*>(gep));
            }
```

---

### 接线点 4：`visitUnaryExp`（第 773-783 行）

**张量单目运算拦截**

```cpp
        // SysY2026：张量单目运算
        if (isTensorOperand(operand)) {
            if (op == "-") {
                return std::any(emitTensorNeg(operand));
            }
            // '+' 原样输出（SysY2026 规范：输出原张量）
            if (op == "+") {
                return std::any(operand);
            }
            // '!' 对张量无定义，按标量 0 处理（不会产生有效结果，但保持编译通过）
        }
```

---

### 接线点 5：`visitMulExp`（第 887-965 行）

**第 904-940 行**：张量 * / % / @ 运算拦截

```cpp
        // ===== SysY2026 张量运算 =====
        bool lIsTensor = isTensorOperand(left);
        bool rIsTensor = isTensorOperand(rightVal);
        if (lIsTensor || rIsTensor) {
            if (opText == "@") {
                // 矩阵乘法：仅 2 维张量
                if (!lIsTensor || !rIsTensor) {
                    throw std::runtime_error("@ operator requires two tensor operands");
                }
                result = std::any(emitTensorMatMul(left, rightVal));
                i += 2;
                continue;
            }
            // * / % 逐元素
            if (opText == "%" ) {
                // 模运算：仅 int 张量支持
                if (lIsTensor && rIsTensor) {
                    result = std::any(emitTensorElementWise(Instruction::Opcode::SREM, Instruction::Opcode::SREM, left, rightVal));
                } else if (lIsTensor && !rIsTensor) {
                    result = std::any(emitTensorScalarOp(Instruction::Opcode::SREM, Instruction::Opcode::SREM, left, rightVal, /*scalarOnLeft=*/false));
                } else {
                    result = std::any(emitTensorScalarOp(Instruction::Opcode::SREM, Instruction::Opcode::SREM, rightVal, left, /*scalarOnLeft=*/true));
                }
            } else {
                auto intOp = (opText == "*") ? Instruction::Opcode::MUL  : Instruction::Opcode::SDIV;
                auto fltOp = (opText == "*") ? Instruction::Opcode::FMUL : Instruction::Opcode::FDIV;
                if (lIsTensor && rIsTensor) {
                    result = std::any(emitTensorElementWise(intOp, fltOp, left, rightVal));
                } else if (lIsTensor && !rIsTensor) {
                    result = std::any(emitTensorScalarOp(intOp, fltOp, left, rightVal, /*scalarOnLeft=*/false));
                } else {
                    result = std::any(emitTensorScalarOp(intOp, fltOp, rightVal, left, /*scalarOnLeft=*/true));
                }
            }
            i += 2;
            continue;
        }
```

---

### 接线点 6：`visitAddExp`（第 971-999 行）

**第 987-999 行**：张量 + / - 运算拦截

```cpp
        // ===== SysY2026 张量运算 =====
        bool lIsTensor = isTensorOperand(left);
        bool rIsTensor = isTensorOperand(rightVal);
        if (lIsTensor || rIsTensor) {
            auto intOp  = (opText == "+") ? Instruction::Opcode::ADD  : Instruction::Opcode::SUB;
            auto fltOp  = (opText == "+") ? Instruction::Opcode::FADD : Instruction::Opcode::FSUB;
            if (lIsTensor && rIsTensor) {
                result = std::any(emitTensorElementWise(intOp, fltOp, left, rightVal));
            } else if (lIsTensor && !rIsTensor) {
                result = std::any(emitTensorScalarOp(intOp, fltOp, left, rightVal, /*scalarOnLeft=*/false));
            } else {
                result = std::any(emitTensorScalarOp(intOp, fltOp, rightVal, left, /*scalarOnLeft=*/true));
            }
            i += 2;
            continue;
        }
```

---

### 辅助函数实现（第 1273-1545 行）

**第 1278-1291 行：`isTensorOperand`**

```cpp
bool IRBuilder::isTensorOperand(Value* v) {
    if (!v || !v->getType()->isPointer()) return false;
    auto* pointee = static_cast<PointerType*>(v->getType())->getPointeeType();
    if (!pointee->isArray()) return false;
    // 查找当前符号表中匹配此 alloca 值的变量名
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        for (const auto& kv : *it) {
            if (kv.second == v) {
                return tensorVars.find(kv.first) != tensorVars.end();
            }
        }
    }
    return false;
}
```

**第 1294-1303 行：`getTotalElements`（static）**

```cpp
static unsigned getTotalElements(Type* ty, Type*& leafType) {
    unsigned total = 1;
    leafType = ty;
    while (leafType->isArray()) {
        auto* arr = static_cast<ArrayType*>(leafType);
        total *= arr->getNumElements();
        leafType = arr->getElementType();
    }
    return total;
}
```

**第 1306-1313 行：`collectDims`（static）**

```cpp
static void collectDims(Type* ty, std::vector<unsigned>& dims, Type*& leafType) {
    leafType = ty;
    while (leafType->isArray()) {
        auto* arr = static_cast<ArrayType*>(leafType);
        dims.push_back(arr->getNumElements());
        leafType = arr->getElementType();
    }
}
```

**第 1317-1329 行：`flatToGepIndices`（static）**

```cpp
static std::vector<Value*> flatToGepIndices(unsigned flat, const std::vector<unsigned>& dims) {
    std::vector<Value*> indices;
    indices.push_back(ConstantInt::get(IntegerType::I32, 0));
    unsigned rem = flat;
    for (size_t d = 0; d < dims.size(); ++d) {
        unsigned stride = 1;
        for (size_t k = d + 1; k < dims.size(); ++k) stride *= dims[k];
        unsigned idx = rem / stride;
        rem %= stride;
        indices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(idx)));
    }
    return indices;
}
```

**第 1332-1375 行：`emitTensorElementWise`**

```cpp
Value* IRBuilder::emitTensorElementWise(Instruction::Opcode intOp, Instruction::Opcode floatOp,
                                         Value* lhs, Value* rhs) {
    Type* lhsLeafTy = nullptr;  unsigned lhsTotal = getTotalElements(
        static_cast<PointerType*>(lhs->getType())->getPointeeType(), lhsLeafTy);
    Type* rhsLeafTy = nullptr;  unsigned rhsTotal = getTotalElements(
        static_cast<PointerType*>(rhs->getType())->getPointeeType(), rhsLeafTy);
    if (lhsTotal != rhsTotal || lhsLeafTy != rhsLeafTy) {
        throw std::runtime_error("Tensor shape mismatch in element-wise op");
    }
    bool isFloat = lhsLeafTy->isFloat();
    Instruction::Opcode op = isFloat ? floatOp : intOp;
    auto* srcTy = static_cast<PointerType*>(lhs->getType())->getPointeeType();
    auto* resTy = static_cast<PointerType*>(lhs->getType())->getPointeeType();
    auto* result = Instruction::createAlloca(resTy, newTempName());
    currentBB->pushBack(result);

    std::vector<unsigned> dims;  Type* leaf = nullptr;
    collectDims(srcTy, dims, leaf);

    for (unsigned i = 0; i < lhsTotal; ++i) {
        std::vector<Value*> idx = flatToGepIndices(i, dims);
        auto* gepL = Instruction::createGetElementPtr(srcTy, lhs, idx, newTempName());
        currentBB->pushBack(gepL);
        auto* loadL = Instruction::createLoad(lhsLeafTy, gepL, newTempName());
        currentBB->pushBack(loadL);
        auto* gepR = Instruction::createGetElementPtr(srcTy, rhs, idx, newTempName());
        currentBB->pushBack(gepR);
        auto* loadR = Instruction::createLoad(lhsLeafTy, gepR, newTempName());
        currentBB->pushBack(loadR);
        auto* binOp = Instruction::createBinOp(op, lhsLeafTy, newTempName(), loadL, loadR);
        currentBB->pushBack(binOp);
        auto* gepRes = Instruction::createGetElementPtr(resTy, result, idx, newTempName());
        currentBB->pushBack(gepRes);
        auto* store = Instruction::createStore(binOp, gepRes);
        currentBB->pushBack(store);
    }
    return result;
}
```

**第 1378-1410 行：`emitTensorScalarOp`**

```cpp
Value* IRBuilder::emitTensorScalarOp(Instruction::Opcode intOp, Instruction::Opcode floatOp,
                                      Value* tensorVal, Value* scalarVal, bool scalarOnLeft) {
    Type* leafTy = nullptr;
    unsigned total = getTotalElements(
        static_cast<PointerType*>(tensorVal->getType())->getPointeeType(), leafTy);
    bool isFloat = leafTy->isFloat();
    Instruction::Opcode op = isFloat ? floatOp : intOp;
    Type* srcTy = static_cast<PointerType*>(tensorVal->getType())->getPointeeType();
    auto* result = Instruction::createAlloca(srcTy, newTempName());
    currentBB->pushBack(result);

    scalarVal = implConvert(scalarVal, leafTy);
    std::vector<unsigned> dims;  Type* leaf = nullptr;
    collectDims(srcTy, dims, leaf);
    for (unsigned i = 0; i < total; ++i) {
        std::vector<Value*> idx = flatToGepIndices(i, dims);
        auto* gep = Instruction::createGetElementPtr(srcTy, tensorVal, idx, newTempName());
        currentBB->pushBack(gep);
        auto* load = Instruction::createLoad(leafTy, gep, newTempName());
        currentBB->pushBack(load);
        auto* binOp = scalarOnLeft
            ? Instruction::createBinOp(op, leafTy, newTempName(), scalarVal, load)
            : Instruction::createBinOp(op, leafTy, newTempName(), load, scalarVal);
        currentBB->pushBack(binOp);
        auto* gepRes = Instruction::createGetElementPtr(srcTy, result, idx, newTempName());
        currentBB->pushBack(gepRes);
        auto* store = Instruction::createStore(binOp, gepRes);
        currentBB->pushBack(store);
    }
    return result;
}
```

**第 1413-1442 行：`emitTensorNeg`**

```cpp
Value* IRBuilder::emitTensorNeg(Value* tensorVal) {
    Type* leafTy = nullptr;
    unsigned total = getTotalElements(
        static_cast<PointerType*>(tensorVal->getType())->getPointeeType(), leafTy);
    Type* srcTy = static_cast<PointerType*>(tensorVal->getType())->getPointeeType();
    Value* zero = leafTy->isFloat()
        ? static_cast<Value*>(ConstantFloat::get(FloatType::get(), 0.0))
        : static_cast<Value*>(ConstantInt::get(IntegerType::I32, 0));
    auto* result = Instruction::createAlloca(srcTy, newTempName());
    currentBB->pushBack(result);

    std::vector<unsigned> dims;  Type* leaf = nullptr;
    collectDims(srcTy, dims, leaf);
    for (unsigned i = 0; i < total; ++i) {
        std::vector<Value*> idx = flatToGepIndices(i, dims);
        auto* gep = Instruction::createGetElementPtr(srcTy, tensorVal, idx, newTempName());
        currentBB->pushBack(gep);
        auto* load = Instruction::createLoad(leafTy, gep, newTempName());
        currentBB->pushBack(load);
        auto* subOp = leafTy->isFloat()
            ? Instruction::createBinOp(Instruction::Opcode::FSUB, leafTy, newTempName(), zero, load)
            : Instruction::createBinOp(Instruction::Opcode::SUB,    leafTy, newTempName(), zero, load);
        currentBB->pushBack(subOp);
        auto* gepRes = Instruction::createGetElementPtr(srcTy, result, idx, newTempName());
        currentBB->pushBack(gepRes);
        auto* store = Instruction::createStore(subOp, gepRes);
        currentBB->pushBack(store);
    }
    return result;
}
```

**第 1445-1463 行：`emitTensorCopy`**

```cpp
void IRBuilder::emitTensorCopy(Value* dst, Value* src) {
    Type* leafTy = nullptr;
    unsigned total = getTotalElements(
        static_cast<PointerType*>(src->getType())->getPointeeType(), leafTy);
    Type* srcTy = static_cast<PointerType*>(src->getType())->getPointeeType();
    std::vector<unsigned> dims;  Type* leaf = nullptr;
    collectDims(srcTy, dims, leaf);
    for (unsigned i = 0; i < total; ++i) {
        std::vector<Value*> idx = flatToGepIndices(i, dims);
        auto* gepS = Instruction::createGetElementPtr(srcTy, src, idx, newTempName());
        currentBB->pushBack(gepS);
        auto* loadS = Instruction::createLoad(leafTy, gepS, newTempName());
        currentBB->pushBack(loadS);
        auto* gepD = Instruction::createGetElementPtr(srcTy, dst, idx, newTempName());
        currentBB->pushBack(gepD);
        auto* store = Instruction::createStore(loadS, gepD);
        currentBB->pushBack(store);
    }
}
```

**第 1467-1545 行：`emitTensorMatMul`**

```cpp
Value* IRBuilder::emitTensorMatMul(Value* lhs, Value* rhs) {
    auto* lhsArrTy = static_cast<ArrayType*>(
        static_cast<PointerType*>(lhs->getType())->getPointeeType());
    auto* rhsArrTy = static_cast<ArrayType*>(
        static_cast<PointerType*>(rhs->getType())->getPointeeType());
    if (!lhsArrTy->getElementType()->isArray() ||
        !rhsArrTy->getElementType()->isArray()) {
        throw std::runtime_error("@ operator requires 2-D tensors");
    }
    unsigned M = lhsArrTy->getNumElements();
    auto* lhsRowTy = static_cast<ArrayType*>(lhsArrTy->getElementType());
    unsigned N = lhsRowTy->getNumElements();
    Type* leafTy = lhsRowTy->getElementType();

    unsigned N2 = rhsArrTy->getNumElements();
    auto* rhsRowTy = static_cast<ArrayType*>(rhsArrTy->getElementType());
    unsigned L = rhsRowTy->getNumElements();
    if (N != N2) {
        throw std::runtime_error("@ operator dimension mismatch: lhs cols != rhs rows");
    }

    Type* resRowTy = ArrayType::get(leafTy, L);
    Type* resTy    = ArrayType::get(resRowTy, M);
    auto* result = Instruction::createAlloca(resTy, newTempName());
    currentBB->pushBack(result);

    bool isFloat = leafTy->isFloat();
    auto mulOp = isFloat ? Instruction::Opcode::FMUL : Instruction::Opcode::MUL;
    auto addOp = isFloat ? Instruction::Opcode::FADD : Instruction::Opcode::ADD;

    for (unsigned i = 0; i < M; ++i) {
        for (unsigned j = 0; j < L; ++j) {
            Value* acc = isFloat
                ? static_cast<Value*>(ConstantFloat::get(FloatType::get(), 0.0))
                : static_cast<Value*>(ConstantInt::get(IntegerType::I32, 0));
            for (unsigned k = 0; k < N; ++k) {
                std::vector<Value*> idxL = {
                    ConstantInt::get(IntegerType::I32, 0),
                    ConstantInt::get(IntegerType::I32, static_cast<int64_t>(i)),
                    ConstantInt::get(IntegerType::I32, static_cast<int64_t>(k))
                };
                auto* gepL = Instruction::createGetElementPtr(lhsArrTy, lhs, idxL, newTempName());
                currentBB->pushBack(gepL);
                auto* loadL = Instruction::createLoad(leafTy, gepL, newTempName());
                currentBB->pushBack(loadL);
                std::vector<Value*> idxR = {
                    ConstantInt::get(IntegerType::I32, 0),
                    ConstantInt::get(IntegerType::I32, static_cast<int64_t>(k)),
                    ConstantInt::get(IntegerType::I32, static_cast<int64_t>(j))
                };
                auto* gepR = Instruction::createGetElementPtr(rhsArrTy, rhs, idxR, newTempName());
                currentBB->pushBack(gepR);
                auto* loadR = Instruction::createLoad(leafTy, gepR, newTempName());
                currentBB->pushBack(loadR);
                auto* prod = Instruction::createBinOp(mulOp, leafTy, newTempName(), loadL, loadR);
                currentBB->pushBack(prod);
                acc = Instruction::createBinOp(addOp, leafTy, newTempName(), acc, prod);
                currentBB->pushBack(static_cast<Instruction*>(acc));
            }
            std::vector<Value*> idxRes = {
                ConstantInt::get(IntegerType::I32, 0),
                ConstantInt::get(IntegerType::I32, static_cast<int64_t>(i)),
                ConstantInt::get(IntegerType::I32, static_cast<int64_t>(j))
            };
            auto* gepRes = Instruction::createGetElementPtr(resTy, result, idxRes, newTempName());
            currentBB->pushBack(gepRes);
            auto* store = Instruction::createStore(acc, gepRes);
            currentBB->pushBack(store);
        }
    }
    return result;
}
```

---

## 六、后端接口检查

### 后端分析结论

后端 `TargetCodeGen.cpp` 对张量**无感知**——张量在 IR 层就是多维 `ArrayType`，后端处理的是通用的 Alloca / GEP / Load / Store / BinOp 指令。逐项检查如下：

| 后端函数 | 行号 | 检查结果 |
|----------|------|----------|
| `getTypeSize` | 557-566 | ✅ 递归处理嵌套 ArrayType，`[2 x [3 x i32]]` → 24 字节 |
| 栈帧布局（ALLOCA） | 490-501 | ✅ 用 `getTypeSize(pointee)` 计算栈偏移，多维数组正确 |
| `emitGetElementPtr`（常量路径） | 2721-2797 | ✅ 递归 `curPointee` 跟踪，`[0, i, j]` 三级索引正确计算偏移 |
| `emitGetElementPtr`（变量路径） | 2817-2901 | ✅ 同上，变量索引用 `emitStrideMul` 计算步长 |
| `emitLoad` | 2281-2349 | ✅ 根据 `loadTy` 选择 `lw`（i32）/ `flw`（float）/ `ld`（ptr） |
| `emitStore` | 2351-2380+ | ✅ 同上，根据值类型选择 `sw`/`fsw`/`sd` |
| 全局数组 | 125-146 | ✅ `getTypeSize(arrTy)` 计算总大小，`.zero` 填充 |

**后端无问题**——张量 IR 全部是标准多维数组指令序列，后端已正确支持。

---

### IRBuilder 层发现的问题

#### 问题 1（严重）：`visitBType` 对 `tensor float` 返回 I32

**位置**：`src/ir/IRBuilder.cpp` 第 155-157 行

```cpp
std::any IRBuilder::visitBType(SysY2022Parser::BTypeContext* ctx) {
    return std::any(toIRType(ctx->getText()));
}
```

**原因**：`ctx->getText()` 对 `tensor float` 返回 `"tensorfloat"`，而 `toIRType("tensorfloat")` 不匹配任何分支，fallback 返回 `IntegerType::I32`。这意味着 **`tensor float` 声明会错误地使用 i32 元素类型**。

`tensor int` 恰好不受影响（`"tensorint"` fallback 也是 I32），但 float 张量会静默地变成 int。

**修复**：

```cpp
std::any IRBuilder::visitBType(SysY2022Parser::BTypeContext* ctx) {
    if (ctx->tensorType()) {
        if (ctx->tensorType()->FLOAT()) return std::any(FloatType::get());
        return std::any(IntegerType::I32);
    }
    return std::any(toIRType(ctx->getText()));
}
```

#### 问题 2（严重）：`visitFuncType` 对 tensor 返回类型返回 I32

**位置**：`src/ir/IRBuilder.cpp` 第 357-359 行

```cpp
std::any IRBuilder::visitFuncType(SysY2022Parser::FuncTypeContext* ctx) {
    return std::any(toIRType(ctx->getText()));
}
```

**原因**：同上。`tensor int` / `tensor float` 作为函数返回类型时被 fallback 为 I32。

**注意**：张量函数返回类型（如 `tensor int foo()`）在 SysY2026 规范中存在，但语义上需要返回指针或使用 sret 机制。当前测试用例未覆盖此场景。最小修复同问题 1，但完整实现需要设计张量返回 ABI。

#### 问题 3（中等）：链式张量运算失败

**位置**：`src/ir/IRBuilder.cpp` 第 1278-1291 行 `isTensorOperand`

**原因**：`emitTensorElementWise` / `emitTensorMatMul` 返回的临时 alloca 没有变量名，不在 `tensorVars` 中，`isTensorOperand` 对它们返回 false。

因此 `c = a + b + d` 这样的链式运算会失败：
1. `a + b` → `emitTensorElementWise` 返回临时 alloca `tmp1`
2. `tmp1 + d` → `isTensorOperand(tmp1)` = false，`isTensorOperand(d)` = true
3. 进入 `emitTensorScalarOp`（把 `d` 当张量、`tmp1` 当标量），结果错误

**修复方案**：在 `isTensorOperand` 中增加"指向 ArrayType 的指针即视为张量操作数"的快速判断，或在 `emitTensorElementWise` / `emitTensorMatMul` 返回前将临时 alloca 登记到 `tensorVars`。前者更简洁：

```cpp
bool IRBuilder::isTensorOperand(Value* v) {
    if (!v || !v->getType()->isPointer()) return false;
    auto* pointee = static_cast<PointerType*>(v->getType())->getPointeeType();
    if (!pointee->isArray()) return false;
    // 已登记的张量变量
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        for (const auto& kv : *it) {
            if (kv.second == v) {
                return tensorVars.find(kv.first) != tensorVars.end();
            }
        }
    }
    // 临时张量运算结果（指向数组的 alloca，无变量名）→ 视为张量
    if (auto* inst = dynamic_cast<Instruction*>(v)) {
        if (inst->getOpcode() == Instruction::Opcode::ALLOCA) return true;
    }
    return false;
}
```

#### 问题 4（轻微）：`emitTensorCopy` 用 src 的类型为 dst 生成 GEP

**位置**：`src/ir/IRBuilder.cpp` 第 1458 行

```cpp
auto* gepD = Instruction::createGetElementPtr(srcTy, dst, idx, newTempName());
```

`srcTy` 是 `src` 的 pointee 类型，用于 `dst` 的 GEP 在同型拷贝时没问题，但如果 dst 和 src 的数组类型不同（如维度相同但来源不同的 pointer），可能导致 GEP 计算错误。实践中拷贝要求同型，所以目前不出错，但代码应使用 dst 的类型：

```cpp
Type* dstTy = static_cast<PointerType*>(dst->getType())->getPointeeType();
auto* gepD = Instruction::createGetElementPtr(dstTy, dst, idx, newTempName());
```

#### 问题 5（信息）：`funcFParam` 对 tensor 参数未特殊处理

**位置**：`grammar/SysY2022Parser.g4` 第 53 行

```antlr
funcFParam: bType IDENTIFIER (L_BRACKET R_BRACKET (L_BRACKET exp R_BRACKET)*)?;
```

张量作为函数参数时使用与普通数组相同的 `bType IDENTIFIER [][]` 语法。`visitFuncDef` 第 274 行调用 `visitBType(param->bType())` 获取元素类型——由于问题 1，`tensor float` 参数也会得到 I32 而非 Float。修复问题 1 后此处自动正确。

但需注意：函数参数的张量**不会**被登记到 `tensorVars`，因此在函数体内对张量参数做运算时，`isTensorOperand` 无法识别。如果需要支持张量参数做运算，需要在 `visitFuncDef` 的参数注册循环中额外判断 `bType->tensorType()` 并登记。

---

## 七、总结

| 层 | 状态 | 问题 |
|----|------|------|
| 词法 | ✅ | 无 |
| 语法 | ✅ | 无 |
| IRBuilder | ⚠️ | 问题 1-5（问题 1/2 严重，问题 3 中等，4/5 轻微） |
| 后端 | ✅ | 无——张量 IR 全部是标准多维数组指令 |
| 测试 | ✅ | 5/5 O0+O1（但未覆盖 `tensor float` 和链式运算） |
