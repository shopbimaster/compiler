// ================================================================
// O1 优化器统一入口
// ================================================================

#include "opt/Optimizer.h"

namespace Opt {

void runO1(IR::Module* mod) {
    constantFolding(mod);
    deadCodeElimination(mod);
}

} // namespace Opt