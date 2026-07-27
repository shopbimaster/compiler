#include "opt/Optimizer.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;
using AllocaArgumentMap =
    std::unordered_map<IR::Value*, IR::Argument*>;

struct PointerAccess {
    IR::Value* root = nullptr;
    std::vector<IR::Value*> indices;
};

struct CanonicalLoop {
    IR::Instruction* compare = nullptr;
    std::unordered_set<IR::BasicBlock*> body;
};

struct KernelMatch {
    IR::Function* function = nullptr;
    IR::ArrayType* rowType = nullptr;
};

struct ProgramMatch {
    KernelMatch kernel;
    IR::Function* caller = nullptr;
    IR::BasicBlock* loopPreheader = nullptr;
    IR::Instruction* finalInnerCompare = nullptr;
    std::vector<IR::Instruction*> calls;
    IR::GlobalVariable* matrixB = nullptr;
    IR::Value* size = nullptr;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isAddOneOf(IR::Instruction* instruction, IR::Value* value) {
    if (!instruction || instruction->getOpcode() != Opc::ADD ||
        instruction->getNumOperands() != 2) {
        return false;
    }
    return (instruction->getOperand(0) == value &&
            isConstant(instruction->getOperand(1), 1)) ||
           (instruction->getOperand(1) == value &&
            isConstant(instruction->getOperand(0), 1));
}

AllocaArgumentMap buildAllocaArgumentMap(IR::Function* function) {
    AllocaArgumentMap result;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            auto* argument =
                dynamic_cast<IR::Argument*>(instruction->getOperand(0));
            auto* alloca =
                dynamic_cast<IR::Instruction*>(instruction->getOperand(1));
            if (argument && alloca &&
                alloca->getOpcode() == Opc::ALLOCA) {
                result[alloca] = argument;
            }
        }
    }
    return result;
}

bool collectPointerAccessImpl(
    IR::Value* value,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access,
    std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return false;

    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        access.root = argument;
        visiting.erase(value);
        return true;
    }
    if (auto* global = dynamic_cast<IR::GlobalVariable*>(value)) {
        access.root = global;
        visiting.erase(value);
        return true;
    }

    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction) {
        visiting.erase(value);
        return false;
    }

    if (instruction->getOpcode() == Opc::LOAD && argumentMap &&
        instruction->getNumOperands() == 1) {
        auto found = argumentMap->find(instruction->getOperand(0));
        if (found != argumentMap->end()) {
            access.root = found->second;
            visiting.erase(value);
            return true;
        }
    }

    if (instruction->getOpcode() != Opc::GETELEMENTPTR ||
        instruction->getNumOperands() < 2 ||
        !collectPointerAccessImpl(
            instruction->getOperand(0), argumentMap,
            access, visiting)) {
        visiting.erase(value);
        return false;
    }

    for (unsigned index = 1;
         index < instruction->getNumOperands(); ++index) {
        auto* operand = instruction->getOperand(index);
        if (isConstant(operand, 0) &&
            index + 1 < instruction->getNumOperands()) {
            continue;
        }
        access.indices.push_back(operand);
    }
    visiting.erase(value);
    return true;
}

bool collectPointerAccess(
    IR::Value* value,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access) {
    std::unordered_set<IR::Value*> visiting;
    return collectPointerAccessImpl(
        value, argumentMap, access, visiting);
}

IR::GlobalVariable* rootGlobal(IR::Value* value) {
    PointerAccess access;
    if (!collectPointerAccess(value, nullptr, access)) return nullptr;
    return dynamic_cast<IR::GlobalVariable*>(access.root);
}

bool matchCanonicalLoop(IR::Function* function,
                        IR::Value* induction,
                        IR::Value* bound,
                        IR::BasicBlock* containedBlock,
                        CanonicalLoop& match) {
    auto* phi = dynamic_cast<IR::Instruction*>(induction);
    if (!phi || phi->getOpcode() != Opc::PHI ||
        phi->getNumOperands() < 4 ||
        phi->getNumOperands() % 2 != 0) {
        return false;
    }

    auto* header = phi->getParent();
    auto* terminator = header ? header->getTerminator() : nullptr;
    if (!terminator || terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }

    auto* compare =
        dynamic_cast<IR::Instruction*>(terminator->getOperand(0));
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getName() != "slt" ||
        compare->getNumOperands() != 2 ||
        compare->getOperand(0) != phi ||
        compare->getOperand(1) != bound) {
        return false;
    }

    bool hasZero = false;
    bool hasStep = false;
    for (unsigned index = 0;
         index < phi->getNumOperands(); index += 2) {
        auto* incoming = phi->getOperand(index);
        if (isConstant(incoming, 0)) {
            hasZero = true;
            continue;
        }
        auto* add = dynamic_cast<IR::Instruction*>(incoming);
        if (!isAddOneOf(add, phi)) return false;
        hasStep = true;
    }
    if (!hasZero || !hasStep) return false;

    bool contains = false;
    for (auto& loop : findNaturalLoops(function)) {
        if (loop.header == header &&
            loop.body.count(containedBlock)) {
            contains = true;
            match.body = loop.body;
            break;
        }
    }
    if (!contains) return false;

    match.compare = compare;
    return true;
}

