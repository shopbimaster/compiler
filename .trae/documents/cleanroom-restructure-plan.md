# 项目净室化重构方案

## Context（背景与目标）

项目经多人/多 agent 迭代，积累了大量调试脚本、零散文档、不一致注释，阅读与维护成本高。本方案系统性整理项目：清理冗余、合并紧耦合小文件、统一注释风格、收敛为单一 README 文档、建立函数→功能→测试的可追溯映射，**且不改变任何代码逻辑、不引入新错误**。

用户已确认三项范围决策：
1. **文件合并**：按主题保守合并（仅小而紧耦合的组，每组验证）
2. **注释统一**：文件头 + inline 规范化（格式/风格统一，不改语义内容、不碰代码）
3. **工作目录**：.trae/.claude 设计内容提炼并入 README 后删除历史草稿，保留 .trae/STATUS.md 作活跃草稿

## 回归门槛（基线）

每个阶段结束后必须满足，否则回滚该阶段：
- **构建**：`wsl make -C build -j$(nproc)` 零错误零警告（与现有一致）
- **测试**：`test_perf_wsl.sh` 60/60；`run_tests.sh func O1` 96 OK/3 DIFF/1 TO；`run_tests.sh hfunc O1` 36 OK/4 DIFF
- **审计**：`git diff` 中除注释/合并/清理外不得有代码逻辑改动

## Phase 1：清理冗余（安全，先做降噪）

**1.1 删除 tracked 调试脚本**（`git rm`）
- `scripts/_*.sh`、`scripts/_*.py`（34 个，如 `_det_*.sh`、`_memo_*.py`、`_wsl_*.sh` 等一次性诊断脚本）
- 保留通用脚本：`build_sylib.sh`、`clean.sh`、`run_tests.sh`、`run_func_tests.sh`、`test_perf_wsl.sh`、`quick_test.sh`、`differential_test.sh`、`exp_gvn_ab.sh`、`install_riscv.sh`（若存在）、`scripts/grammar/`、`scripts/setup/`

**1.2 删除散落产物**：`scripts/mm1.S`、`scripts/mm1_new.S`、`scripts/build`(文件)、`tmp_asm/`

**1.3 根目录文档收敛**：保留 `README.md`（Phase 4 重写）；其余 8 个先提取有用信息再 `git rm`：
`TEST_GUIDE.md`、`DEVELOPMENT_PLAN.md`、`PROGRESS_SUMMARY.md`、`ANALYSIS_spill_waste.md`、`HANDOFF_graph_coloring.md`、`EVAL_20260729.md`、`Token命名对照表.md`、`方案_nonentry_mem2reg退化分析.md`

**1.4 工作目录收敛**：
- `.trae/documents/*.md`（4 个设计草稿）+ `.claude/plan-*.md`（3 个）提取设计要点后 `git rm`
- 保留 `.trae/STATUS.md`（活跃草纸）、`.trae/rules/project_rules.md`、`.claude/settings.json`

**验证**：构建 + 三套件测试。

## Phase 2：保守主题合并（每组单独验证）

合并原则：仅合并小而紧耦合、语义同族的 pass；合并 = 连接 .cpp + 去重 `#include` + 解决匿名命名空间符号冲突 + 更新 CMakeLists。**不改动任何函数体逻辑**。

**Group A — 循环分析（中等波及）**
- 合并：`AffineRecurrenceAnalysis` + `MemoryAccessAnalysis` + `ScalarReductionAnalysis` + `LoopPatternAnalysis`（4 .cpp + 4 .h）
- 产出：`src/opt/LoopAnalysis.cpp` + `include/opt/LoopAnalysis.h`
- 波及：更新 ~10 个消费者的 `#include "opt/XxxAnalysis.h"` → `"opt/LoopAnalysis.h"`（`ConditionalMatrixBlocking`、`InPlaceMatrixBlocking`、`MatrixReductionContraction`、`MatrixReductionTransform`、`RedundantIterationElimination`、`StencilInteriorSpecialization`、`TriangularCopyOptimization`、`DynamicIdempotentLoopElimination`）
- 冲突检查：4 文件匿名命名空间内的 helper 函数名逐一比对，重名者加前缀（如 `affineXxx`/`memAccessXxx`）

**Group B — native lowering（低波及）**
- 合并：`RecursiveMulToNative` + `RepeatedDivRemToNative` + `ModAddRecurrence`（3 .cpp，无独立头，声明在 `include/opt/Optimizer.h`）
- 产出：`src/opt/NativeLowering.cpp`
- 仅改 CMakeLists（3 行→1 行），无消费者 include 变更

