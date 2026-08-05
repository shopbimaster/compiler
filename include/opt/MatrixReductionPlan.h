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
    IR::ConstantInt* initialValue = nullptr;
    IR::ConstantInt* skippedScale = nullptr;
    unsigned initialValueArgumentIndex = 0;
    bool initialValueIsArgument = false;
    unsigned sizeArgumentIndex = 0;
    unsigned scaleArgumentIndex = 0;
    unsigned addendArgumentIndex = 0;
    unsigned destinationArgumentIndex = 0;
    int64_t indexStart = 0;
    int64_t indexStep = 1;
    bool inclusiveUpperBound = false;
};

// A fully proven transformation plan. Building the plan performs all
// structural, control-flow and memory-access legality checks; applying it is
// limited to IR construction and rewiring.
struct MatrixReductionPlan {
    AffineKernelSummary kernel;
    IR::Function* caller = nullptr;
    IR::BasicBlock* loopPreheader = nullptr;
    IR::BasicBlock* finalReductionPreheader = nullptr;
    IR::BasicBlock* finalReductionExit = nullptr;
    IR::Instruction* finalReductionInitialization = nullptr;
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
