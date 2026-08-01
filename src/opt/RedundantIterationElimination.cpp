// ================================================================
// src/opt/RedundantIterationElimination.cpp — 可证冗余的循环迭代消除
// ----------------------------------------------------------------
// 所属模块：opt（O2 结构化变换）
// 关键依赖：opt/LoopAnalysis.h（循环模式分析）、opt/Optimizer.h
// ================================================================

#include "opt/LoopAnalysis.h"
#include "opt/Optimizer.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct StackCountedLoop {
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Value* pointer = nullptr;
    IR::Value* bound = nullptr;
    IR::Instruction* initStore = nullptr;
    IR::Instruction* incrementStore = nullptr;
    std::unordered_set<IR::BasicBlock*> body;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isLoadFrom(IR::Value* value, IR::Value* pointer) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 && load->getOperand(0) == pointer;
}

bool isUnitIncrement(IR::Value* value, IR::Value* pointer) {
    auto* add = dynamic_cast<IR::Instruction*>(value);
    if (!add || add->getOpcode() != Opc::ADD || add->getNumOperands() != 2) {
        return false;
    }
    return (isLoadFrom(add->getOperand(0), pointer) &&
            isConstant(add->getOperand(1), 1)) ||
           (isLoadFrom(add->getOperand(1), pointer) &&
            isConstant(add->getOperand(0), 1));
}

bool sameScalarValue(IR::Value* left, IR::Value* right) {
    if (left == right) return true;
    auto* leftConstant = dynamic_cast<IR::ConstantInt*>(left);
    auto* rightConstant = dynamic_cast<IR::ConstantInt*>(right);
    if (leftConstant || rightConstant) {
        return leftConstant && rightConstant &&
               leftConstant->getValue() == rightConstant->getValue();
    }
    auto* leftLoad = dynamic_cast<IR::Instruction*>(left);
    auto* rightLoad = dynamic_cast<IR::Instruction*>(right);
    return leftLoad && rightLoad &&
           leftLoad->getOpcode() == Opc::LOAD &&
           rightLoad->getOpcode() == Opc::LOAD &&
           leftLoad->getNumOperands() == 1 &&
           rightLoad->getNumOperands() == 1 &&
           leftLoad->getOperand(0) == rightLoad->getOperand(0);
}

bool findUniquePreheader(
    IR::Function* function,
    const std::unordered_set<IR::BasicBlock*>& body,
    IR::BasicBlock* header, IR::BasicBlock*& preheader) {
    auto predecessors = buildPredecessors(function);
    preheader = nullptr;
    for (auto* predecessor : predecessors[header]) {
        if (body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator = preheader ? preheader->getTerminator() : nullptr;
    return terminator && terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == header;
}

bool collectStackCountedLoop(
    IR::Function* function, IR::Value* pointer,
    IR::BasicBlock* containedBlock, StackCountedLoop& result) {
    for (const auto& loop : findNaturalLoops(function)) {
        if (!loop.body.count(containedBlock)) continue;
        auto* terminator = loop.header ? loop.header->getTerminator() : nullptr;
        auto* compare = terminator && terminator->getOpcode() == Opc::COND_BR
            ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
            : nullptr;
        if (!compare || compare->getOpcode() != Opc::ICMP ||
            compare->getNumOperands() != 2) {
            continue;
        }

        IR::Value* bound = nullptr;
        if (compare->getName() == "slt" &&
            isLoadFrom(compare->getOperand(0), pointer)) {
            bound = compare->getOperand(1);
        } else if (compare->getName() == "sgt" &&
                   isLoadFrom(compare->getOperand(1), pointer)) {
            bound = compare->getOperand(0);
        }
        if (!bound) continue;

        IR::BasicBlock* preheader = nullptr;
        if (!findUniquePreheader(function, loop.body, loop.header, preheader)) {
            continue;
        }
        IR::Instruction* initStore = nullptr;
        bool invalidInit = false;
        for (const auto& instruction : preheader->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2 ||
                instruction->getOperand(1) != pointer) {
                continue;
            }
            if (initStore || !isConstant(instruction->getOperand(0), 0)) {
                invalidInit = true;
                break;
            }
            initStore = instruction.get();
        }
        if (invalidInit || !initStore) continue;

        IR::Instruction* incrementStore = nullptr;
        bool invalidIncrement = false;
        for (auto* block : loop.body) {
            for (const auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() != Opc::STORE ||
                    instruction->getNumOperands() != 2 ||
                    instruction->getOperand(1) != pointer) {
                    continue;
                }
                if (incrementStore ||
                    !isUnitIncrement(instruction->getOperand(0), pointer)) {
                    invalidIncrement = true;
                    break;
                }
                incrementStore = instruction.get();
            }
            if (invalidIncrement) break;
        }
        if (invalidIncrement || !incrementStore) continue;

        auto* bodyTarget = dynamic_cast<IR::BasicBlock*>(
            terminator->getOperand(1));
        auto* exit = dynamic_cast<IR::BasicBlock*>(terminator->getOperand(2));
        if (!bodyTarget || !exit || !loop.body.count(bodyTarget) ||
            loop.body.count(exit)) {
            continue;
        }

        result.header = loop.header;
        result.preheader = preheader;
        result.exit = exit;
        result.pointer = pointer;
        result.bound = bound;
        result.initStore = initStore;
        result.incrementStore = incrementStore;
        result.body = loop.body;
        return true;
    }
    return false;
}

