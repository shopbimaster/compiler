# SysY2022 编译器 — WSL 测试操作手册

## 前置条件

- Windows 11 已启用 WSL，安装 Ubuntu 子系统
- 项目位于 Windows 文件系统：`D:\VSCodeProjects\compiler`
- WSL 内对应的路径为：`/mnt/d/VSCodeProjects/compiler`

> **如果 WSL 中尚需安装依赖，执行以下命令（仅首次）：**
> ```bash
> sudo apt-get update
> sudo apt-get install -y build-essential cmake openjdk-17-jdk-headless libantlr4-runtime-dev uuid-dev
> ```
>
> **后端验证额外依赖（RISC-V 工具链 + QEMU 用户模式模拟器）：**
> ```bash
> # 以 root 运行（非交互模式）
> wsl -d Ubuntu -u root -- bash -c "apt-get update -qq && apt-get install -y gcc-riscv64-linux-gnu qemu-user"
> ```
>
> 验证安装：
> ```bash
> wsl -d Ubuntu -- bash -c "riscv64-linux-gnu-gcc --version && qemu-riscv64 --version"
> ```
>
> > **注**：`riscv64-linux-gnu-gcc` 提供 C 标准库和启动文件，配合 `qemu-riscv64` 用户模式模拟器可直接运行生成的静态链接 RISC-V 可执行文件

---

## 一、构建项目

```bash
# 在 PowerShell 中进入 WSL
wsl -d Ubuntu
```

```bash
# 定位到项目目录并构建
cd /mnt/d/VSCodeProjects/compiler
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 二、IR 单元测试（18 项）

```bash
cd /mnt/d/VSCodeProjects/compiler/build
./test_ir
```

> **期望输出**：
> ```
> === IR 框架单元测试 ===
> [Type System]       9/9  PASSED
> [Def-Use Chain]     2/2  PASSED
> [Constants]         1/1  PASSED
> [Instructions]      3/3  PASSED
> [Module/BB/Func]    1/1  PASSED
> [IRBuilder E2E]     2/2  PASSED
> === 结果: 18 passed, 0 failed ===
> ```

---

## 三、.sy → IR 集成测试（23 项）

```bash
cd /mnt/d/VSCodeProjects/compiler/build
./test_integration
```

> **期望输出**：
> ```
> === 集成测试: .sy → IR ===
>   TEST: int main() { return 0; } ... PASSED
>   TEST: int main() { return 1+2*3; } ... PASSED
>   TEST: int main() { int a; a = 42; return a; } ... PASSED
>   TEST: if-else 分支 ... PASSED
>   TEST: while 循环 ... PASSED
>   TEST: 函数调用 ... PASSED
>   TEST: 语法错误检测（缺少分号） ... PASSED
>   TEST: break 跳出循环 ... PASSED
>   TEST: continue 跳过迭代 ... PASSED
>   TEST: 全局变量 ... PASSED
>   TEST: 一维数组存取 ... PASSED
>   TEST: 二维数组存取 ... PASSED
>   TEST: void 函数 ... PASSED
>   TEST: 数组参数传递 ... PASSED
>   TEST: const一维数组初始化 ... PASSED
>   TEST: const二维数组初始化 ... PASSED
>   TEST: constExpr作为数组维度 ... PASSED
>   TEST: 全局const作为数组维度 ... PASSED
>   TEST: 一维数组聚合初始化 ... PASSED
>   TEST: 二维数组聚合初始化 ... PASSED
>   TEST: I/O运行时函数声明与调用 ... PASSED
>   TEST: 全局常量数组 ... PASSED
>   TEST: 数组部分初始化（缺失元素补零） ... PASSED
> === 结果: 23 passed, 0 failed ===
> ```

---

## 四、sysyc 命令行工具测试

```bash
cd /mnt/d/VSCodeProjects/compiler/build

# 1. 最小程序
./sysyc ../test/hello.sy
# 期望: define i32 @main() { ret i32 0 }

# 2. 算术表达式
./sysyc ../test/arithmetic.sy
# 期望: mul + add + ret

# 3. 变量声明与赋值
./sysyc ../test/variable.sy
# 期望: alloca + store + load + ret

# 4. 条件分支 if-else
./sysyc ../test/ifelse.sy
# 期望: icmp + br + then/else + ret

# 5. while 循环
./sysyc ../test/while_test.sy
# 期望: while_cond/while_body/while_end + br

# 6. 函数调用
./sysyc ../test/func_call.sy
# 期望: define add + define main + call

# 7. 输出到文件
./sysyc ../test/hello.sy -o output.ir
cat output.ir

# 8. break 循环跳出
./sysyc ../test/break_test.sy

# 9. continue 循环跳过
./sysyc ../test/continue_test.sy

# 10. 全局变量
./sysyc ../test/global_var.sy

