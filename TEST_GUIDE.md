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

## 三、.sy → IR 集成测试（7 项）

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
> === 结果: 7 passed, 0 failed ===
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
echo "🎉 全量测试全部通过！(25/25)"
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