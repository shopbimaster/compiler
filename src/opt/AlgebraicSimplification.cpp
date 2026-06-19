// ================================================================
// O3: 代数化简 + 强度削减（Algebraic Simplification & Strength Reduction）
// 策略：
//   强度削减：SDIV/SREM/MUL 除以/乘以 2 的幂 → ASHR/AND/SHL
//   恒等式消除：x+0→x, x-0→x, x*1→x, x&0→0, x|0→x, x^0→x 等
// ================================================================

#include "opt/Optimizer.h"
#include <cstdint>
#include <vector>

namespace Opt {
namespace {

// ---- 判断 int64_t 是否等于 2^n，若是则返回 n ----
int isPowerOfTwo(int64_t val) {
    if (val <= 0) return -1;
    if ((val & (val - 1)) != 0) return -1;
    int n = 0;
    while (val > 1) { val >>= 1; ++n; }
    return n;
}

// ---- 在 it 位置之前插入 replacement 指令，然后删除 it 位置的旧指令 ----
// ---- 返回指向 replacement 的迭代器 ----
void replaceWithNewInst(
    IR::BasicBlock::iterator it,
    IR::Instruction* oldInst,
    IR::Instruction* replacement) {
    auto* bb = oldInst->getParent();
    // 先插入新指令
    bb->insert(it, replacement);
    // 替换所有 uses
    oldInst->replaceAllUsesWith(replacement);
    // 删除旧指令
    oldInst->dropAllUses();
    // 找到并删除旧指令（当前位置已变化）
    for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
        if (it2->get() == oldInst) {
            bb->erase(it2);
            break;
        }
    }
}

// ---- 尝试对一条指令做代数化简，成功返回 true ----
// ---- 新指令会被插入到 BB 中，旧指令被删除 ----
bool trySimplify(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    auto* bb = inst->getParent();
    if (!bb) return false;

    if (inst->getNumOperands() < 2) return false;

    auto* l = inst->getOperand(0);
    auto* r = inst->getOperand(1);
    if (!l || !r) return false;

    // ================================================================
    // 恒等式消除 — 右操作数为常量 0 或 1
    // ================================================================
    if (auto* rc = dynamic_cast<IR::ConstantInt*>(r)) {
        int64_t rv = rc->getValue();

        struct Identity { Opc opcode; int64_t val; };
        Identity simpleIdentities[] = {
            {Opc::ADD, 0}, {Opc::SUB, 0}, {Opc::MUL, 1},
            {Opc::OR, 0},  {Opc::XOR, 0}, {Opc::SHL, 0},
            {Opc::ASHR, 0},{Opc::SDIV, 1},
        };

        for (auto& id : simpleIdentities) {
            if (op == id.opcode && rv == id.val) {
                inst->replaceAllUsesWith(l);
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
        }

        // 归零恒等式
        struct { Opc opcode; int64_t val; } zeroIds[] = {
            {Opc::MUL, 0}, {Opc::AND, 0},
        };
        for (auto& z : zeroIds) {
            if (op == z.opcode && rv == z.val) {
                inst->replaceAllUsesWith(rc);
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
        }

        // ================================================================
        // 强度削减：幂运算 → 移位/位与
        // 注意：SDIV/SREM 的强度削减仅对非负数正确，因为：
        //   - ASHR 向负无穷舍入，而 C 除法的 SDIV 向零舍入
        //   - AND 得到非负低 N 位，而 C 取余的 SREM 保留被除数符号
        // 因此仅当左操作数为非负常量时才安全应用。
        // MUL 的强度削减对所有整数均正确。
        // ================================================================
        int shift = isPowerOfTwo(rv);
        if (shift >= 0) {
            auto* i32 = dynamic_cast<IR::IntegerType*>(rc->getType());
            if (!i32) return false;

            // 检查左操作数是否可证明为非负（仅常量检查）
            bool lhsNonNeg = false;
            if (auto* lc = dynamic_cast<IR::ConstantInt*>(l)) {
                lhsNonNeg = (lc->getValue() >= 0);
            }

            if (op == Opc::SDIV && lhsNonNeg) {
                // x / 2^n  →  x >> n    (仅 x >= 0 时正确)
                auto* shiftVal = IR::ConstantInt::get(i32, shift);
                auto* repl = IR::Instruction::createBinOp(
                    Opc::ASHR, inst->getType(), inst->getName() + ".sr", l, shiftVal);
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) {
                        replaceWithNewInst(it, inst, repl);
                        return true;
                    }
                }
            }
            if (op == Opc::SREM && lhsNonNeg) {
                // x % 2^n  →  x & (2^n - 1)    (仅 x >= 0 时正确)
                auto* maskVal = IR::ConstantInt::get(i32, rv - 1);
                auto* repl = IR::Instruction::createBinOp(
                    Opc::AND, inst->getType(), inst->getName() + ".sr", l, maskVal);
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) {
                        replaceWithNewInst(it, inst, repl);
                        return true;
                    }
                }
            }
            if (op == Opc::MUL) {
                // x * 2^n  →  x << n    (对所有整数均正确)
                auto* shiftVal = IR::ConstantInt::get(i32, shift);
                auto* repl = IR::Instruction::createBinOp(
                    Opc::SHL, inst->getType(), inst->getName() + ".sr", l, shiftVal);
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) {
                        replaceWithNewInst(it, inst, repl);
                        return true;
                    }
                }
            }
        }
    }

    // ================================================================
    // 恒等式消除 — 左操作数为常量 0 或 1（非交换律场景）
    // ================================================================
    if (auto* lc = dynamic_cast<IR::ConstantInt*>(l)) {
        int64_t lv = lc->getValue();
        bool isRConst = dynamic_cast<IR::ConstantInt*>(r) != nullptr;

        if (!isRConst) {
            if ((op == Opc::ADD || op == Opc::OR || op == Opc::XOR) && lv == 0) {
                inst->replaceAllUsesWith(r);
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
            if (op == Opc::MUL && lv == 1) {
                inst->replaceAllUsesWith(r);
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
            if (op == Opc::MUL && lv == 0) {
                inst->replaceAllUsesWith(lc);
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
            if (op == Opc::AND && lv == 0) {
                inst->replaceAllUsesWith(lc);
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
        }
    }

    return false;
}

} // namespace

// ================================================================
// algebraicSimplification 入口 — 迭代直到收敛
// ================================================================
bool algebraicSimplification(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    // 强度削减会修改 BB 结构，需重新扫描
                    if (trySimplify(it->get())) {
                        changed = true;
                        anyChanged = true;
                        it = bb->begin(); // 从头重扫
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
    return anyChanged;
}

} // namespace Opt