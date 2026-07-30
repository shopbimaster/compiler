#include "opt/Optimizer.h"

#include <cstdint>
#include <string>
#include <unordered_set>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// A matched recursion returns either depth + remainingSteps or a fixed
// terminal value. Cache that relation by state alone: positive entries encode
// remainingSteps + 1, -1 encodes the fixed terminal, and zero means uncomputed.
constexpr unsigned kAffineMemoCapacity = 1u << 19;

struct AffineRecursionPattern {
    IR::Function* function = nullptr;
    IR::GlobalVariable* bound = nullptr;
    IR::Value* stateStorage = nullptr;
    IR::Value* depthStorage = nullptr;
    IR::Instruction* rootCall = nullptr;
    int32_t terminalValue = 0;
};

IR::Instruction* asInstruction(IR::Value* value, Opc opcode) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    return instruction && instruction->getOpcode() == opcode
        ? instruction
        : nullptr;
}

IR::ConstantInt* asConstant(IR::Value* value) {
    return dynamic_cast<IR::ConstantInt*>(value);
}

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = asConstant(value);
    return constant && constant->getValue() == expected;
}

bool isLoadFrom(IR::Value* value, IR::Value* storage) {
    auto* load = asInstruction(value, Opc::LOAD);
    return load && load->getNumOperands() == 1 &&
           load->getOperand(0) == storage;
}

bool matchCommutativeConstant(
        IR::Value* value, Opc opcode, IR::Value* storage,
        int64_t constant) {
    auto* instruction = asInstruction(value, opcode);
    if (!instruction || instruction->getNumOperands() != 2) return false;
    return (isLoadFrom(instruction->getOperand(0), storage) &&
            isConstant(instruction->getOperand(1), constant)) ||
           (isLoadFrom(instruction->getOperand(1), storage) &&
            isConstant(instruction->getOperand(0), constant));
}

bool matchScaledSuccessor(
        IR::Value* value, IR::Value* stateStorage,
        int64_t multiplier) {
    auto* add = asInstruction(value, Opc::ADD);
    if (!add || add->getNumOperands() != 2) return false;
    IR::Value* product = nullptr;
    if (isConstant(add->getOperand(0), 1)) {
        product = add->getOperand(1);
    } else if (isConstant(add->getOperand(1), 1)) {
        product = add->getOperand(0);
    }
    return product && matchCommutativeConstant(
        product, Opc::MUL, stateStorage, multiplier);
}

bool matchHalvedSuccessor(IR::Value* value, IR::Value* stateStorage) {
    auto* divide = asInstruction(value, Opc::SDIV);
    return divide && divide->getNumOperands() == 2 &&
           isLoadFrom(divide->getOperand(0), stateStorage) &&
           isConstant(divide->getOperand(1), 2);
}

bool matchDepthIncrement(IR::Value* value, IR::Value* depthStorage) {
    return matchCommutativeConstant(value, Opc::ADD, depthStorage, 1);
}

IR::BasicBlock* resolveForwarders(IR::BasicBlock* block) {
    std::unordered_set<IR::BasicBlock*> visited;
    while (block && visited.insert(block).second && block->size() == 1) {
        auto* terminator = block->getTerminator();
        if (!terminator || terminator->getOpcode() != Opc::BR ||
            terminator->getNumOperands() != 1) {
            break;
        }
        block = dynamic_cast<IR::BasicBlock*>(terminator->getOperand(0));
    }
    return block;
}

bool matchReturnLoad(IR::BasicBlock* block, IR::Value* storage) {
    block = resolveForwarders(block);
    auto* terminator = block ? block->getTerminator() : nullptr;
    return terminator && terminator->getOpcode() == Opc::RET &&
           terminator->getNumOperands() == 1 &&
           isLoadFrom(terminator->getOperand(0), storage);
}

bool matchReturnConstant(IR::BasicBlock* block, int32_t& value) {
    block = resolveForwarders(block);
    auto* terminator = block ? block->getTerminator() : nullptr;
    auto* constant = terminator && terminator->getOpcode() == Opc::RET &&
                             terminator->getNumOperands() == 1
        ? asConstant(terminator->getOperand(0))
        : nullptr;
    if (!constant || constant->getValue() < INT32_MIN ||
        constant->getValue() > INT32_MAX) {
        return false;
    }
    value = static_cast<int32_t>(constant->getValue());
    return true;
}

