// ================================================================
// O2: 冗余加载消除 (LoadElimination) — 用最近 STORE 的值替换冗余 LOAD
// 借鉴 Cpl1 的 LoadElimination 设计，基于 ALLOCA 指针直接比较
// 安全约束：若 ALLOCA 在多个 BB 中被 STORE，则不消除其 LOAD
//   （因为后续 Pass（如 TailRecursionElimination）可能将函数转换为循环，
//     此时 LOAD 需要读取更新后的值，而非初始值）
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// 判断一条指令是否可能修改内存（CALL 可能修改任何地址）
bool mayWriteMemory(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    return op == Opc::STORE || op == Opc::CALL;
}

// 判断两个指针是否"必须相同"
// 最保守策略：仅当指针完全相同（同一 Value）时才返回 true
// 即使 GEP 结构等价，也可能因为索引值被 loadElimination 替换而产生错误交互
bool isSamePointer(IR::Value* a, IR::Value* b) {
    return a == b;
}

// 判断指针是否涉及全局变量（直接或通过 GEP 链）
// 全局变量可能被其他函数修改，LOAD 消除不安全
bool involvesGlobal(IR::Value* ptr) {
    if (!ptr) return false;
    if (dynamic_cast<IR::GlobalVariable*>(ptr)) return true;
    auto* inst = dynamic_cast<IR::Instruction*>(ptr);
    if (inst && inst->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR) {
        return involvesGlobal(inst->getOperand(0));
    }
    return false;
}

// 收集所有需要替换的 LOAD → 替换值映射
// key: LOAD 指令, value: 替换为的值
using ReplaceMap = std::unordered_map<IR::Instruction*, IR::Value*>;

void loadElimOnFunction(IR::Function* func) {
    if (func->isExternal()) return;

    // 安全检查：如果函数是递归的（调用自身），跳过 LoadElimination
    // 因为后续 TailRecursionElimination 会将递归转换为循环，
    // 此时 ALLOCA 会在循环体中被更新，而我们看不到这些未来的 STORE
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                if (inst->getOperand(0) == func) {
                    return;  // 递归函数，跳过
                }
            }
        }
    }

    // 第一遍：收集所有在不同 BB 中被 STORE 的 ALLOCA 指针
    // 若一个 ALLOCA 在多个 BB 中被 STORE，则其 LOAD 不可消除
    // （因为后续 Pass 可能将函数转换为循环，LOOP 中的 LOAD 需要读取更新后的值）
    std::unordered_set<IR::Value*> volatileAllocas;
    {
        std::unordered_map<IR::Value*, IR::BasicBlock*> storeBBs;
        for (auto& bb : func->getBlocks()) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    IR::Value* ptr = inst->getOperand(1);
                    auto it = storeBBs.find(ptr);
                    if (it == storeBBs.end()) {
                        storeBBs[ptr] = bb.get();
                    } else if (it->second != bb.get()) {
                        // 同一个指针在多个 BB 中被 STORE，标记为 volatile
                        volatileAllocas.insert(ptr);
                    }
                }
            }
        }
    }

    // 收集需要替换的 LOAD
    ReplaceMap replacements;

    for (auto& bb : func->getBlocks()) {
        // 记录每个地址最后一次 STORE 的值
        // key: pointer, value: stored value
        std::unordered_map<IR::Value*, IR::Value*> lastStoreVal;

        for (auto& inst : bb->getInstructions()) {
            auto* ip = inst.get();

            if (ip->getOpcode() == IR::Instruction::Opcode::STORE) {
                // STORE: operand[0]=value, operand[1]=pointer
                IR::Value* val = ip->getOperand(0);
                IR::Value* ptr = ip->getOperand(1);

                // 跳过涉及全局变量的指针：全局变量可能被其他函数修改，
                // 且 GlobalVariablePromotion 会创建 LOAD→STORE 链路，
                // 这里的 LOAD 消除可能与提升后的代码产生错误交互
                if (involvesGlobal(ptr)) continue;

                // 更新/覆盖 lastStoreVal 中匹配的指针
                bool found = false;
                for (auto it = lastStoreVal.begin(); it != lastStoreVal.end(); ) {
                    if (isSamePointer(it->first, ptr)) {
                        it->second = val;
                        found = true;
                        ++it;
                    } else {
                        ++it;
                    }
                }
                if (!found) {
                    lastStoreVal[ptr] = val;
                }
            }
            else if (ip->getOpcode() == IR::Instruction::Opcode::LOAD) {
                // LOAD: operand[0]=pointer
                IR::Value* ptr = ip->getOperand(0);

                // 安全检查：若此 ALLOCA 在多个 BB 中被 STORE，跳过
                if (volatileAllocas.count(ptr)) continue;

                // 跳过涉及全局变量的指针
                if (involvesGlobal(ptr)) continue;

                // 查找是否有匹配的最近 STORE
                for (auto& [storePtr, storedVal] : lastStoreVal) {
                    if (isSamePointer(storePtr, ptr)) {
                        // 找到匹配的 STORE，标记此 LOAD 可被替换
                        replacements[ip] = storedVal;
                        break;
                    }
                }
            }
            else if (ip->getOpcode() == IR::Instruction::Opcode::CALL) {
                // CALL 可能修改任意内存，清空所有跟踪
                lastStoreVal.clear();
            }
        }
    }

    // 执行替换：仅当 LOAD 的所有 uses 都在同一 BB 内时才替换
    // 避免跨 BB 替换导致 dominance 问题
    for (auto& [load, val] : replacements) {
        auto* loadBB = load->getParent();
        bool allUsesInSameBB = true;
        for (auto& use : load->getUses()) {
            auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
            if (userInst && userInst->getParent() != loadBB) {
                allUsesInSameBB = false;
                break;
            }
        }
        if (allUsesInSameBB) {
            load->replaceAllUsesWith(val);
        }
    }

    // 删除变成死的 LOAD 指令（由后续 DCE 处理）
}

} // namespace

bool loadElimination(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        size_t before = 0;
        for (auto& bb : func->getBlocks()) {
            before += bb->getInstructions().size();
        }

        loadElimOnFunction(func.get());

        size_t after = 0;
        for (auto& bb : func->getBlocks()) {
            after += bb->getInstructions().size();
        }
        if (before != after) changed = true;
    }
    return changed;
}

} // namespace Opt