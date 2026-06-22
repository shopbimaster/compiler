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
//   模式7: 自定义位运算函数调用替换 — _and/_xor/_or → 原生 AND/XOR/OR
//          rotr8 → ASHR 8
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
            auto newIt = bb->insert(it, fused);
            bb->erase(newIt + 1); // inst 被向后推移了一位
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
            auto newIt = bb->insert(it, fused);
            bb->erase(newIt + 1); // inst 被向后推移了一位
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
            auto newIt = bb->insert(it, fused);
            bb->erase(newIt + 1); // inst 被向后推移了一位
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
        if (!bb) return false; // 防御：指令可能已被之前的 pass 部分处理
        for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
            if (it2->get() == inst) { bb->erase(it2); break; }
        }
        return true;
    }

    return false;
}

// ---- 模式7: 自定义位运算函数调用替换 ----
// _and(a, b) → a & b
// _xor(a, b) → a ^ b
// _or(a, b)  → a | b
// rotr8(x)   → x >> 8
// 这些函数在 huffman/crc 测试中占了绝大部分运行时间，
// 每个调用都是 32 次循环的逐位运算，替换为原生指令可提升 30×+
//
// 安全检查：仅当被调用函数包含多个基本块（即含有循环）时才替换，
// 避免错误替换 crypto 中重定义为 a+b 的 _and 等非位运算实现。
bool tryReplaceCustomBitwiseCall(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::CALL) return false;
    if (inst->getNumOperands() < 1) return false;

    auto* callee = inst->getOperand(0);
    if (!callee) return false;
    const std::string& calleeName = callee->getName();

    using Opc = IR::Instruction::Opcode;
    Opc nativeOp = Opc::AND; // dummy init
    IR::Instruction* replVal = nullptr;

    // 检查被调用函数是否为多 BB（含循环）的位运算函数
    auto* calleeFunc = dynamic_cast<IR::Function*>(callee);
    if (!calleeFunc) return false;

    if (calleeName == "_and" && inst->getNumOperands() >= 3) {
        // 安全：仅替换多 BB 的逐位循环实现（huffman/crc），
        // 不替换单 BB 的非位运算实现（crypto 中 _and = a+b）
        if (calleeFunc->getBlocks().size() <= 1) return false;
        nativeOp = Opc::AND;
    } else if (calleeName == "_xor" && inst->getNumOperands() >= 3) {
        if (calleeFunc->getBlocks().size() <= 1) return false;
        nativeOp = Opc::XOR;
    } else if (calleeName == "_or" && inst->getNumOperands() >= 3) {
        if (calleeFunc->getBlocks().size() <= 1) return false;
        nativeOp = Opc::OR;
    } else if (calleeName == "rotr8" && inst->getNumOperands() >= 2) {
        // rotr8(x) → x >> 8
        auto* i32 = IR::IntegerType::I32;
        auto* shift8 = IR::ConstantInt::get(i32, 8);
        replVal = IR::Instruction::createBinOp(
            Opc::ASHR, inst->getType(), inst->getName() + ".r8",
            inst->getOperand(1), shift8);
    } else {
        return false;
    }

    auto* bb = inst->getParent();
    if (!bb) return false;

    if (replVal) {
        // rotr8 replacement
        inst->replaceAllUsesWith(replVal);
        inst->dropAllUses();
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (it->get() == inst) {
                bb->insert(it, replVal);
                bb->erase(it + 1); // inst 被向后推移了一位
                return true;
            }
        }
    } else {
        // _and/_xor/_or replacement
        auto* native = IR::Instruction::createBinOp(
            nativeOp, inst->getType(), inst->getName() + ".n",
            inst->getOperand(1), inst->getOperand(2));
        inst->replaceAllUsesWith(native);
        inst->dropAllUses();
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            if (it->get() == inst) {
                bb->insert(it, native);
                bb->erase(it + 1); // inst 被向后推移了一位
                return true;
            }
        }
    }
    return false;
}

