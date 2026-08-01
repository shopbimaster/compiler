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
//   - all uses of the function are direct calls. External callers enter
//     through a root wrapper, while cloned recursive calls target a
//     separate internal memo function.
//
// The candidate has no writes visible outside one invocation and cannot
// call code other than itself, so global values read by the recursion are
// stable during a root activation. A generation value is assigned at each
// external root entry and passed explicitly through the recursive memo and
// fallback functions. Cache entries are tagged with that generation, which
// prevents reuse after an external caller mutates global input state. On
// generation wraparound the root clears all tags before reusing generation
// one. The direct-mapped table stores both complete argument keys, so hash
// collisions can evict entries but can never return a result for another key.
// ================================================================

#include "opt/Optimizer.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// A uniform 16 MiB budget per memoized function: four i32 arrays containing
// two complete keys, a value, and an epoch. The power-of-two capacity makes
// indexing independent of any inferred argument range.
constexpr unsigned kTableEntries = 1u << 20;
constexpr unsigned kTableMask = kTableEntries - 1;
constexpr unsigned kMaxMemoTablesPerModule = 4;

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

bool isTailSelfCall(IR::Instruction* call, IR::Function* function) {
    if (!call || call->getOpcode() != Opc::CALL ||
        call->getNumOperands() == 0 || call->getOperand(0) != function) {
        return false;
    }
    auto* block = call->getParent();
    if (!block) return false;

    const auto& instructions = block->getInstructions();
    for (size_t index = 0; index + 1 < instructions.size(); ++index) {
        if (instructions[index].get() != call) continue;
        auto* ret = instructions[index + 1].get();
        return ret->getOpcode() == Opc::RET &&
               ret->getNumOperands() == 1 && ret->getOperand(0) == call;
    }
    return false;
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
    bool sawNonTailSelfCall = false;
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            switch (instruction->getOpcode()) {
            case Opc::PHI:
                // Runs pre-Mem2Reg; a PHI here would indicate a shape
                // this pass has not been validated against.
                return false;
            case Opc::WIDE_SMOD_MUL:
                // A proven constant-time modular product has no repeated
                // subproblem tree left to collapse. Wrapping it would only
                // add a table and a lookup on every root call.
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
                sawNonTailSelfCall |= !isTailSelfCall(instruction, function);
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

    // A recursion tree with only tail edges has no branching subproblems for
    // this pass to collapse. Leave that shape intact so the following tail
    // recursion pass can turn it into a loop without memo-table lookups.
    if (!sawNonTailSelfCall) return false;

    // Every use must be a direct call through operand zero. External call
    // sites may execute any number of times: the generated root wrapper
    // starts a fresh cache generation for every dynamic invocation.
    bool sawExternalCall = false;
    for (const auto& use : function->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction || instruction->getOpcode() != Opc::CALL ||
            use.operandNo != 0 || !instruction->getParent() ||
            !instruction->getParent()->getParent()) {
            return false;
        }
        if (instruction->getParent()->getParent() != function) {
            sawExternalCall = true;
        }
    }
    if (!sawExternalCall) return false;

    return true;
}

// ----------------------------------------------------------------
// Transform
// ----------------------------------------------------------------

