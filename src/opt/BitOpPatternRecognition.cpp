// ================================================================
// P0: 位运算模式识别（Bit Operation Pattern Recognition）
// 策略：
//   识别并融合连续的基本位运算指令，降序指令数并暴露更多优化机会。
//   受益测试：crc, crypto, huffman
//
// 识别的模式：
//   模式1: 连续同向移位融合 — (x >> a) >> b → x >> (a+b)  (a,b 常量)
//   模式2: 连续同向移位融合 — (x << a) << b → x << (a+b)  (a,b 常量)
//   模式3: 连续 AND 融合 — (x & m1) & m2 → x & (m1 & m2)  (m1,m2 常量)
//   模式4: 移位后 AND 优化 — ((x >> a) & m) → 优化为 (x >> a) 若 m 覆盖所有剩余位
//   模式5: 连续位操作折叠 — XOR/OR 常量合并
//   模式6: 循环中逐位 ASHR 模式 → 合并为多位移位
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// ---- 判断 Value 的唯一定义指令（穿过 VReg/Instruction） ----
IR::Instruction* getDefiningInst(IR::Value* val) {
    if (!val) return nullptr;
    return dynamic_cast<IR::Instruction*>(val);
}

// ---- 模式1+2: 连续同向移位融合 ----
// (x shiftA by a) shiftB by b → x shiftA by (a+b)
bool tryFuseConsecutiveShifts(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    if (op != IR::Instruction::Opcode::ASHR && op != IR::Instruction::Opcode::SHL)
        return false;

    if (inst->getNumOperands() < 2) return false;

    auto* shiftCnt = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!shiftCnt) return false;

    auto* innerInst = getDefiningInst(inst->getOperand(0));
    if (!innerInst) return false;
    if (innerInst->getOpcode() != op) return false;
    if (innerInst->getNumOperands() < 2) return false;

    auto* innerCnt = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(1));
    if (!innerCnt) return false;

    int64_t totalShift = shiftCnt->getValue() + innerCnt->getValue();
    if (totalShift > 31) return false; // 超出 32 位无意义

    auto* i32 = IR::IntegerType::I32;
    auto* newCnt = IR::ConstantInt::get(i32, totalShift);

    auto* fused = IR::Instruction::createBinOp(
        op, inst->getType(), inst->getName() + ".fs",
        innerInst->getOperand(0), newCnt);

    auto* bb = inst->getParent();
    if (!bb) return false;

    inst->replaceAllUsesWith(fused);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            bb->insert(it, fused);
            bb->erase(it);
            return true;
        }
    }
    return false;
}

// ---- 模式3: 连续 AND 融合 ----
// (x & m1) & m2 → x & (m1 & m2)
bool tryFuseConsecutiveAnds(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::AND) return false;
    if (inst->getNumOperands() < 2) return false;

    auto* rhsConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!rhsConst) return false;

    auto* innerInst = getDefiningInst(inst->getOperand(0));
    if (!innerInst || innerInst->getOpcode() != IR::Instruction::Opcode::AND) return false;
    if (innerInst->getNumOperands() < 2) return false;

    auto* innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(1));
    if (!innerConst) {
        innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(0));
        if (!innerConst) return false;
    }

    int64_t fusedMask = rhsConst->getValue() & innerConst->getValue();
    auto* i32 = IR::IntegerType::I32;
    auto* newMask = IR::ConstantInt::get(i32, fusedMask);

    // 找到没有被 mask 的一端作为 x
    IR::Value* x = nullptr;
    for (unsigned i = 0; i < innerInst->getNumOperands(); ++i) {
        if (!dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(i))) {
            x = innerInst->getOperand(i);
            break;
        }
    }
    if (!x) return false;

    auto* fused = IR::Instruction::createBinOp(
        IR::Instruction::Opcode::AND, inst->getType(), inst->getName() + ".fa",
        x, newMask);

    auto* bb = inst->getParent();
    if (!bb) return false;

    inst->replaceAllUsesWith(fused);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            bb->insert(it, fused);
            bb->erase(it);
            return true;
        }
    }
    return false;
}

