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
            // 记录每个地址最后一条 STORE 指令
            std::unordered_map<IR::Value*, IR::Instruction*> lastStore;
            std::unordered_set<IR::Instruction*> deadStores;

            auto& insts = bb->getInstructions();
            for (size_t i = 0; i < insts.size(); ++i) {
                auto* inst = insts[i].get();

                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    // STORE: operand[0]=value, operand[1]=pointer
                    IR::Value* ptr = inst->getOperand(1);

                    // 检查是否有之前对同一地址的 STORE（且中间没有 LOAD）
                    auto it = lastStore.find(ptr);
                    if (it != lastStore.end() && isSameAddress(it->first, ptr)) {
                        // 前面的 STORE 是死的
                        deadStores.insert(it->second);
                        changed = true;
                    }

                    // 更新 lastStore
                    lastStore[ptr] = inst;
                }
                else if (inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                    // LOAD: operand[0]=pointer
                    // 该地址被读取了，清除 lastStore 记录
                    IR::Value* ptr = inst->getOperand(0);
                    auto it = lastStore.find(ptr);
                    if (it != lastStore.end() && isSameAddress(it->first, ptr)) {
                        lastStore.erase(it);
                    }
                }
                else if (mayWriteMemory(inst)) {
                    // CALL 可能写入任意地址，清空所有 lastStore
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