// ================================================================
// O2: 代码下沉（CodeSink）— 将指令移动到更靠近其使用者的位置
// 借鉴 Cpl1 的 CodeSink 设计
// 策略：
//   在同一个 BB 内，将指令下沉到其最早使用者之前，
//   减少寄存器活跃区间，降低寄存器压力
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// 判断指令是否有副作用，不能下沉
// LOAD 也不能下沉：下沉 LOAD 可能越过 STORE/CALL，
// 改变内存读写顺序，导致读取到错误的值
bool hasSideEffect(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    return op == Opc::STORE || op == Opc::CALL ||
           op == Opc::RET   || op == Opc::BR ||
           op == Opc::COND_BR || op == Opc::ALLOCA ||
           op == Opc::LOAD;
}

// 对单个 BB 做代码下沉
bool sinkInBlock(IR::BasicBlock* bb) {
    // ★ 大 BB 豁免：CodeSink 的 sinkInBlock 算法复杂度为 O(N⁴)
    //   （while(localChanged) 外层循环每次移动后 break 重启 ×
    //    O(N) 候选 × O(N) 位置查找 × O(N) 使用者查找）。
    //   对于 86_long_code2.sy 等含数百项 `a[2*2][20000-1] + ...` 表达式的测试，
    //   BB 可能有数千条指令，导致 CodeSink 挂起。
    //   正常代码的 BB 极少超过 100 条指令，200 的限制安全且无性能损失。
    //   CodeSink 对大 BB 的收益本就有限（寄存器分配器已通过溢出处理压力）。
    if (bb->size() > 200) return false;

    bool changed = false;
    std::unordered_set<IR::Instruction*> moved;

    bool localChanged = true;
    while (localChanged) {
        localChanged = false;

        // 收集所有指令（非 PHI，非终止指令，非副作用指令）
        std::vector<IR::Instruction*> candidates;
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            auto* inst = it->get();
            if (moved.count(inst)) continue;
            if (hasSideEffect(inst)) continue;
            // 跳过终止指令
            auto op = inst->getOpcode();
            if (op == IR::Instruction::Opcode::BR ||
                op == IR::Instruction::Opcode::COND_BR ||
                op == IR::Instruction::Opcode::RET) continue;
            candidates.push_back(inst);
        }

        // 从后往前处理
        for (auto rit = candidates.rbegin(); rit != candidates.rend(); ++rit) {
            IR::Instruction* inst = *rit;
            if (moved.count(inst)) continue;

            // 找到在同 BB 中最早的使用者
            IR::Instruction* earliestUser = nullptr;
            int earliestPos = -1;
            int instPos = -1;

            // 先确定 inst 的位置
            {
                int pos = 0;
                for (auto it = bb->begin(); it != bb->end(); ++it, ++pos) {
                    if (it->get() == inst) { instPos = pos; break; }
                }
            }
            if (instPos < 0) continue;

            // 找到最早的使用者
            for (auto& use : inst->getUses()) {
                auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
                if (!userInst || userInst->getParent() != bb) continue;
                if (userInst->getOpcode() == IR::Instruction::Opcode::PHI) continue;

                int pos = 0;
                for (auto it = bb->begin(); it != bb->end(); ++it, ++pos) {
                    if (it->get() == userInst) break;
                }
                if (earliestPos < 0 || pos < earliestPos) {
                    earliestPos = pos;
                    earliestUser = userInst;
                }
            }

            if (!earliestUser) continue;

            // Sinking shortens the result lifetime but can lengthen one or
            // more input lifetimes by the same (or a larger) distance. Only
            // move when every non-constant input is already live through the
            // destination because of another use.
            bool extendsInputLifetime = false;
            for (unsigned operandIndex = 0;
                 operandIndex < inst->getNumOperands(); ++operandIndex) {
                auto* operand = inst->getOperand(operandIndex);
                if (!operand || dynamic_cast<IR::Constant*>(operand) ||
                    dynamic_cast<IR::BasicBlock*>(operand) ||
                    dynamic_cast<IR::Function*>(operand)) {
                    continue;
                }

                bool alreadyLiveThroughDestination = false;
                for (const auto& use : operand->getUses()) {
                    if (use.user == inst) continue;
                    auto* user = dynamic_cast<IR::Instruction*>(use.user);
                    if (!user || user->getParent() != bb) {
                        alreadyLiveThroughDestination = true;
                        break;
                    }

                    int userPos = 0;
                    for (auto it = bb->begin(); it != bb->end();
                         ++it, ++userPos) {
                        if (it->get() == user) break;
                    }
                    if (userPos >= earliestPos) {
                        alreadyLiveThroughDestination = true;
                        break;
                    }
                }
                if (!alreadyLiveThroughDestination) {
                    extendsInputLifetime = true;
                    break;
                }
            }
            if (extendsInputLifetime) continue;
            if (earliestPos <= instPos + 1) continue; // 已经紧挨着或在使用者之后

            // 检查 inst 和 earliestUser 之间是否有非操作数指令
            bool hasBarrier = false;
            {
                int pos = 0;
                for (auto it = bb->begin(); it != bb->end(); ++it, ++pos) {
                    if (pos <= instPos) continue;
                    if (pos >= earliestPos) break;
                    auto* mid = it->get();
                    bool isOperandOfEarliest = false;
                    for (unsigned opNo = 0; opNo < earliestUser->getNumOperands(); ++opNo) {
                        if (earliestUser->getOperand(opNo) == mid) {
                            isOperandOfEarliest = true;
                            break;
                        }
                    }
                    if (!isOperandOfEarliest) {
                        hasBarrier = true;
                        break;
                    }
                }
            }
            if (!hasBarrier) continue;

            // 移动指令：release + erase + insert
            moved.insert(inst);

            // 找到 inst 的迭代器
            auto instIt = bb->begin();
            while (instIt != bb->end() && instIt->get() != inst) ++instIt;
            if (instIt == bb->end()) continue;

            // 释放所有权
            IR::Instruction* released = instIt->release();
            // 删除空壳
            bb->erase(instIt);

            // 找到 earliestUser 的迭代器
            auto insertIt = bb->begin();
            while (insertIt != bb->end() && insertIt->get() != earliestUser) ++insertIt;
            if (insertIt == bb->end()) {
                // earliestUser 不见了，回退（把指令放回去）
                bb->insert(bb->end(), released);
                continue;
            }

            // 插入到 earliestUser 之前
            bb->insert(insertIt, released);

            localChanged = true;
            changed = true;
            break; // 列表已修改，重新扫描
        }
    }
    return changed;
}

} // namespace

bool codeSink(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        for (auto& bb : func->getBlocks()) {
            if (sinkInBlock(bb.get())) changed = true;
        }
    }
    return changed;
}

} // namespace Opt
