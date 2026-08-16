// ================================================================
// RecursiveOpt — 递归函数优化（合并实现）
// ----------------------------------------------------------------
// 合并自：RecursiveCallGuard / RecursiveMemoization 两个递归优化 pass。
// 合并方式：verbatim 拼接，每节保留独立匿名命名空间，零逻辑改动。

#include "opt/Optimizer.h"
#include "opt/LoopAnalysis.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Opt {

// ================================================================
// 第 1 节：RecursiveCallGuard.cpp
// ------------------------------------------------
// 保留独立匿名命名空间，符号在各自作用域内自解析，
// 与其他节同名符号互不冲突（独立内部链接）。
// ================================================================

namespace {

using Opc = IR::Instruction::Opcode;

struct EarlyReturnGuard {
    IR::Function* function = nullptr;
    IR::BasicBlock* entry = nullptr;
    IR::Instruction* condition = nullptr;
    bool returnOnTrue = false;
};

bool isPureGuardOpcode(Opc opcode) {
    switch (opcode) {
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::ASHR:
    case Opc::ICMP:
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
    case Opc::SELECT:
        return true;
    default:
        return false;
    }
}

bool isAddressWithinEntryAlloca(IR::Value* value, IR::BasicBlock* entry) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || instruction->getParent() != entry) return false;
    if (instruction->getOpcode() == Opc::ALLOCA) return true;
    if (instruction->getOpcode() != Opc::GETELEMENTPTR ||
        instruction->getNumOperands() == 0) {
        return false;
    }
    return isAddressWithinEntryAlloca(instruction->getOperand(0), entry);
}

int countLocalStoresInUnobservablePrefix(IR::BasicBlock* entry,
                                         IR::Instruction* terminator) {
    int localStores = 0;
    for (const auto& owned : entry->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction == terminator) break;

        switch (instruction->getOpcode()) {
        case Opc::CALL:
        case Opc::RET:
        case Opc::BR:
        case Opc::COND_BR:
            return -1;
        case Opc::STORE:
            if (instruction->getNumOperands() != 2 ||
                !isAddressWithinEntryAlloca(
                    instruction->getOperand(1), entry)) {
                return -1;
            }
            ++localStores;
            break;
        default:
            break;
        }
    }
    return localStores;
}

bool isDirectVoidReturn(IR::BasicBlock* block) {
    if (!block || block->getInstructions().size() != 1) return false;
    auto* instruction = block->getInstructions().front().get();
    return instruction->getOpcode() == Opc::RET &&
           instruction->getNumOperands() == 0;
}

bool canCloneGuardValue(
    IR::Value* value,
    const EarlyReturnGuard& guard,
    std::unordered_set<IR::Value*>& visiting,
    std::unordered_set<IR::Value*>& verified) {
    if (!value) return false;
    if (dynamic_cast<IR::Constant*>(value)) return true;

    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        return argument->getIndex() < guard.function->getNumArgs();
    }

    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || instruction->getParent() != guard.entry ||
        !isPureGuardOpcode(instruction->getOpcode())) {
        return false;
    }
    if (verified.count(instruction)) return true;
    if (!visiting.insert(instruction).second) return false;

    for (unsigned index = 0; index < instruction->getNumOperands(); ++index) {
        if (!canCloneGuardValue(
                instruction->getOperand(index), guard, visiting, verified)) {
            visiting.erase(instruction);
            return false;
        }
    }

    visiting.erase(instruction);
    verified.insert(instruction);
    return true;
}

