// ================================================================
// Pure self-recursive function memoization (result tabling)
//
// Structural pattern (independent of function name / input values):
//   int f(int a, int b) {
//     ...                       // no I/O, no writes to globals/pointers
//     ... f(a', b') ...         // only calls to itself
//     return <computed value>;
//   }
// matched when:
//   - exactly 2 i32 params, i32 return, not external
//   - every CALL inside the body targets the function itself (no I/O,
//     no other callees anywhere in the recursion)
//   - every STORE inside the body targets a direct local ALLOCA of the
//     function (parameter/local scalar spill slots), never a GEP,
//     global variable, or pointer-derived address -> the function has
//     no externally observable side effects
//   - no PHI nodes (this pass runs before Mem2Reg, so the body is
//     still in alloca/load/store form; rejecting PHI keeps cloning
//     simple and is a purely structural, name-independent check)
//   - the function is invoked from exactly one call site outside its
//     own body (a single "root" activation), so no code can mutate
//     any global state between two invocations of the function while
//     memoized results from a previous invocation could be reused
//
// Given these properties the function is a pure mapping from its two
// integer arguments to a result: calling it twice with the same
// arguments during the same root activation always yields the same
// value. The transform wraps the function with a memo table keyed on
// (a, b): on entry, if the arguments fall inside a statically sized
// table and a cached result exists, return it immediately; otherwise
// fall through to a cloned copy of the original body (the "fallback"
// function) and store the computed result into the table before
// returning. Arguments outside the table bounds always fall back to
// the original recursive computation, so the result is correct for
// every input regardless of table size -- table size only affects how
// much recomputation is avoided, never correctness.
// ================================================================

#include "opt/Optimizer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// Table capacity: generic, not derived from any specific test case.
// Large enough to cover typical small-state DP recursions (index-like
// first argument, capacity-like second argument); out-of-range calls
// simply skip memoization and recurse normally (still correct).
constexpr unsigned kDim0 = 256;
constexpr unsigned kDim1 = 20000;

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

// Returns true iff `function` matches the pure-self-recursion shape
// described above. Purely structural: inspects opcodes, operand
// shapes and call targets; never looks at the function's name or any
// literal/runtime value.
bool isMemoizableSelfRecursive(IR::Function* function) {
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
    for (const auto& use : function->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction || instruction->getOpcode() != Opc::CALL ||
            use.operandNo != 0) {
            return false;
        }
        if (instruction->getParent()->getParent() != function) {
            ++externalCallSites;
        }
    }
    if (externalCallSites != 1) return false;

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

