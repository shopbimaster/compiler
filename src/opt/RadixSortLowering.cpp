#include "opt/Optimizer.h"

namespace Opt {

bool radixSortLowering(IR::Module* module) {
    (void)module;
    // Replacing a complete recursive sort requires a full semantic proof.
    // Keep the legacy structural recognizer disabled until such a proof exists.
    return false;
}

} // namespace Opt
