#pragma once

// ================================================================
// LoopAnalysis — 循环分析基础设施
// ----------------------------------------------------------------
// 合并自 4 个紧耦合的循环/访存分析模块，为矩阵分块、归约收缩、
// 三角拷贝、冗余迭代消除等结构敏感变换提供统一的分析接口：
//   1. MemoryAccessAnalysis      — 指针访问分解（GEP 链 → root + indices）
//   2. AffineRecurrenceAnalysis  — 仿射递推识别（dest = prev*scale + addend）
//   3. ScalarReductionAnalysis   — 标量归约识别（alloca-backed reduction）
//   4. LoopPatternAnalysis       — 规范计数循环识别（unit-step counted loop）
// ----------------------------------------------------------------
// 依赖：ir/IR.h（类型系统与指令）；LoopPatternAnalysis 需 Optimizer.h
// 中的 findNaturalLoops/NaturalLoop（在 .cpp 中 include，避免头循环）。
// ================================================================

#include "ir/IR.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {

// ================================================================
// 一、内存访问分析（MemoryAccessAnalysis）
// ----------------------------------------------------------------
// 将指针访问分解为语义基对象与其 GEP 链上的动态下标。
// ================================================================

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

// ================================================================
// 二、仿射递推分析（AffineRecurrenceAnalysis）
// ----------------------------------------------------------------
// 识别就地仿射递推：
//   destination = previous(destination) * scale + addend
// 记录表达式指令及其访存路径，但不赋予 root/indices 矩阵语义。
// ================================================================

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

// ================================================================
// 三、标量归约分析（ScalarReductionAnalysis）
// ----------------------------------------------------------------
// 识别经 alloca 表示的循环携带标量归约：
//   acc = identity;
//   ...
//   acc = op(acc, contribution);
// 该形式刻意不依赖 contribution 的来源。
// ================================================================

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

// ================================================================
// 四、循环模式分析（LoopPatternAnalysis）
// ----------------------------------------------------------------
// 识别被结构敏感变换消费的升序单位步长计数循环：
//   for (i = start; i < bound; i += 1)
// 比较也可等价表示为 bound > i。
// ================================================================

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