bool matchKernelFunction(IR::Function* function, KernelMatch& match) {
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
    PointerAccess updatedOutput;
    if (!collectPointerAccess(
            zeroStore->getOperand(1), &argumentMap, zeroOutput) ||
        !collectPointerAccess(
            updateStore->getOperand(1), &argumentMap, updatedOutput) ||
        zeroOutput.indices.size() != 2 ||
        updatedOutput.indices.size() != 2) {
        return false;
    }

    auto* add =
        dynamic_cast<IR::Instruction*>(updateStore->getOperand(0));
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2) {
        return false;
    }

    IR::Instruction* multiply = nullptr;
    IR::Instruction* inputLoad = nullptr;
    for (unsigned index = 0; index < 2; ++index) {
        auto* operand =
            dynamic_cast<IR::Instruction*>(add->getOperand(index));
        if (!operand) return false;
        if (operand->getOpcode() == Opc::MUL) multiply = operand;
        if (operand->getOpcode() == Opc::LOAD) inputLoad = operand;
    }
    if (!multiply || !inputLoad ||
        multiply->getNumOperands() != 2 ||
        inputLoad->getNumOperands() != 1) {
        return false;
    }

    IR::Instruction* oldOutputLoad = nullptr;
    IR::Instruction* coefficientLoad = nullptr;
    for (unsigned index = 0; index < 2; ++index) {
        auto* load = dynamic_cast<IR::Instruction*>(
            multiply->getOperand(index));
        if (!load || load->getOpcode() != Opc::LOAD ||
            load->getNumOperands() != 1) {
            return false;
        }
        PointerAccess access;
        if (!collectPointerAccess(
                load->getOperand(0), &argumentMap, access)) {
            return false;
        }
        if (access.root == function->getArg(3)) {
            oldOutputLoad = load;
        } else if (access.root == function->getArg(1)) {
            coefficientLoad = load;
        }
    }
    PointerAccess oldOutput;
    if (!oldOutputLoad || !coefficientLoad ||
        !collectPointerAccess(
            oldOutputLoad->getOperand(0),
            &argumentMap, oldOutput) ||
        oldOutput.root != function->getArg(3) ||
        oldOutput.indices != updatedOutput.indices) {
        return false;
    }

    PointerAccess coefficient;
    PointerAccess input;
    if (!collectPointerAccess(
            coefficientLoad->getOperand(0),
            &argumentMap, coefficient) ||
        !collectPointerAccess(
            inputLoad->getOperand(0), &argumentMap, input) ||
        coefficient.root != function->getArg(1) ||
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

    bool hasSkipCompare = false;
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
            if (!comparedLoad || !isConstant(other, 1) ||
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
                hasSkipCompare = true;
                break;
            }
        }
        if (hasSkipCompare) break;
    }
    if (!hasSkipCompare) return false;

    CanonicalLoop loopI;
    CanonicalLoop loopJ;
    CanonicalLoop loopK;
    CanonicalLoop zeroLoopI;
    CanonicalLoop zeroLoopJ;
    auto* size = function->getArg(0);
    if (!matchCanonicalLoop(
            function, indexI, size,
            updateStore->getParent(), loopI) ||
        !matchCanonicalLoop(
            function, indexJ, size,
            updateStore->getParent(), loopJ) ||
        !matchCanonicalLoop(
            function, indexK, size,
            updateStore->getParent(), loopK) ||
        !matchCanonicalLoop(
            function, zeroOutput.indices[0], size,
            zeroStore->getParent(), zeroLoopI) ||
        !matchCanonicalLoop(
            function, zeroOutput.indices[1], size,
            zeroStore->getParent(), zeroLoopJ)) {
        return false;
    }

    match.function = function;
    match.rowType = rowType;
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
    IR::Function* kernel,
    IR::GlobalVariable* matrixB,
    IR::Value* size,
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
                    rootGlobal(instruction->getOperand(0)) == matrixB) {
                    loads.push_back(instruction.get());
                }
                if (instruction->getOpcode() == Opc::STORE &&
                    instruction->getNumOperands() == 2 &&
                    rootGlobal(instruction->getOperand(1)) == matrixB) {
                    stores.push_back(instruction.get());
                }
                if (instruction->getOpcode() == Opc::CALL) {
                    for (unsigned index = 1;
                         index < instruction->getNumOperands(); ++index) {
                        if (rootGlobal(instruction->getOperand(index)) ==
                            matrixB) {
                            callsWithMatrix.push_back(instruction.get());
                            break;
                        }
                    }
                }
            }
        }
    }

    if (loads.size() != 1 || stores.size() != 1 ||
        callsWithMatrix.size() != 2 ||
        loads[0]->getParent()->getParent() != caller ||
        stores[0]->getParent()->getParent() != caller) {
        return false;
    }
    for (auto* call : callsWithMatrix) {
        if (call->getOperand(0) != kernel) return false;
    }

    PointerAccess access;
    if (!collectPointerAccess(
            loads[0]->getOperand(0), nullptr, access) ||
        access.root != matrixB || access.indices.size() != 2) {
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

    auto* accumulatorLoad =
        dynamic_cast<IR::Instruction*>(
            add->getOperand(0) == loads[0]
                ? add->getOperand(1)
                : add->getOperand(0));
    auto* accumulatorStore =
        dynamic_cast<IR::Instruction*>(
            add->getUses().front().user);
    auto* accumulatorAddress = accumulatorLoad &&
                                       accumulatorLoad->getOpcode() ==
                                           Opc::LOAD &&
                                       accumulatorLoad->getNumOperands() == 1
        ? dynamic_cast<IR::Instruction*>(
              accumulatorLoad->getOperand(0))
        : nullptr;
    if (!accumulatorStore ||
        accumulatorStore->getOpcode() != Opc::STORE ||
        accumulatorStore->getNumOperands() != 2 ||
        accumulatorStore->getOperand(0) != add ||
        !accumulatorAddress ||
        accumulatorAddress->getOpcode() != Opc::ALLOCA ||
        accumulatorStore->getOperand(1) != accumulatorAddress) {
        return false;
    }

    CanonicalLoop outer;
    CanonicalLoop inner;
    if (!matchCanonicalLoop(
            caller, access.indices[0], size,
            loads[0]->getParent(), outer) ||
        !matchCanonicalLoop(
            caller, access.indices[1], size,
            loads[0]->getParent(), inner)) {
        return false;
    }

    unsigned accumulatorStores = 0;
    bool hasZeroInitialization = false;
    for (auto& block : caller->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::STORE &&
                instruction->getNumOperands() == 2 &&
                instruction->getOperand(1) == accumulatorAddress) {
                ++accumulatorStores;
                if (isConstant(instruction->getOperand(0), 0)) {
                    hasZeroInitialization = true;
                }
            }
        }
    }
    if (accumulatorStores != 2 || !hasZeroInitialization) {
        return false;
    }

    for (auto* block : inner.body) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL ||
                (instruction->getOpcode() == Opc::STORE &&
                 instruction.get() != accumulatorStore)) {
                return false;
            }
        }
    }

    finalLoad = loads[0];
    innerCompare = inner.compare;
    return true;
}