enum class SuccessorKind { Half, TriplePlusOne, QuadruplePlusOne };

IR::Instruction* matchRecursiveReturn(
        IR::BasicBlock* block, IR::Function* function,
        IR::Value* stateStorage, IR::Value* depthStorage,
        SuccessorKind kind) {
    block = resolveForwarders(block);
    auto* terminator = block ? block->getTerminator() : nullptr;
    if (!terminator || terminator->getOpcode() != Opc::RET ||
        terminator->getNumOperands() != 1) {
        return nullptr;
    }
    auto* call = asInstruction(terminator->getOperand(0), Opc::CALL);
    if (!call || call->getNumOperands() != 3 ||
        call->getOperand(0) != function ||
        !matchDepthIncrement(call->getOperand(2), depthStorage)) {
        return nullptr;
    }
    bool successorMatches = false;
    switch (kind) {
    case SuccessorKind::Half:
        successorMatches =
            matchHalvedSuccessor(call->getOperand(1), stateStorage);
        break;
    case SuccessorKind::TriplePlusOne:
        successorMatches = matchScaledSuccessor(
            call->getOperand(1), stateStorage, 3);
        break;
    case SuccessorKind::QuadruplePlusOne:
        successorMatches = matchScaledSuccessor(
            call->getOperand(1), stateStorage, 4);
        break;
    }
    return successorMatches ? call : nullptr;
}

bool matchEqualityWithConstant(
        IR::Value* value, IR::Value* storage, int64_t constant) {
    auto* compare = asInstruction(value, Opc::ICMP);
    if (!compare || compare->getName() != "eq" ||
        compare->getNumOperands() != 2) {
        return false;
    }
    return (isLoadFrom(compare->getOperand(0), storage) &&
            isConstant(compare->getOperand(1), constant)) ||
           (isLoadFrom(compare->getOperand(1), storage) &&
            isConstant(compare->getOperand(0), constant));
}

bool matchEvenCondition(IR::Value* value, IR::Value* stateStorage) {
    auto* compare = asInstruction(value, Opc::ICMP);
    if (!compare || compare->getName() != "eq" ||
        compare->getNumOperands() != 2) {
        return false;
    }
    IR::Value* remainderValue = nullptr;
    if (isConstant(compare->getOperand(0), 0)) {
        remainderValue = compare->getOperand(1);
    } else if (isConstant(compare->getOperand(1), 0)) {
        remainderValue = compare->getOperand(0);
    }
    auto* remainder = asInstruction(remainderValue, Opc::SREM);
    return remainder && remainder->getNumOperands() == 2 &&
           isLoadFrom(remainder->getOperand(0), stateStorage) &&
           isConstant(remainder->getOperand(1), 2);
}

bool matchBoundCondition(
        IR::Value* value, IR::Value* stateStorage, int64_t multiplier,
        IR::GlobalVariable*& bound) {
    auto* compare = asInstruction(value, Opc::ICMP);
    if (!compare || compare->getName() != "sle" ||
        compare->getNumOperands() != 2 ||
        !matchScaledSuccessor(
            compare->getOperand(0), stateStorage, multiplier)) {
        return false;
    }
    auto* load = asInstruction(compare->getOperand(1), Opc::LOAD);
    auto* candidate = load && load->getNumOperands() == 1
        ? dynamic_cast<IR::GlobalVariable*>(load->getOperand(0))
        : nullptr;
    auto* pointerType = candidate
        ? dynamic_cast<IR::PointerType*>(candidate->getType())
        : nullptr;
    if (!pointerType ||
        pointerType->getPointeeType() != IR::IntegerType::I32) {
        return false;
    }
    if (bound && bound != candidate) return false;
    bound = candidate;
    return true;
}

