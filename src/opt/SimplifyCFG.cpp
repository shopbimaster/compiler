// ================================================================
// SimplifyCFG — CFG 简化
// 1. 删除 terminator 之后的死指令（连续 break/return 产生的死代码）
// 2. 常量条件分支折叠：br i1 true, A, B → br A
// 3. 不可达基本块删除
// 4. 空块消除：单前驱单后继的空跳转块直接旁路
//
// ★ PHI 节点安全处理：
//   - foldConstantBranches: 折叠分支时，清理被移除边的 PHI 条目
//   - removeUnreachableBlocks: 删除块前，清理所有引用该块的 PHI 条目
//   - eliminateEmptyBlocks: 更新 target 的 PHI 前驱引用
//   防止悬空指针导致后续 Pass 崩溃。
//
// ★ replaceAllUsesWith 健壮性：
//   Value::replaceAllUsesWith 会验证 use-list 条目的有效性，
//   自动跳过陈旧条目（user 的 operand 已不再指向 this），
//   防止错误 nullify 其他指令的操作数。
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <queue>
#include <unordered_set>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ================================================================
// PHI 节点规范化：将所有 PHI 节点移动到块的开头
//
// 某些优化 Pass（InstCombine/CodeSink/LICM 等）可能在 PHI 节点之间
// 插入非 PHI 指令，破坏 "PHI 必须在块首" 的 IR 不变量。
// 这会导致后续遍历 PHI 的代码（使用 break 遇到非 PHI 即停止）
// 漏掉部分 PHI 节点，引发严重 bug：
//   - foldSinglePredBlock 漏更新 PHI 前驱 → nullifyPhiEntriesForBlock
//     将未更新的 PHI 条目置 null → SEGFAULT（39_fp_params 根因）
//
// 本函数将所有 PHI 节点移动到块首，恢复 IR 不变量。
// ================================================================
void normalizePhiNodes(IR::Function* func) {
    for (auto& bb : func->getBlocks()) {
        // 使用 bb->begin()/end() 而非 getInstructions()，因为后者返回 const 引用，
        // 无法 std::move（unique_ptr 不可复制）
        if (bb->begin() == bb->end()) continue;

        // 收集所有 PHI 指令的指针（不转移所有权）
        std::vector<IR::Instruction*> phiInsts;
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if ((*it)->getOpcode() == Opc::PHI) {
                phiInsts.push_back(it->get());
            }
        }

        if (phiInsts.empty()) continue;

        // 检查 PHI 是否已全部在块首
        bool alreadyNormalized = true;
        {
            auto it = bb->begin();
            for (size_t i = 0; i < phiInsts.size() && it != bb->end(); ++i, ++it) {
                if (it->get() != phiInsts[i]) {
                    alreadyNormalized = false;
                    break;
                }
            }
        }
        if (alreadyNormalized) continue;

        // 释放 PHI 指令的所有权并从原位置移除
        std::vector<std::unique_ptr<IR::Instruction>> phiOwned;
        for (auto* phi : phiInsts) {
            for (auto it = bb->begin(); it != bb->end(); ++it) {
                if (it->get() == phi) {
                    phiOwned.push_back(std::move(*it));
                    bb->erase(it);
                    break;
                }
            }
        }

        // 将 PHI 插入到块首（保持原有相对顺序）
        // bb->insert 接收 Instruction* 裸指针，接管所有权
        auto insertPos = bb->begin();
        for (auto& phi : phiOwned) {
            phi->setParent(bb.get());
            insertPos = bb->insert(insertPos, phi.release());
            ++insertPos;
        }
    }
}

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
    // 使用 continue 而非 break：PHI 节点可能因前序 Pass 被交错在非 PHI 指令之间，
    // 必须扫描全部指令以收集所有 PHI 节点。
    for (auto& inst : phiBlock->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            if (inst->getOperand(i + 1) == predBlock) {
                inst->setOperand(i, nullptr);     // value
                inst->setOperand(i + 1, nullptr); // block
            }
        }
    }
}

// ---- 同目标条件分支折叠 ----
// br i1 cond, A, A → br A（IfConversion 后可能产生此模式）
bool foldSameTargetBranches(IR::Function* func) {
    bool changed = false;

    for (auto& bb : func->getBlocks()) {
        auto* term = bb->getTerminator();
        if (!term || term->getOpcode() != Opc::COND_BR) continue;

        auto* trueDest = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
        auto* falseDest = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
        if (!trueDest || !falseDest) continue;
        if (trueDest != falseDest) continue;

        // 两个目标相同 → 替换为无条件分支
        auto* newBr = IR::Instruction::createBr(trueDest);
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
// 关键安全步骤（顺序不可调换）：
//   1. replaceAllUsesWith(nullptr)：移除可达块中对不可达块内指令的引用
//      （修复 use-after-free：可达指令引用已删除的 PHI → DCE dynamic_cast 崩溃）
//   2. nullifyPhiEntriesForBlock：清理 PHI 中对不可达块本身的引用
//   3. setOperand(i, nullptr)：移除不可达块内指令对其他值的引用
//   4. bb->replaceAllUsesWith(nullptr)：移除 BR/COND_BR 中对不可达块的引用
//   5. 从 blocks vector 中删除
bool removeUnreachableBlocks(IR::Function* func) {
    auto unreachable = findUnreachableBlocks(func);
    if (unreachable.empty()) return false;

    // ★ Step 1: 将不可达块内所有指令的所有 uses 替换为 null
    //   这会安全地从可达块中移除对不可达块内指令（含 PHI）的引用
    //   修复 ASan 发现的 heap-use-after-free：
    //   Mem2Reg 创建的 PHI 在不可达块中，被可达块中的指令引用，
    //   删除块后 PHI 被释放，DCE 遍历可达指令时 dynamic_cast 悬空指针崩溃
    for (auto* bb : unreachable) {
        for (auto& inst : bb->getInstructions()) {
            inst->replaceAllUsesWith(nullptr);
        }
    }

    // ★ Step 2: 清理所有 PHI 节点中对不可达块本身的引用
    for (auto* bb : unreachable) {
        nullifyPhiEntriesForBlock(func, bb);
    }

    // ★ Step 3: 将所有不可达块中指令的操作数置为 null
    //   避免删除块 A 时，A 的指令引用了另一个不可达块 B（已被删除）导致段错误
    for (auto* bb : unreachable) {
        for (auto& inst : bb->getInstructions()) {
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                inst->setOperand(i, nullptr);
            }
        }
    }

    // ★ Step 4: 安全删除所有不可达块
    //   不调用 bb->replaceAllUsesWith(nullptr)，因为可能 nullify 可达块中
    //   BR/COND_BR 的目标（虽然理论上不应发生，但某些 Pass 可能留下残留引用）。
    //   不可达块中的 BR 目标已在 Step 3 被 nullify，删除时析构函数会安全跳过 null 操作数。
    for (auto* bb : unreachable) {
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

// ---- 空块消除 ----
// 如果块 B 只有一条无条件跳转指令（br target），且只有一个前驱 P（BR），
// 则将 P 的 terminator 中对 B 的引用替换为 target，然后安全删除 B。
// 安全检查：
//   1. B 不是 entry block
//   2. B 只有一条指令（terminator br target），没有 PHI 节点
//   3. B 只有一个前驱 P，且 P 的 terminator 是 BR
//   4. target != B（避免自循环）
//   5. 更新 target 的 PHI 中对 B 的引用为 P
//   6. 安全删除 B
bool eliminateEmptyBlocks(IR::Function* func) {
    bool changed = false;
    auto* entry = func->getEntryBlock();

    bool iterChanged = true;
    while (iterChanged) {
        iterChanged = false;

        auto preds = buildPredecessors(func);

        for (auto& bb : func->getBlocks()) {
            auto* B = bb.get();
            if (B == entry) continue;

            // 检查 B 是否只有一条指令（terminator）
            auto& insts = B->getInstructions();
            if (insts.size() != 1) continue;
            auto* term = B->getTerminator();
            if (!term || term->getOpcode() != Opc::BR) continue;

            // 检查 B 是否只有一个前驱
            auto it = preds.find(B);
            if (it == preds.end() || it->second.size() != 1) continue;
            auto* P = it->second[0];

            // 安全限制：只处理 P 的 terminator 是 BR（无条件跳转）的情况
            auto* Pterm = P->getTerminator();
            if (!Pterm || Pterm->getOpcode() != Opc::BR) continue;

            // 获取 B 的跳转目标
            auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
            if (!target || target == B) continue;

            // 更新 target 的 PHI 节点：将 B 的引用替换为 P
            // 使用 continue 而非 break：PHI 节点可能因前序 Pass 被交错
            for (auto& inst : target->getInstructions()) {
                if (inst->getOpcode() != Opc::PHI) continue;
                for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                    if (inst->getOperand(i + 1) == B) {
                        inst->setOperand(i + 1, P);
                    }
                }
            }

            // 更新 P 的 terminator：将对 B 的引用替换为 target
            for (unsigned i = 0; i < Pterm->getNumOperands(); ++i) {
                if (Pterm->getOperand(i) == B) {
                    Pterm->setOperand(i, target);
                }
            }

            // ★ 安全删除 B
            nullifyPhiEntriesForBlock(func, B);
            for (auto& inst : B->getInstructions()) {
                inst->replaceAllUsesWith(nullptr);
            }
            for (auto& inst : B->getInstructions()) {
                for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                    inst->setOperand(i, nullptr);
                }
            }
            B->replaceAllUsesWith(nullptr);
            auto& blocks = func->getBlocks();
            for (auto bit = blocks.begin(); bit != blocks.end(); ++bit) {
                if (bit->get() == B) {
                    blocks.erase(bit);
                    break;
                }
            }

            iterChanged = true;
            changed = true;
            break;
        }
    }

    return changed;
}

// ---- 删除无用的 PHI 指令 ----
// IfConversion 等 Pass 替换 PHI 的使用后，PHI 本身变为 dead 但仍留在 BB 中。
// 这会阻止 foldSinglePredBlock 折叠 and_merge 块（&& 短路求值场景）。
// 此函数在 SimplifyCFG 开始时清理所有 getNumUses()==0 的 PHI 指令。
bool removeDeadPhis(IR::Function* func) {
    bool changed = false;
    for (auto& bb : func->getBlocks()) {
        for (auto it = bb->begin(); it != bb->end(); ) {
            if ((*it)->getOpcode() != Opc::PHI) {
                ++it;
                continue;
            }
            // PHI 无任何使用 → dead
            if ((*it)->getNumUses() == 0) {
                // 清空操作数（避免悬空引用）
                for (unsigned i = 0; i < (*it)->getNumOperands(); ++i) {
                    (*it)->setOperand(i, nullptr);
                }
                it = bb->erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

// ---- 单前驱块折叠（Block Folding）----
// 当块 B 满足以下条件时，将 B 的指令合并到其唯一前驱 P 中：
//   1. B 恰好有 1 个前驱 P
//   2. P 的 terminator 是 BR（无条件跳转到 B）
//   3. B 没有 PHI 节点（因为只有 1 个前驱，PHI 无意义）
//
// 合并操作：
//   - 删除 P 的 BR terminator
//   - 将 B 的所有指令（含 terminator）移动到 P
//   - 更新 B 后继块的 PHI 中对 B 的引用为 P
//   - 删除 B
//
// 这对于消除 && / || 短路求值生成的 and_rhs/and_merge 块链特别重要，
// 能将 6+ 个 BB 的边界检查折叠为单个 BB，使 loopFullUnroll 能展开内层循环。
bool foldSinglePredBlock(IR::Function* func) {
    bool changed = false;
    auto* entry = func->getEntryBlock();

    bool iterChanged = true;
    while (iterChanged) {
        iterChanged = false;
        auto preds = buildPredecessors(func);

        for (auto& bb : func->getBlocks()) {
            auto* B = bb.get();
            if (B == entry) continue;

            // B 必须恰好有 1 个前驱
            auto pit = preds.find(B);
            if (pit == preds.end() || pit->second.size() != 1) continue;
            auto* P = pit->second[0];

            // P 的 terminator 必须是 BR（无条件跳转到 B）
            auto* Pterm = P->getTerminator();
            if (!Pterm || Pterm->getOpcode() != Opc::BR) continue;
            // 确认 P 跳转到 B
            if (Pterm->getOperand(0) != B) continue;

            // B 不能有 PHI 节点
            if (!B->getInstructions().empty()) {
                auto& firstInst = B->getInstructions().front();
                if (firstInst->getOpcode() == Opc::PHI) {
                    continue;
                }
            }

            // 执行合并：
            // 1. 删除 P 的 BR terminator（最后一个指令）
            {
                auto lastIt = P->end();
                --lastIt;
                P->erase(lastIt);
            }

            // 2. 将 B 的所有指令移动到 P（释放 unique_ptr 所有权后添加到 P）
            std::vector<IR::Instruction*> moved;
            for (auto it = B->begin(); it != B->end(); ) {
                auto* inst = it->release(); // 释放所有权，避免 double-delete
                it = B->erase(it);          // 从 B 中移除空 unique_ptr
                moved.push_back(inst);
            }
            for (auto* inst : moved) {
                P->pushBack(inst); // 添加到 P（pushBack 会设置 parent）
            }

            // 3. 更新 B 后继块的 PHI：将 B 的引用替换为 P
            //    B 的 terminator 可能是 BR 或 COND_BR
            //    使用 continue 而非 break：PHI 节点可能因前序 Pass 被交错
            //    在非 PHI 指令之间（39_fp_params SEGFAULT 根因）
            auto* Bterm = P->getTerminator(); // 现在 terminator 是从 B 移过来的
            for (unsigned i = 0; i < Bterm->getNumOperands(); ++i) {
                auto* succ = dynamic_cast<IR::BasicBlock*>(Bterm->getOperand(i));
                if (!succ) continue;
                for (auto& inst : succ->getInstructions()) {
                    if (inst->getOpcode() != Opc::PHI) continue;
                    for (unsigned j = 0; j + 1 < inst->getNumOperands(); j += 2) {
                        if (inst->getOperand(j + 1) == B) {
                            inst->setOperand(j + 1, P);
                        }
                    }
                }
            }

            // 5. 清除 B 的所有引用并删除
            nullifyPhiEntriesForBlock(func, B);
            B->replaceAllUsesWith(nullptr);
            auto& blocks = func->getBlocks();
            for (auto bit = blocks.begin(); bit != blocks.end(); ++bit) {
                if (bit->get() == B) {
                    blocks.erase(bit);
                    break;
                }
            }

            iterChanged = true;
            changed = true;
            break; // 重新开始，blocks 列表已改变
        }
    }

    return changed;
}

// ---- 删除 terminator 之后的死指令 ----
// IRBuilder 可能为连续的 break/return 生成多个 terminator 指令。
// 第一个 BR/COND_BR/RET 之后的指令都是死代码（永不执行）。
// 此函数截断块，保留第一个 terminator 作为块的终结指令。
bool removeDeadCodeAfterTerminator(IR::Function* func) {
    bool changed = false;
    for (auto& bb : func->getBlocks()) {
        bool foundTerm = false;
        for (auto it = bb->begin(); it != bb->end(); ) {
            if (foundTerm) {
                // 此指令在 terminator 之后，是死代码
                (*it)->replaceAllUsesWith(nullptr);
                for (unsigned i = 0; i < (*it)->getNumOperands(); ++i) {
                    (*it)->setOperand(i, nullptr);
                }
                it = bb->erase(it);
                changed = true;
            } else {
                auto op = (*it)->getOpcode();
                if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET) {
                    foundTerm = true;
                }
                ++it;
            }
        }
    }
    return changed;
}

// ---- 单函数 SimplifyCFG ----
bool simplifyCFGOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    if (func->getBlocks().empty()) return false;

    // ★ 规范化 PHI 节点：将所有 PHI 移动到块首
    //   前序 Pass（InstCombine/CodeSink/LICM 等）可能在 PHI 之间插入
    //   非 PHI 指令，破坏 "PHI 必须在块首" 的 IR 不变量，导致
    //   foldSinglePredBlock 等遍历 PHI 的代码漏掉部分 PHI 节点。
    normalizePhiNodes(func);

    bool changed = false;
    bool iterChanged = true;

    while (iterChanged) {
        iterChanged = false;

        if (removeDeadPhis(func)) iterChanged = true;
        if (removeDeadCodeAfterTerminator(func)) iterChanged = true;
        if (foldSameTargetBranches(func)) iterChanged = true;
        if (foldConstantBranches(func)) iterChanged = true;
        if (removeUnreachableBlocks(func)) iterChanged = true;
        if (eliminateEmptyBlocks(func)) iterChanged = true;
        if (foldSinglePredBlock(func)) iterChanged = true;

        if (iterChanged) changed = true;
    }

    // ★ 清理 PHI 节点中的 null 操作数对
    //   nullifyPhiEntriesForBlock/Predecessor 会将已删除块的 PHI 条目置 null，
    //   这些 null 条目必须在代码生成前被移除，否则 TargetCodeGen 的
    //   buildPhiMoveMap 会崩溃（82_long_func 根因）。
    for (auto& bb : func->getBlocks()) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if ((*it)->getOpcode() == Opc::PHI) {
                (*it)->removeNullPhiPairs();
            }
        }
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
