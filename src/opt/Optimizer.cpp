// ================================================================
// O1/O2/O3/P0 优化器统一入口
// O1: 常量折叠 → 死代码消除
// O2: 函数内联 → O1 → CSE → LICM → O1
// O3: 代数化简 → O1 → 循环展开 → 尾递归消除 → O1
// P0: 递归乘→原生MUL → 位运算模式识别 → O3 → O1
// ================================================================

#include "opt/Optimizer.h"

namespace Opt {

void runO1(IR::Module* mod) {
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runO2(IR::Module* mod) {
    inlineExpansion(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
    commonSubexpressionElimination(mod);
    loopInvariantCodeMotion(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runO3(IR::Module* mod) {
    algebraicSimplification(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
    loopInterchange(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
    loopUnrolling(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
    tailRecursionElimination(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
}

void runP0(IR::Module* mod) {
    recursiveMulToNative(mod);
    bitOpPatternRecognition(mod);
    constantFolding(mod);
    deadCodeElimination(mod);
}

} // namespace Opt