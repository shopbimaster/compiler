// ================================================================
// SimplifyCFG — CFG 简化
// 1. 常量条件分支折叠：br i1 true, A, B → br A
// 2. 不可达基本块删除
// 3. 单前驱-单后继块合并（暂时禁用，待修复 26_scope4 兼容性）
// 4. 空块消除（暂时禁用，待修复 26_scope4 兼容性）
//
// ★ PHI 节点安全处理：
//   - foldConstantBranches: 折叠分支时，清理被移除边的 PHI 条目
//   - removeUnreachableBlocks: 删除块前，清理所有引用该块的 PHI 条目
//   防止悬空指针导致后续 Pass 崩溃。
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <queue>
#include <unordered_set>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ================================================================
// 清理 PHI 节点中引用指定前驱块的条目
// 将对应的 (value, block) 操作数对置为 null，避免悬空指针。
// PhiLowering 会跳过 null 操作数，因此这是安全的。
// ================================================================
void nullifyPhiEntriesForBlock(IR::Function* func, IR::BasicBlock* deadBlock) {
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != Opc::PHI) continue;
            // PHI 操作数是 (value, block) 对
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                if (inst->getOperand(i + 1) == deadBlock) {
                    // 置 null：setOperand 会从旧值的 use-list 中移除引用
                    inst->setOperand(i, nullptr);     // value
                    inst->setOperand(i + 1, nullptr); // block
                }
            }
        }
    }
}

// ================================================================
// 清理指定块中 PHI 节点引用特定前驱的条目
// 用于 foldConstantBranches：当 bb 不再跳转到 lostSucc 时，
// 清理 lostSucc 中 PHI 对 bb 的引用。
// ================================================================
void nullifyPhiEntriesForPredecessor(IR::BasicBlock* phiBlock, IR::BasicBlock* predBlock) {
    // PHI 必须在块开头，遍历到第一个非 PHI 指令即可停止
    for (auto& inst : phiBlock->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) break;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            if (inst->getOperand(i + 1) == predBlock) {
                inst->setOperand(i, nullptr);     // value
                inst->setOperand(i + 1, nullptr); // block
            }
        }
    }
}

// ---- 常量条件分支折叠 ----
bool foldConstantBranches(IR::Function* func) {
    bool changed = false;

    for (auto& bb : func->getBlocks()) {
        auto* term = bb->getTerminator();
        if (!term || term->getOpcode() != Opc::COND_BR) continue;

        auto* cond = term->getOperand(0);
        auto* ci = dynamic_cast<IR::ConstantInt*>(cond);
        if (!ci) continue;

        // 常量条件分支 → 无条件分支
        bool takeTrue = (ci->getValue() != 0);
        auto* target = takeTrue ? dynamic_cast<IR::BasicBlock*>(term->getOperand(1))
                                : dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
        if (!target) continue;

        // 被移除的目标块（不再跳转到的块）
        auto* lostSucc = takeTrue ? dynamic_cast<IR::BasicBlock*>(term->getOperand(2))
                                  : dynamic_cast<IR::BasicBlock*>(term->getOperand(1));

        // ★ 清理 lostSucc 中 PHI 对当前块的引用
        // 否则 PHI 会保留陈旧的 (value, bb) 条目，导致后续 Pass 错误
        if (lostSucc) {
            nullifyPhiEntriesForPredecessor(lostSucc, bb.get());
        }

        // 替换为无条件跳转
        auto* newBr = IR::Instruction::createBr(target);
        term->dropAllUses();
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (it->get() == term) {
                it = bb->erase(it);
                bb->insert(it, newBr);
                break;
            }
        }
        changed = true;
    }

    return changed;
}

// ---- 查找不可达块 ----
std::unordered_set<IR::BasicBlock*> findUnreachableBlocks(IR::Function* func) {
    auto succs = buildSuccessors(func);
    std::unordered_set<IR::BasicBlock*> reachable;
    std::queue<IR::BasicBlock*> worklist;

    auto* entry = func->getEntryBlock();
    if (!entry) return {};

    worklist.push(entry);
    reachable.insert(entry);

    while (!worklist.empty()) {
        auto* cur = worklist.front();
        worklist.pop();
        for (auto* succ : succs[cur]) {
            if (reachable.insert(succ).second)
                worklist.push(succ);
        }
    }

    std::unordered_set<IR::BasicBlock*> unreachable;
    for (auto& bb : func->getBlocks()) {
        if (!reachable.count(bb.get()))
            unreachable.insert(bb.get());
    }
    return unreachable;
}

// ---- 删除不可达块 ----
bool removeUnreachableBlocks(IR::Function* func) {
    auto unreachable = findUnreachableBlocks(func);
    if (unreachable.empty()) return false;

    // ★ 先清理所有 PHI 节点中对不可达块的引用
    // 防止删除块后产生悬空指针
    for (auto* bb : unreachable) {
        nullifyPhiEntriesForBlock(func, bb);
    }

    for (auto* bb : unreachable) {
        // 清除所有指令的引用
        for (auto& inst : bb->getInstructions()) {
            inst->dropAllUses();
        }
        // 从函数中移除该块
        auto& blocks = func->getBlocks();
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == bb) {
                blocks.erase(it);
                break;
            }
        }
    }

    return true;
}

// ---- 单函数 SimplifyCFG ----
bool simplifyCFGOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    if (func->getBlocks().empty()) return false;

    bool changed = false;
    bool iterChanged = true;

    while (iterChanged) {
        iterChanged = false;

        if (foldConstantBranches(func)) iterChanged = true;
        if (removeUnreachableBlocks(func)) iterChanged = true;
        // mergeBlocks 和 eliminateEmptyBlocks 暂时禁用
        // 已知问题：与 GlobalVariablePromotion 交互时在 26_scope4 上导致段错误
        // 需要在后续版本中修复 phi 节点更新和 ALLOCA 顺序保持逻辑

        if (iterChanged) changed = true;
    }

    return changed;
}

} // namespace

bool simplifyCFG(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (simplifyCFGOnFunction(func.get()))
                changed = true;
        }
        if (changed) anyChanged = true;
    }
    return anyChanged;
}

} // namespace Opt