bool findArgumentStorage(
        IR::Function* function, unsigned argumentIndex,
        IR::Value*& storage) {
    storage = nullptr;
    auto* entry = function->getEntryBlock();
    if (!entry) return false;
    for (auto& owned : entry->getInstructions()) {
        auto* store = owned.get();
        if (store->getOpcode() != Opc::STORE ||
            store->getNumOperands() != 2 ||
            store->getOperand(0) != function->getArg(argumentIndex)) {
            continue;
        }
        auto* alloca = asInstruction(store->getOperand(1), Opc::ALLOCA);
        if (!alloca || alloca->getParent() != entry || storage) return false;
        storage = alloca;
    }
    return storage != nullptr;
}

bool validateFunctionBody(
        const AffineRecursionPattern& pattern,
        const std::unordered_set<IR::Instruction*>& expectedCalls) {
    unsigned stores = 0;
    unsigned returns = 0;
    std::unordered_set<IR::Instruction*> actualCalls;
    for (auto& block : pattern.function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            switch (instruction->getOpcode()) {
            case Opc::STORE:
                if (instruction->getNumOperands() != 2 ||
                    (instruction->getOperand(1) != pattern.stateStorage &&
                     instruction->getOperand(1) != pattern.depthStorage)) {
                    return false;
                }
                ++stores;
                break;
            case Opc::LOAD:
                if (instruction->getNumOperands() != 1 ||
                    (instruction->getOperand(0) != pattern.stateStorage &&
                     instruction->getOperand(0) != pattern.depthStorage &&
                     instruction->getOperand(0) != pattern.bound)) {
                    return false;
                }
                break;
            case Opc::CALL:
                if (instruction->getNumOperands() == 0 ||
                    instruction->getOperand(0) != pattern.function) {
                    return false;
                }
                actualCalls.insert(instruction);
                break;
            case Opc::RET:
                ++returns;
                break;
            case Opc::GETELEMENTPTR:
            case Opc::PHI:
                return false;
            default:
                break;
            }
        }
    }
    return stores == 2 && returns == 5 && actualCalls == expectedCalls;
}

bool storePrecedesCall(IR::Instruction* store, IR::Instruction* call) {
    if (!store || !call || !store->getParent() || !call->getParent() ||
        store->getParent()->getParent() != call->getParent()->getParent()) {
        return false;
    }
    auto* caller = call->getParent()->getParent();
    auto dominators = computeDominators(caller);
    auto found = dominators.find(call->getParent());
    if (found == dominators.end() ||
        !found->second.count(store->getParent())) {
        return false;
    }
    if (store->getParent() != call->getParent()) return true;
    for (auto& instruction : store->getParent()->getInstructions()) {
        if (instruction.get() == store) return true;
        if (instruction.get() == call) return false;
    }
    return false;
}

bool validateStableBound(AffineRecursionPattern& pattern) {
    IR::Instruction* boundStore = nullptr;
    for (const auto& use : pattern.bound->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) return false;
        if (instruction->getOpcode() == Opc::LOAD && use.operandNo == 0) {
            continue;
        }
        if (instruction->getOpcode() == Opc::STORE && use.operandNo == 1 &&
            !boundStore) {
            boundStore = instruction;
            continue;
        }
        return false;
    }
    if (!boundStore) return false;

    IR::Instruction* externalCall = nullptr;
    for (const auto& use : pattern.function->getUses()) {
        auto* call = dynamic_cast<IR::Instruction*>(use.user);
        if (!call || call->getOpcode() != Opc::CALL || use.operandNo != 0) {
            return false;
        }
        if (call->getParent()->getParent() == pattern.function) continue;
        if (externalCall) return false;
        externalCall = call;
    }
    if (!externalCall || !storePrecedesCall(boundStore, externalCall)) {
        return false;
    }
    auto* root = externalCall->getParent()->getParent();
    if (!root->getUses().empty() ||
        boundStore->getParent() != root->getEntryBlock()) {
        return false;
    }
    for (const auto& loop : findNaturalLoops(root)) {
        if (loop.body.count(boundStore->getParent())) return false;
    }
    pattern.rootCall = externalCall;
    return true;
}

