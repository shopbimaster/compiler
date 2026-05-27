# 🚀 SysY2022 编译器 - 开发规划

## 目标
将 SysY2022 语言编译为 RV64GC 汇编，支持 medany 内存模型，最终在 BOOM FPGA 软核上通过全部 190+ 测试用例。

## 测试用例总览

| 类别 | 路径 | 数量 | 难度 |
|------|------|------|------|
| 功能测试 | `test/Final_Test/functional/` | 100 | O0 |
| 高级功能测试 | `test/Final_Test/h_functional/` | 40 | O0 |
| 性能测试 | `test/Final_Test/performance/` | ~50 | O2~O3 |
| **合计** | | **~190** | |

每条用例包含 `.sy`（源码）、`.in`（标准输入，可选）、`.out`（期望输出）。

---

## 📋 开发路线图

---

## ✅ 第 1 阶段: 中间表示 (IR) 设计（已完成）

**完成日期**: 2026-05-20 | **测试**: 18/18

---

## ✅ 第 2 阶段: IRBuilder Visitor 集成 ANTLR4（已完成）

**完成日期**: 2026-05-21 | **测试**: 25/25 (18 单元 + 7 集成)

**已支持**: 变量声明/赋值、四则运算、比较、短路求值、if-else、while、函数调用

---

## ✅ 第 3 阶段: 前端语言特性补全 — O0 功能测试

**优先级: 🔴 最高** | **目标: 100/100 functional + 40/40 h_functional + 60/60 performance 编译通过** ✅ 完成

### 3.1 控制流增强
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.1a break/continue | 循环中断与继续 | 29_break, 30_continue, 25_while_if, 31-33_while_if | ✅
| 3.1b 嵌套 if-else | 多层嵌套条件分支 | 22-24_if_test3-5, 49_if_complex_expr | ✅
| 3.1c 嵌套 while | 多层嵌套循环 | 27_while_test2, 28_while_test3, 94_nested_loops | ✅
| 3.1d 空语句 | 单独分号 | 42_empty_stmt | ✅

### 3.2 数组
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.2a 一维数组声明 | `int a[10];` 栈分配 | 34_arr_expr_len | ✅
| 3.2b 数组存取 | `a[i] = x; y = a[j];` | 55-61_sort, 62_percolation | ✅
| 3.2c 数组传参 | `void f(int a[])` | 57-60_sort, 67_reverse_output | ✅
| 3.2d 多维数组 | `int a[2][3];` | 83-84_long_array, 96-99_matrix | ✅
| 3.2e 表达式下标 | `a[n+1]` | 34_arr_expr_len | ✅
| 3.2f 数组初始化 | `int a[3] = {1,2,3};` | 07_arr_init_nd (h_func) | ✅

### 3.3 全局变量
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.3a 全局 int 声明 | 带初始值 / 无初始值 | 89_many_globals | ✅
| 3.3b 全局数组 | `int a[100];` 全局数组 | 08_global_arr_init (h_func), 01_mm (perf) | ✅
| 3.3c const 常量 | `const int N = 10;` | 大量用例 | ✅

### 3.4 类型与字面量
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.4a float 语法/常量折叠 | float 常量/全局 float/混合类型折叠 | 95_float, h-10-01 (perf) | ✅
| 3.4b void 函数 | 无返回值函数 | 大量用例 (sort, dijkstra, etc.) | ✅
| 3.4c 十六进制 | `0xFF`, `0xABCD` | 46_hex_defn, 47_hex_oct_add | ✅
| 3.4d 八进制 | `0177` | 47_hex_oct_add | ✅
| 3.4e 一元运算符 | `-`, `!`, `+` | 40_unary_op, 41_unary_op2 | ✅

### 3.5 作用域
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.5a 嵌套块作用域 | `{ int x; }` | 52_scope, 53_scope2 | ✅
| 3.5b 变量遮蔽 | 内层覆盖外层同名变量 | 54_hidden_var, 25-27_scope3-5 (h_func) | ✅

### 3.6 I/O 运行时库
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.6a getint() | 读取整数 | 73_int_io, 38_op_priority4, 43_logi_assign | ✅ 已声明
| 3.6b getch() | 读取字符 | 73_int_io, 68_brainfk | ✅ 已声明
| 3.6c getarray() | 读取数组 | 61_sort_test7, 62_percolation | ✅ 已声明
| 3.6d putint() | 输出整数 | 73_int_io, 66_exgcd | ✅ 已声明
| 3.6e putch() | 输出字符 | 68_brainfk | ✅ 已声明
| 3.6f putarray() | 输出数组 | 67_reverse_output | ✅ 已声明
| 3.6g starttime/stoptime | 计时函数 | 性能测试用 | ✅ 已声明

