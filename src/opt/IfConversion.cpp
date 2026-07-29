// ================================================================
// O2: IfConversion — 将菱形分支转换为 SELECT 指令
// 借鉴 Cpl1 的 IfConversion 设计
//
// 转换前:
//   bb0: cond = icmp ...; br cond, bb1, bb2
//   bb1: ...; br bb3
//   bb3: val = phi [v0, bb0], [v1, bb1]
//
// 转换后:
//   bb0: br bb1
//   bb1: ...; s = select cond, v1, v0; br bb3
//   bb3: val = phi [s, bb1]
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <unordered_set>

namespace Opt {
namespace {

// 判断指令是否可以安全地存在于投机执行路径中
bool isSafeToSpeculate(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    if (op == Opc::STORE || op == Opc::LOAD || op == Opc::CALL)
        return false;
    if (op == Opc::SDIV || op == Opc::SREM || op == Opc::FDIV)
        return false;
    if (op == Opc::RET || op == Opc::BR || op == Opc::COND_BR)
        return false;
    return true;
}

} // anonymous namespace

bool ifConversion(IR::Module* mod) {
    bool changed = false;

    for (auto& fn : mod->getFunctions()) {
        if (fn->isExternal()) continue;

        auto preds = buildPredecessors(fn.get());
        auto succs = buildSuccessors(fn.get());

        // 收集候选块：单前驱、单后继
        struct Candidate {
            IR::BasicBlock* bb;
            IR::BasicBlock* pred;
            IR::BasicBlock* succ;
        };
        std::vector<Candidate> candidates;

        for (auto& bb : fn->getBlocks()) {
            auto* b = bb.get();
            auto& predList = preds[b];
            auto& succList = succs[b];
            if (predList.size() != 1 || succList.size() != 1)
                continue;
            candidates.push_back({b, predList[0], succList[0]});
        }

        for (auto& cand : candidates) {
            IR::BasicBlock* curr = cand.bb;
            IR::BasicBlock* singlePred = cand.pred;
            IR::BasicBlock* singleSucc = cand.succ;

            // 重新验证（CFG 可能已被之前的转换修改）
            auto& predList = preds[curr];
            if (predList.size() != 1 || predList[0] != singlePred)
                continue;

            // 前驱必须有条件分支
            auto* predBr = singlePred->getTerminator();
            if (!predBr || predBr->getOpcode() != IR::Instruction::Opcode::COND_BR)
                continue;

            // 条件分支的两个目标：一个必须是 curr，另一个是 otherDest
            IR::BasicBlock* trueDest = dynamic_cast<IR::BasicBlock*>(predBr->getOperand(1));
            IR::BasicBlock* falseDest = dynamic_cast<IR::BasicBlock*>(predBr->getOperand(2));
            if (!trueDest || !falseDest) continue;

            bool currIsTrue = (trueDest == curr);
            bool currIsFalse = (falseDest == curr);
            if (!currIsTrue && !currIsFalse) continue;

            IR::BasicBlock* otherDest = currIsTrue ? falseDest : trueDest;

            // otherDest 必须能到达 singleSucc（merge 块）。两种合法形态：
            //   形态 1（原有）：otherDest == singleSucc，即 merge 块本身就是另一臂。
            //     br cond, curr, singleSucc ；singleSucc 的 phi 含 [v, singlePred]
            //   形态 2（新增，empty-else/then 菱形）：otherDest 是仅含无条件 BR 的空块，
            //     且其目标 == singleSucc。典型场景：if (cond) { acc += expr; } 编译为
            //     br cond, then, endif ; endif: br merge ; merge: phi [t, then], [acc, endif]
            //     此形态下 endif 的 phi 条目值实际定义在支配 otherDest 的更早块中。
            bool otherIsMerge = (otherDest == singleSucc);
            bool otherIsEmptyMiddle = false;
            if (!otherIsMerge) {
                int otherInstCount = 0;
                IR::BasicBlock* otherTarget = nullptr;
                for (auto& oi : otherDest->getInstructions()) {
                    auto oop = oi->getOpcode();
                    if (oop == IR::Instruction::Opcode::BR) {
                        otherTarget = dynamic_cast<IR::BasicBlock*>(oi->getOperand(0));
                    } else if (oop != IR::Instruction::Opcode::COND_BR) {
                        otherInstCount++;
                    }
                }
                if (otherInstCount == 0 && otherTarget == singleSucc) {
                    otherIsEmptyMiddle = true;
                }
            }
            if (!otherIsMerge && !otherIsEmptyMiddle)
                continue;

            // 检查 curr 中的指令是否安全
            bool safe = true;
            int instCount = 0;
            for (auto& inst : curr->getInstructions()) {
                auto op = inst->getOpcode();
                if (op == IR::Instruction::Opcode::BR ||
                    op == IR::Instruction::Opcode::COND_BR)
                    continue;
                if (!isSafeToSpeculate(inst.get())) {
                    safe = false; break;
                }
                instCount++;
            }
            if (!safe || instCount > 4) continue;

            // 检查 cond 是否只在分支中使用
            auto* cond = predBr->getOperand(0);
            if (cond->getNumUses() != 1) continue;

            // 确定"另一臂"对应的 phi 来源块：
            //   形态 1：phi 条目标签为 singlePred（分支块本身直接跳到 merge）
            //   形态 2：phi 条目标签为 otherDest（空中间块跳到 merge）
            IR::BasicBlock* otherBB = otherIsMerge ? singlePred : otherDest;

            // 将 singleSucc 中的 phi 指令替换为 SELECT + 新 phi
            bool anyConverted = false;
            for (auto& inst : singleSucc->getInstructions()) {
                if (inst->getOpcode() != IR::Instruction::Opcode::PHI)
                    continue;

                // 找到来自 otherBB 和 curr 的值
                IR::Value* valFromOther = nullptr;
                IR::Value* valFromCurr = nullptr;
                for (unsigned i = 0; i < inst->getNumOperands(); i += 2) {
                    auto* bb = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                    if (bb == otherBB) valFromOther = inst->getOperand(i);
                    if (bb == curr) valFromCurr = inst->getOperand(i);
                }
                if (!valFromOther || !valFromCurr) continue;

                IR::Value* trueVal = currIsTrue ? valFromCurr : valFromOther;
                IR::Value* falseVal = currIsTrue ? valFromOther : valFromCurr;

                // 创建 SELECT: select cond, trueVal, falseVal
                static int selCnt = 0;
                std::string selName = "%ifconv.s" + std::to_string(selCnt++);
                auto* select = IR::Instruction::createSelect(cond, trueVal, falseVal, selName);

                // 插入到 curr 的终止指令之前
                auto termIt = curr->end();
                --termIt;
                curr->insert(termIt, select);

                // 替换所有对 phi 的使用为 SELECT
                // 先将 phi 的旧值替换为 SELECT
                inst->replaceAllUsesWith(select);

                anyConverted = true;
            }

            if (!anyConverted) continue;

            // 修改 CFG：将条件分支中对 otherDest 的引用改为 curr。
            //   形态 1：otherDest == singleSucc，改 singleSucc 引用 → curr（原有行为）
            //   形态 2：otherDest == 空中间块，改 otherDest 引用 → curr（空块变不可达，
            //           由后续 removeUnreachableBlocks 清理）
            // 改后 predBr 形如 br cond, curr, curr，由 simplifyCFG 的
            // foldSameTargetBranches 折叠为无条件 br curr，进而触发块合并。
            if (trueDest == otherDest) {
                predBr->setOperand(1, curr);
            }
            if (falseDest == otherDest) {
                predBr->setOperand(2, curr);
            }

            changed = true;
        }
    }

    return changed;
}

} // namespace Opt