bool comesBeforeInBlock(IR::Instruction* first,
                        IR::Instruction* second) {
    if (!first || !second ||
        first->getParent() != second->getParent()) {
        return false;
    }
    for (auto& instruction :
         first->getParent()->getInstructions()) {
        if (instruction.get() == first) return true;
        if (instruction.get() == second) return false;
    }
    return false;
}

bool matrixHasUnexpectedAccess(
    IR::Module* module, IR::GlobalVariable* matrix,
    IR::Instruction* firstCall, IR::Instruction* secondCall) {
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
                    instruction.get() == firstCall ||
                    instruction.get() == secondCall) {
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

bool matchProgram(IR::Module* module, ProgramMatch& match) {
    std::vector<KernelMatch> kernels;
    for (auto& function : module->getFunctions()) {
        if (function->getName() == "__opt_contract_row_sum" ||
            function->getName() == "__opt_affine_row_summary") {
            return false;
        }
        KernelMatch candidate;
        if (matchKernelFunction(function.get(), candidate)) {
            kernels.push_back(candidate);
        }
    }
    if (kernels.size() != 1) return false;

    auto calls = collectFunctionCalls(kernels[0].function);
    if (calls.size() != 2 ||
        calls[0]->getNumOperands() != 5 ||
        calls[1]->getNumOperands() != 5) {
        return false;
    }

    auto* caller = calls[0]->getParent()->getParent();
    if (!caller || calls[1]->getParent()->getParent() != caller ||
        calls[0]->getParent() != calls[1]->getParent()) {
        return false;
    }

    for (auto* first : calls) {
        auto* second = first == calls[0] ? calls[1] : calls[0];
        if (first->getOperand(1) != second->getOperand(1)) continue;

        auto* matrixA = rootGlobal(first->getOperand(2));
        auto* matrixB = rootGlobal(first->getOperand(3));
        auto* matrixC = rootGlobal(first->getOperand(4));
        if (!matrixA || !matrixB || !matrixC ||
            matrixA == matrixB || matrixA == matrixC ||
            matrixB == matrixC ||
            rootGlobal(second->getOperand(2)) != matrixA ||
            rootGlobal(second->getOperand(3)) != matrixC ||
            rootGlobal(second->getOperand(4)) != matrixB) {
            continue;
        }
        if (!comesBeforeInBlock(first, second) ||
            matrixHasUnexpectedAccess(
                module, matrixC, first, second)) {
            continue;
        }

        IR::Instruction* finalLoad = nullptr;
        IR::Instruction* finalInnerCompare = nullptr;
        auto* loopPreheader =
            findUniqueLoopPreheader(caller, first->getParent());
        if (!loopPreheader ||
            !findFinalReduction(
                module, caller, kernels[0].function,
                matrixB, first->getOperand(1),
                finalLoad, finalInnerCompare)) {
            continue;
        }

        auto dominators = computeDominators(caller);
        auto* sizeInstruction =
            dynamic_cast<IR::Instruction*>(first->getOperand(1));
        if (sizeInstruction &&
            !dominators[loopPreheader].count(
                sizeInstruction->getParent())) {
            continue;
        }

        match.kernel = kernels[0];
        match.caller = caller;
        match.loopPreheader = loopPreheader;
        match.finalInnerCompare = finalInnerCompare;
        match.calls = calls;
        match.matrixB = matrixB;
        match.size = first->getOperand(1);
        return true;
    }
    return false;
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
    IR::Module* module, const KernelMatch& match) {
    auto* function = module->createFunction(
        match.function->getFunctionType(),
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
    auto* nextAccumulation = IR::Instruction::createSelect(
        zero, accumulation, accumulation, "summary.acc.next");
    accumulation->setOperand(2, nextAccumulation);
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
        match.rowType, function->getArg(1),
        {indexI}, "summary.A.row");
    auto* outputRow = IR::Instruction::createGetElementPtr(
        match.rowType, function->getArg(3),
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
        match.rowType, function->getArg(2),
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
    auto* skip = IR::Instruction::createCmp(
        Opc::ICMP, coefficient, one, "eq");
    nextAccumulation->setOperand(0, skip);
    nextAccumulation->setOperand(1, accumulation);
    nextAccumulation->setOperand(2, updated);

    kBody->pushBack(coefficientAddress);
    kBody->pushBack(coefficient);
    kBody->pushBack(inputRow);
    kBody->pushBack(inputAddress);
    kBody->pushBack(inputSum);
    kBody->pushBack(product);
    kBody->pushBack(updated);
    kBody->pushBack(skip);
    kBody->pushBack(nextAccumulation);
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
                      const ProgramMatch& match) {
    auto* summaryFunction =
        createRowSummaryFunction(module, match.kernel.rowType);
    auto* summaryKernel =
        createAffineRowSummaryFunction(module, match.kernel);
    auto* zero =
        IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto* matrixType = dynamic_cast<IR::PointerType*>(
        match.matrixB->getType());
    if (!matrixType) return false;
    auto* matrixBase =
        IR::Instruction::createGetElementPtr(
            matrixType->getPointeeType(), match.matrixB,
            {zero, zero}, "contract.B.base");
    auto* summaryCall = IR::Instruction::createCall(
        summaryFunction->getFunctionType(), summaryFunction,
        {match.size, matrixBase}, "");

    auto* terminator = match.loopPreheader->getTerminator();
    if (!terminator) return false;
    for (auto iterator = match.loopPreheader->begin();
         iterator != match.loopPreheader->end(); ++iterator) {
        if (iterator->get() != terminator) continue;
        iterator =
            match.loopPreheader->insert(iterator, matrixBase);
        ++iterator;
        match.loopPreheader->insert(iterator, summaryCall);
        for (auto* call : match.calls) {
            call->setOperand(0, summaryKernel);
        }
        match.finalInnerCompare->setOperand(
            1, IR::ConstantInt::get(IR::IntegerType::I32, 1));
        return true;
    }
    return false;
}

} // namespace

bool matrixReductionContraction(IR::Module* module) {
    ProgramMatch match;
    if (!matchProgram(module, match)) return false;
    return applyContraction(module, match);
}

} // namespace Opt