### 3.7 复杂特性
| 任务 | 内容 | 涉及的测试样例 |
|------|------|---------------|
| 3.7a 递归函数 | 自调用 | 66_exgcd, 72_hanoi, fft (perf) | ✅
| 3.7b 多文件 return | 函数中多个返回点 | 01_multiple_returns (h_func) | ✅
| 3.7c 块内 return | if/while 块内返回 | 02_ret_in_block (h_func) | ✅
| 3.7d 语句表达式 | `x = (a=1, b=2, a+b);` | 44_stmt_expr | ✅
| 3.7e 注释 | `//` 和 `/* */` | 45_comment1, 00_comment2 (h_func) | ✅

---

## 🟡 第 4 阶段: 后端代码生成 — O0 基本正确性

**优先级: 🔴 高（当前阶段）** | **目标: sysyc 输出可执行的 RISC-V 汇编** | **已完成: 8/8 用例端到端通过（含浮点+全局变量）**

### 4.1 指令选择 (Instruction Selection)
| 任务 | IR → RISC-V | 难度 | 状态 |
|------|------------|------|------|
| 4.1a 算术指令 | ADD/SUB/MUL/SDIV/SREM → add/sub/mul/div/rem | ⭐ | ✅ |
| 4.1b 比较指令 | ICMP → slt/sltu/xor+seqz 组合 | ⭐⭐ | ✅ |
| 4.1c 分支指令 | BR/COND_BR → j/beq/bne/blt/bge | ⭐ | ✅ |
| 4.1d 内存指令 | ALLOCA/LOAD/STORE → addi sp/lw/sw | ⭐⭐⭐ | ✅ |
| 4.1e 调用指令 | CALL → jal/jalr (含 call 寄存器) | ⭐⭐⭐ | ✅ |
| 4.1f 返回指令 | RET → ret (jalr zero, ra, 0) | ⭐ | ✅ |
| 4.1g 浮点指令 | FADD/FSUB/FMUL/FDIV → fadd.s/fsub.s/fmul.s/fdiv.s | ⭐⭐ | ✅ |
| 4.1h 全局变量访问 | GLOBAL_ADDR → lui+addi (medany) | ⭐⭐⭐ | ✅ |
| 4.1i GETELEMENTPTR | 地址计算 → slli+add | ⭐⭐ | ✅ |
| 4.1j ICMP/FCMP | 整数/浮点比较 → 条件组合 | ⭐⭐ | ✅ |

### 4.2 栈帧管理
| 任务 | 内容 | 状态 |
|------|------|------|
| 4.2a 函数序言 | 保存 ra、分配栈空间 | ✅ |
| 4.2b 函数尾声 | 恢复 ra、释放栈空间 | ✅ |
| 4.2c 栈变量分配 | alloca 指令 → 栈偏移计算 | ✅ |
| 4.2d 数组栈分配 | 大数组的栈空间计算 | ✅ |

### 4.3 寄存器分配
| 任务 | 内容 | 状态 |
|------|------|------|
| 4.3a 虚拟寄存器 → 栈槽 | naive 方案：每个值分配一个栈槽 | ✅ |
| 4.3b 线性扫描分配 | 进阶：线性扫描寄存器分配器 | ✅ |

### 4.4 调用约定 (RV64 LP64)
| 寄存器 | 用途 |
|--------|------|
| ra (x1) | 返回地址 |
| sp (x2) | 栈指针 |
| fp (x8) | 帧指针 |
| a0-a7 (x10-x17) | 函数参数/返回值 |
| t0-t6 (x5-x7, x28-x31) | 临时寄存器 (caller-saved) |
| s0-s11 (x8-x9, x18-x27) | 保存寄存器 (callee-saved) |
| zero (x0) | 零寄存器 |

### 4.5 已验证的端到端用例（.sy → .S → ELF → QEMU）
| 用例 | 源码 | qemu 返回值 | 期望 | 结果 |
|------|------|-----------|------|------|
| hello | `return 0` | 0 | 0 | ✅ |
| arithmetic | `return 1+2*3` | 7 | 7 | ✅ |
| variable | `int a=42; return a` | 42 | 42 | ✅ |
| ifelse | `if(a<5) return 1; else return 0` | 0 | 0 | ✅ |
| while_test | `while(a<5) a=a+1; return a` | 5 | 5 | ✅ |
| func_call | `return add(3,4)` | 7 | 7 | ✅ |
| global_var | `g=42; return g` | 42 | 42 | ✅ |
| float_cmp | `float a=3.0; a<4.0 → return 1` | 1 | 1 | ✅ |
| array_1d | `int a[3]; a[0]=1;a[1]=2;a[2]=3; return sum` | 6 | 6 | ✅ |
| array_2d | `int a[2][3]; a[0][0]=1;a[1][2]=6; return sum` | 7 | 7 | ✅ |
| array_init | `int a[3]={1,2,3}; return a[0]+a[1]+a[2]` | 6 | 6 | ✅ |

