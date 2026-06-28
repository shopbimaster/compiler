// ================================================================
// O1/O2/O3/P0/P3 优化器统一入口
// 各变换 pass 返回 bool，仅在实际修改 IR 后才运行 CF/DCE 清理
// ================================================================

#include "opt/Optimizer.h"

namespace Opt {

void runO1(IR::Module* mod) {
    constantFolding(mod);
    deadCodeElimination(mod);
    commonSubexpressionElimination(mod);
    loopInvariantCodeMotion(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runO2(IR::Module* mod) {
    // 树摇：移除未使用的函数和全局变量，减少后续优化的工作量
    if (treeShaking(mod)) {
        // 树摇后无需 CF/DCE，因为被移除的函数不影响其他函数
    }
    // 位运算模式识别提前运行，消除自定义位运算函数调用（_and/_or/rotlN 等），
    // 让 read_bits 等函数变为叶子函数，以便后续内联 pass 进行内联
    if (bitOpPatternRecognition(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    inlineExpansion(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
    // 全局变量提升：将频繁访问的标量全局变量提升为局部变量，
    // 让寄存器分配器将其放入寄存器，消除 la + lw/sw 开销
    if (globalVariablePromotion(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (deadStoreElimination(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (instCombine(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (simplifyCFG(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    // SCCP: 稀疏条件常量传播，结合分支条件精确推导常量
    // 在 simplifyCFG 之后运行，可以进一步发现被简化分支中的常量
    if (sparseConditionalConstantPropagation(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (copyPropagation(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (magicDivision(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (loadElimination(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (reassociate(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (ifConversion(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (commonSubexpressionElimination(mod)) { /* CSE changed */ }
    // LICM 已在 O1 中运行，内联的单 BB 函数不含循环，无需再次 LICM
    // 二次 LICM 会与内联后修改的 CFG 交互导致段错误
    constantFolding(mod);
    deadCodeElimination(mod);
    if (adce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    // 代码下沉：将指令移动到更靠近使用者的位置，减少寄存器压力
    if (codeSink(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    // 基本块重排：基于支配树的拓扑排序，优化 fall-through
    // 确保定义在使用之前，避免寄存器分配器的活跃区间错误
    if (basicBlockReordering(mod)) {
        // 仅布局变化，无需 CF/DCE
    }
}

void runO3(IR::Module* mod) {
    if (algebraicSimplification(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
    if (loopInterchange(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
    // 循环强度削弱：将 MUL 替换为累加（依赖 SCEV 分析）
    if (loopStrengthReduce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    // GEP 强度削弱：将数组地址计算的乘法替换为累加地址
    if (gepStrengthReduce(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    // 循环完全展开：基于 SCEV 确定迭代次数，完全展开小循环
    if (loopFullUnroll(mod)) {
        constantFolding(mod);
        deadCodeElimination(mod);
    }
    if (loopUnrolling(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
    if (tailRecursionElimination(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runP0(IR::Module* mod) {
    if (recursiveMulToNative(mod)) { /* changed */ }
    if (bitOpPatternRecognition(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runP3(IR::Module* mod) {
    if (instructionScheduling(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

} // namespace Opt