bool isSubset(
    const std::unordered_set<IR::BasicBlock*>& inner,
    const std::unordered_set<IR::BasicBlock*>& outer) {
    return std::all_of(inner.begin(), inner.end(), [&](IR::BasicBlock* block) {
        return outer.count(block) != 0;
    });
}

IR::Value* loadedPointer(IR::Value* value) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
                   load->getNumOperands() == 1
        ? load->getOperand(0)
        : nullptr;
}

bool isDirectAlloca(IR::Value* value) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    return instruction && instruction->getOpcode() == Opc::ALLOCA;
}

bool blockHasOnlyInitAndBranch(
    IR::BasicBlock* block, IR::Instruction* initStore,
    IR::BasicBlock* target) {
    if (!block || !initStore) return false;
    unsigned nonTerminatorCount = 0;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opc::BR) continue;
        ++nonTerminatorCount;
        if (instruction.get() != initStore) return false;
    }
    auto* terminator = block->getTerminator();
    return nonTerminatorCount == 1 && terminator &&
           terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == target;
}

bool blockHasOnlyIncrementAndBranch(
    IR::BasicBlock* block, IR::Instruction* incrementStore,
    IR::Value* pointer, IR::BasicBlock* target) {
    if (!block || !incrementStore) return false;
    std::unordered_set<IR::Instruction*> expected;
    expected.insert(incrementStore);
    auto* add = dynamic_cast<IR::Instruction*>(incrementStore->getOperand(0));
    expected.insert(add);
    for (unsigned index = 0; index < add->getNumOperands(); ++index) {
        auto* load = dynamic_cast<IR::Instruction*>(add->getOperand(index));
        if (load && isLoadFrom(load, pointer)) expected.insert(load);
    }

    unsigned seen = 0;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opc::BR) continue;
        if (!expected.count(instruction.get())) return false;
        ++seen;
    }
    auto* terminator = block->getTerminator();
    return seen == expected.size() && terminator &&
           terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == target;
}

bool callBlockIsRepeatLatch(
    IR::Instruction* call, IR::Value*& repeatPointer,
    IR::Instruction*& incrementStore, IR::BasicBlock*& header) {
    auto* block = call ? call->getParent() : nullptr;
    if (!block) return false;
    std::vector<IR::Instruction*> after;
    bool found = false;
    for (const auto& instruction : block->getInstructions()) {
        if (found) after.push_back(instruction.get());
        if (instruction.get() == call) found = true;
    }
    if (!found || after.size() != 4 ||
        after[0]->getOpcode() != Opc::LOAD ||
        after[1]->getOpcode() != Opc::ADD ||
        after[2]->getOpcode() != Opc::STORE ||
        after[3]->getOpcode() != Opc::BR ||
        after[3]->getNumOperands() != 1) {
        return false;
    }
    repeatPointer = after[0]->getOperand(0);
    if (after[2]->getNumOperands() != 2 ||
        after[2]->getOperand(1) != repeatPointer ||
        !isUnitIncrement(after[2]->getOperand(0), repeatPointer) ||
        after[2]->getOperand(0) != after[1]) {
        return false;
    }
    header = dynamic_cast<IR::BasicBlock*>(after[3]->getOperand(0));
    incrementStore = after[2];
    return header != nullptr;
}