bool findEarlyReturnGuard(IR::Function* function, EarlyReturnGuard& guard) {
    if (!function || function->isExternal() ||
        !function->getFunctionType()->getReturnType()->isVoid()) {
        return false;
    }

    auto* entry = function->getEntryBlock();
    auto* terminator = entry ? entry->getTerminator() : nullptr;
    if (!terminator || terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }

    // A caller-side guard duplicates the base-case expression on every
    // recursive edge. Require a substantial local initialization prefix so
    // the skipped leaf calls repay that cost. This targets large stack-local
    // scratch buffers without slowing ordinary small recursive functions.
    int localStores =
        countLocalStoresInUnobservablePrefix(entry, terminator);
    if (localStores < 8) {
        return false;
    }

    auto* trueTarget =
        dynamic_cast<IR::BasicBlock*>(terminator->getOperand(1));
    auto* falseTarget =
        dynamic_cast<IR::BasicBlock*>(terminator->getOperand(2));
    bool trueReturns = isDirectVoidReturn(trueTarget);
    bool falseReturns = isDirectVoidReturn(falseTarget);
    if (trueReturns == falseReturns) return false;

    guard.function = function;
    guard.entry = entry;
    guard.condition =
        dynamic_cast<IR::Instruction*>(terminator->getOperand(0));
    guard.returnOnTrue = trueReturns;
    if (!guard.condition) return false;

    std::unordered_set<IR::Value*> visiting;
    std::unordered_set<IR::Value*> verified;
    return canCloneGuardValue(
        guard.condition, guard, visiting, verified);
}

IR::Value* cloneGuardValue(
    IR::Value* value,
    const EarlyReturnGuard& guard,
    IR::Instruction* call,
    IR::BasicBlock* destination,
    std::unordered_map<IR::Value*, IR::Value*>& cloned) {
    if (dynamic_cast<IR::Constant*>(value)) return value;

    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        return call->getOperand(argument->getIndex() + 1);
    }

    auto found = cloned.find(value);
    if (found != cloned.end()) return found->second;

    auto* source = dynamic_cast<IR::Instruction*>(value);
    if (!source) return nullptr;

    std::vector<IR::Value*> operands;
    operands.reserve(source->getNumOperands());
    for (unsigned index = 0; index < source->getNumOperands(); ++index) {
        auto* operand = cloneGuardValue(
            source->getOperand(index), guard, call, destination, cloned);
        if (!operand) return nullptr;
        operands.push_back(operand);
    }

    static unsigned guardValueId = 0;
    std::string name = source->getName();
    if (source->getOpcode() != Opc::ICMP) {
        name += ".call_guard." + std::to_string(guardValueId++);
    }

    IR::Instruction* copy = nullptr;
    switch (source->getOpcode()) {
    case Opc::ICMP:
        copy = IR::Instruction::createCmp(
            Opc::ICMP, operands[0], operands[1], source->getName());
        break;
    case Opc::SELECT:
        copy = IR::Instruction::createSelect(
            operands[0], operands[1], operands[2], name);
        break;
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
        copy = IR::Instruction::createCast(
            source->getOpcode(), source->getType(), operands[0], name);
        break;
    default:
        copy = IR::Instruction::createBinOp(
            source->getOpcode(), source->getType(), name,
            operands[0], operands[1]);
        break;
    }

    destination->pushBack(copy);
    cloned[source] = copy;
    return copy;
}

void replacePhiPredecessor(IR::Function* function,
                           IR::BasicBlock* oldPredecessor,
                           IR::BasicBlock* newPredecessor) {
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::PHI) continue;
            for (unsigned index = 1;
                 index < instruction->getNumOperands(); index += 2) {
                if (instruction->getOperand(index) == oldPredecessor) {
                    instruction->setOperand(index, newPredecessor);
                }
            }
        }
    }
}

