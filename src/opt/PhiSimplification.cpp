// ================================================================
// PhiSimplification — PHI 节点简化
//
// 在 SSA 优化完成后、代码生成前运行，消除冗余 PHI 节点。
// 每个 PHI 节点在代码生成时会在每个前驱块产生一条 mv 指令
// （emitPhiMovesForEdge）。减少 PHI 节点直接减少 mv 指令数。
//
// 简化规则（均保证 SSA 语义安全）：
//   1. 同值 PHI：所有源值相同 → 替换为该值
//      %x = phi [%a, B1], [%a, B2]  →  %x = %a
//      安全性：SSA 中 PHI 的每个源值必须支配对应前驱块，
//      若所有源值相同则该值支配所有前驱块，故支配 PHI 块，可安全替换。
//
//   2. 单源 PHI：只有一对操作数且非自引用 → 替换为该值
//      %x = phi [%a, B1]  →  %x = %a
//      安全性：PHI 退化为赋值，只有一个前驱路径。
//
//   3. 移除不可达前驱的 PHI 条目：
//      PHI 中引用了已不在函数中的前驱块 → 移除该条目
//      （内联/CFG 简化后可能遗留）
//
//   4. 移除自引用条目（当 PHI 有 ≥2 个非自引用源时）：
//      %x = phi [%x, B1], [%a, B2], [%b, B3]  →  %x = phi [%a, B2], [%b, B3]
//      安全性：自引用表示"从该前驱到达时保持原值"。
//      只有当该前驱块确实不直接到达 PHI 块（被其他 pass 证明不可达，
//      或被 SimplifyCFG 改变）时才移除。
//      ★ 保守策略：不自引用移除，除非该前驱块不在当前函数的 CFG 中。
//        避免破坏循环变量的语义。
//
// 迭代直到收敛：简化一个 PHI 可能让另一个 PHI 变为同值。
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace Opt {

namespace {

using Opc = IR::Instruction::Opcode;

// 收集 PHI 的有效 (val, predBB) 对，跳过 null
std::vector<std::pair<IR::Value*, IR::BasicBlock*>> collectPhiPairs(IR::Instruction* phi) {
    std::vector<std::pair<IR::Value*, IR::BasicBlock*>> pairs;
    for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
        auto* val = phi->getOperand(i);
        auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
        if (val && predBB) {
            pairs.push_back({val, predBB});
        }
    }
    return pairs;
}

// 从 BB 中删除指令
void eraseInstFromBB(IR::Instruction* inst) {
    auto* bb = inst->getParent();
    if (!bb) return;
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            bb->erase(it);
            return;
        }
    }
}

