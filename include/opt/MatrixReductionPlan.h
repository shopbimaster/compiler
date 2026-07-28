#pragma once

#include "ir/IR.h"

#include <vector>

namespace Opt {

// Summary of the source kernel properties required by the contracted helper.
// It deliberately contains semantic IR objects instead of test- or
// source-level identifiers.
struct AffineKernelSummary {
    IR::Function* sourceFunction = nullptr;
    IR::ArrayType* rowType = nullptr;
    IR::ConstantInt* skippedScale = nullptr;
};

// A fully proven transformation plan. Building the plan performs all
// structural, control-flow and memory-access legality checks; applying it is
// limited to IR construction and rewiring.
struct MatrixReductionPlan {
    AffineKernelSummary kernel;
    IR::Function* caller = nullptr;
    IR::BasicBlock* loopPreheader = nullptr;
    IR::Instruction* finalInnerCompare = nullptr;
    std::vector<IR::Instruction*> calls;
    IR::GlobalVariable* seedMatrix = nullptr;
    IR::GlobalVariable* resultMatrix = nullptr;
    IR::Value* size = nullptr;
};

bool analyzeMatrixReductionPlan(
    IR::Module* module, MatrixReductionPlan& plan);

bool applyMatrixReductionPlan(
    IR::Module* module, const MatrixReductionPlan& plan);

} // namespace Opt
