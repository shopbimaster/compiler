# SysY2022 编译器 — 测试指导文档

## 项目目录结构

```
compiler/
├── grammar/            # G4 语法定义文件
│   ├── SysY2022Lexer.g4
│   └── SysY2022Parser.g4
├── include/            # 头文件（IR、后端、优化、工具）
│   ├── backend/        # 后端头文件（代码生成、寄存器分配、窥孔优化）
│   ├── ir/             # IR 数据结构头文件
│   ├── opt/            # 优化器头文件
│   └── utils/          # 工具头文件（错误处理、日志）
├── src/                # 源代码
│   ├── backend/        # 后端实现（TargetCodeGen、RegisterAllocator）
│   ├── ir/             # IR 实现（IR、IRBuilder）
│   ├── opt/            # 优化器实现（14个优化Pass）
│   ├── utils/          # 工具实现
│   ├── Compiler.cpp    # 编译器封装
│   └── main.cpp        # 主入口 compiler
├── test/               # 测试用例
│   ├── functional/     # 功能测试用例（100个）
│   ├── h_functional/   # 高阶功能测试用例（40个）
│   ├── performance/    # 性能测试用例（60个）
│   └── *.sy            # 项目级测试用例（instr_sched、loop_unroll 等）
├── tests/              # 测试代码（独立于源码，测评平台不扫描）
│   ├── test_ir.cppx    # IR 单元测试代码
│   └── test_integration.cppx # 集成测试代码
├── scripts/            # 测试与构建脚本
│   ├── run_tests.sh    # [主入口] 统一测试框架
│   ├── test_qemu.sh    # QEMU 端到端快速测试
│   ├── test_qemu_all.sh# QEMU 全量回归测试
│   ├── test_qemu_sched.sh # 指令调度 QEMU 测试
│   ├── test_qemu_unroll.sh # 循环展开 QEMU 测试
│   ├── test_all_functional.sh # 全量功能测试（仅编译+运行）
│   ├── test_runtime.sh # 运行时库 I/O 测试
│   ├── build/          # 构建脚本
│   │   ├── build_sylib.sh  # 运行时库构建
│   │   └── build_backend.sh # 后端快速构建
│   ├── debug/          # 调试脚本（临时/问题定位用）
│   │   ├── _run_func_tests.sh  # 原功能测试（被 run_tests.sh 替代）
│   │   ├── _run_hfunc_tests.sh # 原高阶功能测试（被替代）
│   │   ├── _run_perf_tests.sh  # 原性能测试（被替代）
│   │   ├── _quick_test.sh      # 原快速测试
│   │   ├── _test_one.sh        # 单用例调试
│   │   ├── _test80.sh          # 80号用例调试
│   │   ├── _debug_test.sh      # 调试测试
│   │   ├── _debug_diffs.sh     # 输出差异对比
│   │   ├── _run87.sh           # 87号用例调试
│   │   ├── _test_failing.sh    # 失败用例调试
│   │   ├── _trace.sh           # 汇编跟踪
│   │   ├── _test_stack.sh      # 栈帧测试
│   │   ├── _test_float_min.sh  # 浮点最小测试
│   │   ├── debug-float-segfault.md # 浮点段错误调试记录
│   │   └── _test_*.sy          # 调试用临时测试文件
│   ├── grammar/        # 语法测试脚本
│   │   ├── test-grammar.sh     # G4 语法验证
│   │   └── QUICK_GRAMMAR_TEST.sh # 快速语法验证
│   └── setup/          # 环境配置脚本
│       ├── setup-ubuntu.sh     # Ubuntu 一键部署
│       ├── install_riscv.sh    # RISC-V 工具链安装
│       └── check-references.sh # 引用检查
├── SysYlib/            # SysY 运行时库
│   ├── sylib.c
│   └── sylib.h
├── lib/                # 第三方库
│   └── antlr-4.10.1.jar
├── grammar/            # G4 语法文件
├── CMakeLists.txt      # 主构建配置
├── .gitignore
└── TEST_GUIDE.md       # 本文件
```

---

## 前置条件

- Windows 11 已启用 WSL，安装 Ubuntu 子系统
- 项目位于 Windows 文件系统：`D:\VSCodeProjects\compiler`
- WSL 内对应的路径为：`/mnt/d/VSCodeProjects/compiler`

### 首次环境安装

```bash
# 基础依赖
sudo apt-get update
sudo apt-get install -y build-essential cmake openjdk-17-jdk-headless libantlr4-runtime-dev uuid-dev

# RISC-V 工具链 + QEMU（后端验证必需）
wsl -d Ubuntu -u root -- bash -c "apt-get update -qq && apt-get install -y gcc-riscv64-linux-gnu qemu-user"
```

验证安装：
```bash
wsl -d Ubuntu -- bash -c "riscv64-linux-gnu-gcc --version && qemu-riscv64 --version"
```

---

## 一、构建项目

