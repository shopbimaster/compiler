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
#include <cstdlib>   // std::getenv
#include <string>

namespace Opt {

// ================================================================
// A4：参数化 Pass 开关（见 PassManager.cpp）
//   OPT_DISABLE="mem2reg,globalValueNumbering"  黑名单
//   OPT_ENABLE="globalValueNumbering"           白名单（只跑列出的）
// 宏将 pass 函数名字符串化，与函数调用一起短路求值：
//   pass 被禁用时直接返回 false，跳过其后的 CF/DCE 清理。
// ================================================================
#define PASS_CALL(fn) (Opt::passEnabled(#fn) && (fn(mod)))

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
    //   Mem2Reg 放在内联和全局变量提升之后，避免 InlineExpansion
    //   克隆 phi.ptr ALLOCA 导致的 SEGFAULT（64_calculator 修复）。
    // ================================================================

    // 1a. 树摇
    if (PASS_CALL(treeShaking)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(radixSortLowering)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(repeatedDivRemToNative)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(recursiveModularMulToNative)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(bitOpPatternRecognition)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(powerOfTwoDispatchSimplification)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(tailRecursionElimination)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // P4-pre: EarlyReturn→Select — 在第一次 inlineExpansion 之前运行
    // 目的：将 max/min 等"if-else-RET"小函数先转为 SELECT+RET 单 BB，
    //       缩小函数体积使其满足内联阈值；内联后调用方循环体直接得到 SELECT
    //       而非 COND_BR，使后续 LoopUnrolling 可展开（如 h-4-03 的 max）。
    if (PASS_CALL(earlyReturnToSelect)) {
        simplifyCFG(mod);
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    if (PASS_CALL(inlineExpansion)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // Mem2Reg+PhiLowering：将 alloca/load/store 提升为 SSA（PHI）形式，
    // 让后续优化（SCCP/GVN/CSE 等）能在 SSA 上运行，然后降低 PHI 为
    // alloca/load/store（代码生成前）。
    // 安全性：PhiLowering 产生的 ALLOCA 命名为 "%X.phi.ptr"，
    // isAllocaPromotable 会跳过这些 ALLOCA，避免消耗 callee-saved 寄存器
    // 导致 12_DSU SEGFAULT。
    // ★ 暂时禁用用于诊断 SEGFAULT
    if (PASS_CALL(mem2reg)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 1b. 二次内联：mem2reg 将 alloca/load/store 提升为 SSA 后，原本因
    //     参数 ALLOCA/STORE/LOAD 显得过大而无法内联的函数（如含循环的
    //     叶子函数 getNumPos）现在指令数大幅减少，可被内联。
    //     ★ 依赖 InlineExpansion 的 PHI 克隆支持（mem2reg 引入 PHI 节点）
    //     内联后再次 CF+DCE 清理，并重跑 mem2reg 消除内联引入的临时 ALLOCA
    if (PASS_CALL(inlineExpansion)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 1c. 二次 mem2reg：内联引入的 retAlloca（用于返回值）和其他临时 ALLOCA
    //     应被提升为 SSA，使后续优化（SCCP/CSE/InstCombine 等）更有效
    if (PASS_CALL(mem2reg)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // P4: EarlyReturn→Select — if-else-RET → SELECT+RET
    // 内联后 max/min 等小函数变为 if-else-RET 模式，转换为 SELECT 消除 COND_BR，
    // 使含此类调用的循环体变为单 BB，后续 LoopUnrolling 可展开（如 h-4-03）。
    if (PASS_CALL(earlyReturnToSelect)) {
        simplifyCFG(mod);
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 1d. 二次树摇：内联后被内联函数的 useCount 降为 0，成为死函数。
    //     ★ 已禁用：实测表明删除死函数会改变代码布局，导致 shuffle1 回归 +300ms。
    //     死函数在汇编中作为"填充"使热点函数恰好对齐 cache line，删除后热点函数
    //     跨 cache line 边界，icache miss 增加。正确做法是添加函数对齐而非保留死代码。
    //     保留代码以备未来添加函数对齐后重新启用。
    if (false) treeShaking(mod);

    if (PASS_CALL(globalVariablePromotion)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // ★ 关键优化：在 GVP 之后运行局部 mem2reg，将 GVP 创建的"单 BB STORE"ALLOCA
    //   提升为直接值引用。GVP 将标量全局变量提升为局部 ALLOCA，但 TargetCodeGen
    //   的 ALLOCA 提升可能因全局数组地址缓存耗尽 callee-saved 寄存器而无法生效。
    //   mem2regLocal 只处理所有 STORE 都在同一个 BB（通常是 entry）中的 ALLOCA，
    //   不创建 PHI 节点，安全且无 PHI 爆炸风险。
    //   效果：如 hashmod.local0（在 entry 中 STORE 两次，循环中 LOAD）→ 直接使用
    //   最后一个 STORE 的值，消除循环中的栈 LOAD
    // ★ 暂时禁用用于诊断 SEGFAULT
    if (PASS_CALL(mem2regLocal)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 全局常量传播：将未被 STORE 且有常量初始值的全局变量 LOAD 替换为常量。
    // 在 GVP 之后运行，使后续 SCCP 能传播这些常量到函数参数和循环边界，
    // 触发循环完全展开等优化。
    // ★ 暂时禁用用于诊断 SEGFAULT
    if (PASS_CALL(globalConstantPropagation)) {
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
        if (PASS_CALL(instCombine)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase2Changed = true;
        }

        // 2b. DSE：InstCombine 的 Store-to-Load 前推可能暴露死存储
        if (PASS_CALL(deadStoreElimination)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase2Changed = true;
        }

        // 2c. CFG 简化：常量分支折叠 + 不可达块删除 + 空块消除
        if (PASS_CALL(simplifyCFG)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            phase2Changed = true;
        }

        // 2d. 跳转线程化：消除冗余跳转链 br A -> br B -> br C => br C
        if (PASS_CALL(jumpThreading)) {
            // 跳转线程化可能创造新的SimplifyCFG机会
            simplifyCFG(mod);
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
    if (PASS_CALL(magicDivision)) {
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
    if (PASS_CALL(loadElimination)) {
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
    // 阶段 4→2 反馈：值传播后再次运行算术+指令级化简（1 次）
    //   SCCP 传播常量后，可能产生新的：
    //     - 常量除法（如 sdiv x, y 中 y 现在被 SCCP 确定为常量 7）
    //       → 需要重跑 MagicDivision
    //     - 强度削减机会（如 sdiv x, 2 现在 x 已知非负）
    //       → 需要重跑 AlgebraicSimplification
    //     - 算术恒等式（如 x*1→x, x&0→0，其中 x 现在是常量）
    //       → 需要重跑 InstCombine
    // ================================================================
    {
        bool feedbackChanged = false;

        // 反馈-1: InstCombine 先处理明显的算术恒等式
        if (instCombine(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            feedbackChanged = true;
        }

        // 反馈-2: MagicDivision 处理 SCCP 新暴露的常量除法
        if (PASS_CALL(magicDivision)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            feedbackChanged = true;
        }

        // 反馈-3: AlgebraicSimplification 处理新的强度削减机会
        if (algebraicSimplification(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
            feedbackChanged = true;
        }

        // 反馈-4: DSE 消除死存储
        if (feedbackChanged && deadStoreElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 反馈-5: SimplifyCFG 折叠新常量分支
        if (feedbackChanged && simplifyCFG(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 反馈-6: 如果前面的反馈产生了新常量，再跑一次 SCCP 传播
        if (feedbackChanged && sparseConditionalConstantPropagation(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
    }
    // ================================================================
    // 阶段 5：循环优化（在结构稳定后、所有清理完成后运行）
    //   LICM：内联后循环体中有新的不变量可外提
    //         使用 NaturalLoop 森林 + innermost-first 处理 + 安全检查
    //   LoadElimination：LICM 外提的 LOAD/STORE 可能暴露冗余 LOAD
    //   CSE：LICM 外提代码 + 所有前置变换可能创建公共子表达式
    // ================================================================

    // 5a. LICM（第二次）：内联后循环体中有新的不变量可外提
    //     修复：使用循环森林 + 从最内层到最外层 + 多项安全检查
    if (loopInvariantCodeMotion(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 5b. LoadElimination（第二次）：LICM 外提代码后可能暴露冗余 LOAD
    //     （外提的 LOAD 与循环内的 LOAD 可能引用同一地址）
    if (PASS_CALL(loadElimination)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 5c. CSE（第二次）：LICM 外提代码 + 所有前置变换可能创建公共子表达式
    if (commonSubexpressionElimination(mod)) { /* CSE changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);

    // ================================================================
    // 阶段 6：全局清理与最终优化
    // ================================================================

    // 6a. IfConversion：条件转换
    if (ifConversion(mod)) {
        simplifyCFG(mod);  // 清理 IfConversion 产生的同目标 COND_BR
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
    if (PASS_CALL(basicBlockReordering)) {
        // 仅布局变化，无需 CF/DCE
    }

    // 6e. 最终 CSE：SimplifyCFG/IfConversion/CodeSink/BBReordering 可能合并 BB
    //     或移动指令，产生新的公共子表达式（尤其是 LICM 外提的 GEP）。
    //     必须在所有结构变换之后运行一次 CSE 来清理冗余。
    if (commonSubexpressionElimination(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }

    // 6f. GVN：基于支配树的跨 BB CSE
    // 历史禁用原因：跨 BB 合并延长活跃区间→线性扫描寄存器压力增→净回退
    //   （+1319ms / 60 perf tests）。换用 call-aware 图着色分配器后，其全局溢出
    //   决策消化了长区间，图着色下静态实测 GVN 净降指令/循环 spill、零回退，
    //   正确性 60/60。故正式启用。OPT_DISABLE=globalValueNumbering 可关闭。
    if (PASS_CALL(globalValueNumbering)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        if (commonSubexpressionElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
    }

    matrixReductionContraction(mod);
}

// ================================================================
// O3：循环特定变换（在 O2 通用优化之后运行）
//   循环交换 → 强度削弱 → 完全展开 → 部分展开
//   重要：循环变换后必须运行清理轮，因为：
//     - LoopFullUnroll 把循环变直线代码，归纳变量全成常量
//     - LoopUnrolling 多个迭代体共享子表达式（如 a[i+1]）
//     - LSR/GEPStrengthReduce 简化地址计算，产生新的 addi 链
// ================================================================
void runO3(IR::Module* mod) {
    bool o3Changed = false;

    // LoopInterchange：循环交换（在 LSR/GEP 之前，因为交换改变循环结构）
    if (PASS_CALL(loopInterchange)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // LoopStrengthReduce：循环强度削弱（MUL→累加）
    if (PASS_CALL(loopStrengthReduce)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // E4: LoopRotation — while(cond){body} → guard + do{body}while(cond)
    // 消除回边无条件 j，让循环体成为 fall-through 流。在 FullUnroll 之前运行：
    // 旋转后的 do-while 形态更易被 LoopFullUnroll 识别为单 BB 体。
    // 开关：LOOP_ROTATE_OFF=1 或 OPT_DISABLE=loopRotation
    if (PASS_CALL(loopRotation)) {
        simplifyCFG(mod);
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // LoopFullUnroll：基于 SCEV 确定迭代次数的完全展开
    if (PASS_CALL(loopFullUnroll)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // P1: ReductionSplitting — 多累加器归约分裂（长依赖链消除）
    // 在 LoopUnrolling 之前运行：分裂后仍可被进一步展开。
    // 仅对可证明无溢出的加法归约生效（常量/布尔值），由 P2 超大展开覆盖其余。
    if (PASS_CALL(reductionSplitting)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // LoopUnrolling：部分展开（P2: 最大 16×，tc≤256）
    if (PASS_CALL(loopUnrolling)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // GEPStrengthReduce：GEP 地址计算强度削弱
    // ★ 必须在循环展开之后运行！
    //   原因：LSR 创建的 lsr.ptr PHI 和 lsr.init 值与循环展开交互复杂。
    //   如果在展开前运行，展开器需要解析 LSR PHI 的多个 incoming value，
    //   处理不当会导致 SEGFAULT（19_search 根因：main 中的初始化双重循环
    //   被部分展开后，LSR PHI 的活跃区间与展开体冲突）。
    //   在展开后运行：被完全展开的循环已成为直线代码（无需 LSR），
    //   只有存活下来的循环才会被 LSR 优化，避免 PHI/展开交互。
    // ★ 暂时禁用用于诊断
    if (gepStrengthReduce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
        o3Changed = true;
    }

    // ================================================================
    // O3 后清理轮：循环变换创造大量新机会，必须运行完整清理
    //   1. SCCP：传播展开后的归纳变量常量（如 for(i=0;i<4;i++) → i=0,1,2,3）
    //   2. SimplifyCFG：折叠展开后的死分支
    //   3. CSE：消除展开体间的公共子表达式（如相邻迭代的 a[i+1]/a[i]）
    //   4. InstCombine：折叠新的算术链（如 i*2 在 i 已知时折叠为常量）
    //   5. DSE：消除展开后成为死存储的 STORE
    //   6. LICM：循环交换/展开可能暴露新的循环不变量
    // ================================================================
    if (o3Changed) {
        // 1. SCCP + SimplifyCFG（迭代 2 次以充分传播常量）
        for (int iter = 0; iter < 2; ++iter) {
            bool iterChanged = false;
            if (sparseConditionalConstantPropagation(mod)) {
                constantFolding(mod);
                deadCodeElimination(mod);
                iterChanged = true;
            }
            if (simplifyCFG(mod)) {
                constantFolding(mod);
                deadCodeElimination(mod);
                iterChanged = true;
            }
            if (!iterChanged) break;
        }

        // 2. CSE：消除展开体间的公共子表达式
        if (commonSubexpressionElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 3. InstCombine：折叠新的算术链
        if (instCombine(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 4. DSE：消除展开后的死存储
        if (deadStoreElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 5. LoadElimination：消除展开后的冗余 LOAD
        if (PASS_CALL(loadElimination)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 6. LICM：循环变换后可能暴露新的不变量
        if (loopInvariantCodeMotion(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 7. 最终 CSE + 清理
        if (commonSubexpressionElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }

        // 8. BasicBlockReordering：循环展开后 BB 顺序混乱
        //    必须重新排列以使指令 ID 反映执行顺序，
        //    否则寄存器分配器的活跃区间分析会出错
        //    （如 04_break_continue：临时变量和 PHI 被分配同一寄存器）
        if (PASS_CALL(basicBlockReordering)) {
            deadCodeElimination(mod);
        }
    }
}

// ================================================================
// P0：特殊模式识别
//   重要：P0 后需要运行 InstCombine + SCCP 清理，因为
//   bitOpPatternRecognition 和 recursiveMulToNative 会引入
//   新的算术模式（native mul 替换递归，位运算模式替换函数调用）
// ================================================================
void runP0(IR::Module* mod) {
    bool p0Changed = false;
    if (recursiveMulToNative(mod)) {
        p0Changed = true;
    }
    if (bitOpPatternRecognition(mod)) {
        p0Changed = true;
    }
    if (hoistRecursiveCallGuards(mod)) {
        p0Changed = true;
    }
    constantFolding(mod);
    deadCodeElimination(mod);

    if (p0Changed) {
        // P0 后清理：模式识别产生的新算术链需要折叠
        if (instCombine(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
        if (sparseConditionalConstantPropagation(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
        if (simplifyCFG(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
        if (commonSubexpressionElimination(mod)) {
            constantFolding(mod);
            deadCodeElimination(mod);
        }
    }
}

// ================================================================
// P3：指令调度
// ================================================================
void runP3(IR::Module* mod) {
    if (PASS_CALL(instructionScheduling)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

} // namespace Opt
