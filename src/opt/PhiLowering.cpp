// ================================================================
// PhiLowering — 将 PHI 指令转换为 ALLOCA + STORE + LOAD
//
// 在 Mem2Reg 引入 PHI 指令后，代码生成前需要将 PHI 降级为
// 普通指令。采用"内存降低"策略：
//   1. 为每个 PHI 在 entry 块创建 ALLOCA
//   2. 在每个前驱块中 STORE 源值到 ALLOCA
//   3. 将 PHI 替换为从 ALLOCA 的 LOAD
//
// TargetCodeGen 的 promoteAllocasInFunction 随后会将 ALLOCA
// 提升到寄存器，有效将 STORE/LOAD 对转换为寄存器拷贝。
//
// ★ 简化设计：不使用临时 ALLOCA 方案。
//   临时 ALLOCA 会消耗有限的 callee-saved 寄存器（s0-s11），
//   当 PHI 节点较多时导致寄存器不够分配（12_DSU SEGFAULT 根因）。
//   直接在前驱块末尾 STORE 源值，让寄存器分配器处理 live range。
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <unordered_map>

namespace Opt {

bool phiLowering(IR::Module* mod) {
    bool changed = false;

    for (auto& func : mod->getFunctions()) {
        if (func->isExternal() || func->getBlocks().empty())
            continue;

        // 收集所有 PHI 指令
        struct PhiInfo {
            IR::Instruction* phi;
            IR::BasicBlock* bb;
        };
        std::vector<PhiInfo> phis;

        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::PHI) {
                    phis.push_back({inst.get(), bb.get()});
                }
            }
        }

        if (phis.empty()) continue;

        auto* entry = func->getEntryBlock();
        if (!entry) continue;

        for (auto& [phi, bb] : phis) {
            IR::Type* phiType = phi->getType();
            if (!phiType || phiType->isVoid()) continue;

            // 1. 在 entry 块创建 ALLOCA
            auto* alloca = IR::Instruction::createAlloca(
                phiType, "%" + phi->getName() + ".phi.ptr");
            auto insertPos = entry->begin();
            while (insertPos != entry->end() &&
                   (*insertPos)->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
                ++insertPos;
            }
            entry->insert(insertPos, alloca);

            // 2. 在每个前驱块中 STORE 源值到 ALLOCA（插入到 terminator 之前）
            for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
                IR::Value* srcVal = phi->getOperand(i);
                IR::Value* predBlock = phi->getOperand(i + 1);
                auto* predBB = dynamic_cast<IR::BasicBlock*>(predBlock);
                if (!predBB || !srcVal) continue;

                // 直接 STORE 源值到 PHI ALLOCA
                auto* store = IR::Instruction::createStore(srcVal, alloca);

                // 插入到前驱块 terminator 之前
                auto* term = predBB->getTerminator();
                if (term) {
                    for (auto it = predBB->begin(); it != predBB->end(); ++it) {
                        if (it->get() == term) {
                            predBB->insert(it, store);
                            break;
                        }
                    }
                } else {
                    predBB->pushBack(store);
                }
            }

            // 3. 创建 LOAD 替换 PHI
            auto* load = IR::Instruction::createLoad(
                phiType, alloca, "%" + phi->getName());

            // 4. 替换所有对 PHI 结果的使用
            phi->replaceAllUsesWith(load);

            // 5. 将 LOAD 插入到 PHI 原来的位置，并移除 PHI
            for (auto it = bb->begin(); it != bb->end(); ++it) {
                if (it->get() == phi) {
                    it = bb->erase(it);
                    bb->insert(it, load);
                    break;
                }
            }

            changed = true;
        }
    }

    return changed;
}

} // namespace Opt
