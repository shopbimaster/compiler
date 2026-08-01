// ================================================================
// src/opt/MatrixReductionContraction.cpp — 矩阵归约收缩
// ----------------------------------------------------------------
// 所属模块：opt（O2 阶段 6 全局清理）
// 关键依赖：opt/Optimizer.h、opt/LoopAnalysis.h、opt/MatrixReductionPlan.h
// ================================================================

#include "opt/Optimizer.h"
#include "opt/LoopAnalysis.h"
#include "opt/MatrixReductionPlan.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;
bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool getCommonIterationDomain(
    const std::vector<const CanonicalCountedLoop*>& loops,
    int64_t& start,
    int64_t& step,
    bool& inclusiveUpperBound) {
    bool initialized = false;
    for (auto* loop : loops) {
        auto* constant =
            loop ? dynamic_cast<IR::ConstantInt*>(loop->start)
                 : nullptr;
        if (!constant) return false;
        if (!initialized) {
            start = constant->getValue();
            step = loop->step;
            inclusiveUpperBound =
                loop->inclusiveUpperBound;
            initialized = true;
        } else if (
            constant->getValue() != start ||
            loop->step != step ||
            loop->inclusiveUpperBound !=
                inclusiveUpperBound) {
            return false;
        }
    }
    return initialized;
}

bool isSemanticallyUnusedArgument(
    IR::Argument* argument,
    const AllocaArgumentMap& argumentMap) {
    if (!argument) return false;

    for (const auto& use : argument->getUses()) {
        auto* instruction =
            dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction ||
            instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            use.operandNo != 0) {
            return false;
        }
        auto found = argumentMap.find(
            instruction->getOperand(1));
        if (found == argumentMap.end() ||
            found->second != argument) {
            return false;
        }
    }

    for (const auto& [alloca, mappedArgument] : argumentMap) {
        if (mappedArgument != argument) continue;
        for (const auto& use : alloca->getUses()) {
            auto* instruction =
                dynamic_cast<IR::Instruction*>(use.user);
            if (!instruction ||
                instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2 ||
                use.operandNo != 1 ||
                instruction->getOperand(0) != argument) {
                return false;
            }
        }
    }
    return true;
}

