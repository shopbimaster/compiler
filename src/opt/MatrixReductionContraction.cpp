#include "opt/Optimizer.h"
#include "opt/AffineRecurrenceAnalysis.h"
#include "opt/LoopPatternAnalysis.h"
#include "opt/MatrixReductionPlan.h"
#include "opt/MemoryAccessAnalysis.h"
#include "opt/ScalarReductionAnalysis.h"

#include <algorithm>
#include <limits>
#include <string>
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

struct CanonicalLoopCFG {
    IR::Instruction* compare = nullptr;
    IR::BasicBlock* bodyEntry = nullptr;
    IR::BasicBlock* exit = nullptr;
};

bool getCanonicalLoopCFG(
    IR::BasicBlock* header,
    const std::unordered_set<IR::BasicBlock*>& body,
    IR::Instruction* expectedCompare,
    CanonicalLoopCFG& result) {
    auto* terminator = header ? header->getTerminator() : nullptr;
    const bool validTerminator = terminator &&
        terminator->getOpcode() == Opc::COND_BR &&
        terminator->getNumOperands() == 3;
    auto* compare = validTerminator
        ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
        : nullptr;
    auto* bodyEntry = validTerminator
        ? dynamic_cast<IR::BasicBlock*>(terminator->getOperand(1))
        : nullptr;
    auto* exit = validTerminator
        ? dynamic_cast<IR::BasicBlock*>(terminator->getOperand(2))
        : nullptr;
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        (expectedCompare && compare != expectedCompare) ||
        compare->getNumUses() != 1 ||
        compare->getUses().front().user != terminator ||
        compare->getUses().front().operandNo != 0 ||
        !bodyEntry || !exit || !body.count(bodyEntry) ||
        body.count(exit)) {
        return false;
    }
    result.compare = compare;
    result.bodyEntry = bodyEntry;
    result.exit = exit;
    return true;
}

bool getCanonicalLoopCFG(
    const CanonicalCountedLoop& loop,
    CanonicalLoopCFG& result) {
    return getCanonicalLoopCFG(
        loop.header, loop.body, loop.compare, result);
}

bool hasOnlyCanonicalExit(
    IR::BasicBlock* header,
    const std::unordered_set<IR::BasicBlock*>& body,
    const CanonicalLoopCFG& cfg,
    const SuccMap& successors) {
    for (auto* block : body) {
        auto* terminator = block ? block->getTerminator() : nullptr;
        if (!terminator ||
            (terminator->getOpcode() != Opc::BR &&
             terminator->getOpcode() != Opc::COND_BR)) {
            return false;
        }
        auto found = successors.find(block);
        if (found == successors.end()) return false;
        for (auto* successor : found->second) {
            if (!body.count(successor) &&
                (block != header || successor != cfg.exit)) {
                return false;
            }
        }
    }
    return true;
}

bool hasExactContainingLoops(
    IR::Function* function,
    IR::BasicBlock* block,
    const std::vector<IR::BasicBlock*>& expectedHeaders) {
    std::unordered_set<IR::BasicBlock*> expected(
        expectedHeaders.begin(), expectedHeaders.end());
    if (expected.size() != expectedHeaders.size()) return false;

    std::unordered_set<IR::BasicBlock*> actual;
    for (const auto& loop : findNaturalLoops(function)) {
        if (loop.body.count(block)) actual.insert(loop.header);
    }
    return actual == expected;
}

bool isStrictlyNested(
    const CanonicalCountedLoop& outer,
    const CanonicalCountedLoop& inner) {
    return outer.header && inner.header &&
        outer.header != inner.header &&
        outer.body.count(inner.header) &&
        !inner.body.count(outer.header);
}