// 单函数 PHI 简化（迭代到收敛）
bool simplifyPhiInFunction(IR::Function* func) {
    if (func->isExternal() || func->getBlocks().empty()) return false;

    // 收集函数中所有块（用于检测不可达前驱）
    std::unordered_set<IR::BasicBlock*> funcBlocks;
    for (auto& bb : func->getBlocks()) {
        funcBlocks.insert(bb.get());
    }

    bool changed = false;
    bool iterChanged = true;

    while (iterChanged) {
        iterChanged = false;

        // 待替换：(PHI, replacement)
        std::vector<std::pair<IR::Instruction*, IR::Value*>> toReplace;

        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() != Opc::PHI) continue;

                auto pairs = collectPhiPairs(inst.get());
                if (pairs.empty()) continue;

                IR::Value* self = inst.get();

                // 规则 3: 移除不可达前驱的条目
                // 如果前驱块不在函数中，移除该条目
                bool hasDeadPred = false;
                for (auto& p : pairs) {
                    if (!funcBlocks.count(p.second)) {
                        hasDeadPred = true;
                        break;
                    }
                }
                if (hasDeadPred) {
                    // 标记需要移除的死前驱条目
                    for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                        auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                        if (predBB && !funcBlocks.count(predBB)) {
                            inst->setOperand(i, nullptr);
                            inst->setOperand(i + 1, nullptr);
                        }
                    }
                    inst->removeNullPhiPairs();
                    iterChanged = true;
                    // 重新收集
                    pairs = collectPhiPairs(inst.get());
                    if (pairs.empty()) continue;
                }

                // 规则 1: 同值 PHI — 所有源值相同且非自引用
                IR::Value* firstVal = pairs[0].first;
                bool allSame = true;
                for (auto& p : pairs) {
                    if (p.first != firstVal) { allSame = false; break; }
                }
                if (allSame && firstVal != self) {
                    if (std::getenv("DEBUG_PHI_SIMPL")) {
                        std::fprintf(stderr, "[phi-simpl] %s: same-value phi %s -> ",
                                     func->getName().c_str(), inst->getName().c_str());
                        if (auto* v = dynamic_cast<IR::Value*>(firstVal))
                            std::fprintf(stderr, "%s\n", v->getName().c_str());
                        else
                            std::fprintf(stderr, "(non-value)\n");
                    }
                    toReplace.push_back({inst.get(), firstVal});
                    continue;
                }

                // 规则 2: 单源 PHI — 只有一对且非自引用
                if (pairs.size() == 1 && pairs[0].first != self) {
                    if (std::getenv("DEBUG_PHI_SIMPL")) {
                        std::fprintf(stderr, "[phi-simpl] %s: single-source phi %s -> %s\n",
                                     func->getName().c_str(), inst->getName().c_str(),
                                     pairs[0].first->getName().c_str());
                    }
                    toReplace.push_back({inst.get(), pairs[0].first});
                    continue;
                }

                // 规则 4 保守版: 如果有自引用条目，且移除自引用后只剩一个源
                // %x = phi [%x, B1], [%a, B2]  →  如果 B1 不可达则变为 [%a, B2] → 单源
                // 已被规则 3 覆盖（B1 不可达会被移除）
                // 不做主动的自引用移除（保护循环变量语义）
            }
        }

        // 执行替换（分三步安全删除，避免悬空指针）
        // 问题1：toReplace 中的 PHI 可能互相引用（A 的操作数包含 B）。
        //   如果先删 B，再删 A 时 A 的 dropAllUses 访问已释放的 B → 段错误。
        // 问题2：A 的替换值可能是另一个待删除 PHI B。如果先处理 A（A→B），
        //   再删除 B，使用 A 的指令现在引用 B（已释放）→ 悬空指针。
        // 解决：
        //   1. 过滤：替换值是另一个待删除 PHI 的，跳过本轮（下一轮再处理）
        //   2. 先全部 replaceAllUsesWith
        //   3. 清空待删除 PHI 互相引用的操作数条目
        //   4. 最后 erase（dropAllUses 此时操作数已置 null，安全）

        std::unordered_set<IR::Value*> phiSet;
        for (auto& [phi, val] : toReplace) {
            phiSet.insert(phi);
        }

        // 过滤：替换值不能是另一个待删除 PHI（避免悬空指针）
        std::vector<std::pair<IR::Instruction*, IR::Value*>> filtered;
        for (auto& [phi, val] : toReplace) {
            if (phiSet.count(val)) continue;  // 替换值是待删除 PHI，下一轮处理
            filtered.push_back({phi, val});
        }
        if (filtered.empty()) {
            // 所有 PHI 的替换值都是其他待删除 PHI（循环依赖），跳过避免死循环
            break;
        }

        // 第一步：replaceAllUsesWith
        for (auto& [phi, val] : filtered) {
            phi->replaceAllUsesWith(val);
        }

        // 第二步：清空互相引用的操作数条目
        for (auto& [phi, val] : filtered) {
            for (unsigned i = 0; i < phi->getNumOperands(); ++i) {
                auto* op = phi->getOperand(i);
                if (op && phiSet.count(op)) {
                    phi->setOperand(i, nullptr);
                }
            }
        }

        // 第三步：erase（dropAllUses 此时操作数已置 null，安全）
        for (auto& [phi, val] : filtered) {
            eraseInstFromBB(phi);
            iterChanged = true;
        }

        changed |= iterChanged;
    }

    return changed;
}

} // namespace

bool phiSimplification(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (simplifyPhiInFunction(func.get())) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt
