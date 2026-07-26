// ================================================================
// O2: 跳转线程化（Jump Threading）
// ----------------------------------------------------------------
// 目标：消除冗余的跳转链，直接跳转到最终目标
//
// 转换模式：
//   BB1: br BB2
//   BB2: br BB3       =>   BB1: br BB3 (直接跳转)
//
//   BB1: br cond, BB2, BB3
//   BB2: br BB4       =>   BB1: br cond, BB4, BB3
//
// 收益：
//   - 减少分支指令数量
//   - 改善分支预测
//   - 简化CFG，有利于后续优化
//
// 安全性：
//   - 只处理无PHI节点或PHI节点可安全更新的情况
//   - 保守策略：遇到复杂情况立即放弃
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>

namespace Opt {
namespace {

// 检查BB是否只包含一个无条件跳转
bool isSimpleForwarder(IR::BasicBlock* bb) {
    auto& insts = bb->getInstructions();
    if (insts.empty()) return false;

    // 只有一条指令，且是BR
    if (insts.size() == 1) {
        auto* term = insts.back().get();
        if (term->getOpcode() == IR::Instruction::Opcode::BR &&
            term->getNumOperands() == 1) {
            return true;
        }
    }

    return false;
}

// 获取BB的唯一后继（如果是简单转发器）
IR::BasicBlock* getForwardTarget(IR::BasicBlock* bb) {
    if (!isSimpleForwarder(bb)) return nullptr;

    auto* term = bb->getInstructions().back().get();
    auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
    return target;
}

// 检查是否可以安全地绕过BB（考虑PHI节点）
bool canBypass(IR::BasicBlock* bb) {
    // 如果目标BB没有PHI节点，总是安全的
    auto* target = getForwardTarget(bb);
    if (!target) return false;

    // 检查target是否有PHI节点
    for (auto& inst : target->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::PHI) {
            // 有PHI节点，需要谨慎处理
            // 简化策略：如果bb只有一个前驱，可以安全绕过
            // （更复杂的情况留给PHI lowering处理）
            // 暂时保守：遇到PHI就放弃
            return false;
        }
        // PHI节点总是在BB开头，遇到非PHI就可以停止检查
        if (inst->getOpcode() != IR::Instruction::Opcode::PHI) {
            break;
        }
    }

    return true;
}

// 线程化一个分支：将from的跳转目标从old改为new
bool threadBranch(IR::BasicBlock* from, IR::BasicBlock* oldTarget,
                  IR::BasicBlock* newTarget) {
    if (!from || !oldTarget || !newTarget) return false;
    if (oldTarget == newTarget) return false;

    auto& insts = from->getInstructions();
    if (insts.empty()) return false;

    auto* term = insts.back().get();
    auto op = term->getOpcode();

    // 无条件跳转：br label
    if (op == IR::Instruction::Opcode::BR && term->getNumOperands() == 1) {
        if (term->getOperand(0) == oldTarget) {
            term->setOperand(0, newTarget);
            return true;
        }
    }

    // 条件跳转：br cond, label1, label2
    if (op == IR::Instruction::Opcode::BR && term->getNumOperands() == 3) {
        bool changed = false;
        if (term->getOperand(1) == oldTarget) {
            term->setOperand(1, newTarget);
            changed = true;
        }
        if (term->getOperand(2) == oldTarget) {
            term->setOperand(2, newTarget);
            changed = true;
        }
        return changed;
    }

    return false;
}

// 在一个函数中执行跳转线程化
bool threadJumpsInFunction(IR::Function* func) {
    bool changed = false;

    // 收集所有简单转发器BB
    std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> forwarders;
    for (auto& bb : func->getBlocks()) {
        if (isSimpleForwarder(bb.get())) {
            auto* target = getForwardTarget(bb.get());
            if (target && canBypass(bb.get())) {
                forwarders[bb.get()] = target;
            }
        }
    }

    if (forwarders.empty()) return false;

    // 对每个BB，检查其后继是否可以线程化
    for (auto& bb : func->getBlocks()) {
        auto& insts = bb->getInstructions();
        if (insts.empty()) continue;

        auto* term = insts.back().get();
        auto op = term->getOpcode();

        if (op != IR::Instruction::Opcode::BR) continue;

        // 收集这个BR的所有目标
        std::vector<IR::BasicBlock*> targets;
        if (term->getNumOperands() == 1) {
            // 无条件跳转
            auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
            if (target) targets.push_back(target);
        } else if (term->getNumOperands() == 3) {
            // 条件跳转
            auto* trueTarget = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
            auto* falseTarget = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
            if (trueTarget) targets.push_back(trueTarget);
            if (falseTarget) targets.push_back(falseTarget);
        }

        // 对每个目标，如果它是转发器，尝试线程化
        for (auto* target : targets) {
            auto it = forwarders.find(target);
            if (it != forwarders.end()) {
                auto* finalTarget = it->second;
                // 避免创建自循环（除非原本就有）
                if (finalTarget != bb.get() || target == bb.get()) {
                    if (threadBranch(bb.get(), target, finalTarget)) {
                        changed = true;
                    }
                }
            }
        }
    }

    return changed;
}

} // namespace

bool jumpThreading(IR::Module* mod) {
    if (!mod) return false;

    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        if (threadJumpsInFunction(func.get())) {
            changed = true;
        }
    }

    return changed;
}

} // namespace Opt