bool isWholeGlobalMatrixBase(
    IR::Value* value,
    IR::ArrayType* rowType,
    IR::GlobalVariable*& global) {
    PointerAccess access;
    if (!collectPointerAccess(value, nullptr, access)) return false;
    global = dynamic_cast<IR::GlobalVariable*>(access.root);
    if (!global) return false;
    auto* globalPointer = dynamic_cast<IR::PointerType*>(
        global->getType());
    auto* matrixType = globalPointer
        ? dynamic_cast<IR::ArrayType*>(
              globalPointer->getPointeeType())
        : nullptr;
    if (!rowType || !matrixType ||
        matrixType->getElementType() != rowType) {
        return false;
    }
    for (auto* index : access.indices) {
        if (!isConstant(index, 0)) return false;
    }
    return true;
}

bool instructionPrecedes(
    IR::Instruction* first, IR::Instruction* second) {
    if (!first || !second || first->getParent() != second->getParent()) {
        return false;
    }
    for (auto& owned : first->getParent()->getInstructions()) {
        if (owned.get() == first) return true;
        if (owned.get() == second) return false;
    }
    return false;
}

bool instructionDominates(
    IR::Instruction* definition,
    IR::Instruction* use,
    const DomMap& dominators) {
    if (!definition || !use) return false;
    if (definition->getParent() == use->getParent()) {
        return instructionPrecedes(definition, use);
    }
    auto found = dominators.find(use->getParent());
    return found != dominators.end() &&
        found->second.count(definition->getParent());
}