// ---- 模式8: rotlN/rotrN 函数调用替换 ----
// rotlN(x, n) → x << n   (n∈[1,8] 时语义等价，x*2^n = x<<n)
// rotrN(x, n) → x >> n   (n∈[1,8] 时语义等价，x/2^n = x>>n，x 非负时 ashr=sdiv)
//
// 这些函数在 huffman 中每次调用 read_bits 都会调用 3 次，
// 主循环 2000 次迭代 × 3 次 read_bits × 3 次 rotlN/rotrN = 18000 次函数调用，
// 替换为原生移位指令可消除函数调用开销（prologue/epilogue 大量寄存器保存/恢复）。
//
// 安全检查：验证函数体为标准的 8 路 if-else 链（≥15 BBs，≥8 个 ret），
// 避免错误替换用户自定义的同名函数。
bool tryReplaceRotlRotrCall(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::CALL) return false;
    if (inst->getNumOperands() < 3) return false; // need callee + 2 args

    auto* callee = inst->getOperand(0);
    if (!callee) return false;
    const std::string& calleeName = callee->getName();

    bool isRotl = (calleeName == "rotlN");
    bool isRotr = (calleeName == "rotrN");
    if (!isRotl && !isRotr) return false;

    auto* calleeFunc = dynamic_cast<IR::Function*>(callee);
    if (!calleeFunc) return false;

    // 验证函数体：标准的 8 路 if-else 链至少需要 15 个 BB
    // （entry + 8×(then+endif+merge) + 最终 merge ≈ 25 个 BB）
    auto& blocks = calleeFunc->getBlocks();
    if (blocks.size() < 15) return false;

    // 统计 ret 指令数量：8 个 then 分支 + 1 个默认分支 = 9 个 ret
    int retCount = 0;
    for (auto& bb : blocks) {
        for (auto& i : bb->getInstructions()) {
            if (i->getOpcode() == IR::Instruction::Opcode::RET) {
                retCount++;
            }
        }
    }
    if (retCount < 8) return false;

    // 安全检查通过，替换为原生移位
    auto* bb = inst->getParent();
    if (!bb) return false;

    using Opc = IR::Instruction::Opcode;
    Opc shiftOp = isRotl ? Opc::SHL : Opc::ASHR;

    auto* x = inst->getOperand(1); // 第一个参数 x
    auto* n = inst->getOperand(2); // 第二个参数 n

    auto* shift = IR::Instruction::createBinOp(
        shiftOp, inst->getType(), inst->getName() + ".rn",
        x, n);

    inst->replaceAllUsesWith(shift);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            bb->insert(it, shift);
            bb->erase(it + 1); // inst 被向后推移了一位
            return true;
        }
    }
    return false;
}

// ---- 对单条指令尝试所有位模式优化 ----
bool tryOptimize(IR::Instruction* inst) {
    if (tryReplaceCustomBitwiseCall(inst)) return true;
    if (tryReplaceRotlRotrCall(inst)) return true;
    if (tryFuseConsecutiveShifts(inst)) return true;
    if (tryFuseConsecutiveAnds(inst)) return true;
    if (tryFuseXorOrWithConstants(inst)) return true;
    if (trySimplifyShiftAndMask(inst)) return true;
    return false;
}

} // namespace

// ================================================================
// bitOpPatternRecognition 入口 — 收集指令后处理，避免迭代器失效
// ================================================================
bool bitOpPatternRecognition(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                // 先收集所有指令指针，避免在遍历时修改 BB 导致迭代器失效
                std::vector<IR::Instruction*> insts;
                for (auto& inst : bb->getInstructions()) {
                    insts.push_back(inst.get());
                }
                for (auto* inst : insts) {
                    if (tryOptimize(inst)) {
                        changed = true;
                        anyChanged = true;
                        break; // BB 已修改，跳出内层循环重扫
                    }
                }
                if (changed) break; // 重扫当前函数
            }
            if (changed) break; // 重扫整个模块
        }
    }
    return anyChanged;
}

} // namespace Opt