// ================================================================
// O1/O2 优化器统一入口
// O1: 常量折叠 → 死代码消除
// O2: 函数内联 → O1 → CSE → LICM → O1（清理内联/外提引入的冗余）
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

} // namespace Opt