```bash
cd /mnt/d/VSCodeProjects/compiler
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)           # 构建 compiler 及所有测试程序
# 或仅构建编译器: make compiler
```

构建运行时库：
```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/build/build_sylib.sh
```

---

## 二、统一测试框架（推荐）

统一测试脚本 `scripts/run_tests.sh` 整合了所有测试套件，支持多优化级别。

### 2.1 基本用法

```bash
cd /mnt/d/VSCodeProjects/compiler

# 快速冒烟测试（5个关键用例）
bash scripts/run_tests.sh quick

# 功能测试（100个用例，O0 优化）
bash scripts/run_tests.sh func O0

# 高阶功能测试（40个用例，O3 优化）
bash scripts/run_tests.sh hfunc O3

# 性能测试（60个用例）
bash scripts/run_tests.sh perf O0

# 全量测试（所有套件，O0 优化）
bash scripts/run_tests.sh all O0

# 全量测试（测评服务器级别，全部优化）
bash scripts/run_tests.sh all O1
```

### 2.2 测试套件说明

| 套件 | 命令 | 用例数 | 说明 |
|------|------|--------|------|
| `quick` | `run_tests.sh quick` | 5 | 快速冒烟测试，验证基本功能 |
| `func` | `run_tests.sh func [opt]` | 100 | 功能测试（Final_Test functional） |
| `hfunc` | `run_tests.sh hfunc [opt]` | 40 | 高阶功能测试（Final_Test h_functional） |
| `perf` | `run_tests.sh perf [opt]` | 60 | 性能测试（Final_Test performance） |
| `all` | `run_tests.sh all [opt]` | 200 | 全量测试（三个套件） |

### 2.3 优化级别

**重要**：测评服务器仅支持 `-O1` 这一个优化选项，编译器将其映射为最高优化级别（OALL = O1+O2+O3）。小写选项仅用于本地逐级调试。

| 命令行参数 | 内部优化级别 | 说明 |
|-----------|------------|------|
| `-O1` | OALL | **测评服务器使用**，全部优化 (O1+O2+O3，不含P0/P3) |
| `-O0` | O0 | 无优化 |
| `-o0` | O0 | 无优化（本地调试） |
| `-o1` | O1 | 仅 O1：CF + DCE + CSE + LICM（本地调试） |
| `-o2` | O2 | O1 + 内联（本地调试） |
| `-o3` | O3 | O1+O2 + 代数化简/循环交换/展开/尾递归（本地调试） |

> **注意**：大写 `-O1` 对应全部优化，小写 `-o1` 对应仅 O1 优化。这是因测评服务器只支持 `-O1` 选项，我们必须在此选项下输出最佳性能。

### 2.4 测试流程

每个测试用例经过以下流程：

```
.sy 源文件 → [compiler 编译] → .S 汇编 → [GCC 链接] → ELF → [QEMU 运行] → 结果比对
```

**错误分类统计：**
- **COMPILE FAIL**：compiler 编译失败
- **LINK FAIL**：GCC 汇编/链接失败
- **OUTPUT DIFF**：运行输出与预期不符
- **SEGFAULT**：运行时段错误
- **TIMEOUT**：运行超时

### 2.5 快速冒烟测试用例

| 用例 | 预期返回值 | 说明 |
|------|-----------|------|
| `00_main` | 3 | 最小程序 |
| `01_var_defn2` | 10 | 变量定义 |
| `11_add2` | 9 | 加法运算 |
| `26_while_test1` | 3 | while 循环 |
| `29_break` | 201 | break 跳出 |

---

## 三、IR 单元测试（18 项）

```bash
cd /mnt/d/VSCodeProjects/compiler/build
./test_ir
```

期望输出：
```
=== IR 框架单元测试 ===
[Type System]       9/9  PASSED
[Def-Use Chain]     2/2  PASSED
[Constants]         1/1  PASSED
[Instructions]      3/3  PASSED
[Module/BB/Func]    1/1  PASSED
[IRBuilder E2E]     2/2  PASSED
=== 结果: 18 passed, 0 failed ===
```

---

## 四、集成测试（23 项）

```bash
cd /mnt/d/VSCodeProjects/compiler/build
./test_integration
```

期望：23 passed, 0 failed

---

## 五、其他测试脚本

### 5.1 QEMU 端到端快速测试

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/test_qemu.sh
```

涵盖 O0/O3 两个优化级别，13个关键用例。

### 5.2 QEMU 全量回归测试

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/test_qemu_all.sh [O1|O0|o0|o1|o2|o3]
```

遍历 functional 全部用例，比对 stdout 和退出码。

### 5.3 全量功能测试（仅编译+运行）

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/test_all_functional.sh
```

统计编译/链接/运行结果，不比对输出内容。

### 5.4 指令调度测试

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/test_qemu_sched.sh
```

### 5.5 循环展开测试

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/test_qemu_unroll.sh
```

### 5.6 运行时库测试

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/test_runtime.sh
```