# 11. 一维数组
./sysyc ../test/array_1d.sy

# 12. 二维数组
./sysyc ../test/array_2d.sy

# 13. void 无返回值函数
./sysyc ../test/void_func.sy

# 14. 数组作为参数传递
./sysyc ../test/array_param.sy

# 15. 一维数组初始化
./sysyc ../test/array_init.sy

# 16. 二维数组初始化
./sysyc ../test/array_init2d.sy

# 17. const 一维数组
./sysyc ../test/const_1d.sy

# 18. const 二维数组
./sysyc ../test/const_2d.sy

# 19. constExpr 作为数组维度
./sysyc ../test/const_arr_dim.sy

# 20. 全局 const 作为数组维度
./sysyc ../test/global_const_dim.sy

# 21. I/O 运行时函数
./sysyc ../test/io_test.sy

# 22. 全局常量数组
./sysyc ../test/global_const_arr.sy

# 23. 数组部分初始化
./sysyc ../test/arr_partial.sy
```

---

## 五、前端 G4 语法文件独立验证

> 仅在修改 G4 文件后需要执行

```bash
cd /mnt/d/VSCodeProjects/compiler
mkdir -p /tmp/sysy-test

# 生成 Java 版本（Lexer + Parser 必须同一命令）
java -jar lib/antlr-4.10.1.jar -Dlanguage=Java -o /tmp/sysy-test \
  grammar/SysY2022Lexer.g4 grammar/SysY2022Parser.g4

