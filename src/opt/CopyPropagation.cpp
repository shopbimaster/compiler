// ================================================================
// CopyPropagation — 复写传播
// 识别形如 %x = add %y, 0 的复制指令，将所有 %x 的 use 替换为 %y
// 支持的复制模式：
//   %x = add %y, 0  /  %x = add 0, %y
//   %x = sub %y, 0
//   %x = or  %y, 0  /  %x = or 0, %y
//   %x = xor %y, 0  /  %x = xor 0, %y
//   %x = shl %y, 0
// ================================================================

#include "opt/Optimizer.h"
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// 判断指令是否为复制指令，若是则返回源操作数
IR::Value* isCopyInstruction(IR::Instruction* inst) {
    if (inst->getNumOperands() < 2) return nullptr;
    auto op = inst->getOpcode();
    auto* lhs = inst->getOperand(0);
    auto* rhs = inst->getOperand(1);

    auto isZero = [](IR::Value* v) -> bool {
        auto* ci = dynamic_cast<IR::ConstantInt*>(v);
        return ci && ci->getValue() == 0;
    };

    switch (op) {
        case Opc::ADD:
            if (isZero(rhs)) return lhs;   // x + 0 = x
            if (isZero(lhs)) return rhs;   // 0 + x = x
            break;
        case Opc::SUB:
            if (isZero(rhs)) return lhs;   // x - 0 = x
            break;
        case Opc::OR:
            if (isZero(rhs)) return lhs;   // x | 0 = x
            if (isZero(lhs)) return rhs;   // 0 | x = x
            break;
        case Opc::XOR:
            if (isZero(rhs)) return lhs;   // x ^ 0 = x
            if (isZero(lhs)) return rhs;   // 0 ^ x = x
            break;
        case Opc::SHL:
            if (isZero(rhs)) return lhs;   // x << 0 = x
            break;
        default:
            break;
    }
    return nullptr;
}

bool copyPropagationOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    bool changed = false;

    std::vector<IR::Instruction*> toErase;

    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto* src = isCopyInstruction(inst.get());
            if (!src) continue;
            // 不能替换自己
            if (src == inst.get()) continue;
            // 确保类型匹配
            if (src->getType() != inst->getType()) continue;

            inst->replaceAllUsesWith(src);
            toErase.push_back(inst.get());
            changed = true;
        }
    }

    // 批量删除被替换的复制指令
    for (auto* inst : toErase) {
        inst->dropAllUses();
        auto* parent = inst->getParent();
        if (parent) {
            for (auto it = parent->begin(); it != parent->end(); ++it) {
                if (it->get() == inst) {
                    parent->erase(it);
                    break;
                }
            }
        }
    }

    return changed;
}

} // namespace

bool copyPropagation(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (copyPropagationOnFunction(func.get()))
                changed = true;
        }
        if (changed) anyChanged = true;
    }
    return anyChanged;
}

} // namespace Opt