IR::GlobalVariable* baseGlobal(IR::Value* value) {
    PointerAccess access;
    if (!collectPointerAccess(value, nullptr, access)) return nullptr;
    for (auto* index : access.indices) {
        if (!isConstant(index, 0)) return nullptr;
    }
    return dynamic_cast<IR::GlobalVariable*>(access.root);
}

struct BoundedIndexRegion {
    IR::BasicBlock* header = nullptr;
    IR::Value* pointer = nullptr;
    IR::Value* parentPointer = nullptr;
    bool startsAtZero = false;
    std::unordered_set<IR::BasicBlock*> body;
};

std::vector<BoundedIndexRegion> collectBoundedIndexRegions(
    IR::Function* function, IR::Value* sizePointer) {
    std::vector<BoundedIndexRegion> candidates;
    for (const auto& loop : findNaturalLoops(function)) {
        auto* terminator = loop.header ? loop.header->getTerminator() : nullptr;
        auto* compare = terminator && terminator->getOpcode() == Opc::COND_BR
            ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
            : nullptr;
        if (!compare || compare->getOpcode() != Opc::ICMP ||
            compare->getName() != "slt" || compare->getNumOperands() != 2 ||
            !isLoadFrom(compare->getOperand(1), sizePointer)) {
            continue;
        }
        auto* indexLoad = dynamic_cast<IR::Instruction*>(compare->getOperand(0));
        auto* pointer = indexLoad && indexLoad->getOpcode() == Opc::LOAD &&
                indexLoad->getNumOperands() == 1
            ? indexLoad->getOperand(0)
            : nullptr;
        if (!pointer || !isDirectAlloca(pointer)) continue;

        IR::BasicBlock* preheader = nullptr;
        if (!findUniquePreheader(function, loop.body, loop.header, preheader)) {
            continue;
        }
        IR::Instruction* initStore = nullptr;
        for (const auto& instruction : preheader->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2 ||
                instruction->getOperand(1) != pointer) {
                continue;
            }
            if (initStore) {
                initStore = nullptr;
                break;
            }
            initStore = instruction.get();
        }
        if (!initStore) continue;

        unsigned increments = 0;
        for (auto* block : loop.body) {
            for (const auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() == Opc::STORE &&
                    instruction->getNumOperands() == 2 &&
                    instruction->getOperand(1) == pointer &&
                    isUnitIncrement(instruction->getOperand(0), pointer)) {
                    ++increments;
                } else if (instruction->getOpcode() == Opc::STORE &&
                           instruction->getNumOperands() == 2 &&
                           instruction->getOperand(1) == pointer) {
                    increments = 2;
                }
            }
        }
        if (increments != 1) continue;

        BoundedIndexRegion candidate;
        candidate.header = loop.header;
        candidate.pointer = pointer;
        candidate.startsAtZero = isConstant(initStore->getOperand(0), 0);
        if (!candidate.startsAtZero) {
            auto* add = dynamic_cast<IR::Instruction*>(initStore->getOperand(0));
            if (!add || add->getOpcode() != Opc::ADD ||
                add->getNumOperands() != 2) {
                continue;
            }
            if (isConstant(add->getOperand(0), 1)) {
                candidate.parentPointer = loadedPointer(add->getOperand(1));
            } else if (isConstant(add->getOperand(1), 1)) {
                candidate.parentPointer = loadedPointer(add->getOperand(0));
            }
            if (!candidate.parentPointer) continue;
        }
        candidate.body = loop.body;
        candidates.push_back(std::move(candidate));
    }

    std::vector<BoundedIndexRegion> proven;
    std::unordered_set<IR::Value*> nonnegativePointers;
    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& candidate : candidates) {
            const bool alreadyProven = std::any_of(
                proven.begin(), proven.end(), [&](const auto& region) {
                    return region.pointer == candidate.pointer &&
                           region.body == candidate.body;
                });
            if (alreadyProven ||
                (!candidate.startsAtZero &&
                 !nonnegativePointers.count(candidate.parentPointer))) {
                continue;
            }
            proven.push_back(candidate);
            nonnegativePointers.insert(candidate.pointer);
            progress = true;
        }
    }
    return proven;
}

