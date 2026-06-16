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
    inlineExpansion(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
    if (commonSubexpressionElimination(mod)) { /* CSE changed */ }
    if (loopInvariantCodeMotion(mod)) { /* LICM changed */ }
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