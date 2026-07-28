#pragma once

#include "ir/IR.h"

#include <unordered_set>

namespace Opt {

// Describes the canonical counted-loop form currently consumed by
// structure-sensitive transformations:
//
//   for (i = 0; i < bound; i += 1)
//
// Keeping this descriptor outside an individual transformation makes the
// structural requirement explicit and provides one place to generalize it.
struct CanonicalCountedLoop {
    IR::Instruction* induction = nullptr;
    IR::Instruction* compare = nullptr;
    IR::BasicBlock* header = nullptr;
    IR::Value* bound = nullptr;
    std::unordered_set<IR::BasicBlock*> body;
};

// Recognizes a zero-based, unit-step, signed-less-than natural loop and also
// proves that containedBlock belongs to that loop.
bool analyzeCanonicalCountedLoop(
    IR::Function* function,
    IR::Value* induction,
    IR::Value* bound,
    IR::BasicBlock* containedBlock,
    CanonicalCountedLoop& result);

} // namespace Opt