bool hasClosedAccumulatorUses(
    const ScalarReduction& reduction,
    const CanonicalCountedLoop& outer,
    IR::BasicBlock* outerExit,
    const DomMap& dominators) {
    if (!reduction.accumulatorAddress ||
        !reduction.accumulatorLoad ||
        reduction.accumulatorLoad->getNumUses() != 1 ||
        reduction.accumulatorLoad->getUses().front().user !=
            reduction.update) {
        return false;
    }

    for (const auto& use :
         reduction.accumulatorAddress->getUses()) {
        auto* instruction =
            dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) return false;
        if ((instruction == reduction.initializationStore ||
             instruction == reduction.updateStore) &&
            instruction->getOpcode() == Opc::STORE &&
            use.operandNo == 1) {
            continue;
        }
        if (instruction == reduction.accumulatorLoad &&
            instruction->getOpcode() == Opc::LOAD &&
            use.operandNo == 0) {
            continue;
        }
        auto found = dominators.find(instruction->getParent());
        if (instruction->getOpcode() != Opc::LOAD ||
            use.operandNo != 0 ||
            outer.body.count(instruction->getParent()) ||
            found == dominators.end() ||
            !found->second.count(outerExit)) {
            return false;
        }
    }
    return true;
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
            } else {
                auto* local = dynamic_cast<IR::Instruction*>(
                    instruction->getOperand(1));
                if (!local || local->getOpcode() != Opc::ALLOCA) {
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
        initialOutput.root != destinationArgument ||
        initialOutput.indices.size() != 2 ||
        initialOutput.indices[0] == initialOutput.indices[1]) {
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

    auto dominators = computeDominators(function);
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

    if (!isStrictlyNested(initialLoopI, initialLoopJ) ||
        !isStrictlyNested(loopK, loopI) ||
        !isStrictlyNested(loopI, loopJ) ||
        !hasExactContainingLoops(
            function, initialStore->getParent(),
            {initialLoopI.header, initialLoopJ.header}) ||
        !hasExactContainingLoops(
            function, updateStore->getParent(),
            {loopK.header, loopI.header, loopJ.header})) {
        return false;
    }
    std::unordered_set<IR::BasicBlock*> loopHeaders = {
        initialLoopI.header, initialLoopJ.header,
        loopK.header, loopI.header, loopJ.header};
    if (loopHeaders.size() != 5) return false;
    const auto kernelLoops = findNaturalLoops(function);
    if (kernelLoops.size() != loopHeaders.size()) return false;
    for (const auto& loop : kernelLoops) {
        if (!loopHeaders.count(loop.header)) return false;
    }

    CanonicalLoopCFG initialICfg;
    CanonicalLoopCFG initialJCfg;
    CanonicalLoopCFG kCfg;
    CanonicalLoopCFG iCfg;
    CanonicalLoopCFG jCfg;
    auto successors = buildSuccessors(function);
    if (!getCanonicalLoopCFG(initialLoopI, initialICfg) ||
        !getCanonicalLoopCFG(initialLoopJ, initialJCfg) ||
        !getCanonicalLoopCFG(loopK, kCfg) ||
        !getCanonicalLoopCFG(loopI, iCfg) ||
        !getCanonicalLoopCFG(loopJ, jCfg) ||
        !hasOnlyCanonicalExit(
            initialLoopI.header, initialLoopI.body,
            initialICfg, successors) ||
        !hasOnlyCanonicalExit(
            initialLoopJ.header, initialLoopJ.body,
            initialJCfg, successors) ||
        !hasOnlyCanonicalExit(
            loopK.header, loopK.body, kCfg, successors) ||
        !hasOnlyCanonicalExit(
            loopI.header, loopI.body, iCfg, successors) ||
        !hasOnlyCanonicalExit(
            loopJ.header, loopJ.body, jCfg, successors)) {
        return false;
    }

    auto postDominators = computePostDominators(function);
    auto predecessors = buildPredecessors(function);
    if (!postDominators[function->getEntryBlock()].count(
            initialLoopI.header) ||
        !postDominators[initialICfg.exit].count(loopK.header) ||
        !postDominators[initialICfg.bodyEntry].count(
            initialLoopJ.header) ||
        !postDominators[initialJCfg.bodyEntry].count(
            initialStore->getParent()) ||
        !dominators[loopK.header].count(initialLoopI.header) ||
        !dominators[loopK.header].count(initialICfg.exit) ||
        !postDominators[kCfg.bodyEntry].count(loopI.header) ||
        !postDominators[jCfg.bodyEntry].count(
            updateStore->getParent())) {
        return false;
    }

    for (auto* predecessor : predecessors[initialICfg.exit]) {
        if (predecessor != initialLoopI.header &&
            !initialLoopI.body.count(predecessor)) {
            return false;
        }
    }
    IR::Instruction* onlyReturn = nullptr;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::RET) continue;
            if (onlyReturn) return false;
            onlyReturn = instruction.get();
        }
    }
    if (!onlyReturn ||
        !dominators[onlyReturn->getParent()].count(loopK.header)) {
        return false;
    }

    IR::ConstantInt* skippedCoefficient = nullptr;
    unsigned guardCount = 0;
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* compare = owned.get();
            if (compare->getOpcode() != Opc::ICMP ||
                (compare->getName() != "eq" &&
                 compare->getName() != "ne") ||
                compare->getNumOperands() != 2 ||
                !loopK.body.count(compare->getParent()) ||
                !loopI.body.count(compare->getParent()) ||
                loopJ.body.count(compare->getParent()) ||
                !postDominators[iCfg.bodyEntry].count(
                    compare->getParent())) {
                continue;
            }

            IR::Instruction* comparedLoad = nullptr;
            IR::ConstantInt* skipValue = nullptr;
            for (unsigned index = 0; index < 2; ++index) {
                auto* candidate = dynamic_cast<IR::Instruction*>(
                    compare->getOperand(index));
                auto* constant = dynamic_cast<IR::ConstantInt*>(
                    compare->getOperand(1 - index));
                if (candidate &&
                    candidate->getOpcode() == Opc::LOAD &&
                    constant) {
                    comparedLoad = candidate;
                    skipValue = constant;
                }
            }
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

            auto* terminator = compare->getParent()->getTerminator();
            if (!terminator ||
                terminator->getOpcode() != Opc::COND_BR ||
                terminator->getNumOperands() != 3 ||
                terminator->getOperand(0) != compare ||
                compare->getNumUses() != 1 ||
                compare->getUses().front().user != terminator ||
                compare->getUses().front().operandNo != 0) {
                continue;
            }
            auto* trueTarget = dynamic_cast<IR::BasicBlock*>(
                terminator->getOperand(1));
            auto* falseTarget = dynamic_cast<IR::BasicBlock*>(
                terminator->getOperand(2));
            const bool trueExecutes = trueTarget &&
                dominators[loopJ.header].count(trueTarget);
            const bool falseExecutes = falseTarget &&
                dominators[loopJ.header].count(falseTarget);
            auto* executeTarget = trueExecutes != falseExecutes
                ? (trueExecutes ? trueTarget : falseTarget)
                : nullptr;
            auto* skipTarget = trueExecutes != falseExecutes
                ? (trueExecutes ? falseTarget : trueTarget)
                : nullptr;
            const bool unequalExecutes =
                (compare->getName() == "eq" && falseExecutes) ||
                (compare->getName() == "ne" && trueExecutes);
            if (!executeTarget || !skipTarget || !unequalExecutes ||
                !postDominators[executeTarget].count(loopJ.header) ||
                !postDominators[skipTarget].count(loopI.header)) {
                continue;
            }

            std::vector<IR::BasicBlock*> worklist = {skipTarget};
            std::unordered_set<IR::BasicBlock*> visited;
            bool skipCanEnterUpdateLoop = false;
            while (!worklist.empty()) {
                auto* current = worklist.back();
                worklist.pop_back();
                if (current == loopI.header) continue;
                if (current == loopJ.header) {
                    skipCanEnterUpdateLoop = true;
                    break;
                }
                if (!visited.insert(current).second) continue;
                for (auto* successor : successors[current]) {
                    worklist.push_back(successor);
                }
            }
            if (skipCanEnterUpdateLoop) continue;

            ++guardCount;
            skippedCoefficient = skipValue;
        }
    }
    if (guardCount > 1 ||
        (guardCount == 0 &&
         !postDominators[iCfg.bodyEntry].count(loopJ.header))) {
        return false;
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
    IR::Function* caller,
    IR::GlobalVariable* matrix,
    IR::Value* size,
    int64_t expectedStart,
    int64_t expectedStep,
    bool expectedInclusiveUpperBound,
    const std::vector<IR::Instruction*>& allowedCalls,
    IR::BasicBlock* loopPreheader,
    IR::Instruction*& finalLoad,
    IR::BasicBlock*& reductionPreheader,
    IR::BasicBlock*& reductionExit,
    IR::Instruction*& reductionInitialization) {
    GlobalMemoryEffects effects;
    if (!analyzeGlobalMemoryEffects(matrix, effects)) return false;
    const auto& loads = effects.loads;
    const auto& stores = effects.stores;
    const auto& callsWithMatrix = effects.calls;

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
    CanonicalLoopCFG outerCfg;
    CanonicalLoopCFG innerCfg;
    if (!isStrictlyNested(outer, inner) ||
        !hasExactContainingLoops(
            caller, loads[0]->getParent(),
            {outer.header, inner.header}) ||
        !hasExactContainingLoops(
            caller, reduction.updateStore->getParent(),
            {outer.header, inner.header}) ||
        !getCanonicalLoopCFG(outer, outerCfg) ||
        !getCanonicalLoopCFG(inner, innerCfg) ||
        !hasOnlyCanonicalExit(
            outer.header, outer.body, outerCfg, successors) ||
        !hasOnlyCanonicalExit(
            inner.header, inner.body, innerCfg, successors)) {
        return false;
    }
    for (const auto& loop : findNaturalLoops(caller)) {
        if (outer.body.count(loop.header) &&
            loop.header != outer.header &&
            loop.header != inner.header) {
            return false;
        }
    }
    auto postDominators = computePostDominators(caller);
    if (!postDominators[outerCfg.bodyEntry].count(inner.header) ||
        !postDominators[innerCfg.bodyEntry].count(
            reduction.updateStore->getParent())) {
        return false;
    }
    auto* outerExit = outerCfg.exit;

    auto predecessors = buildPredecessors(caller);
    IR::BasicBlock* outerPreheader = nullptr;
    for (auto* predecessor : predecessors[outer.header]) {
        if (outer.body.count(predecessor)) continue;
        if (outerPreheader) return false;
        outerPreheader = predecessor;
    }
    auto* preheaderTerminator = outerPreheader
        ? outerPreheader->getTerminator()
        : nullptr;
    if (!preheaderTerminator ||
        preheaderTerminator->getOpcode() != Opc::BR ||
        preheaderTerminator->getNumOperands() != 1 ||
        preheaderTerminator->getOperand(0) != outer.header ||
        reduction.initializationStore->getParent() !=
            outerPreheader) {
        return false;
    }
    auto dominators = computeDominators(caller);
    if (!hasClosedAccumulatorUses(
            reduction, outer, outerExit, dominators)) {
        return false;
    }

    // The complete outer loop is bypassed after its scalar result is replaced
    // by an explicit summary reduction.  Reject any observable operation or
    // SSA value escaping that loop rather than relying on its current shape.
    for (auto* block : outer.body) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() == Opc::CALL ||
                (instruction->getOpcode() == Opc::STORE &&
                 instruction != reduction.updateStore)) {
                return false;
            }
            for (const auto& use : instruction->getUses()) {
                auto* user =
                    dynamic_cast<IR::Instruction*>(use.user);
                if (!user || !outer.body.count(user->getParent())) {
                    return false;
                }
            }
        }
    }
    for (auto& instruction : outerExit->getInstructions()) {
        if (instruction->getOpcode() == Opc::PHI) return false;
    }

    finalLoad = loads[0];
    reductionPreheader = outerPreheader;
    reductionExit = outerExit;
    reductionInitialization = reduction.initializationStore;
    return true;
}

