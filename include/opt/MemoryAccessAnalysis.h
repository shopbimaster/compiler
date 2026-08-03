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

// All direct memory operations and calls reached from a global object's
// pointer-def/use graph.  Analysis succeeds only while the address remains in
// forms that can be audited precisely (the global itself or GEP derivatives).
// Storing the address, merging it through PHI/select, returning it, or using
// it in any other unsupported instruction is conservatively treated as an
// escape and makes the analysis fail.
struct GlobalMemoryEffects {
    std::vector<IR::Instruction*> loads;
    std::vector<IR::Instruction*> stores;
    std::vector<IR::Instruction*> calls;
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

bool analyzeGlobalMemoryEffects(
    IR::GlobalVariable* global,
    GlobalMemoryEffects& effects);

} // namespace Opt
