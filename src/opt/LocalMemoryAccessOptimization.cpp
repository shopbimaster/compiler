#include "opt/Optimizer.h"
#include "opt/MemoryAccessAnalysis.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// Keep replacements local enough that eliminating an address or load does
// not create a long live range and increase register pressure.  This is a
// target-independent profitability bound measured in IR instructions.
constexpr std::size_t kLocalReuseWindow = 16;

struct GEPKey {
    IR::Type* type = nullptr;
    std::vector<IR::Value*> operands;

    bool operator==(const GEPKey& other) const {
        return type == other.type && operands == other.operands;
    }
};

struct GEPKeyHash {
    std::size_t operator()(const GEPKey& key) const {
        auto pointerHash = std::hash<std::uintptr_t>{};
        std::size_t seed = pointerHash(
            reinterpret_cast<std::uintptr_t>(key.type));
        for (auto* operand : key.operands) {
            const auto value = pointerHash(
                reinterpret_cast<std::uintptr_t>(operand));
            seed ^= value + 0x9e3779b9U +
                    (seed << 6U) + (seed >> 2U);
        }
        return seed;
    }
};

struct AvailableValue {
    IR::Value* value = nullptr;
    std::size_t position = 0;
};

AllocaArgumentMap buildStableArgumentMap(IR::Function* function) {
    std::unordered_map<IR::Value*, std::vector<IR::Value*>> storedValues;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            auto* slot = dynamic_cast<IR::Instruction*>(
                instruction->getOperand(1));
            if (slot && slot->getOpcode() == Opc::ALLOCA) {
                storedValues[slot].push_back(
                    instruction->getOperand(0));
            }
        }
    }

    AllocaArgumentMap result;
    for (const auto& [slot, values] : storedValues) {
        if (values.empty()) continue;
        auto* argument = dynamic_cast<IR::Argument*>(values.front());
        if (!argument) continue;

        bool stable = true;
        for (auto* value : values) {
            if (value != argument) {
                stable = false;
                break;
            }
        }
        if (!stable) continue;

        // The slot must be used only by direct loads and stores.  Any GEP,
        // call, merge, or other use lets an indirect write bypass the store
        // scan above, so the argument-root proof is abandoned.
        for (const auto& use : slot->getUses()) {
            auto* user = dynamic_cast<IR::Instruction*>(use.user);
            if (!user ||
                !((user->getOpcode() == Opc::LOAD &&
                   use.operandNo == 0) ||
                  (user->getOpcode() == Opc::STORE &&
                   use.operandNo == 1))) {
                stable = false;
                break;
            }
        }
        if (stable) result[slot] = argument;
    }
    return result;
}

GEPKey makeGEPKey(IR::Instruction* instruction) {
    GEPKey key;
    key.type = instruction->getType();
    key.operands.reserve(instruction->getNumOperands());
    for (unsigned index = 0;
         index < instruction->getNumOperands(); ++index) {
        key.operands.push_back(instruction->getOperand(index));
    }
    return key;
}

IR::Value* pointerRoot(
    IR::Value* pointer,
    const AllocaArgumentMap& argumentMap) {
    auto* instruction = dynamic_cast<IR::Instruction*>(pointer);
    while (instruction &&
           instruction->getOpcode() == Opc::GETELEMENTPTR &&
           instruction->getNumOperands() > 0) {
        pointer = instruction->getOperand(0);
        instruction = dynamic_cast<IR::Instruction*>(pointer);
    }
    if (instruction && instruction->getOpcode() == Opc::LOAD &&
        instruction->getNumOperands() == 1) {
        auto found = argumentMap.find(instruction->getOperand(0));
        if (found != argumentMap.end()) return found->second;
    }
    return pointer;
}

bool isAllocaRoot(IR::Value* value) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    return instruction && instruction->getOpcode() == Opc::ALLOCA;
}