bool isInitializationOnlyArgument(
    IR::Argument* argument,
    IR::Instruction* initialStore,
    const AllocaArgumentMap& argumentMap) {
    if (!argument || !initialStore) return false;

    for (const auto& use : argument->getUses()) {
        auto* instruction =
            dynamic_cast<IR::Instruction*>(use.user);
        if (instruction == initialStore &&
            use.operandNo == 0) {
            continue;
        }
        if (!instruction ||
            instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            use.operandNo != 0) {
            return false;
        }
        auto found = argumentMap.find(
            instruction->getOperand(1));
        if (found == argumentMap.end() ||
            found->second != argument) {
            return false;
        }
    }

    for (const auto& [alloca, mappedArgument] : argumentMap) {
        if (mappedArgument != argument) continue;
        for (const auto& use : alloca->getUses()) {
            auto* instruction =
                dynamic_cast<IR::Instruction*>(use.user);
            if (!instruction) return false;
            if (instruction->getOpcode() == Opc::STORE &&
                instruction->getNumOperands() == 2 &&
                use.operandNo == 1 &&
                instruction->getOperand(0) == argument) {
                continue;
            }
            if (instruction->getOpcode() != Opc::LOAD ||
                instruction->getNumOperands() != 1 ||
                use.operandNo != 0) {
                return false;
            }
            for (const auto& loadUse :
                 instruction->getUses()) {
                if (loadUse.user != initialStore ||
                    loadUse.operandNo != 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool matchKernelFunction(
    IR::Function* function, AffineKernelSummary& summary) {
    if (!function || function->isExternal()) {
        return false;
    }

    auto* functionType = function->getFunctionType();
    if (!functionType->getReturnType()->isVoid()) {
        return false;
    }
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL) return false;
        }
    }

    auto argumentMap = buildAllocaArgumentMap(function);
    std::vector<AffineRecurrence> recurrences;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            AffineRecurrence recurrence;
            if (analyzeAffineRecurrence(
                    instruction.get(), &argumentMap,
                    recurrence) &&
                dynamic_cast<IR::Argument*>(
                    recurrence.destination.root) &&
                dynamic_cast<IR::Argument*>(
                    recurrence.scale.root) &&
                dynamic_cast<IR::Argument*>(
                    recurrence.addend.root)) {
                recurrences.push_back(std::move(recurrence));
            }
        }
    }
    if (recurrences.size() != 1) return false;

    const auto& recurrence = recurrences.front();
    auto* destinationArgument =
        dynamic_cast<IR::Argument*>(
            recurrence.destination.root);
    auto* scaleArgument =
        dynamic_cast<IR::Argument*>(recurrence.scale.root);
    auto* addendArgument =
        dynamic_cast<IR::Argument*>(recurrence.addend.root);
    if (!destinationArgument || !scaleArgument ||
        !addendArgument ||
        destinationArgument == scaleArgument ||
        destinationArgument == addendArgument ||
        scaleArgument == addendArgument) {
        return false;
    }

    auto* rowPointer =
        dynamic_cast<IR::PointerType*>(
            destinationArgument->getType());
    auto* rowType = rowPointer
        ? dynamic_cast<IR::ArrayType*>(
              rowPointer->getPointeeType())
        : nullptr;
    if (!rowType ||
        rowType->getElementType() != IR::IntegerType::I32 ||
        rowType->getNumElements() < 2 ||
        scaleArgument->getType() !=
            destinationArgument->getType() ||
        addendArgument->getType() !=
            destinationArgument->getType()) {
        return false;
    }

    std::vector<IR::Instruction*> outputStores;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            PointerAccess access;
            if (collectPointerAccess(
                    instruction->getOperand(1),
                    &argumentMap, access)) {
                if (access.root == destinationArgument) {
                    outputStores.push_back(instruction.get());
                } else if (
                    dynamic_cast<IR::Argument*>(access.root) ||
                    dynamic_cast<IR::GlobalVariable*>(access.root)) {
                    return false;
                }
            }
        }
    }
    if (outputStores.size() != 2) return false;

    IR::Instruction* initialStore = nullptr;
    IR::Instruction* updateStore = recurrence.store;
    IR::ConstantInt* initialValue = nullptr;
    IR::Argument* initialValueArgument = nullptr;
    for (auto* store : outputStores) {
        if (store == updateStore) continue;
        if (initialStore) return false;
        initialValue = dynamic_cast<IR::ConstantInt*>(
            store->getOperand(0));
        if (!initialValue) {
            PointerAccess source;
            if (!collectPointerAccess(
                    store->getOperand(0), &argumentMap,
                    source) ||
                !source.indices.empty()) {
                return false;
            }
            initialValueArgument =
                dynamic_cast<IR::Argument*>(source.root);
        }
        if ((!initialValue && !initialValueArgument) ||
            store->getOperand(0)->getType() !=
                IR::IntegerType::I32) {
            return false;
        }
        initialStore = store;
    }
    if (!initialStore ||
        (!initialValue && !initialValueArgument)) {
        return false;
    }

    PointerAccess initialOutput;
    if (!collectPointerAccess(
            initialStore->getOperand(1), &argumentMap,
            initialOutput) ||
        initialOutput.indices.size() != 2) {
        return false;
    }

    const auto& updatedOutput = recurrence.destination;
    if (updatedOutput.indices.size() != 2 ||
        recurrence.previous.root != destinationArgument ||
        recurrence.previous.indices != updatedOutput.indices) {
        return false;
    }

    const auto& coefficient = recurrence.scale;
    const auto& input = recurrence.addend;
    if (coefficient.root != scaleArgument ||
        input.root != addendArgument ||
        coefficient.indices.size() != 2 ||
        input.indices.size() != 2) {
        return false;
    }

    IR::Value* indexI = updatedOutput.indices[0];
    IR::Value* indexJ = updatedOutput.indices[1];
    IR::Value* indexK = coefficient.indices[1];
    if (coefficient.indices[0] != indexI ||
        input.indices[0] != indexK ||
        input.indices[1] != indexJ ||
        indexI == indexJ || indexI == indexK ||
        indexJ == indexK) {
        return false;
    }

    IR::ConstantInt* skippedCoefficient = nullptr;
    auto dominators = computeDominators(function);
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* compare = owned.get();
            if (compare->getOpcode() != Opc::ICMP ||
                (compare->getName() != "eq" &&
                 compare->getName() != "ne") ||
                compare->getNumOperands() != 2) {
                continue;
            }
            IR::Instruction* comparedLoad = nullptr;
            IR::Value* other = nullptr;
            for (unsigned index = 0; index < 2; ++index) {
                auto* candidate = dynamic_cast<IR::Instruction*>(
                    compare->getOperand(index));
                if (candidate &&
                    candidate->getOpcode() == Opc::LOAD) {
                    comparedLoad = candidate;
                    other = compare->getOperand(1 - index);
                }
            }
            auto* skipValue =
                dynamic_cast<IR::ConstantInt*>(other);
            if (!comparedLoad || !skipValue ||
                comparedLoad->getNumOperands() != 1) {
                continue;
            }
            PointerAccess comparedAccess;
            if (!collectPointerAccess(
                    comparedLoad->getOperand(0),
                    &argumentMap, comparedAccess) ||
                comparedAccess.root != scaleArgument ||
                comparedAccess.indices != coefficient.indices) {
                continue;
            }
            auto* terminator = compare->getParent()
                ? compare->getParent()->getTerminator()
                : nullptr;
            if (!terminator ||
                terminator->getOpcode() != Opc::COND_BR) {
                continue;
            }
            auto* trueTarget =
                dynamic_cast<IR::BasicBlock*>(
                    terminator->getOperand(1));
            auto* falseTarget =
                dynamic_cast<IR::BasicBlock*>(
                    terminator->getOperand(2));
            const bool trueDominatesUpdate =
                trueTarget &&
                dominators[updateStore->getParent()].count(
                    trueTarget);
            const bool falseDominatesUpdate =
                falseTarget &&
                dominators[updateStore->getParent()].count(
                    falseTarget);
            const bool unequalExecutesUpdate =
                (compare->getName() == "eq" &&
                 falseDominatesUpdate &&
                 !trueDominatesUpdate) ||
                (compare->getName() == "ne" &&
                 trueDominatesUpdate &&
                 !falseDominatesUpdate);
            if (unequalExecutesUpdate) {
                if (skippedCoefficient &&
                    skippedCoefficient->getValue() !=
                        skipValue->getValue()) {
                    return false;
                }
                skippedCoefficient = skipValue;
                break;
            }
        }
        if (skippedCoefficient) break;
    }
    CanonicalCountedLoop loopI;
    CanonicalCountedLoop loopJ;
    CanonicalCountedLoop loopK;
    CanonicalCountedLoop initialLoopI;
    CanonicalCountedLoop initialLoopJ;
    IR::Argument* sizeArgument = nullptr;
    for (unsigned index = 0;
         index < function->getNumArgs(); ++index) {
        auto* candidate = function->getArg(index);
        if (candidate->getType() != IR::IntegerType::I32) {
            continue;
        }

        CanonicalCountedLoop candidateLoopI;
        CanonicalCountedLoop candidateLoopJ;
        CanonicalCountedLoop candidateLoopK;
        CanonicalCountedLoop candidateInitialLoopI;
        CanonicalCountedLoop candidateInitialLoopJ;
        if (!analyzeCanonicalCountedLoop(
                function, indexI, candidate,
                updateStore->getParent(),
                candidateLoopI) ||
            !analyzeCanonicalCountedLoop(
                function, indexJ, candidate,
                updateStore->getParent(),
                candidateLoopJ) ||
            !analyzeCanonicalCountedLoop(
                function, indexK, candidate,
                updateStore->getParent(),
                candidateLoopK) ||
            !analyzeCanonicalCountedLoop(
                function, initialOutput.indices[0],
                candidate, initialStore->getParent(),
                candidateInitialLoopI) ||
            !analyzeCanonicalCountedLoop(
                function, initialOutput.indices[1],
                candidate, initialStore->getParent(),
                candidateInitialLoopJ)) {
            continue;
        }
        if (sizeArgument) return false;
        sizeArgument = candidate;
        loopI = std::move(candidateLoopI);
        loopJ = std::move(candidateLoopJ);
        loopK = std::move(candidateLoopK);
        initialLoopI = std::move(candidateInitialLoopI);
        initialLoopJ = std::move(candidateInitialLoopJ);
    }
    if (!sizeArgument) return false;
    if (initialValueArgument &&
        (initialValueArgument == sizeArgument ||
         !isInitializationOnlyArgument(
             initialValueArgument, initialStore,
             argumentMap))) {
        return false;
    }

    for (unsigned index = 0;
         index < function->getNumArgs(); ++index) {
        auto* argument = function->getArg(index);
        if (argument == sizeArgument ||
            argument == scaleArgument ||
            argument == addendArgument ||
            argument == destinationArgument ||
            argument == initialValueArgument) {
            continue;
        }
        if (!isSemanticallyUnusedArgument(
                argument, argumentMap)) {
            return false;
        }
    }

    int64_t indexStart = 0;
    int64_t indexStep = 0;
    bool inclusiveUpperBound = false;
    if (!getCommonIterationDomain(
            {&loopI, &loopJ, &loopK,
             &initialLoopI, &initialLoopJ},
            indexStart, indexStep,
            inclusiveUpperBound) ||
        indexStart < 0 ||
        indexStep <= 0 ||
        indexStep >
            std::numeric_limits<int32_t>::max() ||
        indexStart >
            std::numeric_limits<int32_t>::max() -
                indexStep) {
        return false;
    }
    if ((initialValueArgument ||
         initialValue->getValue() != 0) &&
        inclusiveUpperBound) {
        return false;
    }

    auto postDominators = computePostDominators(function);
    auto* innerTerminator = loopJ.compare->getParent()
        ? loopJ.compare->getParent()->getTerminator()
        : nullptr;
    auto* innerBodyEntry =
        innerTerminator &&
                innerTerminator->getOpcode() == Opc::COND_BR
            ? dynamic_cast<IR::BasicBlock*>(
                  innerTerminator->getOperand(1))
            : nullptr;
    if (!innerBodyEntry ||
        !postDominators[innerBodyEntry].count(
            updateStore->getParent())) {
        return false;
    }

    if (!skippedCoefficient) {
        auto* rowTerminator = loopI.compare->getParent()
            ? loopI.compare->getParent()->getTerminator()
            : nullptr;
        auto* rowBodyEntry =
            rowTerminator &&
                    rowTerminator->getOpcode() == Opc::COND_BR
                ? dynamic_cast<IR::BasicBlock*>(
                      rowTerminator->getOperand(1))
                : nullptr;
        if (!rowBodyEntry ||
            !postDominators[rowBodyEntry].count(
                loopJ.compare->getParent())) {
            return false;
        }
    }

    summary.sourceFunction = function;
    summary.rowType = rowType;
    summary.initialValue = initialValue;
    summary.skippedScale = skippedCoefficient;
    summary.initialValueIsArgument =
        initialValueArgument != nullptr;
    if (initialValueArgument) {
        summary.initialValueArgumentIndex =
            initialValueArgument->getIndex();
    }
    summary.sizeArgumentIndex =
        sizeArgument->getIndex();
    summary.scaleArgumentIndex =
        scaleArgument->getIndex();
    summary.addendArgumentIndex =
        addendArgument->getIndex();
    summary.destinationArgumentIndex =
        destinationArgument->getIndex();
    summary.indexStart = indexStart;
    summary.indexStep = indexStep;
    summary.inclusiveUpperBound =
        inclusiveUpperBound;
    return true;
}