// Clones one instruction, remapping operands through `valueMap` and
// `blockMap`. Self-calls are redirected to `recursiveTarget` and receive
// the current root generation as their third argument.
// Mirrors the pattern used by LoopFullUnroll::cloneInstruction.
IR::Instruction* cloneBodyInstruction(
        IR::Instruction* instruction,
        IR::Function* selfFunction,
        IR::Function* recursiveTarget,
        IR::Value* generation,
        std::unordered_map<IR::Value*, IR::Value*>& valueMap,
        std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>& blockMap) {
    auto remap = [&](IR::Value* value) -> IR::Value* {
        if (!value) return nullptr;
        if (value == selfFunction) return recursiveTarget;
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
        bool isSelfCall = instruction->getOperand(0) == selfFunction;
        auto* callee = dynamic_cast<IR::Function*>(ops[0]);
        if (!callee) return nullptr;
        std::vector<IR::Value*> args(ops.begin() + 1, ops.end());
        if (isSelfCall) args.push_back(generation);
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
        IR::Function* recursiveTarget, IR::FunctionType* fallbackType,
        const std::string& fallbackName) {
    auto* fallback = module->createFunction(
        fallbackType, fallbackName, false);

    std::unordered_map<IR::Value*, IR::Value*> valueMap;
    std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> blockMap;
    for (unsigned i = 0; i < original->getNumArgs(); ++i) {
        valueMap[original->getArg(i)] = fallback->getArg(i);
    }
    auto* generation = fallback->getArg(2);
    for (auto& block : original->getBlocks()) {
        blockMap[block.get()] = fallback->createBlock(block->getName());
    }

    std::vector<std::pair<IR::Instruction*, IR::Instruction*>> clones;
    for (auto& block : original->getBlocks()) {
        auto* newBlock = blockMap[block.get()];
        for (auto& owned : block->getInstructions()) {
            auto* clone = cloneBodyInstruction(
                owned.get(), original, recursiveTarget, generation,
                valueMap, blockMap);
            if (!clone) return nullptr;
            valueMap[owned.get()] = clone;
            newBlock->pushBack(clone);
            clones.emplace_back(owned.get(), clone);
        }
    }

    // A valid SSA operand may be defined in a dominator block that appears
    // later in the module's block vector. The first pass can only use the
    // original value as a placeholder in that case; repair every operand
    // after the complete instruction map is available.
    auto finalRemap = [&](IR::Value* value) -> IR::Value* {
        if (!value) return nullptr;
        if (value == original) return recursiveTarget;
        if (auto* block = dynamic_cast<IR::BasicBlock*>(value)) {
            auto found = blockMap.find(block);
            return found != blockMap.end() ? found->second : nullptr;
        }
        if (dynamic_cast<IR::ConstantInt*>(value) ||
            dynamic_cast<IR::ConstantFloat*>(value) ||
            dynamic_cast<IR::GlobalVariable*>(value) ||
            dynamic_cast<IR::Function*>(value)) {
            return value;
        }
        auto found = valueMap.find(value);
        return found != valueMap.end() ? found->second : nullptr;
    };
    for (const auto& pair : clones) {
        auto* source = pair.first;
        auto* clone = pair.second;
        for (unsigned index = 0; index < source->getNumOperands(); ++index) {
            auto* sourceOperand = source->getOperand(index);
            auto* mapped = finalRemap(sourceOperand);
            if (sourceOperand && !mapped) return nullptr;
            clone->setOperand(index, mapped);
        }
    }
    return fallback;
}

// Build the internal memo lookup. It is never called directly from source
// code: the root wrapper supplies the generation and cloned self-calls carry
// that same value through the recursion.
void buildMemoLookup(
        IR::Function* memo, IR::Function* fallback,
        IR::GlobalVariable* keyATable, IR::GlobalVariable* keyBTable,
        IR::GlobalVariable* valueTable, IR::GlobalVariable* epochTable) {
    auto* i32 = IR::IntegerType::I32;
    auto* i1 = IR::IntegerType::I1;
    auto* entry = memo->createBlock("memo.entry");
    auto* hit = memo->createBlock("memo.hit");
    auto* miss = memo->createBlock("memo.miss");

    auto* argA = memo->getArg(0);
    auto* argB = memo->getArg(1);
    auto* generation = memo->getArg(2);
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* hashFactorA = IR::ConstantInt::get(i32, -1640531527);
    auto* hashFactorB = IR::ConstantInt::get(i32, -2048144789);
    auto* tableMask = IR::ConstantInt::get(
        i32, static_cast<int64_t>(kTableMask));
    auto* hashA = IR::Instruction::createBinOp(
        Opc::MUL, i32, "memo.hash.a", argA, hashFactorA);
    auto* hashB = IR::Instruction::createBinOp(
        Opc::MUL, i32, "memo.hash.b", argB, hashFactorB);
    auto* mixedHash = IR::Instruction::createBinOp(
        Opc::XOR, i32, "memo.hash.mixed", hashA, hashB);
    auto* epochIndex = IR::Instruction::createBinOp(
        Opc::AND, i32, "memo.hash.index", mixedHash, tableMask);
    auto* epochAddress = IR::Instruction::createGetElementPtr(
        i32, epochTable, {zero, epochIndex}, "memo.epoch.addr");
    auto* valueAddress = IR::Instruction::createGetElementPtr(
        i32, valueTable, {zero, epochIndex}, "memo.value.addr");
    auto* keyAAddress = IR::Instruction::createGetElementPtr(
        i32, keyATable, {zero, epochIndex}, "memo.key.a.addr");
    auto* keyBAddress = IR::Instruction::createGetElementPtr(
        i32, keyBTable, {zero, epochIndex}, "memo.key.b.addr");
    auto* cachedEpoch = IR::Instruction::createLoad(
        i32, epochAddress, "memo.epoch.cached");
    auto* cachedKeyA = IR::Instruction::createLoad(
        i32, keyAAddress, "memo.key.a.cached");
    auto* cachedKeyB = IR::Instruction::createLoad(
        i32, keyBAddress, "memo.key.b.cached");
    auto* epochMatches = IR::Instruction::createCmp(
        Opc::ICMP, cachedEpoch, generation, "eq");
    auto* keyAMatches = IR::Instruction::createCmp(
        Opc::ICMP, cachedKeyA, argA, "eq");
    auto* keyBMatches = IR::Instruction::createCmp(
        Opc::ICMP, cachedKeyB, argB, "eq");
    auto* epochAndKeyA = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.hit.epoch.key.a", epochMatches, keyAMatches);
    auto* cacheHit = IR::Instruction::createBinOp(
        Opc::AND, i1, "memo.hit.all.keys", epochAndKeyA, keyBMatches);
    entry->pushBack(hashA);
    entry->pushBack(hashB);
    entry->pushBack(mixedHash);
    entry->pushBack(epochIndex);
    entry->pushBack(epochAddress);
    entry->pushBack(valueAddress);
    entry->pushBack(keyAAddress);
    entry->pushBack(keyBAddress);
    entry->pushBack(cachedEpoch);
    entry->pushBack(cachedKeyA);
    entry->pushBack(cachedKeyB);
    entry->pushBack(epochMatches);
    entry->pushBack(keyAMatches);
    entry->pushBack(keyBMatches);
    entry->pushBack(epochAndKeyA);
    entry->pushBack(cacheHit);
    entry->pushBack(IR::Instruction::createCondBr(cacheHit, hit, miss));
    auto* cachedValue = IR::Instruction::createLoad(
        i32, valueAddress, "memo.value.cached");
    hit->pushBack(cachedValue);
    hit->pushBack(IR::Instruction::createRet(cachedValue));
    auto* computed = IR::Instruction::createCall(
        fallback->getFunctionType(), fallback,
        {argA, argB, generation}, "memo.computed");
    miss->pushBack(computed);
    miss->pushBack(IR::Instruction::createStore(computed, valueAddress));
    miss->pushBack(IR::Instruction::createStore(argA, keyAAddress));
    miss->pushBack(IR::Instruction::createStore(argB, keyBAddress));
    miss->pushBack(IR::Instruction::createStore(generation, epochAddress));
    miss->pushBack(IR::Instruction::createRet(computed));
}

// Rebuild the source-visible function as the root entry. It allocates a
// fresh generation for every dynamic call. Only a 32-bit wrap takes the
// clearing path; normal calls do not scan the table.
void buildRootWrapper(
        IR::Function* root, IR::Function* memo,
        IR::GlobalVariable* generationCounter,
        IR::GlobalVariable* epochTable) {
    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* tableEntries = IR::ConstantInt::get(
        i32, static_cast<int64_t>(kTableEntries));

    root->getBlocks().clear();
    auto* entry = root->createBlock("memo.root.entry");
    auto* clearInit = root->createBlock("memo.root.clear.init");
    auto* clearHeader = root->createBlock("memo.root.clear.header");
    auto* clearBody = root->createBlock("memo.root.clear.body");
    auto* invoke = root->createBlock("memo.root.invoke");

    auto* clearIndexSlot = IR::Instruction::createAlloca(
        i32, "memo.clear.index");
    auto* currentGeneration = IR::Instruction::createLoad(
        i32, generationCounter, "memo.generation.current");
    auto* nextGeneration = IR::Instruction::createBinOp(
        Opc::ADD, i32, "memo.generation.next", currentGeneration, one);
    auto* wrapped = IR::Instruction::createCmp(
        Opc::ICMP, nextGeneration, zero, "eq");
    auto* selectedGeneration = IR::Instruction::createSelect(
        wrapped, one, nextGeneration, "memo.generation.selected");
    entry->pushBack(clearIndexSlot);
    entry->pushBack(currentGeneration);
    entry->pushBack(nextGeneration);
    entry->pushBack(wrapped);
    entry->pushBack(selectedGeneration);
    entry->pushBack(IR::Instruction::createStore(
        selectedGeneration, generationCounter));
    entry->pushBack(IR::Instruction::createCondBr(
        wrapped, clearInit, invoke));

    clearInit->pushBack(IR::Instruction::createStore(zero, clearIndexSlot));
    clearInit->pushBack(IR::Instruction::createBr(clearHeader));

    auto* clearIndex = IR::Instruction::createLoad(
        i32, clearIndexSlot, "memo.clear.index.current");
    auto* indexInRange = IR::Instruction::createCmp(
        Opc::ICMP, clearIndex, tableEntries, "slt");
    clearHeader->pushBack(clearIndex);
    clearHeader->pushBack(indexInRange);
    clearHeader->pushBack(IR::Instruction::createCondBr(
        indexInRange, clearBody, invoke));

    auto* epochAddress = IR::Instruction::createGetElementPtr(
        i32, epochTable, {zero, clearIndex}, "memo.clear.epoch.addr");
    auto* nextIndex = IR::Instruction::createBinOp(
        Opc::ADD, i32, "memo.clear.index.next", clearIndex, one);
    clearBody->pushBack(epochAddress);
    clearBody->pushBack(IR::Instruction::createStore(zero, epochAddress));
    clearBody->pushBack(nextIndex);
    clearBody->pushBack(IR::Instruction::createStore(
        nextIndex, clearIndexSlot));
    clearBody->pushBack(IR::Instruction::createBr(clearHeader));

    auto* result = IR::Instruction::createCall(
        memo->getFunctionType(), memo,
        {root->getArg(0), root->getArg(1), selectedGeneration},
        "memo.root.result");
    invoke->pushBack(result);
    invoke->pushBack(IR::Instruction::createRet(result));
}

// Clone the original body into a fallback with an explicit generation
// parameter, create its memo lookup, then turn the original into the root
// entry. Returns false if cloning cannot represent the candidate body.
bool buildMemoWrapper(
        IR::Module* module, IR::Function* original, int uniqueId) {
    auto* i32 = IR::IntegerType::I32;
    std::string suffix = std::to_string(uniqueId);
    auto* recursiveType = IR::FunctionType::get(i32, {i32, i32, i32});
    auto* memo = module->createFunction(
        recursiveType, "__opt_memo_lookup_" + suffix, false);
    auto* fallback = cloneAsFallback(
        module, original, memo, recursiveType,
        "__opt_memo_fallback_" + suffix);
    if (!fallback) return false;

    auto* tableType = IR::ArrayType::get(i32, kTableEntries);
    auto* keyATable = module->createGlobalVariable(
        IR::PointerType::get(tableType),
        "__opt_memo_key_a_" + suffix, false);
    auto* keyBTable = module->createGlobalVariable(
        IR::PointerType::get(tableType),
        "__opt_memo_key_b_" + suffix, false);
    auto* valueTable = module->createGlobalVariable(
        IR::PointerType::get(tableType),
        "__opt_memo_value_" + suffix, false);
    auto* epochTable = module->createGlobalVariable(
        IR::PointerType::get(tableType),
        "__opt_memo_epoch_" + suffix, false);
    auto* generationCounter = module->createGlobalVariable(
        IR::PointerType::get(i32), "__opt_memo_generation_" + suffix,
        false, IR::ConstantInt::get(i32, 0));

    buildMemoLookup(
        memo, fallback, keyATable, keyBTable, valueTable, epochTable);
    buildRootWrapper(original, memo, generationCounter, epochTable);
    return true;
}

bool generatedNamesAvailable(IR::Module* module, int uniqueId) {
    std::string suffix = std::to_string(uniqueId);
    const std::vector<std::string> names = {
        "__opt_memo_lookup_" + suffix,
        "__opt_memo_fallback_" + suffix,
        "__opt_memo_key_a_" + suffix,
        "__opt_memo_key_b_" + suffix,
        "__opt_memo_value_" + suffix,
        "__opt_memo_epoch_" + suffix,
        "__opt_memo_generation_" + suffix,
    };
    auto isReserved = [&](const std::string& name) {
        for (const auto& reserved : names) {
            if (name == reserved) return true;
        }
        return false;
    };
    for (const auto& function : module->getFunctions()) {
        if (isReserved(function->getName())) return false;
    }
    for (const auto& global : module->getGlobals()) {
        if (isReserved(global->getName())) return false;
    }
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
    unsigned tablesBuilt = 0;
    for (auto* function : candidates) {
        if (tablesBuilt == kMaxMemoTablesPerModule) break;
        while (!generatedNamesAvailable(module, uniqueCounter)) {
            ++uniqueCounter;
        }
        int uniqueId = uniqueCounter++;
        if (buildMemoWrapper(module, function, uniqueId)) {
            changed = true;
            ++tablesBuilt;
        }
    }
    return changed;
}

} // namespace Opt
