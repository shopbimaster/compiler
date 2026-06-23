// ================================================================
// O2: ADCE（Aggressive Dead Code Elimination）— 激进死代码消除
// 借鉴 Cpl1 的 ADCE 设计
//
// 与 DCE 的区别：
//   DCE 只删除"无使用者的指令"（局部死代码）
//   ADCE 使用活性分析标记"对程序输出有贡献的指令"，
//   可以消除不可达代码和死指令。
//
// 算法（简化版）：
//   1. Mark: 从 STORE/RET/CALL/BR 出发，标记所有"关键"指令
//   2. 迭代标记所有被关键指令使用的操作数
//   3. Sweep: 删除未标记的非终止指令（保留 BR/COND_BR/RET）
// ================================================================

#include "opt/Optimizer.h"
#include <deque>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

bool isSideEffectingCall(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::CALL)
        return false;
    return true;
}

} // anonymous namespace

bool adce(IR::Module* mod) {
    bool changed = false;

    for (auto& fn : mod->getFunctions()) {
        if (fn->isExternal()) continue;

        // ================================================================
        // Mark 阶段
        // ================================================================
        std::unordered_set<IR::Instruction*> critical;
        std::deque<IR::Instruction*> worklist;

        auto markAsCritical = [&](IR::Instruction* inst) {
            if (critical.insert(inst).second) {
                worklist.push_back(inst);
            }
        };

        // 初始标记：STORE、RET、CALL、所有BR/COND_BR（终止指令不可删除）
        for (auto& bb : fn->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                auto op = inst->getOpcode();
                using Opc = IR::Instruction::Opcode;
                if (op == Opc::STORE || op == Opc::RET) {
                    markAsCritical(inst.get());
                } else if (op == Opc::CALL && isSideEffectingCall(inst.get())) {
                    markAsCritical(inst.get());
                } else if (op == Opc::BR || op == Opc::COND_BR) {
                    markAsCritical(inst.get());
                }
            }
        }

        // 工作列表传播：标记所有被关键指令直接或间接使用的指令
        while (!worklist.empty()) {
            auto* inst = worklist.front();
            worklist.pop_front();

            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                auto* op = dynamic_cast<IR::Instruction*>(inst->getOperand(i));
                if (op) {
                    markAsCritical(op);
                }
            }
        }

        // ================================================================
        // Sweep 阶段：只删除非终止指令
        // ================================================================
        struct DeadInfo {
            IR::Instruction* inst;
            IR::BasicBlock* bb;
        };
        std::vector<DeadInfo> deadList;

        for (auto& bb : fn->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (critical.find(inst.get()) == critical.end()) {
                    auto op = inst->getOpcode();
                    using Opc = IR::Instruction::Opcode;
                    // 保留所有终止指令
                    if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET)
                        continue;
                    deadList.push_back({inst.get(), bb.get()});
                }
            }
        }

        if (deadList.empty()) continue;

        // 步骤1: replaceAllUsesWith undef — 解决死指令间相互引用（B使用A，A被删后B有悬空指针）
        auto* undef = IR::ConstantInt::get(IR::IntegerType::I32, 0);
        for (auto& d : deadList) {
            d.inst->replaceAllUsesWith(undef);
        }
        // 步骤2: dropAllUses — 从操作数（如ALLOCA）的use列表中移除死指令
        for (auto& d : deadList) {
            d.inst->dropAllUses();
        }
        // 步骤3: 批量 erase
        for (auto& d : deadList) {
            for (auto it = d.bb->begin(); it != d.bb->end(); ++it) {
                if (it->get() == d.inst) {
                    d.bb->erase(it);
                    changed = true;
                    break;
                }
            }
        }
    }

    return changed;
}

} // namespace Opt