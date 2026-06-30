// ================================================================
// O1/O2/O3/P0/P3 优化器统一入口
// 借鉴 Cpl3 的分阶段调度策略：
//   构造阶段 → 指令级化简 → 算术优化 → 值传播 → 循环优化 → 清理
//
// 关键设计决策（基于 Cpl3 分析 + 实测验证）：
//   1. 算术优化在值传播之前：MagicDivision/AlgebraicSimplification 产生新常量
//      后，SCCP 才能传播这些常量到分支条件和使用点
//   2. LICM 在内联之后：内联后循环体中出现新不变量，此时外提收益最大
//   3. 尾递归在内联之前：转循环后可被内联，借鉴 Cpl3 的构造阶段策略
//   4. 阶段 2/3 迭代收敛：各 pass 互相创造机会，最多 2 次迭代
//   5. 从最内层到最外层处理循环：避免内层循环变换影响外层循环结构
// ================================================================

#include "opt/Optimizer.h"

namespace Opt {

// ================================================================
// O1：基础安全优化（无依赖，总是有益）
// 注意：LICM 已移至 O2 阶段 5，在内联之后运行以捕获更多不变量
// ================================================================
void runO1(IR::Module* mod) {
    constantFolding(mod);
    deadCodeElimination(mod);
    commonSubexpressionElimination(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
}

// ================================================================
// O2：中层优化管线（分阶段调度，迭代收敛）
// ================================================================
void runO2(IR::Module* mod) {
    // ================================================================
    // 阶段 1：结构化变换
    //   先做函数级别的结构变换（删除、简化、尾递归→循环、内联），
    //   为后续所有优化创造更大的优化空间。
    //   借鉴 Cpl3：尾递归在内联之前，转换后的循环函数可被内联。
    // ================================================================

    // 1a. 树摇：移除未使用的函数和全局变量
    treeShaking(mod);

    // 1b. 位运算模式识别：消除自定义位运算函数调用（_and/_or/rotlN 等），
    //     让 read_bits 等函数变为叶子函数，为后续内联做准备
    if (bitOpPatternRecognition(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 1c. 尾递归消除 → 循环转换（在函数内联之前！）
    //     借鉴 Cpl3：将尾递归转为循环后，函数更简单，更容易被内联
    if (tailRecursionElimination(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 1d. 函数内联：展开小函数，暴露跨函数优化机会
    //     内联后循环体变大，LICM 在阶段 5 运行以捕获新的不变量
    inlineExpansion(mod);
    constantFolding(mod);
    deadCodeElimination(mod);

    // 1e. 全局变量提升：内联后暴露更多全局变量访问模式
    if (globalVariablePromotion(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // ================================================================
    // 阶段 2：指令级化简 + CFG 简化（迭代 2 次收敛）
    //   InstCombine（含 Store-to-Load 前推）暴露死存储，
    //   DSE 消除死存储，SimplifyCFG 清理 CFG，
    //   循环迭代可使各 pass 互相创造的机会被充分利用。
    // ================================================================
    for (int iter = 0; iter < 2; ++iter) {
        bool phase2Changed = false;

        // 2a. InstCombine：代数恒等式化简 + Store-to-Load 前推
        if (instCombine(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase2Changed = true;
        }

        // 2b. DSE：InstCombine 的 Store-to-Load 前推可能暴露死存储
        if (deadStoreElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase2Changed = true;
        }

        // 2c. CFG 简化：常量分支折叠 + 不可达块删除 + 空块消除
        if (simplifyCFG(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase2Changed = true;
        }

        if (!phase2Changed) break;
    }

    // ================================================================
    // 阶段 3：算术优化（在值传播之前！）
    //   关键设计决策：算术优化产生新常量，SCCP 需要在这些常量产生后
    //   才能将其传播到分支条件和使用点。如果顺序颠倒（先 SCCP 后算术），
    //   SCCP 会错过这些常量。
    //
    //   顺序：MagicDivision → AlgebraicSimplification → Reassociate
    //   每个 pass 都可能产生新常量，为后续 pass 创造机会
    // ================================================================

    // 3a. MagicDivision：常量除法 → 乘法+移位序列（产生新常量）
    if (magicDivision(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 3b. 代数化简：强度削减（sdiv→ashr 等）+ 恒等式消除（产生新常量）
    if (algebraicSimplification(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 3c. Reassociate：表达式重结合，优化常量折叠机会（产生新常量）
    if (reassociate(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 3d. LoadElimination：消除冗余 LOAD（简化后续分析）
    if (loadElimination(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // ================================================================
    // 阶段 4：值级别分析 + 传播（在算术优化之后！迭代 2 次收敛）
    //   此时算术优化已产生大量新常量，SCCP 可以传播这些常量到：
    //   - 分支条件（使 SimplifyCFG 能折叠更多分支）
    //   - 算术运算（使 InstCombine 能进一步化简）
    //   - 复制指令（使 CopyPropagation 能消除更多冗余）
    // ================================================================
    for (int iter = 0; iter < 2; ++iter) {
        bool phase4Changed = false;

        // 4a. SCCP：稀疏条件常量传播（结合分支条件精确推导常量）
        if (sparseConditionalConstantPropagation(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase4Changed = true;
        }

        // 4b. CFG 简化：SCCP 解析常量分支后折叠新常量分支
        if (simplifyCFG(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase4Changed = true;
        }

        // 4c. CopyPropagation：传播 SCCP 解析的常量或复写值
        //     注意：InstCombine 已在阶段 2 做了类似工作，但 SCCP 可能
        //     暴露新的复制模式（如 x = add y, 0），需要再次传播
        if (copyPropagation(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase4Changed = true;
        }

        if (!phase4Changed) break;
    }

    // ================================================================
    // 阶段 4→2 反馈：值传播后再次运行指令级化简（1 次）
    //   SCCP 传播常量后，InstCombine 可能发现新的化简机会
    //   （如 x*1→x, x&0→0 等，其中 x 现在是常量）
    // ================================================================
    {
        bool feedbackChanged = false;
        if (instCombine(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            feedbackChanged = true;
        }
        if (feedbackChanged && deadStoreElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
        if (feedbackChanged && simplifyCFG(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
    }

    // ================================================================
    // 阶段 5：循环优化（在结构稳定后、所有清理完成后运行）
    //   LICM：内联后循环体中有新的不变量可外提
    //         使用 NaturalLoop 森林 + innermost-first 处理 + 安全检查
    //   CSE：LICM 外提代码 + 所有前置变换可能创建公共子表达式
    // ================================================================

    // 5a. LICM（第二次）：内联后循环体中有新的不变量可外提
    //     修复：使用循环森林 + 从最内层到最外层 + 多项安全检查
    if (loopInvariantCodeMotion(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 5b. CSE（第二次）：LICM 外提代码 + 所有前置变换可能创建公共子表达式
    if (commonSubexpressionElimination(mod)) { /* CSE changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);

    // ================================================================
    // 阶段 6：全局清理与最终优化
    // ================================================================

    // 6a. IfConversion：条件转换
    if (ifConversion(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 6b. ADCE：激进死代码消除
    if (adce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 6c. CodeSink：代码下沉，减少寄存器压力
    if (codeSink(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 6d. BasicBlockReordering：基于支配树的拓扑排序，优化 fall-through
    if (basicBlockReordering(mod)) {
        // 仅布局变化，无需 CF/DCE
    }
}

// ================================================================
// O3：循环特定变换（在 O2 通用优化之后运行）
//   循环交换 → 强度削弱 → 完全展开 → 部分展开
// ================================================================
void runO3(IR::Module* mod) {
    // LoopInterchange：循环交换（在 LSR/GEP 之前，因为交换改变循环结构）
    if (loopInterchange(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);

    // LoopStrengthReduce：循环强度削弱（MUL→累加）
    if (loopStrengthReduce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // GEPStrengthReduce：GEP 地址计算强度削弱
    if (gepStrengthReduce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // LoopFullUnroll：基于 SCEV 确定迭代次数的完全展开
    if (loopFullUnroll(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // LoopUnrolling：部分展开（最大 8×）
    if (loopUnrolling(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

// ================================================================
// P0：特殊模式识别
// ================================================================
void runP0(IR::Module* mod) {
    if (recursiveMulToNative(mod)) { /* changed */ }
    if (bitOpPatternRecognition(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

// ================================================================
// P3：指令调度
// ================================================================
void runP3(IR::Module* mod) {
    if (instructionScheduling(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

} // namespace Opt