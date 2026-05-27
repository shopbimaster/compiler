// ================================================================
// O1: 死代码消除（Aggressive DCE）— 从副作用指令反向标记活性
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

bool hasSideEffects(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    return op == Opc::STORE || op == Opc::CALL ||
           op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
           op == Opc::ALLOCA;
}

void dceOnFunction(IR::Function* func) {
    if (func->isExternal()) return;

    // 收集所有指令
    std::vector<IR::Instruction*> allInsts;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            allInsts.push_back(inst.get());
        }
    }

    // 从副作用指令出发，反向标记活性
    std::unordered_set<IR::Instruction*> live;
    std::vector<IR::Instruction*> worklist;

    for (auto* inst : allInsts) {
        if (hasSideEffects(inst)) {
            live.insert(inst);
            worklist.push_back(inst);
        }
    }

    while (!worklist.empty()) {
        auto* inst = worklist.back();
        worklist.pop_back();
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto* op = inst->getOperand(i);
            if (!op) continue;
            if (auto* defInst = dynamic_cast<IR::Instruction*>(op)) {
                if (live.insert(defInst).second) {
                    worklist.push_back(defInst);
                }
            }
        }
    }

    // 移除死指令
    bool removed = true;
    while (removed) {
        removed = false;
        for (auto& bb : func->getBlocks()) {
            for (auto it = bb->begin(); it != bb->end(); ) {
                if (!live.count(it->get())) {
                    (*it)->dropAllUses();
                    it = bb->erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
        }
    }
}

} // namespace

void deadCodeElimination(IR::Module* mod) {
    for (auto& func : mod->getFunctions()) {
        dceOnFunction(func.get());
    }
}

} // namespace Opt