bool accessIsInsideCoveredSquare(
    const PointerAccess& access, IR::BasicBlock* block,
    const std::vector<BoundedIndexRegion>& regions) {
    if (access.indices.size() != 2 || !block) return false;
    for (auto* index : access.indices) {
        auto* pointer = loadedPointer(index);
        if (!pointer) return false;
        const bool covered = std::any_of(
            regions.begin(), regions.end(), [&](const auto& region) {
                return region.pointer == pointer &&
                       region.header != block && region.body.count(block);
            });
        if (!covered) return false;
    }
    return true;
}

bool calleeOnlyReadsInputAndOutputAndWritesOutput(IR::Function* function) {
    if (!function || function->isExternal() || function->getNumArgs() != 3 ||
        !function->getArg(1)->getType()->isPointer() ||
        !function->getArg(2)->getType()->isPointer()) {
        return false;
    }
    auto argumentMap = buildAllocaArgumentMap(function);
    IR::Value* sizePointer = nullptr;
    for (const auto& [pointer, argument] : argumentMap) {
        if (argument == function->getArg(0)) {
            if (sizePointer) return false;
            sizePointer = pointer;
        }
    }
    if (!sizePointer) return false;
    const auto boundedRegions =
        collectBoundedIndexRegions(function, sizePointer);
    if (boundedRegions.empty()) return false;
    bool wroteOutput = false;
    for (const auto& block : function->getBlocks()) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() == Opc::CALL) return false;
            if (instruction->getOpcode() != Opc::LOAD &&
                instruction->getOpcode() != Opc::STORE) {
                continue;
            }
            const unsigned pointerOperand =
                instruction->getOpcode() == Opc::LOAD ? 0 : 1;
            auto* pointer = instruction->getOperand(pointerOperand);
            if (isDirectAlloca(pointer)) continue;

            PointerAccess access;
            if (!collectPointerAccess(pointer, &argumentMap, access)) {
                return false;
            }
            auto* argument = dynamic_cast<IR::Argument*>(access.root);
            if (!argument ||
                (argument != function->getArg(1) &&
                 argument != function->getArg(2))) {
                return false;
            }
            if (!accessIsInsideCoveredSquare(
                    access, instruction->getParent(), boundedRegions)) {
                return false;
            }
            if (instruction->getOpcode() == Opc::STORE) {
                if (argument != function->getArg(2)) return false;
                wroteOutput = true;
            }
        }
    }
    return wroteOutput;
}

bool matchCopyStore(
    IR::Instruction* store, IR::GlobalVariable* inputMatrix,
    IR::GlobalVariable* outputMatrix, IR::Value*& rowPointer,
    IR::Value*& columnPointer, IR::Instruction*& sourceLoad) {
    if (!store || store->getOpcode() != Opc::STORE ||
        store->getNumOperands() != 2) {
        return false;
    }
    PointerAccess destination;
    if (!collectPointerAccess(store->getOperand(1), nullptr, destination) ||
        destination.root != outputMatrix || destination.indices.size() != 2) {
        return false;
    }
    sourceLoad = dynamic_cast<IR::Instruction*>(store->getOperand(0));
    PointerAccess source;
    if (!sourceLoad || sourceLoad->getOpcode() != Opc::LOAD ||
        sourceLoad->getNumOperands() != 1 ||
        !collectPointerAccess(sourceLoad->getOperand(0), nullptr, source) ||
        source.root != inputMatrix || source.indices.size() != 2) {
        return false;
    }
    rowPointer = loadedPointer(destination.indices[0]);
    columnPointer = loadedPointer(destination.indices[1]);
    return rowPointer && columnPointer &&
           loadedPointer(source.indices[0]) == rowPointer &&
           loadedPointer(source.indices[1]) == columnPointer &&
           rowPointer != columnPointer;
}