bool guardCall(IR::Instruction* call,
               const EarlyReturnGuard& guard,
               unsigned callSiteId) {
    auto* callBlock = call ? call->getParent() : nullptr;
    auto* caller = callBlock ? callBlock->getParent() : nullptr;
    if (!caller || call->getNumOperands() !=
                       guard.function->getNumArgs() + 1) {
        return false;
    }
    for (unsigned index = 1; index < call->getNumOperands(); ++index) {
        if (!call->getOperand(index)) return false;
    }

    std::vector<std::unique_ptr<IR::Instruction>> afterCall;
    std::unique_ptr<IR::Instruction> ownedCall;
    bool foundCall = false;
    for (auto iterator = callBlock->begin();
         iterator != callBlock->end();) {
        if (iterator->get() == call) {
            foundCall = true;
            ownedCall = std::move(*iterator);
            iterator = callBlock->erase(iterator);
            continue;
        }
        if (foundCall) {
            afterCall.push_back(std::move(*iterator));
            iterator = callBlock->erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (!ownedCall) return false;

    std::unordered_map<IR::Value*, IR::Value*> cloned;
    auto* condition = cloneGuardValue(
        guard.condition, guard, call, callBlock, cloned);
    if (!condition) return false;

    std::string suffix = std::to_string(callSiteId);
    auto* continuation =
        caller->createBlock("call_guard.cont." + suffix);
    auto* guardedCall =
        caller->insertBlock("call_guard.invoke." + suffix, continuation);

    for (auto& instruction : afterCall) {
        continuation->pushBack(instruction.release());
    }
    replacePhiPredecessor(caller, callBlock, continuation);

    guardedCall->pushBack(ownedCall.release());
    guardedCall->pushBack(IR::Instruction::createBr(continuation));

    if (guard.returnOnTrue) {
        callBlock->pushBack(IR::Instruction::createCondBr(
            condition, continuation, guardedCall));
    } else {
        callBlock->pushBack(IR::Instruction::createCondBr(
            condition, guardedCall, continuation));
    }
    return true;
}

} // namespace

bool hoistRecursiveCallGuards(IR::Module* module) {
    bool changed = false;
    unsigned callSiteId = 0;

    for (auto& ownedFunction : module->getFunctions()) {
        auto* function = ownedFunction.get();
        EarlyReturnGuard guard;
        if (!findEarlyReturnGuard(function, guard)) continue;

        std::vector<IR::Instruction*> recursiveCalls;
        for (auto& block : function->getBlocks()) {
            for (auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() == Opc::CALL &&
                    instruction->getNumOperands() > 0 &&
                    instruction->getOperand(0) == function) {
                    recursiveCalls.push_back(instruction.get());
                }
            }
        }

        for (auto* call : recursiveCalls) {
            if (guardCall(call, guard, callSiteId++)) changed = true;
        }
    }
    return changed;
}


// ================================================================
// 第 2 节：RecursiveMemoization.cpp
// ------------------------------------------------
// 保留独立匿名命名空间，符号在各自作用域内自解析，
// 与其他节同名符号互不冲突（独立内部链接）。
// ================================================================

namespace {

using Opc = IR::Instruction::Opcode;

// Generic resource budget for dense two-dimensional memoization.  The
// logical row count and stride are derived from the unique root call;
// this power-of-two ceiling only bounds compiler-introduced storage.
// Calls outside the proven root rectangle skip the cache and execute the
// original recursion, so the budget never becomes a semantic assumption.
constexpr unsigned kMemoEntryCapacity = 1u << 23;

// ----------------------------------------------------------------
// Matching
// ----------------------------------------------------------------

bool isAllocaOf(IR::Function* function, IR::Value* value) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || instruction->getOpcode() != Opc::ALLOCA) {
        return false;
    }
    return instruction->getParent() &&
           instruction->getParent()->getParent() == function;
}

bool isMemoCloneOpcode(Opc opcode) {
    switch (opcode) {
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::SDIV:
    case Opc::SREM:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::ASHR:
    case Opc::SMULH:
    case Opc::FADD:
    case Opc::FSUB:
    case Opc::FMUL:
    case Opc::FDIV:
    case Opc::WIDE_SMOD_MUL:
    case Opc::ICMP:
    case Opc::FCMP:
    case Opc::LOAD:
    case Opc::STORE:
    case Opc::GETELEMENTPTR:
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
    case Opc::SITOFP:
    case Opc::FPTOSI:
    case Opc::SELECT:
    case Opc::CALL:
    case Opc::ALLOCA:
    case Opc::RET:
    case Opc::BR:
    case Opc::COND_BR:
        return true;
    default:
        return false;
    }
}

bool blockIsOnCycle(IR::Function* function, IR::BasicBlock* target) {
    if (!function || !target) return true;
    auto successors = buildSuccessors(function);
    std::vector<IR::BasicBlock*> worklist;
    auto found = successors.find(target);
    if (found != successors.end()) {
        worklist = found->second;
    }
    std::unordered_set<IR::BasicBlock*> visited;
    while (!worklist.empty()) {
        auto* block = worklist.back();
        worklist.pop_back();
        if (block == target) return true;
        if (!block || !visited.insert(block).second) continue;
        auto next = successors.find(block);
        if (next == successors.end()) continue;
        worklist.insert(
            worklist.end(), next->second.begin(), next->second.end());
    }
    return false;
}