bool matrixHasUnexpectedAccess(
    IR::GlobalVariable* matrix,
    const std::vector<IR::Instruction*>& allowedCalls) {
    GlobalMemoryEffects effects;
    if (!analyzeGlobalMemoryEffects(matrix, effects) ||
        !effects.loads.empty() || !effects.stores.empty()) {
        return true;
    }
    for (auto* call : effects.calls) {
        if (std::find(
                allowedCalls.begin(), allowedCalls.end(), call) ==
            allowedCalls.end()) {
            return true;
        }
    }
    return false;
}

bool matrixIsValidLiveIn(
    IR::GlobalVariable* matrix,
    IR::Function* caller,
    const std::vector<IR::Instruction*>& allowedCalls,
    IR::BasicBlock* loopPreheader) {
    GlobalMemoryEffects effects;
    if (!analyzeGlobalMemoryEffects(matrix, effects)) return false;
    for (auto* call : effects.calls) {
        if (std::find(
                allowedCalls.begin(), allowedCalls.end(), call) ==
            allowedCalls.end()) {
            return false;
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
    std::vector<IR::Instruction*> directAccesses = effects.loads;
    directAccesses.insert(
        directAccesses.end(),
        effects.stores.begin(), effects.stores.end());
    for (auto* access : directAccesses) {
        if (access->getParent()->getParent() != caller ||
            reachable.count(access->getParent())) {
            return false;
        }
    }
    return true;
}

bool matrixHasUnexpectedCallUse(
    IR::GlobalVariable* matrix,
    const std::vector<IR::Instruction*>& allowedCalls) {
    GlobalMemoryEffects effects;
    if (!analyzeGlobalMemoryEffects(matrix, effects)) return true;
    for (auto* call : effects.calls) {
        if (std::find(
                allowedCalls.begin(), allowedCalls.end(), call) ==
            allowedCalls.end()) {
            return true;
        }
    }
    return false;
}

struct ProvenCallLoop {
    NaturalLoop loop;
    CanonicalLoopCFG cfg;
    IR::BasicBlock* preheader = nullptr;
};

bool compareIsTrueForInitialValue(
    IR::Instruction* compare,
    IR::Value* induction,
    IR::ConstantInt* start) {
    if (!compare || compare->getNumOperands() != 2 || !start) {
        return false;
    }
    auto* lhs = compare->getOperand(0) == induction
        ? start
        : dynamic_cast<IR::ConstantInt*>(compare->getOperand(0));
    auto* rhs = compare->getOperand(1) == induction
        ? start
        : dynamic_cast<IR::ConstantInt*>(compare->getOperand(1));
    if (!lhs || !rhs) return false;
    if (compare->getName() == "slt") {
        return lhs->getValue() < rhs->getValue();
    }
    if (compare->getName() == "sle") {
        return lhs->getValue() <= rhs->getValue();
    }
    if (compare->getName() == "sgt") {
        return lhs->getValue() > rhs->getValue();
    }
    if (compare->getName() == "sge") {
        return lhs->getValue() >= rhs->getValue();
    }
    return false;
}

bool proveAllocaCallInduction(
    IR::Function* function,
    const NaturalLoop& loop,
    const CanonicalLoopCFG& cfg,
    IR::Instruction* inductionLoad,
    const DomMap& dominators,
    const DomMap& postDominators) {
    (void)function;
    if (!inductionLoad ||
        inductionLoad->getOpcode() != Opc::LOAD ||
        inductionLoad->getNumOperands() != 1 ||
        inductionLoad->getParent() != loop.header) {
        return false;
    }
    auto* address = dynamic_cast<IR::Instruction*>(
        inductionLoad->getOperand(0));
    if (!address || address->getOpcode() != Opc::ALLOCA) {
        return false;
    }

    std::vector<IR::Instruction*> loads;
    std::vector<IR::Instruction*> stores;
    for (const auto& use : address->getUses()) {
        auto* instruction =
            dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) return false;
        if (instruction->getOpcode() == Opc::LOAD &&
            use.operandNo == 0) {
            loads.push_back(instruction);
        } else if (instruction->getOpcode() == Opc::STORE &&
                   use.operandNo == 1) {
            stores.push_back(instruction);
        } else {
            return false;
        }
    }
    if (loads.empty() || loads.size() > 2 || stores.size() != 2) {
        return false;
    }

    IR::Instruction* initialStore = nullptr;
    IR::Instruction* updateStore = nullptr;
    for (auto* store : stores) {
        if (loop.body.count(store->getParent())) {
            if (updateStore) return false;
            updateStore = store;
        } else {
            if (initialStore) return false;
            initialStore = store;
        }
    }
    auto* start = initialStore
        ? dynamic_cast<IR::ConstantInt*>(initialStore->getOperand(0))
        : nullptr;
    auto* update = updateStore
        ? dynamic_cast<IR::Instruction*>(updateStore->getOperand(0))
        : nullptr;
    auto postDomFound = postDominators.find(cfg.bodyEntry);
    if (!start || !update || update->getOpcode() != Opc::ADD ||
        update->getNumOperands() != 2 || update->getNumUses() != 1 ||
        update->getUses().front().user != updateStore ||
        !instructionDominates(initialStore, cfg.compare, dominators) ||
        postDomFound == postDominators.end() ||
        !postDomFound->second.count(updateStore->getParent())) {
        return false;
    }

    IR::Instruction* updateLoad = nullptr;
    IR::ConstantInt* step = nullptr;
    for (unsigned index = 0; index < 2; ++index) {
        auto* load = dynamic_cast<IR::Instruction*>(
            update->getOperand(index));
        auto* constant = dynamic_cast<IR::ConstantInt*>(
            update->getOperand(1 - index));
        if (load && load->getOpcode() == Opc::LOAD &&
            load->getNumOperands() == 1 &&
            load->getOperand(0) == address && constant) {
            updateLoad = load;
            step = constant;
        }
    }
    if (!updateLoad || !step || step->getValue() <= 0 ||
        updateLoad->getNumUses() != 1 ||
        updateLoad->getUses().front().user != update) {
        return false;
    }
    for (auto* load : loads) {
        if (load != inductionLoad && load != updateLoad) return false;
    }
    if (inductionLoad != updateLoad &&
        (inductionLoad->getNumUses() != 1 ||
         inductionLoad->getUses().front().user != cfg.compare)) {
        return false;
    }
    return compareIsTrueForInitialValue(
        cfg.compare, inductionLoad, start);
}

bool provePositiveCallLoop(
    IR::Function* function,
    IR::BasicBlock* callBlock,
    ProvenCallLoop& result) {
    const NaturalLoop* containingLoop = nullptr;
    auto loops = findNaturalLoops(function);
    for (const auto& loop : loops) {
        if (!loop.body.count(callBlock)) continue;
        if (containingLoop) return false;
        containingLoop = &loop;
    }
    if (!containingLoop ||
        !getCanonicalLoopCFG(
            containingLoop->header, containingLoop->body,
            nullptr, result.cfg)) {
        return false;
    }
    auto successors = buildSuccessors(function);
    if (!hasOnlyCanonicalExit(
            containingLoop->header, containingLoop->body,
            result.cfg, successors)) {
        return false;
    }
    for (const auto& loop : loops) {
        if (containingLoop->body.count(loop.header) &&
            loop.header != containingLoop->header) {
            return false;
        }
    }

    auto predecessors = buildPredecessors(function);
    IR::BasicBlock* preheader = nullptr;
    for (auto* predecessor : predecessors[containingLoop->header]) {
        if (containingLoop->body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* preheaderTerminator = preheader
        ? preheader->getTerminator()
        : nullptr;
    if (!preheaderTerminator ||
        preheaderTerminator->getOpcode() != Opc::BR ||
        preheaderTerminator->getNumOperands() != 1 ||
        preheaderTerminator->getOperand(0) != containingLoop->header) {
        return false;
    }

    auto postDominators = computePostDominators(function);
    if (!postDominators[result.cfg.bodyEntry].count(callBlock)) {
        return false;
    }
    auto dominators = computeDominators(function);
    IR::Value* induction = nullptr;
    IR::Value* bound = nullptr;
    for (unsigned index = 0; index < 2; ++index) {
        auto* candidate = dynamic_cast<IR::Instruction*>(
            result.cfg.compare->getOperand(index));
        auto* constant = dynamic_cast<IR::ConstantInt*>(
            result.cfg.compare->getOperand(1 - index));
        if (candidate && constant) {
            induction = candidate;
            bound = constant;
        }
    }
    if (!induction || !bound) return false;

    bool positiveTripCount = false;
    if (auto* phi = dynamic_cast<IR::Instruction*>(induction);
        phi && phi->getOpcode() == Opc::PHI) {
        CanonicalCountedLoop canonical;
        IR::ConstantInt* start = nullptr;
        if (analyzeCanonicalCountedLoop(
                function, phi, bound, callBlock, canonical) &&
            canonical.header == containingLoop->header) {
            start = dynamic_cast<IR::ConstantInt*>(canonical.start);
            positiveTripCount = compareIsTrueForInitialValue(
                result.cfg.compare, phi, start);
        }
    } else {
        positiveTripCount = proveAllocaCallInduction(
            function, *containingLoop, result.cfg,
            dynamic_cast<IR::Instruction*>(induction),
            dominators, postDominators);
    }
    if (!positiveTripCount) return false;

    result.loop = *containingLoop;
    result.preheader = preheader;
    return true;
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
    const AffineKernelSummary& kernel,
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
    IR::GlobalVariable* coefficientMatrix = nullptr;
    IR::GlobalVariable* seedMatrix = nullptr;
    if (!size ||
        !isWholeGlobalMatrixBase(
            callArgument(
                orderedCalls.front(),
                kernel.scaleArgumentIndex),
            kernel.rowType,
            coefficientMatrix) ||
        !isWholeGlobalMatrixBase(
            callArgument(
                orderedCalls.front(),
                kernel.addendArgumentIndex),
            kernel.rowType,
            seedMatrix) ||
        seedMatrix->isConstant() ||
        coefficientMatrix == seedMatrix) {
        return false;
    }

    auto* currentMatrix = seedMatrix;
    std::unordered_set<IR::GlobalVariable*> carrierMatrices = {
        seedMatrix};
    for (auto* call : orderedCalls) {
        IR::GlobalVariable* callCoefficientMatrix = nullptr;
        IR::GlobalVariable* inputMatrix = nullptr;
        IR::GlobalVariable* outputMatrix = nullptr;
        if (callArgument(
                call, kernel.sizeArgumentIndex) != size ||
            !isWholeGlobalMatrixBase(
                callArgument(call, kernel.scaleArgumentIndex),
                kernel.rowType,
                callCoefficientMatrix) ||
            !isWholeGlobalMatrixBase(
                callArgument(call, kernel.addendArgumentIndex),
                kernel.rowType,
                inputMatrix) ||
            !isWholeGlobalMatrixBase(
                callArgument(call, kernel.destinationArgumentIndex),
                kernel.rowType,
                outputMatrix) ||
            callCoefficientMatrix != coefficientMatrix ||
            inputMatrix != currentMatrix ||
            outputMatrix->isConstant() ||
            outputMatrix == inputMatrix ||
            outputMatrix == coefficientMatrix) {
            return false;
        }
        carrierMatrices.insert(outputMatrix);
        currentMatrix = outputMatrix;
    }
    auto* resultMatrix = currentMatrix;

    ProvenCallLoop callLoop;
    if (!provePositiveCallLoop(caller, callBlock, callLoop)) {
        return false;
    }
    for (auto* block : callLoop.loop.body) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL &&
                !callSet.count(instruction.get())) {
                return false;
            }
        }
    }
    auto* loopPreheader = callLoop.preheader;
    IR::Instruction* finalLoad = nullptr;
    IR::BasicBlock* finalReductionPreheader = nullptr;
    IR::BasicBlock* finalReductionExit = nullptr;
    IR::Instruction* finalReductionInitialization = nullptr;
    if (!loopPreheader ||
        !findFinalReduction(
            caller, resultMatrix, size,
            kernel.indexStart,
            kernel.indexStep,
            kernel.inclusiveUpperBound,
            orderedCalls, loopPreheader,
            finalLoad, finalReductionPreheader,
            finalReductionExit,
            finalReductionInitialization)) {
        return false;
    }

    for (auto* matrix : carrierMatrices) {
        if (matrix == resultMatrix) continue;
        if (matrix == seedMatrix) {
            if (!matrixIsValidLiveIn(
                    matrix, caller,
                    orderedCalls, loopPreheader)) {
                return false;
            }
        } else if (matrixHasUnexpectedAccess(
                       matrix, orderedCalls)) {
            return false;
        }
    }
    if (matrixHasUnexpectedCallUse(
            coefficientMatrix, orderedCalls)) {
        return false;
    }

    auto dominators = computeDominators(caller);
    auto* sizeInstruction =
        dynamic_cast<IR::Instruction*>(size);
    if ((sizeInstruction &&
         !dominators[loopPreheader].count(
             sizeInstruction->getParent())) ||
        !dominators[finalReductionPreheader].count(
            loopPreheader) ||
        !dominators[finalReductionPreheader].count(
            callLoop.cfg.exit)) {
        return false;
    }

    plan.kernel = kernel;
    plan.caller = caller;
    plan.loopPreheader = loopPreheader;
    plan.finalReductionPreheader = finalReductionPreheader;
    plan.finalReductionExit = finalReductionExit;
    plan.finalReductionInitialization =
        finalReductionInitialization;
    plan.calls = std::move(orderedCalls);
    plan.seedMatrix = seedMatrix;
    plan.resultMatrix = resultMatrix;
    plan.size = size;
    return true;
}

bool buildMatrixReductionPlan(
    IR::Module* module, MatrixReductionPlan& plan) {
    const auto isGeneratedHelperName = [](const std::string& name) {
        return name == "__opt_contract_row_sum" ||
            name == "__opt_affine_row_summary" ||
            name == "__opt_reduction_summary_total";
    };
    for (auto& function : module->getFunctions()) {
        if (isGeneratedHelperName(function->getName())) {
            return false;
        }
    }
    for (auto& global : module->getGlobals()) {
        if (isGeneratedHelperName(global->getName())) return false;
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
                    kernel, blockCalls, candidate)) {
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
