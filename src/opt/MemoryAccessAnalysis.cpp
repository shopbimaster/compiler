#include "opt/MemoryAccessAnalysis.h"

#include <unordered_set>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
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

} // namespace

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

bool analyzeGlobalMemoryEffects(
    IR::GlobalVariable* global,
    GlobalMemoryEffects& effects) {
    if (!global) return false;

    effects = {};
    std::vector<IR::Value*> worklist = {global};
    std::unordered_set<IR::Value*> visited;
    std::unordered_set<IR::Instruction*> seenLoads;
    std::unordered_set<IR::Instruction*> seenStores;
    std::unordered_set<IR::Instruction*> seenCalls;

    while (!worklist.empty()) {
        auto* pointer = worklist.back();
        worklist.pop_back();
        if (!visited.insert(pointer).second) continue;

        for (const auto& use : pointer->getUses()) {
            auto* instruction =
                dynamic_cast<IR::Instruction*>(use.user);
            if (!instruction) return false;

            switch (instruction->getOpcode()) {
                case Opc::GETELEMENTPTR:
                    if (use.operandNo != 0) return false;
                    worklist.push_back(instruction);
                    break;
                case Opc::LOAD:
                    if (use.operandNo != 0) return false;
                    if (seenLoads.insert(instruction).second) {
                        effects.loads.push_back(instruction);
                    }
                    break;
                case Opc::STORE:
                    if (use.operandNo != 1) {
                        // The pointer value itself is being stored.
                        return false;
                    }
                    if (seenStores.insert(instruction).second) {
                        effects.stores.push_back(instruction);
                    }
                    break;
                case Opc::CALL:
                    if (use.operandNo == 0) return false;
                    if (seenCalls.insert(instruction).second) {
                        effects.calls.push_back(instruction);
                    }
                    break;
                default:
                    return false;
            }
        }
    }
    return true;
}

} // namespace Opt