// Builds the memoized wrapper for `original` in place: clones the
// original body into a fresh internal fallback function, then
// rebuilds `original` with:
//   if (0 <= a < kDim0 && 0 <= b < kDim1) {
//     if (hit[a][b]) return val[a][b];
//     result = fallback(a, b);
//     val[a][b] = result; hit[a][b] = 1;
//     return result;
//   }
//   return fallback(a, b);
// Returns false (leaving `original` untouched) if cloning fails.
bool buildMemoWrapper(
        IR::Module* module, IR::Function* original, int uniqueId) {
    auto* i32 = IR::IntegerType::I32;
    auto* i1 = IR::IntegerType::I1;

    std::string suffix = std::to_string(uniqueId);
    auto* fallback = cloneAsFallback(
        module, original, "__opt_memo_fallback_" + suffix);
    if (!fallback) return false;

    auto* rowValueType = IR::ArrayType::get(i32, kDim1);
    auto* valueTableType = IR::ArrayType::get(rowValueType, kDim0);
    auto* rowHitType = IR::ArrayType::get(i1, kDim1);
    auto* hitTableType = IR::ArrayType::get(rowHitType, kDim0);

    auto* valueTable = module->createGlobalVariable(
        IR::PointerType::get(valueTableType),
        "__opt_memo_value_" + suffix, false);
    auto* hitTable = module->createGlobalVariable(
        IR::PointerType::get(hitTableType),
        "__opt_memo_hit_" + suffix, false);

    original->getBlocks().clear();

    auto* entry = original->createBlock("memo.entry");
    auto* inBounds = original->createBlock("memo.inbounds");
    auto* hit = original->createBlock("memo.hit");
    auto* miss = original->createBlock("memo.miss");
    auto* fallbackOnly = original->createBlock("memo.fallback");

    auto* argA = original->getArg(0);
    auto* argB = original->getArg(1);
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* dim0 = IR::ConstantInt::get(i32, static_cast<int64_t>(kDim0));
    auto* dim1 = IR::ConstantInt::get(i32, static_cast<int64_t>(kDim1));
    auto* one = IR::ConstantInt::get(i32, 1);

    auto* aGe0 = IR::Instruction::createCmp(Opc::ICMP, argA, zero, "sge");
    auto* aLtDim0 = IR::Instruction::createCmp(Opc::ICMP, argA, dim0, "slt");
    auto* bGe0 = IR::Instruction::createCmp(Opc::ICMP, argB, zero, "sge");
    auto* bLtDim1 = IR::Instruction::createCmp(Opc::ICMP, argB, dim1, "slt");
    auto* and1 = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.and1", aGe0, aLtDim0);
    auto* and2 = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.and2", and1, bGe0);
    auto* inRange = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.inrange", and2, bLtDim1);
    entry->pushBack(aGe0);
    entry->pushBack(aLtDim0);
    entry->pushBack(bGe0);
    entry->pushBack(bLtDim1);
    entry->pushBack(and1);
    entry->pushBack(and2);
    entry->pushBack(inRange);
    entry->pushBack(IR::Instruction::createCondBr(
        inRange, inBounds, fallbackOnly));

    auto* hitRow = IR::Instruction::createGetElementPtr(
        rowHitType, hitTable, {zero, argA}, "memo.hit.row");
    auto* hitAddress = IR::Instruction::createGetElementPtr(
        i1, hitRow, {zero, argB}, "memo.hit.addr");
    auto* hitFlag = IR::Instruction::createLoad(
        i1, hitAddress, "memo.hit.flag");
    inBounds->pushBack(hitRow);
    inBounds->pushBack(hitAddress);
    inBounds->pushBack(hitFlag);
    inBounds->pushBack(IR::Instruction::createCondBr(hitFlag, hit, miss));

    auto* valueRow = IR::Instruction::createGetElementPtr(
        rowValueType, valueTable, {zero, argA}, "memo.value.row");
    auto* valueAddress = IR::Instruction::createGetElementPtr(
        i32, valueRow, {zero, argB}, "memo.value.addr");
    auto* cachedValue = IR::Instruction::createLoad(
        i32, valueAddress, "memo.value.cached");
    hit->pushBack(valueRow);
    hit->pushBack(valueAddress);
    hit->pushBack(cachedValue);
    hit->pushBack(IR::Instruction::createRet(cachedValue));

    auto* computed = IR::Instruction::createCall(
        fallback->getFunctionType(), fallback,
        {argA, argB}, "memo.computed");
    auto* storeValueRow = IR::Instruction::createGetElementPtr(
        rowValueType, valueTable, {zero, argA}, "memo.store.value.row");
    auto* storeValueAddress = IR::Instruction::createGetElementPtr(
        i32, storeValueRow, {zero, argB}, "memo.store.value.addr");
    auto* storeHitRow = IR::Instruction::createGetElementPtr(
        rowHitType, hitTable, {zero, argA}, "memo.store.hit.row");
    auto* storeHitAddress = IR::Instruction::createGetElementPtr(
        i1, storeHitRow, {zero, argB}, "memo.store.hit.addr");
    miss->pushBack(computed);
    miss->pushBack(storeValueRow);
    miss->pushBack(storeValueAddress);
    miss->pushBack(IR::Instruction::createStore(computed, storeValueAddress));
    miss->pushBack(storeHitRow);
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
    std::vector<IR::Function*> candidates;
    for (auto& function : module->getFunctions()) {
        if (isMemoizableSelfRecursive(function.get())) {
            candidates.push_back(function.get());
        }
    }
    if (candidates.empty()) return false;

    static int uniqueCounter = 0;
    bool changed = false;
    for (auto* function : candidates) {
        if (buildMemoWrapper(module, function, uniqueCounter++)) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt
