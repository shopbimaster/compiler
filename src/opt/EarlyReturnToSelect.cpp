// ================================================================
// P4: EarlyReturn→Select 转换
// 将 if-else-return 模式转换为 select + ret，消除 COND_BR。
//
// 目标：h-4-03 的 max(int a, int b) { if(a<b) return b; else return a; }
// 内联后 f(x) 中出现 3 个 if-else-RET → 转为 3 个 SELECT → 循环体变单 BB → 可展开。
//
// 转换前:
//   entry:
//     %cond = icmp slt %a, %b
//     cond_br %cond, %then, %else
//   then:
//     ret %X        // 仅 RET，无其他计算
//   else:
//     ret %Y        // 仅 RET，无其他计算
//
// 转换后:
//   entry:
//     %cond = icmp slt %a, %b
//     %sel = select %cond, %X, %Y
//     ret %sel
//
// ★ 安全限制（v2 修复）：
//   - then/else 必须仅含 RET 指令（无计算指令），避免克隆 valueMap 复杂性
//   - 若 then/else 含计算指令，交给 IfConversion 处理（它有完善的投机检查）
//   - then/else 仅有一个前驱（entry）
//   - 返回值类型一致
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Opt {

namespace {

using Opc = IR::Instruction::Opcode;

// 检查 BB 是否仅含 RET 指令（无其他非终止指令）
// 返回 RET 指令指针，若无 RET 或有其他指令返回 nullptr
IR::Instruction* isPureRetBlock(IR::BasicBlock* bb) {
    IR::Instruction* retInst = nullptr;
    for (auto& inst : bb->getInstructions()) {
        Opc op = inst->getOpcode();
        if (op == Opc::RET) {
            if (retInst) return nullptr;  // 多个 RET
            retInst = inst.get();
        } else if (op == Opc::BR || op == Opc::COND_BR) {
            continue;  // 跳过控制流（不应出现，但防御性处理）
        } else {
            return nullptr;  // 有非 RET 非控制流指令
        }
    }
    return retInst;
}

// 获取 RET 指令的返回值（无返回值返回 nullptr）
IR::Value* getReturnValue(IR::Instruction* retInst) {
    if (retInst->getOpcode() != Opc::RET) return nullptr;
    if (retInst->getNumOperands() == 0) return nullptr;
    return retInst->getOperand(0);
}

// 处理单个 if-else-return 模式
// 返回 true 若成功转换
bool convertIfElseReturn(IR::BasicBlock* entry, IR::Function* func) {
    auto* condBr = entry->getTerminator();
    if (!condBr || condBr->getOpcode() != Opc::COND_BR) return false;

    auto* thenBB = dynamic_cast<IR::BasicBlock*>(condBr->getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(condBr->getOperand(2));
    if (!thenBB || !elseBB) return false;
    if (thenBB == elseBB) return false;

    // then/else 必须各只有一个前驱（entry）
    auto preds = buildPredecessors(func);
    if (preds[thenBB].size() != 1 || preds[thenBB][0] != entry) return false;
    if (preds[elseBB].size() != 1 || preds[elseBB][0] != entry) return false;

    // ★ 安全限制：then/else 必须仅含 RET（无计算指令）
    auto* thenRet = isPureRetBlock(thenBB);
    auto* elseRet = isPureRetBlock(elseBB);
    if (!thenRet || !elseRet) return false;

    // 获取返回值
    IR::Value* thenRetVal = getReturnValue(thenRet);
    IR::Value* elseRetVal = getReturnValue(elseRet);

    // 两路返回值必须同时有值或同时无值（void 函数）
    if ((thenRetVal == nullptr) != (elseRetVal == nullptr)) return false;

    // 无返回值（void 函数）：两路都是 ret void，直接合并为 ret
    if (thenRetVal == nullptr) {
        auto* newRet = IR::Instruction::createRet(nullptr);
        auto brIt = std::find_if(entry->begin(), entry->end(),
            [&](std::unique_ptr<IR::Instruction>& p) { return p.get() == condBr; });
        if (brIt == entry->end()) return false;
        entry->insert(brIt, newRet);
        entry->erase(brIt);
        return true;
    }

    // 有返回值：创建 select cond, thenVal, elseVal
    // ★ 安全检查：返回值不能是 then/else 块内定义的指令（因为我们要删除这些块）
    //   返回值必须来自 entry 或更早的块（函数参数、entry 中的计算等）
    if (auto* thenInst = dynamic_cast<IR::Instruction*>(thenRetVal)) {
        if (thenInst->getParent() == thenBB) return false;  // 值在 then 中定义
    }
    if (auto* elseInst = dynamic_cast<IR::Instruction*>(elseRetVal)) {
        if (elseInst->getParent() == elseBB) return false;  // 值在 else 中定义
    }

    IR::Value* cond = condBr->getOperand(0);

    // 创建 select cond, thenVal, elseVal
    static int selCnt = 0;
    std::string selName = "%ers.sel" + std::to_string(selCnt++);
    auto* select = IR::Instruction::createSelect(cond, thenRetVal, elseRetVal, selName);

    // 创建 ret select
    auto* newRet = IR::Instruction::createRet(select);

    // 找到 cond_br 的位置（每次插入后重新查找，避免 vector insert 导致迭代器失效）
    auto findBr = [&]() {
        return std::find_if(entry->begin(), entry->end(),
            [&](std::unique_ptr<IR::Instruction>& p) { return p.get() == condBr; });
    };

    auto brIt = findBr();
    if (brIt == entry->end()) return false;

    // 插入 select（在 cond_br 之前）
    entry->insert(brIt, select);
    // ★ 插入后迭代器可能失效，重新查找 cond_br
    brIt = findBr();
    if (brIt == entry->end()) return false;

    // 插入 newRet（在 cond_br 之前）
    entry->insert(brIt, newRet);
    // ★ 重新查找 cond_br
    brIt = findBr();
    if (brIt == entry->end()) return false;

    // 移除 cond_br
    entry->erase(brIt);

    return true;
}

} // namespace

bool earlyReturnToSelect(IR::Module* mod) {
    bool anyChanged = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        // 重复扫描直到无更多转换
        bool funcChanged = true;
        while (funcChanged) {
            funcChanged = false;
            for (auto& bb : func->getBlocks()) {
                if (convertIfElseReturn(bb.get(), func.get())) {
                    funcChanged = true;
                    anyChanged = true;
                    break;  // 重新扫描（CFG 已变）
                }
            }
        }
    }
    return anyChanged;
}

} // namespace Opt