> **验证环境**: WSL Ubuntu + riscv64-linux-gnu-gcc (14.2.0) + qemu-riscv64 (10.2.1)
> **验证命令**: `build/sysyc -S test/xxx.sy -o xxx.S && riscv64-linux-gnu-gcc -static -o xxx xxx.S && qemu-riscv64 xxx`

---

## 🟡 第 5 阶段: I/O 运行时库

**优先级: 🟡 中** | **目标: 链接运行时库后测试用例可运行在 RISC-V 模拟器**

### 5.1 运行时函数实现 (C/汇编)
| 函数 | 实现方式 |
|------|---------|
| getint() | 从 stdin 读取整数 |
| getch() | 从 stdin 读取字符 |
| getarray(a[]) | 读取 n 个整数到数组 |
| putint(n) | 输出整数到 stdout |
| putch(c) | 输出字符到 stdout |
| putarray(n, a[]) | 输出数组 |
| starttime() | 读取 cycle CSR |
| stoptime() | 读取 cycle CSR 求差 |

### 5.2 测试流程
```
1. 编译器: .sy → .S (RISC-V 汇编)
2. 汇编器: .S → .o (riscv64-unknown-elf-gcc -c)
3. 链接: .o + libsysy.a → elf (riscv64-unknown-elf-gcc -static)
4. 运行: spike pk elf < input.txt → 输出
5. 对比: diff output.txt expected.out
```

---

## 🟡 第 6 阶段: 优化 Pass — O1~O3 性能测试

**优先级: 🟡 中** | **目标: 性能测试通过，分数达标**

### 6.1 优化等级定义

| 等级 | 包含的优化 | 预期加速 |
|------|----------|---------|
| O0 | 无优化（仅正确翻译） | 1× |
| O1 | 常量折叠、死代码消除、窥孔优化 | ✅ 已实现 |
| O2 | 函数内联、CSE、循环不变量外提、线性扫描寄存器分配 | ✅ 已实现 |
| O3 | +循环交换、循环分块、循环展开、指令调度、位运算模式识别 | 5~100× |

### 6.2 O2 实现详情

| Pass | 文件 | 策略 | 状态 |
|------|------|------|------|
| 函数内联 | `src/opt/InlineExpansion.cpp` | ≤2 基本块、非递归叶子函数、指令数 < 20 | ✅ |
| 局部 CSE | `src/opt/CSE.cpp` | 基本块内哈希表去重（opcode + operands） | ✅ |
| LICM | `src/opt/LICM.cpp` | 支配树 + 自然循环检测 + 前置块外提（保守跳过头块含 PHI 的循环） | ✅ |

**O2 流水线**: `inlineExpansion → constantFolding → deadCodeElimination → CSE → LICM → constantFolding → deadCodeElimination`

### 6.3 关键优化详解

| 优化 | 受益测试 | 重要度 |
|------|---------|--------|
| **位运算模式识别** | crc, crypto, huffman (循环逐位运算→原生指令) | **P0** |
| **除/取模→移位/位与** | 03_sort (基数排序), h-1-01 (Collatz) | **P0** |
| **递归乘法→原生乘法** | fft (multiply 递归实现整数乘法) | **P0** |
| 函数内联 | 全部 (热路径小函数) | P1 |
| 循环分块(Tiling) | mm, conv2d, sl, transpose, lu (大矩阵) | P1 |
| 循环交换 | 01_mm (k-loop 在最外层) | P1 |
| 尾递归消除 | h-1-01, fft, radixSort | P1 |
| CSE | 全部 | P2 |
| 循环不变量外提 | conv2d, crc, crypto | P2 |
| 指令调度 | optimization_scheduling | P3 |

---

## 🟡 第 7 阶段: FPGA 上板验证

**优先级: 🟢 低（需硬件就绪）**

| 任务 | 状态 |
|------|------|
| 7.1 BOOM FPGA 环境搭建 | ⏳ 待硬件 |
| 7.2 加载 bitstream | ⏳ |
| 7.3 自动化测试脚本 | ⏳ |
| 7.4 性能基准测试 | ⏳ |

---

