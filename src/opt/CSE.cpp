// ================================================================
// O2: 局部公共子表达式消除（Local CSE）
// 在每个基本块内用哈希表去重（opcode, operands）相同的指令
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <string>
#include <cstdint>

namespace Opt {
namespace {

// ---- CSE 键：将 (opcode, ops, 可选 condition) 哈希为一个 64 位值 ----
struct CSEKey {
    IR::Instruction::Opcode opcode;
    IR::Value* op0;
    IR::Value* op1;
    std::string condition; // 仅 ICMP/FCMP 用于区分 eq/ne/slt/sle/sgt/sge

    bool operator==(const CSEKey& other) const {
        return opcode == other.opcode &&
               op0 == other.op0 &&
               op1 == other.op1 &&
               condition == other.condition;
    }
};

struct CSEKeyHash {
    std::size_t operator()(const CSEKey& k) const {
        auto h = std::hash<uintptr_t>{};
        std::size_t seed = static_cast<std::size_t>(k.opcode);
        seed ^= h(reinterpret_cast<uintptr_t>(k.op0)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(reinterpret_cast<uintptr_t>(k.op1)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        if (!k.condition.empty())
            seed ^= std::hash<std::string>{}(k.condition) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// ---- 判断指令是否适合 CSE ----
bool canCSE(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;

    // 排除有副作用的指令
    if (op == Opc::STORE || op == Opc::CALL ||
        op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
        op == Opc::ALLOCA || op == Opc::PHI)
        return false;
    // LOAD 可能别名，保守不优化
    if (op == Opc::LOAD) return false;
    // GETELEMENTPTR 不优化
    if (op == Opc::GETELEMENTPTR) return false;

    return true;
}

// ---- 构造 CSE 键 ----
CSEKey makeKey(IR::Instruction* inst) {
    CSEKey key;
    key.opcode = inst->getOpcode();
    key.op0 = inst->getNumOperands() > 0 ? inst->getOperand(0) : nullptr;
    key.op1 = inst->getNumOperands() > 1 ? inst->getOperand(1) : nullptr;

    // ICMP/FCMP 的名称携带比较条件（eq/ne/slt/sle/sgt/sge）
    auto op = inst->getOpcode();
    if (op == IR::Instruction::Opcode::ICMP || op == IR::Instruction::Opcode::FCMP) {
        key.condition = inst->getName();
    }
    return key;
}

// ---- 单个基本块内的 CSE ----
bool cseOnBlock(IR::BasicBlock* bb) {
    std::unordered_map<CSEKey, IR::Instruction*, CSEKeyHash> available;
    bool changed = false;

    for (auto it = bb->begin(); it != bb->end(); ) {
        auto* inst = it->get();
        if (!canCSE(inst)) {
            ++it;
            continue;
        }

        auto key = makeKey(inst);
        auto found = available.find(key);
        if (found != available.end() && found->second != inst) {
            // 找到重复 — 用第一个出现的结果替换所有使用
            inst->replaceAllUsesWith(found->second);
            inst->dropAllUses();
            it = bb->erase(it);
            changed = true;
        } else {
            available[key] = inst;
            ++it;
        }
    }

    return changed;
}

} // namespace

void commonSubexpressionElimination(IR::Module* mod) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                if (cseOnBlock(bb.get())) changed = true;
            }
        }
    }
}

} // namespace Opt