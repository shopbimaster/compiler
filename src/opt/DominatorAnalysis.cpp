// ================================================================
// 共享支配者分析 — LICM/LoopUnroll/LoopInterchange 复用
// 避免多次重复计算支配树，减少编译时间
// ================================================================

#include "opt/Optimizer.h"

namespace Opt {

PredMap buildPredecessors(IR::Function* func) {
    PredMap pred;
    for (auto& bb : func->getBlocks()) {
        pred[bb.get()];
        auto* term = bb->getTerminator();
        if (!term) continue;
        if (term->getOpcode() == IR::Instruction::Opcode::BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(0)))
                pred[t].push_back(bb.get());
        } else if (term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(1)))
                pred[t].push_back(bb.get());
            if (auto* e = dynamic_cast<IR::BasicBlock*>(term->getOperand(2)))
                pred[e].push_back(bb.get());
        }
    }
    return pred;
}

SuccMap buildSuccessors(IR::Function* func) {
    SuccMap succ;
    for (auto& bb : func->getBlocks()) {
        succ[bb.get()];
        auto* term = bb->getTerminator();
        if (!term) continue;
        if (term->getOpcode() == IR::Instruction::Opcode::BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(0)))
                succ[bb.get()].push_back(t);
        } else if (term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(1)))
                succ[bb.get()].push_back(t);
            if (auto* e = dynamic_cast<IR::BasicBlock*>(term->getOperand(2)))
                succ[bb.get()].push_back(e);
        }
    }
    return succ;
}

DomMap computeDominators(IR::Function* func) {
    auto preds = buildPredecessors(func);
    auto* entry = func->getEntryBlock();
    if (!entry) return {};

    std::vector<IR::BasicBlock*> allBBs;
    for (auto& bb : func->getBlocks()) allBBs.push_back(bb.get());
    BBSet allSet(allBBs.begin(), allBBs.end());

    DomMap dom;
    for (auto* bb : allBBs) dom[bb] = allSet;
    dom[entry] = {entry};

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* bb : allBBs) {
            if (bb == entry) continue;
            BBSet inter = allSet;
            for (auto* p : preds[bb]) {
                BBSet temp;
                for (auto* d : inter)
                    if (dom[p].count(d)) temp.insert(d);
                inter = std::move(temp);
            }
            inter.insert(bb);
            if (inter != dom[bb]) {
                dom[bb] = std::move(inter);
                changed = true;
            }
        }
    }
    return dom;
}

bool strictlyDominates(IR::BasicBlock* a, IR::BasicBlock* b, const DomMap& dom) {
    auto it = dom.find(b);
    if (it == dom.end()) return false;
    return it->second.count(a) && a != b;
}

} // namespace Opt