## 🎯 近期可立即实现的任务

由于 FPGA 尚未就绪，以下组件可以独立开发并**在 QEMU/Spike RISC-V 模拟器上验证**：

### 第一优先级（本周）
1. ~~**前端补全** — break/continue、数组、全局变量、作用域 → 更多 functional 测试通过~~ ✅ 已完成
2. **后端 O0 代码生成** — IR → RISC-V 汇编（指令选择 + 栈帧 + naive 寄存器分配）
3. **I/O 运行时库** — getint/putint 等基础函数实现

### 第二优先级（下周）
4. ~~**完整 functional 100/100 通过**~~ ✅ 已完成
5. ~~**h_functional 测试适配**~~ ✅ 已完成
6. **O1 优化 Pass**

### 第三优先级（后续）
7. **性能测试逐个攻关**
8. **O2/O3 优化 Pass**

---

## 📊 语言特性 vs 测试覆盖矩阵

| 特性 | functional | h_functional | performance | 当前状态 |
|------|:---:|:---:|:---:|:---:|
| 基本运算/表达式 | ✅ | ✅ | ✅ | ✅ 已支持 |
| if-else | ✅ | ✅ | ✅ | ✅ 已支持 |
| while | ✅ | ✅ | ✅ | ✅ 已支持 |
| 函数+参数 | ✅ | ✅ | ✅ | ✅ 已支持 |
| 短路求值 | ✅ | ✅ | - | ✅ 已支持 |
| break/continue | ✅ | ✅ | - | ✅ 已支持 |
| 一维数组 | ✅ | ✅ | ✅ | ✅ 已支持 |
| 多维数组 | ✅ | ✅ | ✅ | ✅ 已支持 |
| 全局变量 | ✅ | ✅ | ✅ | ✅ 已支持 |
| 全局数组 | ✅ | ✅ | ✅ | ✅ 已支持 |
| const 常量 | ✅ | ✅ | ✅ | ✅ 已支持（编译期求值+混合类型） |
| const 数组初始化 | - | ✅ | - | ✅ 已支持（递归 GEP+store） |
| float 常量/浮点语法 | ✅ | ✅ | ✅ | ✅ 已支持（常量折叠，运行时运算待完善） |
| void 函数 | ✅ | ✅ | ✅ | ✅ 已支持 |
| 递归函数 | ✅ | ✅ | ✅ | ✅ 已支持 |
| 作用域/变量遮蔽 | ✅ | ✅ | - | ✅ 已支持 |
| 十六进制/八进制 | ✅ | - | - | ✅ 已支持 |
| I/O 函数声明 | ✅ | ✅ | - | ✅ 已支持（12 个内置声明） |
| 数组初始化 | - | ✅ | - | ✅ 已支持 |
| 全局数组初始化 | - | ✅ | - | ✅ 已支持 |
| 语句表达式 | ✅ | - | - | ✅ 已支持 |
| 一元运算 (-, !, +) | ✅ | ✅ | ✅ | ✅ 已支持 |
| 全局 const 引用 | ✅ | ✅ | - | ✅ 已支持（constEvalPrimary lVal） |
| 部分数组初始化 | ✅ | ✅ | - | ✅ 已支持（缺失元素补零） |
| 表达式数组下标 | ✅ | ✅ | - | ✅ 已支持 |

---

## 🛠️ 技术学习需求

| 主题 | 优先级 | 用途 |
|-----|--------|------|
| RISC-V RV64GC 指令集 | 🔴 高 | 后端代码生成 |
| RISC-V 调用约定 (LP64) | 🔴 高 | 函数序言/尾声/参数传递 |
| 寄存器分配 (线性扫描) | 🟡 中 | 虚拟寄存器 → 物理寄存器 |
| RISC-V 浮点指令 (F/D扩展) | 🟡 中 | float 类型支持 |
| Spike/QEMU RISC-V 模拟器 | 🟡 中 | 本地测试验证 |
| 循环优化 (tiling/interchange/unroll) | 🟢 低 | 性能测试阶段 |
| 位运算模式匹配 | 🟢 低 | crc/crypto 性能优化 |

---

## 📝 备注

- **当前日期**: 2026-05-27
- **当前阶段**: 第 6 阶段（O2 优化）— O1 ✅ + O2 ✅（内联+CSE+LICM），52/52 全部测试通过
- **架构决策**: Visitor 直接生成 IR，无中间 AST；无 SemanticAnalyzer
- **测试策略**: 单元测试 + 集成测试 + 端到端对比 diff
- **本地验证**: QEMU/Spike RISC-V 模拟器可代替 FPGA 进行功能验证