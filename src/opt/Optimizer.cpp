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
    if (commonSubexpressionElimination(mod)) { /* CSE changed */ }
    // LICM 已在 O1 中运行，内联的单 BB 函数不含循环，无需再次 LICM
    // 二次 LICM 会与内联后修改的 CFG 交互导致段错误
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runO3(IR::Module* mod) {
    if (algebraicSimplification(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
    if (loopInterchange(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
    if (loopUnrolling(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
    if (tailRecursionElimination(mod)) { /* changed */ }
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runP0(IR::Module* mod) {
    // recursiveMulToNative 已知导致 crypto 编译段错误，暂禁用
    // if (recursiveMulToNative(mod)) { /* changed */ }
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