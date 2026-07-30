#pragma once

#include "ir/IR.h"

#include <unordered_map>
#include <vector>

namespace Opt {

// A pointer access decomposed into its semantic base object and the dynamic
// indices applied through its GEP chain.
struct PointerAccess {
    IR::Value* root = nullptr;
    std::vector<IR::Value*> indices;
};

// Front-end lowering may spill pointer arguments to allocas before use.
// Recording those spills lets access analysis recover the original argument.
using AllocaArgumentMap =
    std::unordered_map<IR::Value*, IR::Argument*>;

AllocaArgumentMap buildAllocaArgumentMap(IR::Function* function);

bool collectPointerAccess(
    IR::Value* value,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access);

IR::GlobalVariable* rootGlobal(IR::Value* value);

} // namespace Opt