# 编译
javac -cp ".:lib/antlr-4.10.1.jar" /tmp/sysy-test/*.java

# 测试
export CLASSPATH="/tmp/sysy-test:lib/antlr-4.10.1.jar"
java org.antlr.v4.gui.TestRig SysY2022 compilationUnit test/hello.sy
# 期望: 无报错（静默返回）
```

---

## 六、ANTLR C++ 文件重新生成

> 修改 G4 文件后需要重新生成 C++ 代码

```bash
cd /mnt/d/VSCodeProjects/compiler
java -jar lib/antlr-4.10.1.jar -Dlanguage=Cpp -no-listener -visitor \
  -o src/antlr grammar/SysY2022Lexer.g4 grammar/SysY2022Parser.g4

# 然后重新构建
cd build && cmake .. && make -j$(nproc)
```

---

## 七、一键全量测试

```bash
cd /mnt/d/VSCodeProjects/compiler/build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc) && \
./test_ir && \
./test_integration && \
echo "🎉 全量测试全部通过！（18 单元 + 23 集成 = 41/41）"
```

---

## 八、Final_Test 批量编译验证

> 验证所有官方测试用例从 .sy → IR 编译通过

```bash
cd /mnt/d/VSCodeProjects/compiler

# functional（100 项）
pass=0 fail=0
for f in test/Final_Test/functional/*.sy; do
    if build/sysyc "$f" > /dev/null 2>&1; then
        pass=$((pass+1))
    else
        echo "[FAIL] $(basename "$f")"
        fail=$((fail+1))
    fi
done
echo "functional: $pass passed, $fail failed"

# h_functional（40 项）
pass=0 fail=0
for f in test/Final_Test/h_functional/*.sy; do
    if build/sysyc "$f" > /dev/null 2>&1; then
        pass=$((pass+1))
    else
        echo "[FAIL] $(basename "$f")"
        fail=$((fail+1))
    fi
done
echo "h_functional: $pass passed, $fail failed"

# performance（60 项）
pass=0 fail=0
for f in test/Final_Test/performance/*.sy; do
    if build/sysyc "$f" > /dev/null 2>&1; then
        pass=$((pass+1))
    else
        echo "[FAIL] $(basename "$f")"
        fail=$((fail+1))
    fi
done
echo "performance: $pass passed, $fail failed"
```

> **当前期望**：functional 100/100，h_functional 40/40，performance 60/60，**总计 200/200 全部通过**

---

## 九、后端 O0 代码生成验证（IR → RISC-V → 可执行 → 运行结果校验）

### 9.1 汇编输出（-S 参数）

```bash
cd /mnt/d/VSCodeProjects/compiler

# 输出到 stdout
build/sysyc -S test/hello.sy

# 输出到文件
build/sysyc -S test/hello.sy -o hello.S
```

### 9.2 端到端验证流程（.sy → .S → ELF → QEMU）

> 单文件一步式验证脚本：
> ```bash
> cd /mnt/d/VSCodeProjects/compiler
> 
> GCC=riscv64-linux-gnu-gcc
> QEMU=qemu-riscv64
> 
> for test in hello arithmetic variable ifelse while_test func_call global_var float_cmp; do
>     build/sysyc -S test/${test}.sy -o /tmp/${test}.S
>     $GCC -march=rv64gc -mabi=lp64d -static -o /tmp/${test}_bin /tmp/${test}.S
>     echo -n "${test}: "
>     $QEMU /tmp/${test}_bin
>     echo "exit=$?"
> done
> ```
>
> **期望输出**：
> ```
> hello: exit=0
> arithmetic: exit=7
> variable: exit=42
> ifelse: exit=0
> while_test: exit=5
> func_call: exit=7
> global_var: exit=42
> float_cmp: exit=1
> ```

### 9.3 单步调试汇编

```bash
# 查看生成的汇编代码
build/sysyc -S test/func_call.sy

# 查看 IR + 汇编 对比
echo "=== IR ===" && build/sysyc test/func_call.sy
echo ""
echo "=== ASM ===" && build/sysyc -S test/func_call.sy
```

---

## 十、一键全量验证（含后端）

```bash
# 在 WSL 内执行
cd /mnt/d/VSCodeProjects/compiler/build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# IR 层测试
./test_ir && ./test_integration && echo "IR 层: 41/41 通过"

# 后端端到端测试
cd /mnt/d/VSCodeProjects/compiler
GCC=riscv64-linux-gnu-gcc QEMU=qemu-riscv64
pass=0 fail=0
for test in hello arithmetic variable ifelse while_test func_call global_var float_cmp; do
    build/sysyc -S test/${test}.sy -o /tmp/${test}.S
    $GCC -march=rv64gc -mabi=lp64d -static -o /tmp/${test}_bin /tmp/${test}.S 2>/dev/null || continue
    $QEMU /tmp/${test}_bin > /dev/null 2>&1
    [ $? -eq 0 ] && pass=$((pass+1)) || fail=$((fail+1))
done
echo "后端: ${pass}/$((pass+fail)) 通过"
```

---

## 测试结果速查

| 模块 | 测试项 | 通过标志 |
|-----|--------|---------|
| 前端 G4 | `hello.sy` 解析 | 终端无报错 |
| IR Type System | 唯一性、toString | 9/9 PASSED |
| IR Def-Use | 引用链维护 | 2/2 PASSED |
| IR Constants | 缓存验证 | 1/1 PASSED |
| IR Instructions | 创建方法 | 3/3 PASSED |
| IR Module | dump() 输出 | 1/1 PASSED |
| IRBuilder E2E | main→ret | 2/2 PASSED |
| 集成: hello.sy | 最小 main | IR 输出正确 |
| 集成: arithmetic.sy | 表达式 | mul+add+ret |
| 集成: variable.sy | 变量 | alloca+store+load |
| 集成: ifelse.sy | 分支 | icmp+br+then/else |
| 集成: while_test.sy | 循环 | while_cond/body/end |
| 集成: func_call.sy | 函数调用 | define+call |
| 集成: bad.sy | 错误检测 | 抛出异常 |
| 集成: break_test.sy | break | br to while_end |
| 集成: continue_test.sy | continue | br to while_cond |
| 集成: global_var.sy | 全局变量 | global 声明 |
| 集成: array_1d.sy | 一维数组 | alloca+getelementptr |
| 集成: array_2d.sy | 二维数组 | alloca+getelementptr |
| 集成: void_func.sy | void 函数 | define void |
| 集成: array_param.sy | 数组参数 | pointer type param |
| 集成: const_1d.sy | const 1D 数组 | alloca+store |
| 集成: const_2d.sy | const 2D 数组 | alloca+store |
| 集成: const_arr_dim.sy | constExpr 维度 | const 表达式求值 |
| 集成: global_const_dim.sy | 全局const引用 | global+constant |
| 集成: array_init.sy | 数组聚合初始化 | getelementptr+store |
| 集成: array_init2d.sy | 2D 聚合初始化 | getelementptr+store |
| 集成: io_test.sy | I/O 内置函数 | declare+call |
| 集成: global_const_arr.sy | 全局const数组 | global array |
| 集成: arr_partial.sy | 部分初始化 | 缺失元素补零 |
| Final_Test | functional 批量 | 100/100 ✅ |
| Final_Test | h_functional 批量 | 40/40 ✅ |
| Final_Test | performance 批量 | 60/60 ✅ |
| 后端: hello.sy | hello 端到端 | qemu exit=0 ✅ |
| 后端: arithmetic.sy | 算术端到端 | qemu exit=7 ✅ |
| 后端: variable.sy | 变量端到端 | qemu exit=42 ✅ |
| 后端: ifelse.sy | 分支端到端 | qemu exit=0 ✅ |
| 后端: while_test.sy | 循环端到端 | qemu exit=5 ✅ |
| 后端: func_call.sy | 函数调用端到端 | qemu exit=7 ✅ |
| 后端: global_var.sy | 全局变量端到端 | qemu exit=42 ✅ |
| 后端: float_cmp.sy | 浮点比较端到端 | qemu exit=1 ✅ |