std::vector<IR::Instruction*> collectFunctionCalls(
    IR::Function* function) {
    std::vector<IR::Instruction*> calls;
    for (const auto& use : function->getUses()) {
        auto* instruction =
            dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction ||
            instruction->getOpcode() != Opc::CALL ||
            use.operandNo != 0) {
            return {};
        }
        calls.push_back(instruction);
    }
    return calls;
}

bool findFinalReduction(
    IR::Module* module,
    IR::Function* caller,
    IR::GlobalVariable* matrix,
    IR::Value* size,
    int64_t expectedStart,
    int64_t expectedStep,
    bool expectedInclusiveUpperBound,
    const std::vector<IR::Instruction*>& allowedCalls,
    IR::BasicBlock* loopPreheader,
    IR::Instruction*& finalLoad,
    IR::Instruction*& innerCompare,
    IR::Value*& innerInduction) {
    std::vector<IR::Instruction*> loads;
    std::vector<IR::Instruction*> stores;
    std::vector<IR::Instruction*> callsWithMatrix;

    for (auto& function : module->getFunctions()) {
        for (auto& block : function->getBlocks()) {
            for (auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() == Opc::LOAD &&
                    instruction->getNumOperands() == 1 &&
                    rootGlobal(instruction->getOperand(0)) == matrix) {
                    loads.push_back(instruction.get());
                }
                if (instruction->getOpcode() == Opc::STORE &&
                    instruction->getNumOperands() == 2 &&
                    rootGlobal(instruction->getOperand(1)) == matrix) {
                    stores.push_back(instruction.get());
                }
                if (instruction->getOpcode() == Opc::CALL) {
                    for (unsigned index = 1;
                         index < instruction->getNumOperands(); ++index) {
                        if (rootGlobal(instruction->getOperand(index)) ==
                            matrix) {
                            callsWithMatrix.push_back(instruction.get());
                            break;
                        }
                    }
                }
            }
        }
    }

    if (loads.size() != 1 ||
        callsWithMatrix.empty() ||
        loads[0]->getParent()->getParent() != caller) {
        return false;
    }
    for (auto* call : callsWithMatrix) {
        if (std::find(
                allowedCalls.begin(), allowedCalls.end(), call) ==
            allowedCalls.end()) {
            return false;
        }
    }

    auto successors = buildSuccessors(caller);
    auto reachableFromPreheader =
        [&](IR::BasicBlock* target) {
            std::vector<IR::BasicBlock*> worklist = {
                loopPreheader};
            std::unordered_set<IR::BasicBlock*> visited = {
                loopPreheader};
            while (!worklist.empty()) {
                auto* block = worklist.back();
                worklist.pop_back();
                if (block == target) return true;
                for (auto* successor : successors[block]) {
                    if (visited.insert(successor).second) {
                        worklist.push_back(successor);
                    }
                }
            }
            return false;
        };
    for (auto* store : stores) {
        if (store->getParent()->getParent() != caller ||
            reachableFromPreheader(store->getParent())) {
            return false;
        }
    }

    PointerAccess access;
    if (!collectPointerAccess(
            loads[0]->getOperand(0), nullptr, access) ||
        access.root != matrix || access.indices.size() != 2) {
        return false;
    }

    auto* add = loads[0]->getNumUses() == 1
        ? dynamic_cast<IR::Instruction*>(
              loads[0]->getUses().front().user)
        : nullptr;
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2 ||
        add->getNumUses() != 1) {
        return false;
    }

    ScalarReduction reduction;
    if (!analyzeAllocaScalarReduction(
            caller, add, loads[0], reduction) ||
        reduction.kind != ScalarReductionKind::Add) {
        return false;
    }

    CanonicalCountedLoop outer;
    CanonicalCountedLoop inner;
    if (!analyzeCanonicalCountedLoop(
            caller, access.indices[0], size,
            loads[0]->getParent(), outer) ||
        !analyzeCanonicalCountedLoop(
            caller, access.indices[1], size,
            loads[0]->getParent(), inner)) {
        return false;
    }
    int64_t reductionStart = 0;
    int64_t reductionStep = 0;
    bool reductionInclusiveUpperBound = false;
    if (!getCommonIterationDomain(
            {&outer, &inner}, reductionStart,
            reductionStep,
            reductionInclusiveUpperBound) ||
        reductionStart != expectedStart ||
        reductionStep != expectedStep ||
        reductionInclusiveUpperBound !=
            expectedInclusiveUpperBound) {
        return false;
    }

    for (auto* block : inner.body) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL ||
                (instruction->getOpcode() == Opc::STORE &&
                 instruction.get() != reduction.updateStore)) {
                return false;
            }
        }
    }
    auto* innerTerminator = inner.header
        ? inner.header->getTerminator()
        : nullptr;
    auto* innerBodyEntry =
        innerTerminator &&
                innerTerminator->getOpcode() == Opc::COND_BR
            ? dynamic_cast<IR::BasicBlock*>(
                  innerTerminator->getOperand(1))
            : nullptr;
    auto* innerExit =
        innerTerminator &&
                innerTerminator->getOpcode() == Opc::COND_BR
            ? dynamic_cast<IR::BasicBlock*>(
                  innerTerminator->getOperand(2))
            : nullptr;
    if (!innerTerminator ||
        !innerBodyEntry || !innerExit ||
        !inner.body.count(innerBodyEntry) ||
        inner.body.count(innerExit) ||
        inner.compare->getNumUses() != 1 ||
        inner.compare->getUses().front().user !=
            innerTerminator ||
        inner.compare->getUses().front().operandNo != 0) {
        return false;
    }

    finalLoad = loads[0];
    innerCompare = inner.compare;
    innerInduction = inner.induction;
    return true;
}