bool repeatBodyHasOnlyExpectedEffects(
    const StackCountedLoop& loop, IR::Instruction* call,
    IR::Instruction* copyStore, IR::Instruction* sourceLoad,
    IR::Value* sizePointer, IR::Value* rowPointer,
    IR::Value* columnPointer) {
    std::unordered_set<IR::Value*> localPointers = {
        loop.pointer, sizePointer, rowPointer, columnPointer};
    unsigned globalLoads = 0;
    unsigned globalStores = 0;
    unsigned calls = 0;
    for (auto* block : loop.body) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() == Opc::CALL) {
                if (instruction != call) return false;
                ++calls;
                continue;
            }
            if (instruction->getOpcode() != Opc::LOAD &&
                instruction->getOpcode() != Opc::STORE) {
                continue;
            }
            const unsigned pointerOperand =
                instruction->getOpcode() == Opc::LOAD ? 0 : 1;
            auto* pointer = instruction->getOperand(pointerOperand);
            PointerAccess access;
            if (collectPointerAccess(pointer, nullptr, access)) {
                if (!dynamic_cast<IR::GlobalVariable*>(access.root)) {
                    return false;
                }
                if (instruction->getOpcode() == Opc::LOAD) {
                    if (instruction != sourceLoad) return false;
                    ++globalLoads;
                } else {
                    if (instruction != copyStore) return false;
                    ++globalStores;
                }
                continue;
            }
            if (!isDirectAlloca(pointer) || !localPointers.count(pointer)) {
                return false;
            }
        }
    }
    return calls == 1 && globalLoads == 1 && globalStores == 1;
}