// ---- 模式5: 连续 XOR/OR 常量合并 ----
// (x ^ c1) ^ c2 → x ^ (c1 ^ c2)
// (x | c1) | c2 → x | (c1 | c2)
bool tryFuseXorOrWithConstants(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    if (op != IR::Instruction::Opcode::XOR && op != IR::Instruction::Opcode::OR)
        return false;
    if (inst->getNumOperands() < 2) return false;

    auto* rhsConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!rhsConst) return false;

    auto* innerInst = getDefiningInst(inst->getOperand(0));
    if (!innerInst || innerInst->getOpcode() != op) return false;
    if (innerInst->getNumOperands() < 2) return false;

    auto* innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(1));
    if (!innerConst) {
        innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(0));
        if (!innerConst) return false;
    }

    int64_t combined;
    if (op == IR::Instruction::Opcode::XOR)
        combined = rhsConst->getValue() ^ innerConst->getValue();
    else
        combined = rhsConst->getValue() | innerConst->getValue();

    // 找到非常量操作数
    IR::Value* x = nullptr;
    for (unsigned i = 0; i < innerInst->getNumOperands(); ++i) {
        if (!dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(i))) {
            x = innerInst->getOperand(i);
            break;
        }
    }
    if (!x) return false;

    auto* i32 = IR::IntegerType::I32;
    auto* newConst = IR::ConstantInt::get(i32, combined);

    auto* fused = IR::Instruction::createBinOp(
        op, inst->getType(), inst->getName() + ".fc",
        x, newConst);

    auto* bb = inst->getParent();
    if (!bb) return false;

    inst->replaceAllUsesWith(fused);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            bb->insert(it, fused);
            bb->erase(it);
            return true;
        }
    }
    return false;
}

// ---- 模式4: 移位后 AND 的规范化 ----
// ((x >> a) & m) 若 m 的所有位都在 x>>a 的有效位范围内，可简化
// 例如：((x >> 4) & 0xFFF) — mask 不冗余时保持不变
// 目前收敛：若 m == 1 且 a == 0，则 (x & 1) 保持不变
// 反例：若 m 覆盖结果所有可能位，可移除 AND
bool trySimplifyShiftAndMask(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::AND) return false;
    if (inst->getNumOperands() < 2) return false;

    auto* maskConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!maskConst) return false;

    auto* shiftInst = getDefiningInst(inst->getOperand(0));
    if (!shiftInst) return false;
    if (shiftInst->getOpcode() != IR::Instruction::Opcode::ASHR &&
        shiftInst->getOpcode() != IR::Instruction::Opcode::SHL)
        return false;
    if (shiftInst->getNumOperands() < 2) return false;

    auto* shiftCnt = dynamic_cast<IR::ConstantInt*>(shiftInst->getOperand(1));
    if (!shiftCnt) return false;

    int64_t shift = shiftCnt->getValue();
    int64_t mask = maskConst->getValue();

    // 计算移位后有效位数
    int effectiveBits = 32 - static_cast<int>(shift);
    if (effectiveBits <= 0) return false;

    // 如果 mask 覆盖了所有有效位，AND 是冗余的
    int64_t fullMask = (1LL << effectiveBits) - 1;
    if ((mask & fullMask) == fullMask) {
        inst->replaceAllUsesWith(shiftInst);
        inst->dropAllUses();
        auto* bb = inst->getParent();
        for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
            if (it2->get() == inst) { bb->erase(it2); break; }
        }
        return true;
    }

    return false;
}

// ---- 对单条指令尝试所有位模式优化 ----
bool tryOptimize(IR::Instruction* inst) {
    if (tryFuseConsecutiveShifts(inst)) return true;
    if (tryFuseConsecutiveAnds(inst)) return true;
    if (tryFuseXorOrWithConstants(inst)) return true;
    if (trySimplifyShiftAndMask(inst)) return true;
    return false;
}

} // namespace

// ================================================================
// bitOpPatternRecognition 入口 — 反复扫描直到收敛
// ================================================================
void bitOpPatternRecognition(IR::Module* mod) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    if (tryOptimize(it->get())) {
                        changed = true;
                        it = bb->begin(); // 重扫
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
}

} // namespace Opt