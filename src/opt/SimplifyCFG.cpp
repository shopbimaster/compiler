// ================================================================
// SimplifyCFG — CFG 简化
// 1. 常量条件分支折叠：br i1 true, A, B → br A
// 2. 不可达基本块删除
// 3. 单前驱-单后继块合并（暂时禁用，待修复 26_scope4 兼容性）
// 4. 空块消除（暂时禁用，待修复 26_scope4 兼容性）
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <queue>
#include <unordered_set>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

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