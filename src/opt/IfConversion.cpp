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
#include <cstdlib>
#include <iostream>

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

// ================================================================
// D2: 双路菱形 IfConversion 辅助
// ================================================================

// 把 src 块的非 terminator/非 PHI 指令移动到 dst 块的 terminator 之前。
// 所有权转移：unique_ptr.release() 交出裸指针，src.erase 销毁空 unique_ptr，
// dst.insert 重新接管所有权（参考 InlineExpansion 的 release 模式）。
void moveInstsBefore(IR::BasicBlock* src, IR::BasicBlock* dst) {
    using Opc = IR::Instruction::Opcode;
    std::vector<IR::Instruction*> toMove;
    for (auto& inst : src->getInstructions()) {
        auto op = inst->getOpcode();
        if (op == Opc::BR || op == Opc::COND_BR || op == Opc::PHI) continue;
        toMove.push_back(inst.get());
    }
    for (auto* inst : toMove) {
        for (auto it = src->begin(); it != src->end(); ++it) {
            if (it->get() == inst) {
                it->release();          // 释放所有权给裸指针 inst
                src->erase(it);         // 销毁 null unique_ptr（无害）
                break;
            }
        }
        // 插入到 dst 的 terminator 之前（重新接管所有权）
        auto termIt = dst->end();
        --termIt;
        dst->insert(termIt, inst);
    }
}

// D2: 双路菱形 IfConversion
//   pred: cond ? thenBB : elseBB
//   thenBB/elseBB 均单前驱(pred)单后继，且汇合到同一 mergeBB
//   mergeBB 前驱恰好 {thenBB, elseBB}
// 转换：上提两路算术到 pred，每个 PHI → select(cond, vThen, vElse)，
//       pred 的 COND_BR 改为 BR(mergeBB)，then/else 失去前驱变不可达（SimplifyCFG 清理）。
// 返回 true 表示完成一次转换（调用方应循环直到不动点）。
bool convertDualPathDiamond(IR::Function* fn) {
    using Opc = IR::Instruction::Opcode;
    auto preds = buildPredecessors(fn);
    auto succs = buildSuccessors(fn);

    for (auto& bb : fn->getBlocks()) {
        auto* pred = bb.get();
        auto* predBr = pred->getTerminator();
        if (!predBr || predBr->getOpcode() != Opc::COND_BR) continue;

        auto* thenBB = dynamic_cast<IR::BasicBlock*>(predBr->getOperand(1));
        auto* elseBB = dynamic_cast<IR::BasicBlock*>(predBr->getOperand(2));
        if (!thenBB || !elseBB || thenBB == elseBB) continue;

        // 两路均单前驱(pred)单后继，且后继相同(mergeBB)
        if (preds[thenBB].size() != 1 || preds[thenBB][0] != pred) continue;
        if (preds[elseBB].size() != 1 || preds[elseBB][0] != pred) continue;
        if (succs[thenBB].size() != 1 || succs[elseBB].size() != 1) continue;
        auto* mergeBB = succs[thenBB][0];
        if (succs[elseBB][0] != mergeBB) continue;
        if (mergeBB == pred || mergeBB == thenBB || mergeBB == elseBB) continue;

        // mergeBB 前驱恰好 {thenBB, elseBB}（无其他前驱，PHI 才能健全替换）
        auto& mergePreds = preds[mergeBB];
        if (mergePreds.size() != 2) continue;
        bool mergeOk = (mergePreds[0] == thenBB && mergePreds[1] == elseBB) ||
                       (mergePreds[0] == elseBB && mergePreds[1] == thenBB);
        if (!mergeOk) continue;

        // cond 单 use（仅在 COND_BR 中）
        auto* cond = predBr->getOperand(0);
        if (cond->getNumUses() != 1) continue;

        // thenBB/elseBB 各 ≤2 条 safe-to-speculate 算术，且不含浮点 def
        auto checkBlock = [](IR::BasicBlock* b, int& cnt, bool& hasFloat) -> bool {
            cnt = 0; hasFloat = false;
            for (auto& inst : b->getInstructions()) {
                auto op = inst->getOpcode();
                if (op == Opc::BR || op == Opc::COND_BR || op == Opc::PHI) continue;
                if (!isSafeToSpeculate(inst.get())) return false;
                if (inst->getType() && inst->getType()->isFloat()) hasFloat = true;
                cnt++;
            }
            return true;
        };
        int thenN = 0, elseN = 0;
        bool thenFloat = false, elseFloat = false;
        if (!checkBlock(thenBB, thenN, thenFloat) || thenN > 2) continue;
        if (!checkBlock(elseBB, elseN, elseFloat) || elseN > 2) continue;
        if (thenFloat || elseFloat) continue;  // 第一版排除浮点（emitSelect 浮点 fallback 风险）

        // mergeBB 所有 PHI 必须可转换：有 then+else 条目且非浮点。
        // 若有任一浮点 PHI 或缺前驱条目的 PHI，跳过整个菱形（避免悬空 PHI 条目）。
        std::vector<IR::Instruction*> targetPhis;
        bool allConvertible = true;
        for (auto& inst : mergeBB->getInstructions()) {
            if (inst->getOpcode() != Opc::PHI) continue;  // PHI 不必在块首，扫描全部
            if (inst->getType() && inst->getType()->isFloat()) { allConvertible = false; break; }
            IR::Value* vThen = nullptr;
            IR::Value* vElse = nullptr;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto* b = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                if (b == thenBB) vThen = inst->getOperand(i);
                if (b == elseBB) vElse = inst->getOperand(i);
            }
            if (!vThen || !vElse) { allConvertible = false; break; }
            targetPhis.push_back(inst.get());
        }
        if (!allConvertible || targetPhis.empty()) continue;

        // ★ 执行转换
        // 1. 上提 thenBB/elseBB 的算术指令到 pred 的 COND_BR 之前
        //    SSA 保证两路 def 互不冲突（互斥路径），且不 def cond（cond 在 pred 之前 def）
        moveInstsBefore(thenBB, pred);
        moveInstsBefore(elseBB, pred);

        // 2. 为每个目标 PHI 创建 select(cond, vThen, vElse)，替换 PHI
        for (auto* phi : targetPhis) {
            IR::Value* vThen = nullptr;
            IR::Value* vElse = nullptr;
            for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
                auto* b = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
                if (b == thenBB) vThen = phi->getOperand(i);
                if (b == elseBB) vElse = phi->getOperand(i);
            }
            static int selCnt = 0;
            std::string selName = "%ifconv.d" + std::to_string(selCnt++);
            auto* select = IR::Instruction::createSelect(cond, vThen, vElse, selName);
            auto termIt = pred->end();
            --termIt;
            pred->insert(termIt, select);
            phi->replaceAllUsesWith(select);
        }

        // 3. 删除被替换的 PHI（use 已为 0，erase 触发 ~User::dropAllUses 清理 use-list）
        for (auto* phi : targetPhis) {
            for (auto it = mergeBB->begin(); it != mergeBB->end(); ++it) {
                if (it->get() == phi) {
                    mergeBB->erase(it);  // unique_ptr 析构 delete PHI
                    break;
                }
            }
        }

        // 4. CFG 重写：pred 的 COND_BR → BR(mergeBB)
        //    直接 erase（unique_ptr delete COND_BR，dropAllUses 移除对 cond/then/else 的 use）
        for (auto it = pred->begin(); it != pred->end(); ++it) {
            if (it->get() == predBr) {
                pred->erase(it);
                break;
            }
        }
        pred->pushBack(IR::Instruction::createBr(mergeBB));

        const char* dbg = std::getenv("DBG_IFCONV");
        if (dbg && std::string(dbg) == "1") {
            std::cerr << "[DBG_IFCONV] dual-path diamond at " << pred->getName()
                      << " (then=" << thenBB->getName() << ", else=" << elseBB->getName()
                      << ", merge=" << mergeBB->getName() << ")\n";
        }
        return true;  // 完成一次转换，调用方重新扫描到不动点
    }
    return false;
}

} // anonymous namespace

