// ================================================================
// O3: 尾递归消除（Tail Recursion Elimination）
// 策略：
//   检测函数的尾递归调用（CALL self 后紧跟 RET），将其转换为循环跳转
//   用 store 更新参数 alloca + br 回函数体开头替代 call + ret
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <vector>

namespace Opt {
namespace {

// ---- 判断指令是否为尾调用（末尾 CALL self 后紧接 RET） ----
bool isTailCall(IR::Instruction* callInst) {
    if (callInst->getOpcode() != IR::Instruction::Opcode::CALL) return false;
    auto* bb = callInst->getParent();
    if (!bb) return false;

    // 找到 CALL 在 BB 中的位置
    int idx = -1;
    int total = static_cast<int>(bb->getInstructions().size());
    const auto& insts = bb->getInstructions();
    for (int i = 0; i < total; ++i) {
        if (insts[i].get() == callInst) { idx = i; break; }
    }
    if (idx < 0 || idx >= total - 1) return false;

    // 下一条必须是 RET
    auto* next = insts[idx + 1].get();
    if (next->getOpcode() != IR::Instruction::Opcode::RET) return false;

    // RET 的操作数必须是 CALL 的结果（或者 void RET）
    if (next->getNumOperands() > 0 && next->getOperand(0) != callInst) return false;

    return true;
}

// ---- 查找参数 alloca：在 entry BB 中找到 store Argument → alloca 的映射 ----
std::unordered_map<IR::Argument*, IR::Instruction*> findParamAllocas(IR::Function* func) {
    std::unordered_map<IR::Argument*, IR::Instruction*> argToAlloca;
    auto* entry = func->getEntryBlock();
    if (!entry) return argToAlloca;

    for (auto& inst : entry->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
        if (inst->getNumOperands() < 2) continue;
        auto* arg = dynamic_cast<IR::Argument*>(inst->getOperand(0));
        if (!arg) continue;
        auto* allocaInst = dynamic_cast<IR::Instruction*>(inst->getOperand(1));
        if (allocaInst && allocaInst->getOpcode() == IR::Instruction::Opcode::ALLOCA) {
            argToAlloca[arg] = allocaInst;
        }
    }
    return argToAlloca;
}

// ---- 找到函数体第一个 BB（entry 之后的第一个非空 BB） ----
IR::BasicBlock* findBodyBlock(IR::Function* func) {
    bool foundEntry = false;
    for (auto& bb : func->getBlocks()) {
        if (foundEntry && !bb->empty()) return bb.get();
        if (bb.get() == func->getEntryBlock()) foundEntry = true;
    }
    return nullptr;
}

// ---- 对单个尾调用做消除变换 ----
bool eliminateTailCall(
    IR::Instruction* callInst,
    IR::Function* func,
    const std::unordered_map<IR::Argument*, IR::Instruction*>& argAllocas) {
    auto* bb = callInst->getParent();
    auto* bodyBB = findBodyBlock(func);
    auto* callee = callInst->getOperand(0);
    if (callee != static_cast<IR::Value*>(func)) return false;
    if (!bodyBB) return false;
    if (argAllocas.empty()) return false;

    // 1. 为每个参数创建 store 指令（call args[i] → param alloca）
    //    参数从 CALL 的 operands[1..N] 获取
    //    需要找到对应的 Argument → alloca 映射
    for (unsigned i = 0; i < func->getNumArgs(); ++i) {
        auto* arg = func->getArg(i);
        auto allocIt = argAllocas.find(arg);
        if (allocIt == argAllocas.end()) continue;

        auto* val = callInst->getOperand(i + 1);
        if (!val) continue;

        auto* storeInst = IR::Instruction::createStore(val, allocIt->second);
        // 在 CALL 之前插入 store
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (it->get() == callInst) {
                bb->insert(it, storeInst);
                break;
            }
        }
    }

    // 2. 找到 RET 指令（紧接 CALL 之后）
    IR::Instruction* retInst = nullptr;
    bool foundCall = false;
    for (auto& inst : bb->getInstructions()) {
        if (foundCall && inst->getOpcode() == IR::Instruction::Opcode::RET) {
            retInst = inst.get();
            break;
        }
        if (inst.get() == callInst) foundCall = true;
    }
    if (!retInst) return false;

    // 3. 将 RET 替换为 BR bodyBB
    auto* brInst = IR::Instruction::createBr(bodyBB);

    // 在 RET 位置插入 BR，然后删除 RET 和 CALL
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == retInst) {
            bb->insert(it, brInst);
            break;
        }
    }

    // 删除 RET
    retInst->dropAllUses();
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == retInst) {
            bb->erase(it);
            break;
        }
    }

    // 删除 CALL（其 uses 已经被 stores 替换，RET 不再引用它）
    callInst->dropAllUses();
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == callInst) {
            bb->erase(it);
            break;
        }
    }

    return true;
}

// ---- 单函数尾递归消除 ----
bool eliminateOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;

    // 检查是否有自调用
    bool hasSelfCall = false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL &&
                inst->getOperand(0) == static_cast<IR::Value*>(func)) {
                hasSelfCall = true;
                break;
            }
        }
        if (hasSelfCall) break;
    }
    if (!hasSelfCall) return false;

    auto argAllocas = findParamAllocas(func);
    if (argAllocas.empty()) return false;

    bool changed = false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (isTailCall(inst.get())) {
                if (eliminateTailCall(inst.get(), func, argAllocas))
                    changed = true;
            }
        }
    }
    return changed;
}

} // namespace

void tailRecursionElimination(IR::Module* mod) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (eliminateOnFunction(func.get())) changed = true;
        }
    }
}

} // namespace Opt