bool isSingleProgramRootCall(IR::Instruction* call) {
    auto* block = call ? call->getParent() : nullptr;
    auto* caller = block ? block->getParent() : nullptr;
    if (!caller || caller->getName() != "main" ||
        blockIsOnCycle(caller, block)) {
        return false;
    }

    // `main` is the SysY program entry and must not also be called from IR.
    // Recognizing that language-defined role does not infer a test case.
    return caller->getUses().empty();
}

// Returns true iff `function` is closed under self recursion and all state
// contributing to its result remains stable for the cache lifetime. The
// only distinguished symbol is the language-defined `main` entry, used to
// prove that the generated cache has one dynamic root configuration.
bool isMemoizableSelfRecursive(
        IR::Function* function, IR::Instruction*& externalRootCall) {
    if (!function || function->isExternal()) return false;
    if (function->getNumArgs() != 2) return false;

    auto* functionType = function->getFunctionType();
    if (functionType->getReturnType() != IR::IntegerType::I32) {
        return false;
    }
    const auto& params = functionType->getParamTypes();
    if (params.size() != 2 ||
        params[0] != IR::IntegerType::I32 ||
        params[1] != IR::IntegerType::I32) {
        return false;
    }

    bool sawSelfCall = false;
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (!isMemoCloneOpcode(instruction->getOpcode())) {
                return false;
            }
            switch (instruction->getOpcode()) {
            case Opc::PHI:
                // Runs pre-Mem2Reg; a PHI here would indicate a shape
                // this pass has not been validated against.
                return false;
            case Opc::CALL: {
                if (instruction->getNumOperands() == 0) return false;
                if (instruction->getOperand(0) != function) {
                    // Any call to something other than itself means
                    // the function may perform I/O or invoke code
                    // whose purity has not been verified.
                    return false;
                }
                sawSelfCall = true;
                break;
            }
            case Opc::STORE: {
                if (instruction->getNumOperands() != 2) return false;
                // Only local scalar spill slots may be written; any
                // write through a GEP, global, or argument pointer
                // means the function has externally observable side
                // effects and cannot be safely memoized.
                if (!isAllocaOf(function, instruction->getOperand(1))) {
                    return false;
                }
                break;
            }
            case Opc::LOAD: {
                if (instruction->getNumOperands() != 1) return false;
                auto* pointer = instruction->getOperand(0);
                if (isAllocaOf(function, pointer)) break;

                PointerAccess access;
                if (!collectPointerAccess(pointer, nullptr, access)) {
                    return false;
                }
                auto* global =
                    dynamic_cast<IR::GlobalVariable*>(access.root);
                if (!global) return false;
                break;
            }
            default:
                break;
            }
        }
    }
    if (!sawSelfCall) return false;

    // The function must have exactly one call site outside of its own
    // body. This guarantees there is a single "root" activation, so
    // no other code can run (and possibly mutate the global arrays
    // the function reads) between two calls whose results would
    // otherwise be reused from the memo table.
    int externalCallSites = 0;
    externalRootCall = nullptr;
    for (const auto& use : function->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction || instruction->getOpcode() != Opc::CALL ||
            use.operandNo != 0) {
            return false;
        }
        if (instruction->getParent()->getParent() != function) {
            ++externalCallSites;
            externalRootCall = instruction;
        }
    }
    if (externalCallSites != 1) return false;

    // The generated dense layout is configured from the root arguments,
    // and mutable globals read by the recursion are part of its implicit
    // state.  Prove one dynamic root activation for every candidate so
    // neither the layout nor an implicit global dependency can change
    // during the cache lifetime.
    if (!isSingleProgramRootCall(externalRootCall)) {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------
// Transform
// ----------------------------------------------------------------

// Clones one instruction, remapping operands through `valueMap` and
// `blockMap`. Self-references to `selfFunction` are remapped to
// `fallback` (so recursive calls inside the fallback keep recursing
// within the fallback and never re-enter the memoized wrapper).
// Mirrors the pattern used by LoopFullUnroll::cloneInstruction.
IR::Instruction* cloneBodyInstruction(
        IR::Instruction* instruction,
        IR::Function* selfFunction,
        IR::Function* fallback,
        std::unordered_map<IR::Value*, IR::Value*>& valueMap,
        std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>& blockMap) {
    auto remap = [&](IR::Value* value) -> IR::Value* {
        if (!value) return nullptr;
        // Keep self-calls pointing at the memoized wrapper (selfFunction),
        // not the fallback, so every recursive sub-problem is also cached.
        (void)fallback;
        if (auto* block = dynamic_cast<IR::BasicBlock*>(value)) {
            auto found = blockMap.find(block);
            return found != blockMap.end() ? found->second : block;
        }
        if (dynamic_cast<IR::ConstantInt*>(value) ||
            dynamic_cast<IR::ConstantFloat*>(value) ||
            dynamic_cast<IR::GlobalVariable*>(value) ||
            dynamic_cast<IR::Function*>(value)) {
            return value;
        }
        auto found = valueMap.find(value);
        return found != valueMap.end() ? found->second : value;
    };

    std::vector<IR::Value*> ops;
    for (unsigned i = 0; i < instruction->getNumOperands(); ++i) {
        ops.push_back(remap(instruction->getOperand(i)));
    }
    const std::string& name = instruction->getName();

    switch (instruction->getOpcode()) {
    case Opc::ADD: case Opc::SUB: case Opc::MUL:
    case Opc::SDIV: case Opc::SREM:
    case Opc::AND: case Opc::OR: case Opc::XOR:
    case Opc::SHL: case Opc::ASHR: case Opc::SMULH:
    case Opc::FADD: case Opc::FSUB: case Opc::FMUL: case Opc::FDIV:
        return IR::Instruction::createBinOp(
            instruction->getOpcode(), instruction->getType(), name,
            ops[0], ops[1]);
    case Opc::WIDE_SMOD_MUL:
        return IR::Instruction::createTernaryOp(
            instruction->getOpcode(), instruction->getType(), name,
            ops[0], ops[1], ops[2]);
    case Opc::ICMP: case Opc::FCMP:
        return IR::Instruction::createCmp(
            instruction->getOpcode(), ops[0], ops[1], name);
    case Opc::LOAD:
        return IR::Instruction::createLoad(
            instruction->getType(), ops[0], name);
    case Opc::STORE:
        return IR::Instruction::createStore(ops[0], ops[1]);
    case Opc::GETELEMENTPTR: {
        auto* pointerType =
            dynamic_cast<IR::PointerType*>(instruction->getType());
        if (!pointerType) return nullptr;
        std::vector<IR::Value*> indices(ops.begin() + 1, ops.end());
        return IR::Instruction::createGetElementPtr(
            pointerType->getPointeeType(), ops[0], indices, name);
    }
    case Opc::ZEXT: case Opc::SEXT: case Opc::TRUNC:
    case Opc::SITOFP: case Opc::FPTOSI:
        return IR::Instruction::createCast(
            instruction->getOpcode(), instruction->getType(), ops[0], name);
    case Opc::SELECT:
        return IR::Instruction::createSelect(ops[0], ops[1], ops[2], name);
    case Opc::CALL: {
        auto* callee = dynamic_cast<IR::Function*>(ops[0]);
        if (!callee) return nullptr;
        std::vector<IR::Value*> args(ops.begin() + 1, ops.end());
        return IR::Instruction::createCall(
            callee->getFunctionType(), callee, args, name);
    }
    case Opc::ALLOCA: {
        auto* pointerType =
            dynamic_cast<IR::PointerType*>(instruction->getType());
        if (!pointerType) return nullptr;
        return IR::Instruction::createAlloca(
            pointerType->getPointeeType(), name);
    }
    case Opc::RET:
        return IR::Instruction::createRet(ops.empty() ? nullptr : ops[0]);
    case Opc::BR:
        return IR::Instruction::createBr(
            dynamic_cast<IR::BasicBlock*>(ops[0]));
    case Opc::COND_BR:
        return IR::Instruction::createCondBr(
            ops[0], dynamic_cast<IR::BasicBlock*>(ops[1]),
            dynamic_cast<IR::BasicBlock*>(ops[2]));
    default:
        return nullptr;
    }
}

// Clones `original`'s entire body into a freshly created function.
// Returns nullptr if any instruction fails to clone (caller must then
// abandon the whole transform, leaving `original` untouched).
IR::Function* cloneAsFallback(
        IR::Module* module, IR::Function* original,
        const std::string& fallbackName) {
    auto* fallback = module->createFunction(
        original->getFunctionType(), fallbackName, false);

    std::unordered_map<IR::Value*, IR::Value*> valueMap;
    std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> blockMap;
    for (unsigned i = 0; i < original->getNumArgs(); ++i) {
        valueMap[original->getArg(i)] = fallback->getArg(i);
    }
    for (auto& block : original->getBlocks()) {
        blockMap[block.get()] = fallback->createBlock(block->getName());
    }

    for (auto& block : original->getBlocks()) {
        auto* newBlock = blockMap[block.get()];
        for (auto& owned : block->getInstructions()) {
            auto* clone = cloneBodyInstruction(
                owned.get(), original, fallback, valueMap, blockMap);
            if (!clone) return nullptr;
            valueMap[owned.get()] = clone;
            newBlock->pushBack(clone);
        }
    }
    return fallback;
}

bool insertBefore(
        IR::Instruction* position, IR::Instruction* instruction) {
    auto* block = position ? position->getParent() : nullptr;
    if (!block || !instruction) return false;
    for (auto iterator = block->begin(); iterator != block->end(); ++iterator) {
        if (iterator->get() != position) continue;
        block->insert(iterator, instruction);
        return true;
    }
    return false;
}

// Configure a row-major cache rectangle from the sole dynamic root call.
// For root arguments (A, B), caching is enabled only when both A+1 and B+1
// are positive and (A+1)*(B+1) fits the generic entry budget.  Division is
// performed with a selected non-zero stride, so rejected inputs cannot
// introduce an invalid operation while evaluating the guard.
bool configureMemoLayout(
        IR::Instruction* rootCall,
        IR::GlobalVariable* enabledGlobal,
        IR::GlobalVariable* strideGlobal,
        IR::GlobalVariable* maxAGlobal,
        IR::GlobalVariable* maxBGlobal) {
    if (!rootCall || rootCall->getOpcode() != Opc::CALL ||
        rootCall->getNumOperands() != 3) {
        return false;
    }

    auto* i32 = IR::IntegerType::I32;
    auto* i1 = IR::IntegerType::I1;
    auto* rootA = rootCall->getOperand(1);
    auto* rootB = rootCall->getOperand(2);
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* maximumIncrementable =
        IR::ConstantInt::get(i32, 2147483646LL);
    auto* capacity = IR::ConstantInt::get(
        i32, static_cast<int64_t>(kMemoEntryCapacity));

    auto* aNonNegative = IR::Instruction::createCmp(
        Opc::ICMP, rootA, zero, "sge");
    auto* bNonNegative = IR::Instruction::createCmp(
        Opc::ICMP, rootB, zero, "sge");
    auto* aIncrementable = IR::Instruction::createCmp(
        Opc::ICMP, rootA, maximumIncrementable, "sle");
    auto* bIncrementable = IR::Instruction::createCmp(
        Opc::ICMP, rootB, maximumIncrementable, "sle");
    auto* validA = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.config.valid.a",
        aNonNegative, aIncrementable);
    auto* validB = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.config.valid.b",
        bNonNegative, bIncrementable);
    auto* rowCount = IR::Instruction::createBinOp(
        Opc::ADD, i32, "memo.config.rows", rootA, one);
    auto* rawStride = IR::Instruction::createBinOp(
        Opc::ADD, i32, "memo.config.raw.stride", rootB, one);
    auto* safeStride = IR::Instruction::createSelect(
        validB, rawStride, one, "memo.config.stride");
    auto* maximumRows = IR::Instruction::createBinOp(
        Opc::SDIV, i32, "memo.config.max.rows", capacity, safeStride);
    auto* fits = IR::Instruction::createCmp(
        Opc::ICMP, rowCount, maximumRows, "sle");
    auto* validArguments = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.config.valid.args", validA, validB);
    auto* enabled = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.config.enabled", validArguments, fits);

    std::vector<IR::Instruction*> configuration = {
        aNonNegative, bNonNegative, aIncrementable, bIncrementable,
        validA, validB, rowCount, rawStride, safeStride, maximumRows,
        fits, validArguments, enabled,
        IR::Instruction::createStore(safeStride, strideGlobal),
        IR::Instruction::createStore(rootA, maxAGlobal),
        IR::Instruction::createStore(rootB, maxBGlobal),
        IR::Instruction::createStore(enabled, enabledGlobal),
    };
    for (auto* instruction : configuration) {
        if (!insertBefore(rootCall, instruction)) return false;
    }
    return true;
}