bool ifConversion(IR::Module* mod) {
    bool changed = false;

    for (auto& fn : mod->getFunctions()) {
        if (fn->isExternal()) continue;

        // D2: 双路菱形 IfConversion（先于单路执行——单路只处理三角形 pred→curr→succ+pred→succ，
        // 无法处理菱形 pred→then→merge + pred→else→merge）。循环到不动点处理级联。
        // 开关 IFCONV_EXT_OFF=1 禁用双路（保留单路）；DBG_IFCONV=1 打印生效点。
        {
            const char* extOff = std::getenv("IFCONV_EXT_OFF");
            bool extDisabled = extOff && std::string(extOff) == "1";
            if (!extDisabled) {
                while (convertDualPathDiamond(fn.get())) {
                    changed = true;
                }
            }
        }

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

            // 条件分支必须同时指向 curr 和 singleSucc
            IR::BasicBlock* trueDest = dynamic_cast<IR::BasicBlock*>(predBr->getOperand(1));
            IR::BasicBlock* falseDest = dynamic_cast<IR::BasicBlock*>(predBr->getOperand(2));
            if (!trueDest || !falseDest) continue;

            bool currIsTrue = (trueDest == curr && falseDest == singleSucc);
            bool currIsFalse = (falseDest == curr && trueDest == singleSucc);
            if (!currIsTrue && !currIsFalse) continue;

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

            // 确定 true/false 值的来源
            IR::BasicBlock* trueInBB = currIsTrue ? curr : singlePred;
            IR::BasicBlock* falseInBB = currIsTrue ? singlePred : curr;

            // 将 singleSucc 中的 phi 指令替换为 SELECT + 新 phi
            bool anyConverted = false;
            for (auto& inst : singleSucc->getInstructions()) {
                if (inst->getOpcode() != IR::Instruction::Opcode::PHI)
                    continue;

                // 找到来自 singlePred 和 curr 的值
                IR::Value* valFromPred = nullptr;
                IR::Value* valFromCurr = nullptr;
                for (unsigned i = 0; i < inst->getNumOperands(); i += 2) {
                    auto* bb = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                    if (bb == singlePred) valFromPred = inst->getOperand(i);
                    if (bb == curr) valFromCurr = inst->getOperand(i);
                }
                if (!valFromPred || !valFromCurr) continue;

                IR::Value* trueVal = currIsTrue ? valFromCurr : valFromPred;
                IR::Value* falseVal = currIsTrue ? valFromPred : valFromCurr;

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

            // 修改 CFG：将条件分支中对 singleSucc 的引用改为 curr
            if (trueDest == singleSucc) {
                predBr->setOperand(1, curr);
            }
            if (falseDest == singleSucc) {
                predBr->setOperand(2, curr);
            }

            changed = true;
        }
    }

    return changed;
}

} // namespace Opt