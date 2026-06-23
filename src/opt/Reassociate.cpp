// ================================================================
// O2: 表达式重排 (Reassociate) — 规范化交换律操作数以提升 CSE 命中率
// 借鉴 Cpl1 的 Reassociate 设计
// 将 a+b 和 b+a 规范化为相同的操作数顺序
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <vector>

namespace Opt {
namespace {

// 可交换的操作（commutative）
bool isCommutative(IR::Instruction::Opcode op) {
    using Opc = IR::Instruction::Opcode;
    return op == Opc::ADD || op == Opc::MUL ||
           op == Opc::FADD || op == Opc::FMUL ||
           op == Opc::AND || op == Opc::OR  || op == Opc::XOR;
}

// 获取 Value 的"排名"用于排序：常量(0) < 参数(1) < 指令(2)
int getRank(IR::Value* v) {
    if (dynamic_cast<IR::ConstantInt*>(v)) return 0;
    if (dynamic_cast<IR::ConstantFloat*>(v)) return 0;
    if (dynamic_cast<IR::Argument*>(v)) return 1;
    return 2;
}

// 比较两个 Value，用于确定规范顺序
bool shouldSwap(IR::Value* a, IR::Value* b) {
    int ra = getRank(a);
    int rb = getRank(b);
    if (ra != rb) return ra > rb;  // 常量应在前
    // 同 rank 按名称排序
    return a->getName() > b->getName();
}

void reassociateOnFunction(IR::Function* func) {
    if (func->isExternal()) return;

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                auto* ip = inst.get();
                if (!isCommutative(ip->getOpcode())) continue;
                if (ip->getNumOperands() < 2) continue;

                IR::Value* a = ip->getOperand(0);
                IR::Value* b = ip->getOperand(1);

                if (shouldSwap(a, b)) {
                    // 交换操作数
                    ip->setOperand(0, b);
                    ip->setOperand(1, a);
                    changed = true;
                }
            }
        }
    }
}

} // namespace

bool reassociate(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        size_t before = 0;
        for (auto& bb : func->getBlocks()) {
            before += bb->getInstructions().size();
        }
        reassociateOnFunction(func.get());
        size_t after = 0;
        for (auto& bb : func->getBlocks()) {
            after += bb->getInstructions().size();
        }
        // Reassociate 不改变指令数，但通过 setOperand 改变操作数顺序
        // 检查是否有变化用于后续 CSE
    }
    return true; // 总是返回 true 以触发后续 CF+DCE+CSE
}

} // namespace Opt