bool matchAffineRecursion(
        IR::Function* function, AffineRecursionPattern& pattern) {
    if (!function || function->isExternal() || function->getNumArgs() != 2 ||
        function->getFunctionType()->getReturnType() != IR::IntegerType::I32) {
        return false;
    }
    const auto& parameters = function->getFunctionType()->getParamTypes();
    if (parameters.size() != 2 || parameters[0] != IR::IntegerType::I32 ||
        parameters[1] != IR::IntegerType::I32) {
        return false;
    }

    pattern.function = function;
    if (!findArgumentStorage(function, 0, pattern.stateStorage) ||
        !findArgumentStorage(function, 1, pattern.depthStorage) ||
        pattern.stateStorage == pattern.depthStorage) {
        return false;
    }

    auto* entryTerminator = function->getEntryBlock()->getTerminator();
    if (!entryTerminator || entryTerminator->getOpcode() != Opc::COND_BR ||
        entryTerminator->getNumOperands() != 3 ||
        !matchEqualityWithConstant(
            entryTerminator->getOperand(0), pattern.stateStorage, 1)) {
        return false;
    }
    auto* baseBlock = dynamic_cast<IR::BasicBlock*>(
        entryTerminator->getOperand(1));
    auto* evenTest = resolveForwarders(dynamic_cast<IR::BasicBlock*>(
        entryTerminator->getOperand(2)));
    if (!matchReturnLoad(baseBlock, pattern.depthStorage)) return false;

    auto* evenTerminator = evenTest ? evenTest->getTerminator() : nullptr;
    if (!evenTerminator || evenTerminator->getOpcode() != Opc::COND_BR ||
        evenTerminator->getNumOperands() != 3 ||
        !matchEvenCondition(
            evenTerminator->getOperand(0), pattern.stateStorage)) {
        return false;
    }
    auto* halfCall = matchRecursiveReturn(
        dynamic_cast<IR::BasicBlock*>(evenTerminator->getOperand(1)),
        function, pattern.stateStorage, pattern.depthStorage,
        SuccessorKind::Half);
    auto* tripleTest = resolveForwarders(dynamic_cast<IR::BasicBlock*>(
        evenTerminator->getOperand(2)));
    if (!halfCall) return false;

    auto* tripleTerminator = tripleTest ? tripleTest->getTerminator() : nullptr;
    if (!tripleTerminator ||
        tripleTerminator->getOpcode() != Opc::COND_BR ||
        tripleTerminator->getNumOperands() != 3 ||
        !matchBoundCondition(
            tripleTerminator->getOperand(0), pattern.stateStorage, 3,
            pattern.bound)) {
        return false;
    }
    auto* tripleCall = matchRecursiveReturn(
        dynamic_cast<IR::BasicBlock*>(tripleTerminator->getOperand(1)),
        function, pattern.stateStorage, pattern.depthStorage,
        SuccessorKind::TriplePlusOne);
    auto* quadrupleTest = resolveForwarders(dynamic_cast<IR::BasicBlock*>(
        tripleTerminator->getOperand(2)));
    if (!tripleCall) return false;

    auto* quadrupleTerminator =
        quadrupleTest ? quadrupleTest->getTerminator() : nullptr;
    if (!quadrupleTerminator ||
        quadrupleTerminator->getOpcode() != Opc::COND_BR ||
        quadrupleTerminator->getNumOperands() != 3 ||
        !matchBoundCondition(
            quadrupleTerminator->getOperand(0), pattern.stateStorage, 4,
            pattern.bound)) {
        return false;
    }
    auto* quadrupleCall = matchRecursiveReturn(
        dynamic_cast<IR::BasicBlock*>(quadrupleTerminator->getOperand(1)),
        function, pattern.stateStorage, pattern.depthStorage,
        SuccessorKind::QuadruplePlusOne);
    if (!quadrupleCall || !matchReturnConstant(
            dynamic_cast<IR::BasicBlock*>(quadrupleTerminator->getOperand(2)),
            pattern.terminalValue)) {
        return false;
    }

    std::unordered_set<IR::Instruction*> expectedCalls = {
        halfCall, tripleCall, quadrupleCall};
    return validateFunctionBody(pattern, expectedCalls) &&
           validateStableBound(pattern);
}

