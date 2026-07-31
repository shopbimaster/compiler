// ================================================================
// O2: 死存储消除 (DSE) — 消除被后续 STORE 覆盖且中间无 LOAD 的冗余 STORE
// 借鉴 Cpl1 的 DSE 设计，基于 ALLOCA 指针直接比较
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// 判断一条指令是否可能修改内存（调用、其他 STORE 等）
bool mayWriteMemory(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    return op == Opc::STORE || op == Opc::CALL;
}

// 判断一条指令是否可能读取内存
bool mayReadMemory(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    return op == Opc::LOAD || op == Opc::CALL;
}

// 两个指针是否"必须相同"：直接比较 ALLOCA/GEP/GlobalVariable 指针
// 对于 GEP，需要基址和偏移都相同
bool isSameAddress(IR::Value* a, IR::Value* b) {
    // 直接同一个指针对象
    if (a == b) return true;

    // 都是 GEP：比较基址和所有索引
    if (a->getType() && a->getType()->isPointer() &&
        b->getType() && b->getType()->isPointer()) {
        auto* instA = dynamic_cast<IR::Instruction*>(a);
        auto* instB = dynamic_cast<IR::Instruction*>(b);
        if (instA && instB &&
            instA->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR &&
            instB->getOpcode() == IR::Instruction::Opcode::GETELEMENTPTR) {
            if (instA->getNumOperands() != instB->getNumOperands()) return false;
            for (unsigned i = 0; i < instA->getNumOperands(); ++i) {
                if (!isSameAddress(instA->getOperand(i), instB->getOperand(i)))
                    return false;
            }
            return true;
        }
    }
    return false;
}

void dseOnFunction(IR::Function* func) {
    if (func->isExternal()) return;

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto& bb : func->getBlocks()) {
            // 记录每个地址最后一条 STORE 指令。
            // 注意：不能用 unordered_map<ptr,...> 做键查找 —— 结构相同但对象
            // 不同的 GEP（如循环内每次迭代重算的 a[i][j]）指针值不相等，
            // 哈希查找必然 miss，isSameAddress 的 GEP 等价判断永远走不到。
            // 因此改为线性扫描 + isSameAddress 比较。
            std::vector<std::pair<IR::Value*, IR::Instruction*>> lastStore;
            std::unordered_set<IR::Instruction*> deadStores;

            auto findSlot = [&lastStore](IR::Value* ptr) -> int {
                for (size_t k = 0; k < lastStore.size(); ++k) {
                    if (isSameAddress(lastStore[k].first, ptr))
                        return static_cast<int>(k);
                }
                return -1;
            };

            auto& insts = bb->getInstructions();
            for (size_t i = 0; i < insts.size(); ++i) {
                auto* inst = insts[i].get();

                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    // STORE: operand[0]=value, operand[1]=pointer
                    IR::Value* ptr = inst->getOperand(1);

                    int slot = findSlot(ptr);
                    if (slot >= 0) {
                        // 同一地址上一条 STORE 被本条完全覆盖（中间无 LOAD）→ 死存储
                        deadStores.insert(lastStore[slot].second);
                        lastStore[slot].second = inst;
                        changed = true;
                    } else {
                        lastStore.emplace_back(ptr, inst);
                    }
                }
                else if (inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                    // LOAD: operand[0]=pointer
                    // 该地址被读取了，清除 lastStore 记录。
                    // 保守：无法证明不别名的记录也一并失效。
                    IR::Value* ptr = inst->getOperand(0);
                    int slot = findSlot(ptr);
                    if (slot >= 0) {
                        lastStore.erase(lastStore.begin() + slot);
                    } else {
                        // 未知地址的 LOAD 可能读到任何已记录地址 → 全部失效
                        lastStore.clear();
                    }
                }
                else if (mayWriteMemory(inst) || mayReadMemory(inst)) {
                    // CALL 可能读写任意地址，清空所有 lastStore
                    lastStore.clear();
                }
            }


            // 移除死 STORE
            if (!deadStores.empty()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    if (deadStores.count(it->get())) {
                        (*it)->dropAllUses();
                        it = bb->erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
}

} // namespace

bool deadStoreElimination(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        // 记录删除前的指令数
        size_t before = 0;
        for (auto& bb : func->getBlocks()) {
            before += bb->getInstructions().size();
        }
        dseOnFunction(func.get());
        size_t after = 0;
        for (auto& bb : func->getBlocks()) {
            after += bb->getInstructions().size();
        }
        if (before != after) changed = true;
    }
    return changed;
}

} // namespace Opt