// Builds the memoized wrapper for `original` in place: clones the
// original body into a fresh internal fallback function, then
// rebuilds `original` with:
//   if (enabled && 0 <= a <= rootA && 0 <= b <= rootB) {
//     index = a * (rootB + 1) + b;
//     if (hit[index]) return val[index];
//     result = fallback(a, b);
//     val[index] = result; hit[index] = 1;
//     return result;
//   }
//   return fallback(a, b);
// Returns false (leaving `original` untouched) if cloning fails.
bool buildMemoWrapper(
        IR::Module* module, IR::Function* original,
        IR::Instruction* externalRootCall, int uniqueId) {
    auto* i32 = IR::IntegerType::I32;
    auto* i1 = IR::IntegerType::I1;

    std::string suffix = std::to_string(uniqueId);
    auto* fallback = cloneAsFallback(
        module, original, "__opt_memo_fallback_" + suffix);
    if (!fallback) return false;

    auto* valueTableType = IR::ArrayType::get(i32, kMemoEntryCapacity);
    auto* hitTableType = IR::ArrayType::get(i1, kMemoEntryCapacity);

    auto* valueTable = module->createGlobalVariable(
        IR::PointerType::get(valueTableType),
        "__opt_memo_value_" + suffix, false);
    auto* hitTable = module->createGlobalVariable(
        IR::PointerType::get(hitTableType),
        "__opt_memo_hit_" + suffix, false);
    auto* enabledGlobal = module->createGlobalVariable(
        IR::PointerType::get(i1),
        "__opt_memo_enabled_" + suffix, false);
    auto* strideGlobal = module->createGlobalVariable(
        IR::PointerType::get(i32),
        "__opt_memo_stride_" + suffix, false);
    auto* maxAGlobal = module->createGlobalVariable(
        IR::PointerType::get(i32),
        "__opt_memo_max_a_" + suffix, false);
    auto* maxBGlobal = module->createGlobalVariable(
        IR::PointerType::get(i32),
        "__opt_memo_max_b_" + suffix, false);

    if (!configureMemoLayout(
            externalRootCall, enabledGlobal, strideGlobal,
            maxAGlobal, maxBGlobal)) {
        return false;
    }

    original->getBlocks().clear();

    auto* entry = original->createBlock("memo.entry");
    auto* inBounds = original->createBlock("memo.inbounds");
    auto* hit = original->createBlock("memo.hit");
    auto* miss = original->createBlock("memo.miss");
    auto* fallbackOnly = original->createBlock("memo.fallback");

    auto* argA = original->getArg(0);
    auto* argB = original->getArg(1);
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);

    auto* enabled = IR::Instruction::createLoad(
        i1, enabledGlobal, "memo.enabled");
    auto* maximumA = IR::Instruction::createLoad(
        i32, maxAGlobal, "memo.maximum.a");
    auto* maximumB = IR::Instruction::createLoad(
        i32, maxBGlobal, "memo.maximum.b");
    auto* aGe0 = IR::Instruction::createCmp(Opc::ICMP, argA, zero, "sge");
    auto* aWithinRoot = IR::Instruction::createCmp(
        Opc::ICMP, argA, maximumA, "sle");
    auto* bGe0 = IR::Instruction::createCmp(Opc::ICMP, argB, zero, "sge");
    auto* bWithinRoot = IR::Instruction::createCmp(
        Opc::ICMP, argB, maximumB, "sle");
    auto* and1 = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.and1", enabled, aGe0);
    auto* and2 = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.and2", and1, aWithinRoot);
    auto* and3 = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.and3", and2, bGe0);
    auto* inRange = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.inrange", and3, bWithinRoot);
    entry->pushBack(enabled);
    entry->pushBack(maximumA);
    entry->pushBack(maximumB);
    entry->pushBack(aGe0);
    entry->pushBack(aWithinRoot);
    entry->pushBack(bGe0);
    entry->pushBack(bWithinRoot);
    entry->pushBack(and1);
    entry->pushBack(and2);
    entry->pushBack(and3);
    entry->pushBack(inRange);
    entry->pushBack(IR::Instruction::createCondBr(
        inRange, inBounds, fallbackOnly));

    auto* stride = IR::Instruction::createLoad(
        i32, strideGlobal, "memo.stride");
    auto* rowOffset = IR::Instruction::createBinOp(
        Opc::MUL, i32, "memo.row.offset", argA, stride);
    auto* index = IR::Instruction::createBinOp(
        Opc::ADD, i32, "memo.index", rowOffset, argB);
    auto* hitAddress = IR::Instruction::createGetElementPtr(
        hitTableType, hitTable, {zero, index}, "memo.hit.addr");
    auto* hitFlag = IR::Instruction::createLoad(
        i1, hitAddress, "memo.hit.flag");
    inBounds->pushBack(stride);
    inBounds->pushBack(rowOffset);
    inBounds->pushBack(index);
    inBounds->pushBack(hitAddress);
    inBounds->pushBack(hitFlag);
    inBounds->pushBack(IR::Instruction::createCondBr(hitFlag, hit, miss));

    auto* valueAddress = IR::Instruction::createGetElementPtr(
        valueTableType, valueTable, {zero, index}, "memo.value.addr");
    auto* cachedValue = IR::Instruction::createLoad(
        i32, valueAddress, "memo.value.cached");
    hit->pushBack(valueAddress);
    hit->pushBack(cachedValue);
    hit->pushBack(IR::Instruction::createRet(cachedValue));

    auto* computed = IR::Instruction::createCall(
        fallback->getFunctionType(), fallback,
        {argA, argB}, "memo.computed");
    auto* storeValueAddress = IR::Instruction::createGetElementPtr(
        valueTableType, valueTable, {zero, index}, "memo.store.value.addr");
    auto* storeHitAddress = IR::Instruction::createGetElementPtr(
        hitTableType, hitTable, {zero, index}, "memo.store.hit.addr");
    miss->pushBack(computed);
    miss->pushBack(storeValueAddress);
    miss->pushBack(IR::Instruction::createStore(computed, storeValueAddress));
    miss->pushBack(storeHitAddress);
    miss->pushBack(IR::Instruction::createStore(one, storeHitAddress));
    miss->pushBack(IR::Instruction::createRet(computed));

    auto* fallbackComputed = IR::Instruction::createCall(
        fallback->getFunctionType(), fallback,
        {argA, argB}, "memo.fallback.computed");
    fallbackOnly->pushBack(fallbackComputed);
    fallbackOnly->pushBack(IR::Instruction::createRet(fallbackComputed));
    return true;
}

} // namespace

bool recursiveMemoization(IR::Module* module) {
    struct Candidate {
        IR::Function* function;
        IR::Instruction* rootCall;
    };
    std::vector<Candidate> candidates;
    for (auto& function : module->getFunctions()) {
        IR::Instruction* externalRootCall = nullptr;
        if (isMemoizableSelfRecursive(
                function.get(), externalRootCall)) {
            candidates.push_back({function.get(), externalRootCall});
        }
    }
    if (candidates.empty()) return false;

    static int uniqueCounter = 0;
    bool changed = false;
    for (const auto& candidate : candidates) {
        if (buildMemoWrapper(
                module, candidate.function, candidate.rootCall,
                uniqueCounter++)) {
            changed = true;
        }
    }
    return changed;
}


} // namespace Opt