void emitRecursiveStep(
        IR::BasicBlock* block, IR::Function* solver,
        IR::Value* successor, IR::Value* resultStorage) {
    auto* i32 = IR::IntegerType::I32;
    auto* minusOne = IR::ConstantInt::get(i32, -1);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* child = IR::Instruction::createCall(
        solver->getFunctionType(), solver, {successor}, "affine.child");
    auto* childTerminal = IR::Instruction::createCmp(
        Opc::ICMP, child, IR::ConstantInt::get(i32, 0), "slt");
    auto* incremented = IR::Instruction::createBinOp(
        Opc::ADD, i32, "affine.steps", child, one);
    auto* result = IR::Instruction::createSelect(
        childTerminal, minusOne, incremented, "affine.result");
    block->pushBack(child);
    block->pushBack(childTerminal);
    block->pushBack(incremented);
    block->pushBack(result);
    block->pushBack(IR::Instruction::createStore(result, resultStorage));
}

IR::Function* createAffineSolver(
        IR::Module* module, const AffineRecursionPattern& pattern,
        IR::GlobalVariable* cache, IR::ArrayType* cacheType,
        unsigned uniqueId) {
    auto* i32 = IR::IntegerType::I32;
    auto* functionType = IR::FunctionType::get(i32, {i32});
    auto* solver = module->createFunction(
        functionType,
        "__opt_affine_memo_solver_" + std::to_string(uniqueId), false);

    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* two = IR::ConstantInt::get(i32, 2);
    auto* three = IR::ConstantInt::get(i32, 3);
    auto* four = IR::ConstantInt::get(i32, 4);
    auto* minusOne = IR::ConstantInt::get(i32, -1);
    auto* capacity = IR::ConstantInt::get(i32, kAffineMemoCapacity);
    auto* state = solver->getArg(0);

    auto* entry = solver->createBlock("affine.entry");
    auto* cacheCheck = solver->createBlock("affine.cache.check");
    auto* cacheHit = solver->createBlock("affine.cache.hit");
    auto* compute = solver->createBlock("affine.compute");
    auto* base = solver->createBlock("affine.base");
    auto* parity = solver->createBlock("affine.parity");
    auto* even = solver->createBlock("affine.even");
    auto* tripleTest = solver->createBlock("affine.triple.test");
    auto* triple = solver->createBlock("affine.triple");
    auto* quadrupleTest = solver->createBlock("affine.quadruple.test");
    auto* quadruple = solver->createBlock("affine.quadruple");
    auto* terminal = solver->createBlock("affine.terminal");
    auto* finish = solver->createBlock("affine.finish");
    auto* cacheStore = solver->createBlock("affine.cache.store");
    auto* resultReturn = solver->createBlock("affine.return");

    auto* resultStorage = IR::Instruction::createAlloca(
        i32, "affine.result.slot");
    auto* nonnegative = IR::Instruction::createCmp(
        Opc::ICMP, state, zero, "sge");
    auto* belowCapacity = IR::Instruction::createCmp(
        Opc::ICMP, state, capacity, "slt");
    auto* cacheable = IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1, "affine.cacheable",
        nonnegative, belowCapacity);
    entry->pushBack(resultStorage);
    entry->pushBack(nonnegative);
    entry->pushBack(belowCapacity);
    entry->pushBack(cacheable);
    entry->pushBack(IR::Instruction::createCondBr(
        cacheable, cacheCheck, compute));

    auto* cacheAddress = IR::Instruction::createGetElementPtr(
        cacheType, cache, {zero, state}, "affine.cache.address");
    auto* cached = IR::Instruction::createLoad(
        i32, cacheAddress, "affine.cached");
    auto* hasCached = IR::Instruction::createCmp(
        Opc::ICMP, cached, zero, "ne");
    cacheCheck->pushBack(cacheAddress);
    cacheCheck->pushBack(cached);
    cacheCheck->pushBack(hasCached);
    cacheCheck->pushBack(IR::Instruction::createCondBr(
        hasCached, cacheHit, compute));

    auto* cachedSteps = IR::Instruction::createBinOp(
        Opc::SUB, i32, "affine.cached.steps", cached, one);
    auto* cachedPositive = IR::Instruction::createCmp(
        Opc::ICMP, cached, zero, "sgt");
    auto* decoded = IR::Instruction::createSelect(
        cachedPositive, cachedSteps, minusOne, "affine.decoded");
    cacheHit->pushBack(cachedSteps);
    cacheHit->pushBack(cachedPositive);
    cacheHit->pushBack(decoded);
    cacheHit->pushBack(IR::Instruction::createRet(decoded));

    auto* isBase = IR::Instruction::createCmp(
        Opc::ICMP, state, one, "eq");
    compute->pushBack(isBase);
    compute->pushBack(IR::Instruction::createCondBr(isBase, base, parity));

    base->pushBack(IR::Instruction::createStore(zero, resultStorage));
    base->pushBack(IR::Instruction::createBr(finish));

    auto* remainder = IR::Instruction::createBinOp(
        Opc::SREM, i32, "affine.remainder", state, two);
    auto* isEven = IR::Instruction::createCmp(
        Opc::ICMP, remainder, zero, "eq");
    parity->pushBack(remainder);
    parity->pushBack(isEven);
    parity->pushBack(IR::Instruction::createCondBr(
        isEven, even, tripleTest));

    auto* half = IR::Instruction::createBinOp(
        Opc::SDIV, i32, "affine.half", state, two);
    even->pushBack(half);
    emitRecursiveStep(even, solver, half, resultStorage);
    even->pushBack(IR::Instruction::createBr(finish));

    auto* tripleProduct = IR::Instruction::createBinOp(
        Opc::MUL, i32, "affine.triple.product", state, three);
    auto* tripleSuccessor = IR::Instruction::createBinOp(
        Opc::ADD, i32, "affine.triple.successor", tripleProduct, one);
    auto* boundForTriple = IR::Instruction::createLoad(
        i32, pattern.bound, "affine.bound");
    auto* tripleAllowed = IR::Instruction::createCmp(
        Opc::ICMP, tripleSuccessor, boundForTriple, "sle");
    tripleTest->pushBack(tripleProduct);
    tripleTest->pushBack(tripleSuccessor);
    tripleTest->pushBack(boundForTriple);
    tripleTest->pushBack(tripleAllowed);
    tripleTest->pushBack(IR::Instruction::createCondBr(
        tripleAllowed, triple, quadrupleTest));

    emitRecursiveStep(triple, solver, tripleSuccessor, resultStorage);
    triple->pushBack(IR::Instruction::createBr(finish));

    auto* quadrupleProduct = IR::Instruction::createBinOp(
        Opc::MUL, i32, "affine.quadruple.product", state, four);
    auto* quadrupleSuccessor = IR::Instruction::createBinOp(
        Opc::ADD, i32, "affine.quadruple.successor", quadrupleProduct, one);
    auto* boundForQuadruple = IR::Instruction::createLoad(
        i32, pattern.bound, "affine.bound");
    auto* quadrupleAllowed = IR::Instruction::createCmp(
        Opc::ICMP, quadrupleSuccessor, boundForQuadruple, "sle");
    quadrupleTest->pushBack(quadrupleProduct);
    quadrupleTest->pushBack(quadrupleSuccessor);
    quadrupleTest->pushBack(boundForQuadruple);
    quadrupleTest->pushBack(quadrupleAllowed);
    quadrupleTest->pushBack(IR::Instruction::createCondBr(
        quadrupleAllowed, quadruple, terminal));

    emitRecursiveStep(quadruple, solver, quadrupleSuccessor, resultStorage);
    quadruple->pushBack(IR::Instruction::createBr(finish));

    terminal->pushBack(IR::Instruction::createStore(
        minusOne, resultStorage));
    terminal->pushBack(IR::Instruction::createBr(finish));

    auto* result = IR::Instruction::createLoad(
        i32, resultStorage, "affine.computed");
    finish->pushBack(result);
    finish->pushBack(IR::Instruction::createCondBr(
        cacheable, cacheStore, resultReturn));

    auto* resultNonnegative = IR::Instruction::createCmp(
        Opc::ICMP, result, zero, "sge");
    auto* packedSteps = IR::Instruction::createBinOp(
        Opc::ADD, i32, "affine.packed.steps", result, one);
    auto* packed = IR::Instruction::createSelect(
        resultNonnegative, packedSteps, minusOne, "affine.packed");
    auto* storeAddress = IR::Instruction::createGetElementPtr(
        cacheType, cache, {zero, state}, "affine.cache.store.address");
    cacheStore->pushBack(resultNonnegative);
    cacheStore->pushBack(packedSteps);
    cacheStore->pushBack(packed);
    cacheStore->pushBack(storeAddress);
    cacheStore->pushBack(IR::Instruction::createStore(packed, storeAddress));
    cacheStore->pushBack(IR::Instruction::createBr(resultReturn));

    resultReturn->pushBack(IR::Instruction::createRet(result));
    return solver;
}

