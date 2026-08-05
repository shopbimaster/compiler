#include "opt/MemoryAccessAnalysis.h"
#include "opt/Optimizer.h"

#include <unordered_set>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

} // namespace

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