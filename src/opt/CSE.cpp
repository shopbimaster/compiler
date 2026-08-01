// ================================================================
// O2: 公共子表达式消除（CSE）
// 单基本块内用哈希表去重 (opcode, all operands) 相同的指令
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <string>
#include <cstdint>
#include <vector>

namespace Opt {
namespace {

// ---- CSE 键：将 (opcode, all operands, 可选 condition) 哈希 ----
struct CSEKey {
    IR::Instruction::Opcode opcode;
    std::vector<IR::Value*> ops;
    std::string condition; // 仅 ICMP/FCMP 用于区分 eq/ne/slt/sle/sgt/sge

    bool operator==(const CSEKey& other) const {
        return opcode == other.opcode &&
               ops == other.ops &&
               condition == other.condition;
    }
};

struct CSEKeyHash {
    std::size_t operator()(const CSEKey& k) const {
        auto h = std::hash<uintptr_t>{};
        std::size_t seed = static_cast<std::size_t>(k.opcode);
        for (auto* v : k.ops) {
            seed ^= h(reinterpret_cast<uintptr_t>(v)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
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
    // LOAD：允许单 BB 内 CSE，但需要额外的安全性检查
    // 两个 LOAD 相同指针且中间无 STORE → 第二个 LOAD 冗余
    // 这消除了循环体中对同一 ALLOCA 的重复 LOAD，减少冗余 mv 指令
    if (op == Opc::LOAD) return true;
    // GETELEMENTPTR：禁用同 BB 内 CSE
    //   原因：CSE 可能将多个紧接 LOAD/STORE 的 GEP 合并为 1 个，GEP 结果活跃区间
    //   从 1 条指令延长到 15+ 条，增加寄存器压力导致溢出。
    //   GEPStrengthReduce 内部已有 lsrCache 对相同 base/iv 的 GEP 去重，
    //   CSE 对 LSR 无额外收益，且寄存器压力增加可能抵消局部收益。
    //   ★ 深度调查（2026-07）：即使限定为"中间 GEP"（仅被其他 GEP 使用的 GEP），
    //   汇编指令数减少 32%（728→497）、内存操作减少，但 QEMU 性能仍回归 +200ms。
    //   根因：CSE 改变寄存器分配（lsr.ptr 减少→其他值分配到不同寄存器），
    //   QEMU TCG 将 RISC-V 的 12 个 s 寄存器映射到 x86 仅 6 个 callee-saved，
    //   寄存器分配变化导致 host 寄存器映射变差。真实 BOOM 硬件不受此影响，
    //   但 QEMU 测试基准必须接受。结论：不改变寄存器分配的优化才安全。
    if (op == Opc::GETELEMENTPTR) return false;

    return true;
}

// ---- 构造 CSE 键 ----
CSEKey makeKey(IR::Instruction* inst) {
    CSEKey key;
    key.opcode = inst->getOpcode();
    // 收集所有操作数，确保多操作数指令（如 GEP）正确区分
    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        key.ops.push_back(inst->getOperand(i));
    }

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

        // 当遇到 STORE 时，清除所有缓存的 LOAD 条目（保守但安全）
        // 因为 STORE 可能改变 LOAD 的返回值
        if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
            // 移除所有 LOAD 类型的缓存条目
            for (auto it2 = available.begin(); it2 != available.end(); ) {
                if (it2->first.opcode == IR::Instruction::Opcode::LOAD) {
                    it2 = available.erase(it2);
                } else {
                    ++it2;
                }
            }
            ++it;
            continue;
        }

        // CALL 可能修改内存，也清除 LOAD 缓存
        if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
            for (auto it2 = available.begin(); it2 != available.end(); ) {
                if (it2->first.opcode == IR::Instruction::Opcode::LOAD) {
                    it2 = available.erase(it2);
                } else {
                    ++it2;
                }
            }
            ++it;
            continue;
        }

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

bool commonSubexpressionElimination(IR::Module* mod) {
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
    return changed;
}

} // namespace Opt
