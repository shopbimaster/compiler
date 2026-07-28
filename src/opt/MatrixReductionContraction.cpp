#include "opt/Optimizer.h"
#include "opt/AffineRecurrenceAnalysis.h"
#include "opt/LoopPatternAnalysis.h"
#include "opt/MatrixReductionPlan.h"
#include "opt/MemoryAccessAnalysis.h"
#include "opt/ScalarReductionAnalysis.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;
bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool matchKernelFunction(
    IR::Function* function, AffineKernelSummary& summary) {
    if (!function || function->isExternal() ||
        function->getNumArgs() != 4) {
        return false;
    }

    auto* functionType = function->getFunctionType();
    const auto& parameters = functionType->getParamTypes();
    if (!functionType->getReturnType()->isVoid() ||
        parameters.size() != 4 ||
        parameters[0] != IR::IntegerType::I32 ||
        parameters[1] != parameters[2] ||
        parameters[1] != parameters[3]) {
        return false;
    }

    auto* rowPointer =
        dynamic_cast<IR::PointerType*>(parameters[1]);
    auto* rowType = rowPointer
        ? dynamic_cast<IR::ArrayType*>(
              rowPointer->getPointeeType())
        : nullptr;
    if (!rowType ||
        rowType->getElementType() != IR::IntegerType::I32 ||
        rowType->getNumElements() < 2) {
        return false;
    }
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL) return false;
        }
    }

    auto argumentMap = buildAllocaArgumentMap(function);
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
                if (access.root == function->getArg(3)) {
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

    IR::Instruction* zeroStore = nullptr;
    IR::Instruction* updateStore = nullptr;
    for (auto* store : outputStores) {
        if (isConstant(store->getOperand(0), 0)) {
            zeroStore = store;
        } else {
            updateStore = store;
        }
    }
    if (!zeroStore || !updateStore) return false;

    PointerAccess zeroOutput;
    if (!collectPointerAccess(
            zeroStore->getOperand(1), &argumentMap, zeroOutput) ||
        zeroOutput.indices.size() != 2) {
        return false;
    }

    AffineRecurrence recurrence;
    if (!analyzeAffineRecurrence(
            updateStore, &argumentMap, recurrence)) {
        return false;
    }
    const auto& updatedOutput = recurrence.destination;
    if (updatedOutput.indices.size() != 2 ||
        recurrence.previous.root != function->getArg(3) ||
        recurrence.previous.indices != updatedOutput.indices) {
        return false;
    }

    const auto& coefficient = recurrence.scale;
    const auto& input = recurrence.addend;
    if (coefficient.root != function->getArg(1) ||
        input.root != function->getArg(2) ||
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
                compare->getName() != "eq" ||
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
                comparedAccess.root != function->getArg(1) ||
                comparedAccess.indices != coefficient.indices) {
                continue;
            }
            auto* terminator = compare->getParent()
                ? compare->getParent()->getTerminator()
                : nullptr;
            auto* falseTarget = terminator &&
                                        terminator->getOpcode() ==
                                            Opc::COND_BR
                ? dynamic_cast<IR::BasicBlock*>(
                      terminator->getOperand(2))
                : nullptr;
            if (falseTarget &&
                dominators[updateStore->getParent()].count(falseTarget)) {
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
    CanonicalCountedLoop zeroLoopI;
    CanonicalCountedLoop zeroLoopJ;
    auto* size = function->getArg(0);
    if (!analyzeCanonicalCountedLoop(
            function, indexI, size,
            updateStore->getParent(), loopI) ||
        !analyzeCanonicalCountedLoop(
            function, indexJ, size,
            updateStore->getParent(), loopJ) ||
        !analyzeCanonicalCountedLoop(
            function, indexK, size,
            updateStore->getParent(), loopK) ||
        !analyzeCanonicalCountedLoop(
            function, zeroOutput.indices[0], size,
            zeroStore->getParent(), zeroLoopI) ||
        !analyzeCanonicalCountedLoop(
            function, zeroOutput.indices[1], size,
            zeroStore->getParent(), zeroLoopJ)) {
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
    summary.skippedScale = skippedCoefficient;
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
    const std::vector<IR::Instruction*>& allowedCalls,
    IR::BasicBlock* loopPreheader,
    IR::Instruction*& finalLoad,
    IR::Instruction*& innerCompare) {
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

    for (auto* block : inner.body) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL ||
                (instruction->getOpcode() == Opc::STORE &&
                 instruction.get() != reduction.updateStore)) {
                return false;
            }
        }
    }

    finalLoad = loads[0];
    innerCompare = inner.compare;
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
        if (call->getNumOperands() != 5 ||
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

    auto* size = orderedCalls.front()->getOperand(1);
    auto* coefficientMatrix =
        rootGlobal(orderedCalls.front()->getOperand(2));
    auto* seedMatrix =
        rootGlobal(orderedCalls.front()->getOperand(3));
    if (!coefficientMatrix || !seedMatrix ||
        coefficientMatrix == seedMatrix) {
        return false;
    }

    auto* currentMatrix = seedMatrix;
    std::unordered_set<IR::GlobalVariable*> carrierMatrices = {
        seedMatrix};
    for (auto* call : orderedCalls) {
        auto* inputMatrix = rootGlobal(call->getOperand(3));
        auto* outputMatrix = rootGlobal(call->getOperand(4));
        if (call->getOperand(1) != size ||
            rootGlobal(call->getOperand(2)) != coefficientMatrix ||
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
    if (!loopPreheader ||
        !findFinalReduction(
            module, caller, resultMatrix, size,
            orderedCalls, loopPreheader,
            finalLoad, finalInnerCompare)) {
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

IR::Instruction* makePhi(
    IR::Type* type, const std::string& name,
    IR::Value* firstValue, IR::BasicBlock* firstBlock,
    IR::Value* secondValue, IR::BasicBlock* secondBlock) {
    auto* phi = IR::Instruction::createPhi(type, name, 4);
    phi->addOperand(firstValue);
    phi->addOperand(firstBlock);
    phi->addOperand(secondValue);
    phi->addOperand(secondBlock);
    return phi;
}

IR::Function* createAffineRowSummaryFunction(
    IR::Module* module, const AffineKernelSummary& summary) {
    auto* function = module->createFunction(
        summary.sourceFunction->getFunctionType(),
        "__opt_affine_row_summary", false);
    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);

    auto* entry = function->createBlock("entry");
    auto* iHeader = function->createBlock("summary.i.cond");
    auto* iBody = function->createBlock("summary.i.body");
    auto* kHeader = function->createBlock("summary.k.cond");
    auto* kBody = function->createBlock("summary.k.body");
    auto* iLatch = function->createBlock("summary.i.latch");
    auto* exit = function->createBlock("summary.exit");

    auto* iNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "summary.i.next", nullptr, one);
    auto* kNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "summary.k.next", nullptr, one);
    auto* indexI = makePhi(
        i32, "summary.i", zero, entry, iNext, iLatch);
    auto* indexK = makePhi(
        i32, "summary.k", zero, iBody, kNext, kBody);
    auto* accumulation =
        IR::Instruction::createPhi(i32, "summary.acc", 4);
    accumulation->addOperand(zero);
    accumulation->addOperand(iBody);
    accumulation->addOperand(nullptr);
    accumulation->addOperand(kBody);
    iNext->setOperand(0, indexI);
    kNext->setOperand(0, indexK);

    entry->pushBack(IR::Instruction::createBr(iHeader));
    iHeader->pushBack(indexI);
    auto* iCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexI, function->getArg(0), "slt");
    iHeader->pushBack(iCompare);
    iHeader->pushBack(
        IR::Instruction::createCondBr(iCompare, iBody, exit));

    auto* rowA = IR::Instruction::createGetElementPtr(
        summary.rowType, function->getArg(1),
        {indexI}, "summary.A.row");
    auto* outputRow = IR::Instruction::createGetElementPtr(
        summary.rowType, function->getArg(3),
        {indexI}, "summary.C.row");
    iBody->pushBack(rowA);
    iBody->pushBack(outputRow);
    iBody->pushBack(IR::Instruction::createBr(kHeader));

    kHeader->pushBack(indexK);
    kHeader->pushBack(accumulation);
    auto* kCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexK, function->getArg(0), "slt");
    kHeader->pushBack(kCompare);
    kHeader->pushBack(
        IR::Instruction::createCondBr(
            kCompare, kBody, iLatch));

    auto* coefficientAddress =
        IR::Instruction::createGetElementPtr(
            i32, rowA, {zero, indexK},
            "summary.A.element");
    auto* coefficient = IR::Instruction::createLoad(
        i32, coefficientAddress, "summary.coefficient");
    auto* inputRow = IR::Instruction::createGetElementPtr(
        summary.rowType, function->getArg(2),
        {indexK}, "summary.B.row");
    auto* inputAddress =
        IR::Instruction::createGetElementPtr(
            i32, inputRow, {zero, zero},
            "summary.B.sum.addr");
    auto* inputSum = IR::Instruction::createLoad(
        i32, inputAddress, "summary.B.sum");
    auto* product = IR::Instruction::createBinOp(
        Opc::MUL, i32, "summary.product",
        accumulation, coefficient);
    auto* updated = IR::Instruction::createBinOp(
        Opc::ADD, i32, "summary.updated",
        product, inputSum);
    IR::Instruction* skip = nullptr;
    IR::Instruction* nextAccumulation = updated;
    if (summary.skippedScale) {
        auto* skippedCoefficient = IR::ConstantInt::get(
            i32, summary.skippedScale->getValue());
        skip = IR::Instruction::createCmp(
            Opc::ICMP, coefficient, skippedCoefficient, "eq");
        nextAccumulation = IR::Instruction::createSelect(
            skip, accumulation, updated, "summary.acc.next");
    }
    accumulation->setOperand(2, nextAccumulation);

    kBody->pushBack(coefficientAddress);
    kBody->pushBack(coefficient);
    kBody->pushBack(inputRow);
    kBody->pushBack(inputAddress);
    kBody->pushBack(inputSum);
    kBody->pushBack(product);
    kBody->pushBack(updated);
    if (skip) {
        kBody->pushBack(skip);
        kBody->pushBack(nextAccumulation);
    }
    kBody->pushBack(kNext);
    kBody->pushBack(IR::Instruction::createBr(kHeader));

    auto* outputAddress =
        IR::Instruction::createGetElementPtr(
            i32, outputRow, {zero, zero},
            "summary.C.sum.addr");
    iLatch->pushBack(outputAddress);
    iLatch->pushBack(
        IR::Instruction::createStore(
            accumulation, outputAddress));
    iLatch->pushBack(iNext);
    iLatch->pushBack(IR::Instruction::createBr(iHeader));
    exit->pushBack(IR::Instruction::createRet(nullptr));
    return function;
}

