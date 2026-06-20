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

// ---- 找到函数体入口 BB ----
// 尾递归消除后，BR 需要跳转到函数体的起始位置（包括 base case 检查）。
// 如果 entry block 中包含 allocas/init-stores 和 base case 检查，
// 则将 entry block 拆分为 init 和 body 两部分，BR 跳转到 body。
// 该函数是幂等的：拆分后 entry 的 terminator 变为 unconditional BR，
// 再次调用时直接返回 BR 的目标。
IR::BasicBlock* findBodyBlock(IR::Function* func) {
    auto* entry = func->getEntryBlock();
    if (!entry) return nullptr;
    using Opc = IR::Instruction::Opcode;

    // 如果 entry 已经被拆分过（terminator 是 unconditional BR），
    // 直接返回 BR 的目标
    auto* term = entry->getTerminator();
    if (term && term->getOpcode() == Opc::BR) {
        return dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
    }

    // 找到 entry block 中第一个非 alloca/init-store 的指令位置
    size_t splitIdx = 0;
    const auto& insts = entry->getInstructions();
    for (size_t i = 0; i < insts.size(); ++i) {
        auto op = insts[i]->getOpcode();
        if (op == Opc::ALLOCA) continue;
        if (op == Opc::STORE) {
            auto* val = insts[i]->getOperand(0);
            auto* ptr = insts[i]->getOperand(1);
            // init-store: store Argument → alloca
            if (dynamic_cast<IR::Argument*>(val)) {
                auto* ptrInst = dynamic_cast<IR::Instruction*>(ptr);
                if (ptrInst && ptrInst->getOpcode() == Opc::ALLOCA) {
                    continue;
                }
            }
        }
        splitIdx = i;
        break;
    }

    if (splitIdx == 0) {
        // 没有 init 指令，entry 本身就是 body
        return entry;
    }

    if (splitIdx >= insts.size()) {
        // 只有 init 指令，没有 body（不应该发生）
        return nullptr;
    }

    // 需要拆分 entry block：创建新的 body block
    auto* bodyBB = func->createBlock("body");

    // 将 splitIdx 之后的指令移动到 bodyBB
    auto it = entry->begin();
    for (size_t i = 0; i < splitIdx; ++i) ++it;
    while (it != entry->end()) {
        auto* inst = it->release();  // 释放 unique_ptr 所有权
        bodyBB->pushBack(inst);      // bodyBB 取得所有权
        it = entry->erase(it);       // 删除空 unique_ptr，返回下一个迭代器
    }

    // 在 entry 末尾添加 BR bodyBB
    auto* br = IR::Instruction::createBr(bodyBB);
    entry->pushBack(br);

    return bodyBB;
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

    std::vector<IR::Instruction*> tailCalls;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (isTailCall(inst.get())) {
                tailCalls.push_back(inst.get());
            }
        }
    }
    bool changed = false;
    for (auto* call : tailCalls) {
        if (eliminateTailCall(call, func, argAllocas))
            changed = true;
    }
    return changed;
}

} // namespace

bool tailRecursionElimination(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (eliminateOnFunction(func.get())) { changed = true; anyChanged = true; }
        }
    }
    return anyChanged;
}

} // namespace Opt