### 5.7 语法测试

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/grammar/test-grammar.sh
bash scripts/grammar/QUICK_GRAMMAR_TEST.sh
```

---

## 六、一键全量测试

```bash
cd /mnt/d/VSCodeProjects/compiler/build
cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# IR 层测试
./test_ir && ./test_integration && echo "IR 层: 41/41 通过"

# 后端全量测试
cd /mnt/d/VSCodeProjects/compiler
bash scripts/run_tests.sh all O0
```

---

## 七、清理方案

### 7.1 清理脚本

```bash
cd /mnt/d/VSCodeProjects/compiler
bash scripts/clean.sh
```

清理内容：
- `/tmp/sysy_test_*` 临时测试目录
- `~/tmp/` 下的测试中间文件（.S、_bin、_out.txt）
- `build/` 目录（CMake 构建产物）
- 根目录下的 `CMakeCache.txt`、`CMakeFiles/` 等 CMake 残留

### 7.2 手动清理命令

```bash
# 清理临时测试文件
rm -rf /tmp/sysy_test_* /tmp/func_test_* /tmp/qemu_test_* /tmp/quick_*
rm -rf ~/tmp/    # 调试脚本产生的临时文件
rm -rf ~/tmp2/   # 调试脚本产生的临时文件
rm -rf ~/tmp3/   # 调试脚本产生的临时文件

# 清理构建产物
cd /mnt/d/VSCodeProjects/compiler
rm -rf build/

# 清理 CMake 残留
rm -f CMakeCache.txt
rm -rf CMakeFiles/
rm -f cmake_install.cmake
rm -f Makefile

# 清理 ANTLR 生成文件
rm -rf src/antlr/
```

### 7.3 .gitignore 已排除项

- `build/` — 构建产物
- `src/antlr/` — ANTLR 生成文件
- `test/functional/`、`test/h_functional/`、`test/performance/` — 官方测试用例（不提交）
- `*.o`、`*.obj`、`*.s`、`*.elf` — 编译中间文件
- `tmp/`、`temp/` — 临时目录
- `.vscode/`、`.idea/` — IDE 配置

### 7.4 可安全删除的目录/文件

| 路径 | 说明 | 删除影响 |
|------|------|---------|
| `build/` | CMake 构建产物 | 需重新 cmake + make |
| `src/antlr/` | ANTLR 自动生成 | 需重新生成 |
| `scripts/debug/_test_*.sy` | 调试用临时测试文件 | 无影响 |
| `scripts/debug/_input_fa40.txt` | 调试用输入文件 | 无影响 |
| `/tmp/sysy_test_*` | 测试运行临时文件 | 无影响 |
| `~/tmp/`、`~/tmp2/`、`~/tmp3/` | 调试脚本临时目录 | 无影响 |

---

## 八、G4 语法文件验证

> 仅在修改 G4 文件后需要执行

```bash
cd /mnt/d/VSCodeProjects/compiler
mkdir -p /tmp/sysy-test

# 生成 Java 版本
java -jar lib/antlr-4.10.1.jar -Dlanguage=Java -o /tmp/sysy-test \
  grammar/SysY2022Lexer.g4 grammar/SysY2022Parser.g4

# 编译
javac -cp ".:lib/antlr-4.10.1.jar" /tmp/sysy-test/*.java

# 测试
export CLASSPATH="/tmp/sysy-test:lib/antlr-4.10.1.jar"
java org.antlr.v4.gui.TestRig SysY2022 compilationUnit test/hello.sy
```

---

## 九、ANTLR C++ 文件重新生成

```bash
cd /mnt/d/VSCodeProjects/compiler
java -jar lib/antlr-4.10.1.jar -Dlanguage=Cpp -no-listener -visitor \
  -o src/antlr grammar/SysY2022Lexer.g4 grammar/SysY2022Parser.g4

cd build && cmake .. && make -j$(nproc)
```

---

## 十、测试结果速查

| 模块 | 测试项 | 通过标志 |
|-----|--------|---------|
| 统一框架 | quick 冒烟测试 | 5/5 PASS |
| 统一框架 | func O0 功能测试 | Compile 100 OK, Runtime 100 OK |
| 统一框架 | hfunc O0 高阶测试 | Compile 40 OK |
| 统一框架 | perf O0 性能测试 | Compile 60 OK |
| IR Type System | 唯一性、toString | 9/9 PASSED |
| IR Def-Use | 引用链维护 | 2/2 PASSED |
| IR Constants | 缓存验证 | 1/1 PASSED |
| IR Instructions | 创建方法 | 3/3 PASSED |
| IR Module | dump() 输出 | 1/1 PASSED |
| IRBuilder E2E | main→ret | 2/2 PASSED |
| 集成测试 | .sy → IR | 23/23 PASSED |
| Final_Test | functional 批量 | 100/100 |
| Final_Test | h_functional 批量 | 40/40 |
| Final_Test | performance 批量 | 60/60 |