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

// ================================================================
// 后支配树（PostDominatorTree）— 用于 ADCE
// 后支配：d 后支配 n 当且仅当从 n 到函数出口的所有路径都经过 d
// 通过反转 CFG（前驱↔后继）并计算支配树得到
// ================================================================

// 获取函数的出口块（RET 指令所在的块）
static std::vector<IR::BasicBlock*> getExitBlocks(IR::Function* func) {
    std::vector<IR::BasicBlock*> exits;
    for (auto& bb : func->getBlocks()) {
        auto* term = bb->getTerminator();
        if (term && term->getOpcode() == IR::Instruction::Opcode::RET) {
            exits.push_back(bb.get());
        }
    }
    return exits;
}

// 计算后支配树：以（虚拟）出口块为根，在反转 CFG 上计算支配树
// 如果只有一个出口，以该出口为根；如果有多个出口，创建一个虚拟根
// 返回：每个块的后支配者集合
DomMap computePostDominators(IR::Function* func) {
    auto succs = buildSuccessors(func);
    auto preds = buildPredecessors(func);
    auto exits = getExitBlocks(func);

    if (exits.empty()) return {};

    std::vector<IR::BasicBlock*> allBBs;
    for (auto& bb : func->getBlocks()) allBBs.push_back(bb.get());
    BBSet allSet(allBBs.begin(), allBBs.end());

    // 反转 CFG：原来的后继变成"前驱"（用于后支配计算）
    // 即：在反转图上，bb 的"前驱"是原图中 bb 的后继
    auto& revPreds = succs; // 反转后：后继→"前驱"

    DomMap pdom;
    for (auto* bb : allBBs) pdom[bb] = allSet;

    if (exits.size() == 1) {
        // 单出口：以该出口为根
        pdom[exits[0]] = {exits[0]};
    } else {
        // 多出口：所有出口块的后支配者只有自己
        // 实际上对于多出口函数，每个出口后支配自己但不后支配其他块
        // 简化：每个出口只后支配自己，其余块的后支配集是所有块的交集
        for (auto* exit : exits) {
            pdom[exit] = {exit};
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* bb : allBBs) {
            // 跳过出口块（已固定）
            bool isExit = false;
            for (auto* e : exits) {
                if (e == bb) { isExit = true; break; }
            }
            if (isExit) continue;

            BBSet inter = allSet;
            bool first = true;
            for (auto* s : revPreds[bb]) {
                if (first) {
                    inter = pdom[s];
                    first = false;
                } else {
                    BBSet temp;
                    for (auto* d : inter)
                        if (pdom[s].count(d)) temp.insert(d);
                    inter = std::move(temp);
                }
            }
            inter.insert(bb);
            if (inter != pdom[bb]) {
                pdom[bb] = std::move(inter);
                changed = true;
            }
        }
    }
    return pdom;
}

// 计算后支配边界（PostDominanceFrontier）
// PDF(B) = 后支配树中，B 的后支配边界
// 用于 ADCE：当指令在块 B 中被标记为 live 时，
// B 的后支配边界中的条件分支也需要被标记为 live
using PDFMap = std::unordered_map<IR::BasicBlock*, BBSet>;

PDFMap computePostDominanceFrontier(IR::Function* func, const DomMap& pdom) {
    PDFMap pdf;
    auto succs = buildSuccessors(func);

    for (auto& bb : func->getBlocks()) {
        pdf[bb.get()];
    }

    for (auto& bb : func->getBlocks()) {
        auto* b = bb.get();
        auto& succList = succs[b];
        if (succList.size() < 2) continue;

        for (auto* s : succList) {
            // 如果 b 不严格后支配 s，则 s 在 b 的后支配边界中
            auto it = pdom.find(s);
            if (it == pdom.end()) continue;
            if (!it->second.count(b) || b == s) {
                pdf[b].insert(s);
            }
        }
    }
    return pdf;
}

} // namespace Opt