**Group C — recursive（低波及）**
- 合并：`RecursiveCallGuard` + `RecursiveMemoization`（2 .cpp，无独立头）
- 产出：`src/opt/RecursiveOpt.cpp`
- 仅改 CMakeLists

**每组步骤**：① 读全部候选文件 → ② 列匿名命名空间符号表查冲突 → ③ 合并为单文件（保留各 pass 注释分隔）→ ④ 去重 include → ⑤ 改 CMakeLists（Group A 另改消费者 include + 删旧 .h）→ ⑥ 构建 + perf/func/hfunc → ⑦ git diff 审计无逻辑改动。

## Phase 3：注释标准化（分批验证）

**3.0 规范（写入 README）**
- 文件头块（统一格式）：文件路径、所属模块、pass/类名、一句话职责、关键依赖、环境开关、（若有）注意事项
- inline：`//` 单行注释为主；领域逻辑用中文（与现有一致）；`/* */` 仅用于多行块注释；注释与代码间一个空格；删除被注释掉的死代码

**3.1 文件头统一**：为 `src/`、`include/` 全部源文件补齐统一文件头块（纯添加注释，零逻辑风险）

**3.2 inline 规范化**：逐文件规范化注释格式（`/* */` 单行→`//`、统一 `//` 后空格、删除注释掉的死代码、修明显风格不一致）。**仅改注释行，不改代码行**。
- 安全闸：每批 5–10 文件后构建；每阶段后 `git diff` 审计——diff 中不得出现非注释行的改动（可用 `git diff -w` + 人工抽查代码行未变）

**3.3 依赖链梳理**：README 记录模块依赖图（frontend→IR→opt→backend），并确认 `include/` 无环引用（现状已无独立 pass 头循环，仅 LoopAnalysis.h 内部 include MemoryAccessAnalysis.h，合并后自洽）

## Phase 4：README 单一文档

重写 `README.md` 为唯一项目文档（无单独简介/测试指导文件），含：
1. **宏观思路**：架构图（输入→ANTLR4→IRBuilder→IR→opt 管线→backend→RV64GC）、设计原则（无独立 AST、Visitor 直生 IR、medany/RV64GC/BOOM 目标）
2. **函数→功能列表**：按模块（frontend/IR/opt/backend）列出每个文件/pass 的入口函数与职责；opt 按 O1/O2/O3 管线顺序分组
3. **测试映射**：每个功能标注对应单元测试（`tests/test_*.cppx`：test_ir/test_peephole/test_matrix_blocking/test_integration）与系统测试（`test/functional` 99、`test/h_functional` 40、`test/performance` 60 + 脚本命令）
4. **依赖链**：模块依赖图 + include 关系
5. **注释规范**：Phase 3 的标准
6. **构建/测试命令**：cmake/make/ctest + run_tests.sh/test_perf_wsl.sh 用法 + 优化级别表（-O1/-O0/-o1/-o2/-o3）

## Phase 5：终验

- 全量 `wsl make -C build -j$(nproc)` 重建
- `test_perf_wsl.sh` 60/60；`run_tests.sh func O1`；`run_tests.sh hfunc O1`
- 与基线逐项比对，零新增失败
- `git diff --stat` 总览：仅清理/合并/注释/README，无逻辑改动

## 关键文件

- 构建：`CMakeLists.txt`（Phase 2 改源文件列表）
- 合并产出：`src/opt/LoopAnalysis.cpp`+`include/opt/LoopAnalysis.h`、`src/opt/NativeLowering.cpp`、`src/opt/RecursiveOpt.cpp`
- 文档：`README.md`（重写）
- 清理：`scripts/_*`、根目录 8 个 .md、`.trae/documents/*`、`.claude/plan-*`

## 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 合并引发符号冲突/链接错 | 每组合并前列符号表，重名加前缀；每组后构建+测试 |
| 注释编辑误伤代码 | 仅改注释行；每批构建；git diff 审计代码行未变 |
| include 波及遗漏 | grep 全量搜索旧头名；合并后构建会暴露遗漏 |
| 清理误删有用脚本 | 保留通用脚本清单已明确；调试脚本均已废弃 |

## 执行顺序

Phase 1 → 2(A→B→C) → 3 → 4 → 5。每阶段后汇报结果，可作为天然检查点暂停/调整。