IR::Function* createRowSummaryFunction(
    IR::Module* module, IR::ArrayType* rowType) {
    auto* i32 = IR::IntegerType::I32;
    auto* rowPointer = IR::PointerType::get(rowType);
    auto* type = IR::FunctionType::get(
        IR::VoidType::get(), {i32, rowPointer});
    auto* function = module->createFunction(
        type, "__opt_contract_row_sum", false);

    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* entry = function->createBlock("entry");
    auto* iHeader = function->createBlock("rows.i.cond");
    auto* iBody = function->createBlock("rows.i.body");
    auto* jHeader = function->createBlock("rows.j.cond");
    auto* jBody = function->createBlock("rows.j.body");
    auto* iLatch = function->createBlock("rows.i.latch");
    auto* exit = function->createBlock("rows.exit");

    auto* iNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rows.i.next", nullptr, one);
    auto* jNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rows.j.next", nullptr, one);
    auto* sumNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rows.sum.next", nullptr, nullptr);
    auto* indexI = makePhi(
        i32, "rows.i", zero, entry, iNext, iLatch);
    auto* indexJ = makePhi(
        i32, "rows.j", zero, iBody, jNext, jBody);
    auto* sum = makePhi(
        i32, "rows.sum", zero, iBody, sumNext, jBody);
    iNext->setOperand(0, indexI);
    jNext->setOperand(0, indexJ);

    entry->pushBack(IR::Instruction::createBr(iHeader));
    iHeader->pushBack(indexI);
    auto* iCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexI, function->getArg(0), "slt");
    iHeader->pushBack(iCompare);
    iHeader->pushBack(
        IR::Instruction::createCondBr(iCompare, iBody, exit));

    auto* row = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1),
        {indexI}, "rows.row");
    iBody->pushBack(row);
    iBody->pushBack(IR::Instruction::createBr(jHeader));

    jHeader->pushBack(indexJ);
    jHeader->pushBack(sum);
    auto* jCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexJ, function->getArg(0), "slt");
    jHeader->pushBack(jCompare);
    jHeader->pushBack(
        IR::Instruction::createCondBr(
            jCompare, jBody, iLatch));

    auto* elementAddress =
        IR::Instruction::createGetElementPtr(
            i32, row, {zero, indexJ}, "rows.element.addr");
    auto* element = IR::Instruction::createLoad(
        i32, elementAddress, "rows.element");
    sumNext->setOperand(0, sum);
    sumNext->setOperand(1, element);
    jBody->pushBack(elementAddress);
    jBody->pushBack(element);
    jBody->pushBack(sumNext);
    jBody->pushBack(jNext);
    jBody->pushBack(IR::Instruction::createBr(jHeader));

    auto* outputAddress =
        IR::Instruction::createGetElementPtr(
            i32, row, {zero, zero}, "rows.output.addr");
    iLatch->pushBack(outputAddress);
    iLatch->pushBack(
        IR::Instruction::createStore(sum, outputAddress));
    iLatch->pushBack(iNext);
    iLatch->pushBack(IR::Instruction::createBr(iHeader));
    exit->pushBack(IR::Instruction::createRet(nullptr));
    return function;
}