// This deliberately proves only a small set of no-alias cases:
//
//  * distinct allocas in the current invocation are disjoint;
//  * a current alloca cannot be reached by an incoming pointer;
//  * distinct global objects are disjoint under defined source semantics.
//
// Arguments, loaded pointers, PHIs, selects, and unsupported pointer forms
// remain may-alias.  The pass never relies on different argument values being
// independent.
bool mayAlias(
    IR::Value* first, IR::Value* second,
    const AllocaArgumentMap& argumentMap) {
    if (first == second) return true;

    auto* firstRoot = pointerRoot(first, argumentMap);
    auto* secondRoot = pointerRoot(second, argumentMap);
    if (!firstRoot || !secondRoot) return true;
    if (firstRoot == secondRoot) return true;

    const bool firstAlloca = isAllocaRoot(firstRoot);
    const bool secondAlloca = isAllocaRoot(secondRoot);
    const bool firstArgument =
        dynamic_cast<IR::Argument*>(firstRoot) != nullptr;
    const bool secondArgument =
        dynamic_cast<IR::Argument*>(secondRoot) != nullptr;

    auto* firstGlobal =
        dynamic_cast<IR::GlobalVariable*>(firstRoot);
    auto* secondGlobal =
        dynamic_cast<IR::GlobalVariable*>(secondRoot);
    if (firstAlloca &&
        (secondAlloca || secondArgument || secondGlobal)) {
        return false;
    }
    if (secondAlloca &&
        (firstAlloca || firstArgument || firstGlobal)) {
        return false;
    }
    if (firstGlobal && secondGlobal) return false;

    return true;
}

bool eliminateShortRangeGEPs(IR::BasicBlock* block) {
    std::unordered_map<GEPKey, AvailableValue, GEPKeyHash> available;
    bool changed = false;
    std::size_t position = 0;

    for (auto iterator = block->begin();
         iterator != block->end();) {
        auto* instruction = iterator->get();
        if (instruction->getOpcode() != Opc::GETELEMENTPTR) {
            ++position;
            ++iterator;
            continue;
        }

        auto key = makeGEPKey(instruction);
        auto found = available.find(key);
        if (found != available.end() &&
            position - found->second.position <= kLocalReuseWindow) {
            instruction->replaceAllUsesWith(found->second.value);
            instruction->dropAllUses();
            iterator = block->erase(iterator);
            changed = true;
            continue;
        }

        available[key] = {instruction, position};
        ++position;
        ++iterator;
    }
    return changed;
}

bool eliminateShortRangeLoads(
    IR::BasicBlock* block,
    const AllocaArgumentMap& argumentMap) {
    std::unordered_map<IR::Value*, AvailableValue> knownMemory;
    bool changed = false;
    std::size_t position = 0;

    for (auto iterator = block->begin();
         iterator != block->end();) {
        auto* instruction = iterator->get();

        if (instruction->getOpcode() == Opc::CALL) {
            knownMemory.clear();
            ++position;
            ++iterator;
            continue;
        }

        if (instruction->getOpcode() == Opc::STORE &&
            instruction->getNumOperands() == 2) {
            auto* storedValue = instruction->getOperand(0);
            auto* pointer = instruction->getOperand(1);
            for (auto known = knownMemory.begin();
                 known != knownMemory.end();) {
                if (mayAlias(
                        known->first, pointer, argumentMap)) {
                    known = knownMemory.erase(known);
                } else {
                    ++known;
                }
            }
            knownMemory[pointer] = {storedValue, position};
            ++position;
            ++iterator;
            continue;
        }

        if (instruction->getOpcode() == Opc::LOAD &&
            instruction->getNumOperands() == 1) {
            auto* pointer = instruction->getOperand(0);
            auto found = knownMemory.find(pointer);
            if (found != knownMemory.end() &&
                position - found->second.position <= kLocalReuseWindow) {
                instruction->replaceAllUsesWith(found->second.value);
                instruction->dropAllUses();
                iterator = block->erase(iterator);
                changed = true;
                continue;
            }
            knownMemory[pointer] = {instruction, position};
        }

        ++position;
        ++iterator;
    }
    return changed;
}

bool optimizeFunction(IR::Function* function) {
    if (!function || function->isExternal()) return false;

    bool changed = false;
    const auto argumentMap = buildStableArgumentMap(function);
    for (auto& block : function->getBlocks()) {
        // Normalize equivalent addresses first, so exact-pointer load/store
        // forwarding can consume the canonical pointer value.
        changed |= eliminateShortRangeGEPs(block.get());
        changed |= eliminateShortRangeLoads(
            block.get(), argumentMap);
    }
    return changed;
}

} // namespace

bool localMemoryAccessOptimization(IR::Module* module) {
    bool changed = false;
    for (auto& function : module->getFunctions()) {
        changed |= optimizeFunction(function.get());
    }
    return changed;
}

} // namespace Opt