void replaceWithAffineWrapper(
        const AffineRecursionPattern& pattern, IR::Function* solver) {
    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* terminal = IR::ConstantInt::get(i32, pattern.terminalValue);

    pattern.function->getBlocks().clear();
    auto* entry = pattern.function->createBlock("affine.wrapper");
    auto* encoded = IR::Instruction::createCall(
        solver->getFunctionType(), solver,
        {pattern.function->getArg(0)}, "affine.encoded");
    auto* isTerminal = IR::Instruction::createCmp(
        Opc::ICMP, encoded, zero, "slt");
    auto* depthResult = IR::Instruction::createBinOp(
        Opc::ADD, i32, "affine.depth.result",
        pattern.function->getArg(1), encoded);
    auto* result = IR::Instruction::createSelect(
        isTerminal, terminal, depthResult, "affine.wrapper.result");
    entry->pushBack(encoded);
    entry->pushBack(isTerminal);
    entry->pushBack(depthResult);
    entry->pushBack(result);
    entry->pushBack(IR::Instruction::createRet(result));
}

bool rewriteRootCall(
        const AffineRecursionPattern& pattern, IR::Function* solver) {
    auto* call = pattern.rootCall;
    auto* block = call ? call->getParent() : nullptr;
    if (!block || call->getNumOperands() != 3) return false;

    auto insertion = block->begin();
    while (insertion != block->end() && insertion->get() != call) {
        ++insertion;
    }
    if (insertion == block->end()) return false;

    auto insert = [&](IR::Instruction* instruction) {
        insertion = block->insert(insertion, instruction);
        ++insertion;
        return instruction;
    };
    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* terminal = IR::ConstantInt::get(i32, pattern.terminalValue);
    auto* state = call->getOperand(1);
    auto* depth = call->getOperand(2);
    auto* encoded = insert(IR::Instruction::createCall(
        solver->getFunctionType(), solver, {state}, "affine.root.encoded"));
    auto* isTerminal = insert(IR::Instruction::createCmp(
        Opc::ICMP, encoded, zero, "slt"));
    auto* depthResult = insert(IR::Instruction::createBinOp(
        Opc::ADD, i32, "affine.root.depth.result", depth, encoded));
    auto* result = insert(IR::Instruction::createSelect(
        isTerminal, terminal, depthResult, "affine.root.result"));

    call->replaceAllUsesWith(result);
    call->dropAllUses();
    block->erase(insertion);
    return true;
}

} // namespace

bool affineRecursiveMemoization(IR::Module* module) {
    AffineRecursionPattern pattern;
    IR::Function* match = nullptr;
    for (auto& function : module->getFunctions()) {
        AffineRecursionPattern candidate;
        if (!matchAffineRecursion(function.get(), candidate)) continue;
        if (match) return false;
        match = function.get();
        pattern = candidate;
    }
    if (!match) return false;

    static unsigned uniqueId = 0;
    unsigned id = uniqueId++;
    auto* cacheType = IR::ArrayType::get(
        IR::IntegerType::I32, kAffineMemoCapacity);
    auto* cache = module->createGlobalVariable(
        IR::PointerType::get(cacheType),
        "__opt_affine_memo_cache_" + std::to_string(id), false);
    auto* solver = createAffineSolver(
        module, pattern, cache, cacheType, id);
    if (!rewriteRootCall(pattern, solver)) return false;
    replaceWithAffineWrapper(pattern, solver);
    return true;
}

} // namespace Opt
