#pragma once

#include "opt/MemoryAccessAnalysis.h"

namespace Opt {

// Describes an in-place affine recurrence:
//
//   destination = previous(destination) * scale + addend
//
// The analysis records both the expression instructions and their memory
// access paths, but does not assign matrix-specific meanings to the roots or
// indices.
struct AffineRecurrence {
    IR::Instruction* store = nullptr;
    IR::Instruction* update = nullptr;
    IR::Instruction* multiply = nullptr;
    IR::Instruction* previousLoad = nullptr;
    IR::Instruction* scaleLoad = nullptr;
    IR::Instruction* addendLoad = nullptr;
    PointerAccess destination;
    PointerAccess previous;
    PointerAccess scale;
    PointerAccess addend;
};

bool analyzeAffineRecurrence(
    IR::Instruction* store,
    const AllocaArgumentMap* argumentMap,
    AffineRecurrence& result);

} // namespace Opt