bool matrixHasUnexpectedAccess(
    IR::Module* module, IR::GlobalVariable* matrix,
    const std::vector<IR::Instruction*>& allowedCalls) {
    for (auto& function : module->getFunctions()) {
        for (auto& block : function->getBlocks()) {
            for (auto& instruction : block->getInstructions()) {
                if ((instruction->getOpcode() == Opc::LOAD &&
                     instruction->getNumOperands() == 1 &&
                     rootGlobal(instruction->getOperand(0)) == matrix) ||
                    (instruction->getOpcode() == Opc::STORE &&
                     instruction->getNumOperands() == 2 &&
                     rootGlobal(instruction->getOperand(1)) == matrix)) {
                    return true;
                }
                if (instruction->getOpcode() != Opc::CALL ||
                    std::find(
                        allowedCalls.begin(), allowedCalls.end(),
                        instruction.get()) != allowedCalls.end()) {
                    continue;
                }
                for (unsigned index = 1;
                     index < instruction->getNumOperands(); ++index) {
                    if (rootGlobal(instruction->getOperand(index)) ==
                        matrix) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool matrixIsValidLiveIn(
    IR::Module* module, IR::GlobalVariable* matrix,
    IR::Function* caller,
    const std::vector<IR::Instruction*>& allowedCalls,
    IR::BasicBlock* loopPreheader) {
    std::vector<IR::Instruction*> loads;
    std::vector<IR::Instruction*> stores;
    for (auto& function : module->getFunctions()) {
        for (auto& block : function->getBlocks()) {
            for (auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() == Opc::LOAD &&
                    instruction->getNumOperands() == 1 &&
                    rootGlobal(instruction->getOperand(0)) == matrix) {
                    loads.push_back(instruction.get());
                }
                if (instruction->getOpcode() == Opc::STORE &&
                    instruction->getNumOperands() == 2 &&
                    rootGlobal(instruction->getOperand(1)) == matrix) {
                    stores.push_back(instruction.get());
                }
                if (instruction->getOpcode() != Opc::CALL) continue;
                bool referencesMatrix = false;
                for (unsigned index = 1;
                     index < instruction->getNumOperands(); ++index) {
                    if (rootGlobal(instruction->getOperand(index)) ==
                        matrix) {
                        referencesMatrix = true;
                        break;
                    }
                }
                if (referencesMatrix &&
                    std::find(
                        allowedCalls.begin(), allowedCalls.end(),
                        instruction.get()) == allowedCalls.end()) {
                    return false;
                }
            }
        }
    }

    auto successors = buildSuccessors(caller);
    std::vector<IR::BasicBlock*> worklist = {loopPreheader};
    std::unordered_set<IR::BasicBlock*> reachable = {loopPreheader};
    while (!worklist.empty()) {
        auto* block = worklist.back();
        worklist.pop_back();
        for (auto* successor : successors[block]) {
            if (reachable.insert(successor).second) {
                worklist.push_back(successor);
            }
        }
    }
    std::vector<IR::Instruction*> directAccesses = loads;
    directAccesses.insert(
        directAccesses.end(), stores.begin(), stores.end());
    for (auto* access : directAccesses) {
        if (access->getParent()->getParent() != caller ||
            reachable.count(access->getParent())) {
            return false;
        }
    }
    return true;
}

IR::BasicBlock* findUniqueLoopPreheader(
    IR::Function* function, IR::BasicBlock* callBlock) {
    const NaturalLoop* containingLoop = nullptr;
    auto loops = findNaturalLoops(function);
    for (const auto& loop : loops) {
        if (!loop.body.count(callBlock)) continue;
        if (!containingLoop ||
            loop.body.size() < containingLoop->body.size()) {
            containingLoop = &loop;
        }
    }
    if (!containingLoop) return nullptr;

    auto predecessors = buildPredecessors(function);
    IR::BasicBlock* preheader = nullptr;
    for (auto* predecessor :
         predecessors[containingLoop->header]) {
        if (containingLoop->body.count(predecessor)) continue;
        if (preheader) return nullptr;
        preheader = predecessor;
    }
    return preheader;
}

IR::Value* callArgument(
    IR::Instruction* call, unsigned argumentIndex) {
    if (!call ||
        call->getOpcode() != Opc::CALL ||
        argumentIndex + 1 >= call->getNumOperands()) {
        return nullptr;
    }
    return call->getOperand(argumentIndex + 1);
}

bool matchCallChain(
    IR::Module* module, const AffineKernelSummary& kernel,
    const std::vector<IR::Instruction*>& calls,
    MatrixReductionPlan& plan) {
    if (calls.empty()) return false;
    auto* caller = calls[0]->getParent()->getParent();
    auto* callBlock = calls[0]->getParent();
    if (!caller || !callBlock) return false;
    std::unordered_set<IR::Instruction*> callSet;
    for (auto* call : calls) {
        if (call->getNumOperands() !=
                kernel.sourceFunction->getNumArgs() + 1 ||
            call->getParent()->getParent() != caller ||
            call->getParent() != callBlock) {
            return false;
        }
        callSet.insert(call);
    }

    std::vector<IR::Instruction*> orderedCalls;
    for (auto& instruction : callBlock->getInstructions()) {
        if (callSet.count(instruction.get())) {
            orderedCalls.push_back(instruction.get());
        }
    }
    if (orderedCalls.size() != calls.size()) return false;

    auto* size = callArgument(
        orderedCalls.front(),
        kernel.sizeArgumentIndex);
    auto* coefficientMatrix =
        rootGlobal(callArgument(
            orderedCalls.front(),
            kernel.scaleArgumentIndex));
    auto* seedMatrix =
        rootGlobal(callArgument(
            orderedCalls.front(),
            kernel.addendArgumentIndex));
    if (!size || !coefficientMatrix || !seedMatrix ||
        coefficientMatrix == seedMatrix) {
        return false;
    }

    auto* currentMatrix = seedMatrix;
    std::unordered_set<IR::GlobalVariable*> carrierMatrices = {
        seedMatrix};
    for (auto* call : orderedCalls) {
        auto* inputMatrix = rootGlobal(callArgument(
            call, kernel.addendArgumentIndex));
        auto* outputMatrix = rootGlobal(callArgument(
            call, kernel.destinationArgumentIndex));
        if (callArgument(
                call, kernel.sizeArgumentIndex) != size ||
            rootGlobal(callArgument(
                call, kernel.scaleArgumentIndex)) !=
                coefficientMatrix ||
            inputMatrix != currentMatrix ||
            !outputMatrix ||
            outputMatrix == inputMatrix ||
            outputMatrix == coefficientMatrix) {
            return false;
        }
        carrierMatrices.insert(outputMatrix);
        currentMatrix = outputMatrix;
    }
    auto* resultMatrix = currentMatrix;

    auto* loopPreheader =
        findUniqueLoopPreheader(caller, callBlock);
    IR::Instruction* finalLoad = nullptr;
    IR::Instruction* finalInnerCompare = nullptr;
    IR::Value* finalInnerInduction = nullptr;
    if (!loopPreheader ||
        !findFinalReduction(
            module, caller, resultMatrix, size,
            kernel.indexStart,
            kernel.indexStep,
            kernel.inclusiveUpperBound,
            orderedCalls, loopPreheader,
            finalLoad, finalInnerCompare,
            finalInnerInduction)) {
        return false;
    }

    for (auto* matrix : carrierMatrices) {
        if (matrix == resultMatrix) continue;
        if (matrix == seedMatrix) {
            if (!matrixIsValidLiveIn(
                    module, matrix, caller,
                    orderedCalls, loopPreheader)) {
                return false;
            }
        } else if (matrixHasUnexpectedAccess(
                       module, matrix, orderedCalls)) {
            return false;
        }
    }

    auto dominators = computeDominators(caller);
    auto* sizeInstruction =
        dynamic_cast<IR::Instruction*>(size);
    if (sizeInstruction &&
        !dominators[loopPreheader].count(
            sizeInstruction->getParent())) {
        return false;
    }

    plan.kernel = kernel;
    plan.caller = caller;
    plan.loopPreheader = loopPreheader;
    plan.finalInnerCompare = finalInnerCompare;
    plan.finalInnerInduction = finalInnerInduction;
    plan.calls = std::move(orderedCalls);
    plan.seedMatrix = seedMatrix;
    plan.resultMatrix = resultMatrix;
    plan.size = size;
    return true;
}

bool buildMatrixReductionPlan(
    IR::Module* module, MatrixReductionPlan& plan) {
    for (auto& function : module->getFunctions()) {
        if (function->getName() == "__opt_contract_row_sum" ||
            function->getName() == "__opt_affine_row_summary") {
            return false;
        }
    }

    std::vector<MatrixReductionPlan> plans;
    for (auto& function : module->getFunctions()) {
        AffineKernelSummary kernel;
        if (!matchKernelFunction(function.get(), kernel)) continue;

        auto calls = collectFunctionCalls(function.get());
        std::unordered_map<
            IR::BasicBlock*, std::vector<IR::Instruction*>>
            callsByBlock;
        for (auto* call : calls) {
            if (!call || !call->getParent()) continue;
            callsByBlock[call->getParent()].push_back(call);
        }
        for (const auto& [block, blockCalls] : callsByBlock) {
            (void)block;
            MatrixReductionPlan candidate;
            if (matchCallChain(
                    module, kernel, blockCalls, candidate)) {
                plans.push_back(std::move(candidate));
            }
        }
    }

    if (plans.size() != 1) return false;
    plan = std::move(plans.front());
    return true;
}

} // namespace

bool analyzeMatrixReductionPlan(
    IR::Module* module, MatrixReductionPlan& plan) {
    return buildMatrixReductionPlan(module, plan);
}

bool matrixReductionContraction(IR::Module* module) {
    MatrixReductionPlan plan;
    if (!analyzeMatrixReductionPlan(module, plan)) return false;
    return applyMatrixReductionPlan(module, plan);
}

} // namespace Opt
