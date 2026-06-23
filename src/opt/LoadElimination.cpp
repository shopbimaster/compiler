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
// 对于 ALLOCA/GEP，直接比较结构等价性
bool isSamePointer(IR::Value* a, IR::Value* b) {
    if (a == b) return true;

    // 对于 GEP，递归比较基址和所有索引
    auto* ia = dynamic_cast<IR::Instruction*>(a);
    auto* ib = dynamic_cast<IR::Instruction*>(b);
    if (ia && ib &&
        ia->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR &&
        ib->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR) {
        if (ia->getNumOperands() != ib->getNumOperands()) return false;
        for (unsigned i = 0; i < ia->getNumOperands(); ++i) {
            if (!isSamePointer(ia->getOperand(i), ib->getOperand(i)))
                return false;
        }
        return true;
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

    // 执行替换：将 LOAD 的所有 uses 替换为 STORE 的值
    for (auto& [load, val] : replacements) {
        load->replaceAllUsesWith(val);
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