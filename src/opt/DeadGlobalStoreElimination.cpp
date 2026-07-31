#include "opt/Optimizer.h"

#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

bool collectWriteOnlyStores(
    IR::Value* pointer, std::unordered_set<IR::Value*>& visited,
    std::vector<IR::Instruction*>& stores) {
    if (!pointer || !visited.insert(pointer).second) return true;
    for (const auto& use : pointer->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) return false;
        if (instruction->getOpcode() == Opc::GETELEMENTPTR &&
            use.operandNo == 0) {
            if (!collectWriteOnlyStores(
                    instruction, visited, stores)) {
                return false;
            }
            continue;
        }
        if (instruction->getOpcode() == Opc::STORE &&
            use.operandNo == 1 &&
            instruction->getNumOperands() == 2) {
            stores.push_back(instruction);
            continue;
        }
        return false;
    }
    return true;
}

bool eraseInstruction(IR::Instruction* instruction) {
    auto* block = instruction ? instruction->getParent() : nullptr;
    if (!block) return false;
    for (auto iterator = block->begin(); iterator != block->end();
         ++iterator) {
        if (iterator->get() != instruction) continue;
        block->erase(iterator);
        return true;
    }
    return false;
}

} // namespace

bool deadGlobalStoreElimination(IR::Module* module) {
    std::vector<IR::Instruction*> deadStores;
    for (const auto& global : module->getGlobals()) {
        std::unordered_set<IR::Value*> visited;
        std::vector<IR::Instruction*> stores;
        if (collectWriteOnlyStores(global.get(), visited, stores) &&
            !stores.empty()) {
            deadStores.insert(
                deadStores.end(), stores.begin(), stores.end());
        }
    }

    bool changed = false;
    for (auto* store : deadStores) {
        changed |= eraseInstruction(store);
    }
    return changed;
}

} // namespace Opt
