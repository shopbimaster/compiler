// ================================================================
// O1: 死代码消除（Aggressive DCE）— 从副作用指令反向标记活性
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

bool hasSideEffects(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    return op == Opc::STORE || op == Opc::CALL ||
           op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
           op == Opc::ALLOCA;
}

void dceOnFunction(IR::Function* func) {
    if (func->isExternal()) return;

    // ★ IR 规范化：删除 BB 中第一条 terminator 之后的所有指令。
    //   IRBuilder 在 break/continue 后不创建新 BB，导致同一 BB 中
    //   出现多条 br 指令（如 `break;continue;` 生成两条 br）。
    //   getTerminator() 返回 insts.back()，会错误地选择最后一条 br
    //   作为 terminator，导致 buildPredecessors 构建错误的 CFG，
    //   进而使 mem2reg 在循环 header 创建自引用 PHI → 无限循环
    //   （04_break_continue TIMEOUT 根因）。
    //   修复：保留第一条 terminator，删除其后所有指令。
    for (auto& bb : func->getBlocks()) {
        bool seenTerm = false;
        for (auto it = bb->begin(); it != bb->end(); ) {
            auto op = (*it)->getOpcode();
            if (seenTerm) {
                // 第一条 terminator 之后的指令：清空操作数后删除
                (*it)->replaceAllUsesWith(nullptr);
                for (unsigned i = 0; i < (*it)->getNumOperands(); ++i) {
                    (*it)->setOperand(i, nullptr);
                }
                it = bb->erase(it);
            } else if (op == IR::Instruction::Opcode::BR
                       || op == IR::Instruction::Opcode::COND_BR
                       || op == IR::Instruction::Opcode::RET) {
                seenTerm = true;
                ++it;
            } else {
                ++it;
            }
        }
    }

    // 收集所有指令
    std::vector<IR::Instruction*> allInsts;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            allInsts.push_back(inst.get());
        }
    }

    // 从副作用指令出发，反向标记活性
    std::unordered_set<IR::Instruction*> live;
    std::vector<IR::Instruction*> worklist;

    // 临时调试：构建所有有效指针的集合
    std::unordered_set<void*> validInstPtrs(allInsts.begin(), allInsts.end());
    // 收集所有 BB 指针
    std::unordered_set<void*> validBBPtrs;
    for (auto& bb : func->getBlocks()) {
        validBBPtrs.insert(bb.get());
    }
    // 收集所有参数指针
    std::unordered_set<void*> validArgPtrs;
    for (unsigned i = 0; i < func->getNumArgs(); ++i) {
        validArgPtrs.insert(func->getArg(i));
    }

    for (auto* inst : allInsts) {
        if (hasSideEffects(inst)) {
            live.insert(inst);
            worklist.push_back(inst);
        }
    }

    while (!worklist.empty()) {
        auto* inst = worklist.back();
        worklist.pop_back();
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto* op = inst->getOperand(i);
            if (!op) continue;
            // 安全检查：只在已知有效指令指针上做标记
            // 避免对悬空指针做 dynamic_cast 导致崩溃
            if (validInstPtrs.count(op)) {
                auto* defInst = static_cast<IR::Instruction*>(op);
                if (live.insert(defInst).second) {
                    worklist.push_back(defInst);
                }
            }
            // 如果 op 不在 validInstPtrs 中，它不是当前函数的指令
            // 可能是常量、全局变量、函数、BB、参数或悬空指针
            // 对于 DCE，非指令操作数无需标记 live，直接跳过
        }
    }

    // 移除死指令
    // ★ 安全删除顺序（修复 use-after-free）：
    //   1. replaceAllUsesWith(nullptr)：先将其他指令对本指令的引用置 null
    //      这样后续删除引用本指令的死指令时，其操作数已是 null，不会访问已释放内存
    //   2. setOperand(i, nullptr)：将本指令的操作数置 null
    //      dropAllUses() 只移除 use-list 条目但不清空 operands 向量，
    //      析构函数会再次调用 dropAllUses() 访问已释放的操作数 → UAF
    bool removed = true;
    while (removed) {
        removed = false;
        for (auto& bb : func->getBlocks()) {
            for (auto it = bb->begin(); it != bb->end(); ) {
                if (!live.count(it->get())) {
                    (*it)->replaceAllUsesWith(nullptr);
                    for (unsigned i = 0; i < (*it)->getNumOperands(); ++i) {
                        (*it)->setOperand(i, nullptr);
                    }
                    it = bb->erase(it);
                    removed = true;
                } else {
                    ++it;
                }
            }
        }
    }
}

} // namespace

void deadCodeElimination(IR::Module* mod) {
    for (auto& func : mod->getFunctions()) {
        dceOnFunction(func.get());
    }
}

} // namespace Opt