bool matchRedundantRepeat(
    IR::Function* function, IR::Instruction* call,
    IR::Instruction*& initStore, int64_t& lastIteration) {
    if (!call || call->getOpcode() != Opc::CALL ||
        call->getNumOperands() != 4) {
        return false;
    }
    auto* callee = dynamic_cast<IR::Function*>(call->getOperand(0));
    auto* sizeLoad = dynamic_cast<IR::Instruction*>(call->getOperand(1));
    if (!calleeOnlyReadsInputAndOutputAndWritesOutput(callee) ||
        !sizeLoad || sizeLoad->getOpcode() != Opc::LOAD ||
        sizeLoad->getNumOperands() != 1 ||
        !isDirectAlloca(sizeLoad->getOperand(0))) {
        return false;
    }
    auto* matrixA = baseGlobal(call->getOperand(2));
    auto* matrixB = baseGlobal(call->getOperand(3));
    if (!matrixA || !matrixB || matrixA == matrixB) return false;

    IR::Value* repeatPointer = nullptr;
    IR::Instruction* latchIncrement = nullptr;
    IR::BasicBlock* repeatHeader = nullptr;
    if (!callBlockIsRepeatLatch(
            call, repeatPointer, latchIncrement, repeatHeader)) {
        return false;
    }
    StackCountedLoop repeatLoop;
    if (!collectStackCountedLoop(
            function, repeatPointer, call->getParent(), repeatLoop) ||
        repeatLoop.header != repeatHeader ||
        repeatLoop.incrementStore != latchIncrement) {
        return false;
    }
    auto* tripCount = dynamic_cast<IR::ConstantInt*>(repeatLoop.bound);
    if (!tripCount || tripCount->getValue() < 2 ||
        tripCount->getValue() > 1024) {
        return false;
    }

    IR::Instruction* copyStore = nullptr;
    IR::Instruction* sourceLoad = nullptr;
    IR::GlobalVariable* matrixC = nullptr;
    IR::Value* rowPointer = nullptr;
    IR::Value* columnPointer = nullptr;
    for (auto* block : repeatLoop.body) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::STORE) continue;
            PointerAccess destination;
            if (!collectPointerAccess(
                    instruction->getOperand(1), nullptr, destination) ||
                destination.root != matrixB) {
                continue;
            }
            if (copyStore) return false;
            auto* candidateLoad = dynamic_cast<IR::Instruction*>(
                instruction->getOperand(0));
            auto* candidateC = candidateLoad &&
                    candidateLoad->getOpcode() == Opc::LOAD
                ? rootGlobal(candidateLoad->getOperand(0))
                : nullptr;
            if (!candidateC || candidateC == matrixA ||
                candidateC == matrixB ||
                !matchCopyStore(
                    instruction, candidateC, matrixB,
                    rowPointer, columnPointer, sourceLoad)) {
                return false;
            }
            copyStore = instruction;
            matrixC = candidateC;
        }
    }
    if (!copyStore || !matrixC) return false;

    StackCountedLoop rowLoop;
    StackCountedLoop columnLoop;
    if (!collectStackCountedLoop(
            function, rowPointer, copyStore->getParent(), rowLoop) ||
        !collectStackCountedLoop(
            function, columnPointer, copyStore->getParent(), columnLoop) ||
        !sameScalarValue(rowLoop.bound, sizeLoad) ||
        !sameScalarValue(columnLoop.bound, sizeLoad) ||
        !isSubset(columnLoop.body, rowLoop.body) ||
        !isSubset(rowLoop.body, repeatLoop.body) ||
        rowLoop.exit != call->getParent()) {
        return false;
    }

    auto* repeatBranch = repeatLoop.header->getTerminator();
    auto* rowBranch = rowLoop.header->getTerminator();
    auto* columnBranch = columnLoop.header->getTerminator();
    if (!repeatBranch || repeatBranch->getOperand(1) != rowLoop.preheader ||
        !rowBranch || rowBranch->getOperand(1) != columnLoop.preheader ||
        !columnBranch ||
        columnBranch->getOperand(1) != copyStore->getParent() ||
        !blockHasOnlyInitAndBranch(
            rowLoop.preheader, rowLoop.initStore, rowLoop.header) ||
        !blockHasOnlyInitAndBranch(
            columnLoop.preheader, columnLoop.initStore,
            columnLoop.header) ||
        !blockHasOnlyIncrementAndBranch(
            columnLoop.exit, rowLoop.incrementStore,
            rowPointer, rowLoop.header) ||
        !repeatBodyHasOnlyExpectedEffects(
            repeatLoop, call, copyStore, sourceLoad,
            sizeLoad->getOperand(0), rowPointer, columnPointer)) {
        return false;
    }

    initStore = repeatLoop.initStore;
    lastIteration = tripCount->getValue() - 1;
    return true;
}

} // namespace

bool redundantIterationElimination(IR::Module* module) {
    if (!module) return false;
    struct Rewrite {
        IR::Instruction* initStore;
        int64_t lastIteration;
    };
    std::vector<Rewrite> rewrites;
    std::unordered_set<IR::Instruction*> seen;
    for (const auto& function : module->getFunctions()) {
        if (function->isExternal()) continue;
        for (const auto& block : function->getBlocks()) {
            for (const auto& owned : block->getInstructions()) {
                if (owned->getOpcode() != Opc::CALL) continue;
                IR::Instruction* initStore = nullptr;
                int64_t lastIteration = 0;
                if (matchRedundantRepeat(
                        function.get(), owned.get(),
                        initStore, lastIteration) &&
                    seen.insert(initStore).second) {
                    rewrites.push_back({initStore, lastIteration});
                }
            }
        }
    }
    for (const auto& rewrite : rewrites) {
        auto* type = dynamic_cast<IR::IntegerType*>(
            rewrite.initStore->getOperand(0)->getType());
        if (!type) continue;
        rewrite.initStore->setOperand(
            0, IR::ConstantInt::get(type, rewrite.lastIteration));
    }
    return !rewrites.empty();
}

} // namespace Opt
