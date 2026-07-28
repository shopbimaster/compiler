#pragma once

#include "ir/IR.h"

#include <unordered_set>

namespace Opt {

// Describes an ascending unit-step counted loop consumed by
// structure-sensitive transformations:
//
//   for (i = start; i < bound; i += 1)
//
// The comparison may also be represented equivalently as bound > i.
struct CanonicalCountedLoop {
    IR::Instruction* induction = nullptr;
    IR::Instruction* compare = nullptr;
    IR::BasicBlock* header = nullptr;
    IR::Value* start = nullptr;
    IR::Value* bound = nullptr;
    int64_t step = 0;
    unsigned boundOperand = 1;
    bool inclusiveUpperBound = false;
    std::unordered_set<IR::BasicBlock*> body;
};

// Recognizes an ascending, unit-step, exclusive-upper-bound natural loop and
// also proves that containedBlock belongs to that loop.
bool analyzeCanonicalCountedLoop(
    IR::Function* function,
    IR::Value* induction,
    IR::Value* bound,
    IR::BasicBlock* containedBlock,
    CanonicalCountedLoop& result);

} // namespace Opt