bool applyContraction(IR::Module* module,
                      const MatrixReductionPlan& plan) {
    auto* summaryFunction =
        createRowSummaryFunction(module, plan.kernel.rowType);
    auto* summaryKernel =
        createAffineRowSummaryFunction(module, plan.kernel);
    auto* zero =
        IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto* matrixType = dynamic_cast<IR::PointerType*>(
        plan.seedMatrix->getType());
    if (!matrixType) return false;
    auto* matrixBase =
        IR::Instruction::createGetElementPtr(
            matrixType->getPointeeType(), plan.seedMatrix,
            {zero, zero}, "contract.B.base");
    auto* summaryCall = IR::Instruction::createCall(
        summaryFunction->getFunctionType(), summaryFunction,
        {plan.size, matrixBase}, "");

    auto* terminator = plan.loopPreheader->getTerminator();
    if (!terminator) return false;
    for (auto iterator = plan.loopPreheader->begin();
         iterator != plan.loopPreheader->end(); ++iterator) {
        if (iterator->get() != terminator) continue;
        iterator =
            plan.loopPreheader->insert(iterator, matrixBase);
        ++iterator;
        plan.loopPreheader->insert(iterator, summaryCall);
        for (auto* call : plan.calls) {
            call->setOperand(0, summaryKernel);
        }
        plan.finalInnerCompare->setOperand(
            1, IR::ConstantInt::get(IR::IntegerType::I32, 1));
        return true;
    }
    return false;
}

} // namespace

bool analyzeMatrixReductionPlan(
    IR::Module* module, MatrixReductionPlan& plan) {
    return buildMatrixReductionPlan(module, plan);
}

bool matrixReductionContraction(IR::Module* module) {
    MatrixReductionPlan plan;
    if (!analyzeMatrixReductionPlan(module, plan)) return false;
    return applyContraction(module, plan);
}

} // namespace Opt
