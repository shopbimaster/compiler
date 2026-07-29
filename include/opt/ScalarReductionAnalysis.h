#pragma once

#include "ir/IR.h"

namespace Opt {

enum class ScalarReductionKind {
    Add,
    Multiply,
};

// Describes a loop-carried scalar reduction represented through an alloca:
//
//   acc = identity;
//   ...
//   acc = op(acc, contribution);
//
// This form is intentionally independent of the source of the contribution.
struct ScalarReduction {
    ScalarReductionKind kind = ScalarReductionKind::Add;
    IR::Instruction* accumulatorAddress = nullptr;
    IR::Instruction* accumulatorLoad = nullptr;
    IR::Instruction* initializationStore = nullptr;
    IR::Instruction* update = nullptr;
    IR::Instruction* updateStore = nullptr;
    IR::Value* contribution = nullptr;
};

// Recognizes an alloca-backed scalar reduction whose update consumes the
// supplied contribution. The accumulator must have one identity
// initialization and one update store.
bool analyzeAllocaScalarReduction(
    IR::Function* function,
    IR::Instruction* update,
    IR::Value* contribution,
    ScalarReduction& result);

} // namespace Opt
