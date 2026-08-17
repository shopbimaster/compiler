#pragma once

#include "ir/IR.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Opt {

// Reasons are intentionally target-independent.  Target feature filtering is
// performed later, after this analysis has proved that changing the loop's
// execution order is semantically valid.
enum class VectorRejectReason {
    None,
    NotInnermost,
    MultipleExits,
    UnsupportedControlFlow,
    MissingCanonicalInduction,
    NonUnitStep,
    NonInvariantBound,
    CallInLoop,
    UnknownSideEffect,
    UnsupportedInstruction,
    UnanalyzableMemoryAccess,
    NonContiguousMemoryAccess,
    UnsupportedElementType,
    PossibleAlias,
    LoopCarriedDependence,
    PotentialInductionOverflow,
    LiveOutValue,
    NoMemoryWrite,
};

enum class VectorPlanKind {
    Fill,
    Copy,
    Elementwise,
};

enum class VectorMemoryAccessKind {
    Load,
    Store,
};

// A proven contiguous memory access.  The last GEP index has the form
// induction + inductionOffset.  All preceding indices are loop-invariant.
struct VectorMemoryAccess {
    VectorMemoryAccessKind kind = VectorMemoryAccessKind::Load;
    IR::Instruction* instruction = nullptr;
    IR::Value* root = nullptr;
    IR::Type* elementType = nullptr;
    IR::Value* linearIndex = nullptr;
    int64_t inductionOffset = 0;
    std::vector<IR::Value*> invariantIndices;
};

// This is a target-independent description of a loop that is legal to
// vectorize.  It never owns or mutates IR objects.
struct VectorPlan {
    VectorPlanKind kind = VectorPlanKind::Elementwise;
    IR::Function* function = nullptr;
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* latch = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Instruction* induction = nullptr;
    IR::Value* inductionStorage = nullptr;
    IR::Instruction* compare = nullptr;
    IR::Value* start = nullptr;
    IR::Value* bound = nullptr;
    bool inclusiveUpperBound = false;
    int64_t constantTripCount = -1;
    std::vector<IR::BasicBlock*> blocks;
    std::vector<IR::Instruction*> scalarOperations;
    std::vector<VectorMemoryAccess> memoryAccesses;
};

struct VectorizationReport {
    IR::Function* function = nullptr;
    IR::BasicBlock* header = nullptr;
    VectorRejectReason reason = VectorRejectReason::None;
    std::string detail;
    VectorPlan plan;

    bool accepted() const { return reason == VectorRejectReason::None; }
};

// Reports are returned in source IR order: module function order followed by
// loop-header block order.  The analysis is read-only and does not inspect the
// environment or target instruction set.
std::vector<VectorizationReport>
analyzeVectorizationCandidates(IR::Function* function);

std::vector<VectorizationReport>
analyzeVectorizationCandidates(IR::Module* module);

const char* vectorRejectReasonName(VectorRejectReason reason);
std::string formatVectorizationReport(const VectorizationReport& report);

} // namespace Opt
