// ================================================================
// O3: 代数化简 + 强度削减（Algebraic Simplification & Strength Reduction）
// 策略：
//   强度削减：SDIV/SREM/MUL 除以/乘以 2 的幂 → ASHR/AND/SHL
//   恒等式消除：x+0→x, x-0→x, x*1→x, x&0→0, x|0→x, x^0→x 等
// ================================================================

#include "opt/Optimizer.h"
#include <climits>
#include <cstdint>
#include <set>
#include <vector>

namespace Opt {
namespace {

// ================================================================
// 非负值分析：计算可证明为非负的 Value 集合
// 用于 SDIV/SREM 强度削减（x/2^n → x>>n, x%2^n → x&(2^n-1)）
//
// 传播规则（基于 C 有符号整数溢出是 UB 的假设）：
//   - ConstantInt >= 0 → 非负
//   - ICMP 结果（0 或 1）→ 非负
//   - ADD x,y → 若 x,y 均非负 → 非负（溢出为 UB）
//   - MUL x,y → 若 x,y 均非负 → 非负（溢出为 UB）
//   - SUB x,c → 若 x 非负且 c <= 0 → 非负（x - 负数 = x + 正数）
//   - SDIV x,c → 若 x 非负且 c > 0 → 非负
//   - SREM x,c → 若 x 非负且 c > 0 → 非负
//   - ASHR x,c → 若 x 非负 → 非负
//   - SHL x,c → 若 x 非负 → 非负（溢出为 UB）
//   - AND/OR/XOR x,y → 若 x,y 均非负 → 非负
//   - SELECT → 若两个分支均非负 → 非负
//   - PHI → 若所有 incoming value 均非负 → 非负（归纳证明）
//   - ALLOCA/LOAD → 若 alloca 的所有 STORE 均为非负值 → LOAD 非负
// ================================================================
using NonNegSet = std::set<IR::Value*>;

// 非负 alloca 集合：所有 STORE 到此 alloca 的值均为非负
// 从非负 alloca LOAD 的值也是非负
// 使用归纳证明：初始值非负 + 变换保持非负 → 所有 LOAD 非负
std::set<IR::Instruction*> g_nonNegAllocas;

bool isNonNegativeValue(IR::Value* v, const NonNegSet& nonNeg);

// 检查指令 inst 的结果是否非负（给定当前 nonNeg 集合）
bool computeInstNonNeg(IR::Instruction* inst, const NonNegSet& nonNeg) {
    using Opc = IR::Instruction::Opcode;
    auto op = inst->getOpcode();

    // ICMP 结果总是 0 或 1 → 非负
    if (op == Opc::ICMP) return true;

    // LOAD from non-negative alloca → 非负
    if (op == Opc::LOAD && inst->getNumOperands() >= 1) {
        auto* ptr = inst->getOperand(0);
        if (auto* ptrInst = dynamic_cast<IR::Instruction*>(ptr)) {
            if (g_nonNegAllocas.count(ptrInst)) return true;
        }
    }

    if (inst->getNumOperands() < 2) {
        // SELECT 有 3 个操作数
        if (op == Opc::SELECT && inst->getNumOperands() >= 3) {
            return isNonNegativeValue(inst->getOperand(1), nonNeg) &&
                   isNonNegativeValue(inst->getOperand(2), nonNeg);
        }
        return false;
    }

    auto* l = inst->getOperand(0);
    auto* r = inst->getOperand(1);

    switch (op) {
    case Opc::ADD:
        return isNonNegativeValue(l, nonNeg) && isNonNegativeValue(r, nonNeg);
    case Opc::MUL:
        return isNonNegativeValue(l, nonNeg) && isNonNegativeValue(r, nonNeg);
    case Opc::SUB: {
        bool lNN = isNonNegativeValue(l, nonNeg);
        if (!lNN) return false;
        if (auto* rc = dynamic_cast<IR::ConstantInt*>(r)) {
            return rc->getValue() <= 0;
        }
        return false;
    }
    case Opc::SDIV: {
        bool lNN = isNonNegativeValue(l, nonNeg);
        if (!lNN) return false;
        if (auto* rc = dynamic_cast<IR::ConstantInt*>(r)) {
            return rc->getValue() > 0;
        }
        return isNonNegativeValue(r, nonNeg);
    }
    case Opc::SREM: {
        bool lNN = isNonNegativeValue(l, nonNeg);
        if (!lNN) return false;
        if (auto* rc = dynamic_cast<IR::ConstantInt*>(r)) {
            return rc->getValue() > 0;
        }
        return false;
    }
    case Opc::ASHR:
        return isNonNegativeValue(l, nonNeg);
    case Opc::SHL:
        return isNonNegativeValue(l, nonNeg);
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
        return isNonNegativeValue(l, nonNeg) && isNonNegativeValue(r, nonNeg);
    default:
        return false;
    }
}

bool isNonNegativeValue(IR::Value* v, const NonNegSet& nonNeg) {
    if (!v) return false;
    if (nonNeg.count(v)) return true;
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(v)) {
        return ci->getValue() >= 0;
    }
    // 检查是否是从非负 alloca 的 LOAD
    if (auto* inst = dynamic_cast<IR::Instruction*>(v)) {
        if (inst->getOpcode() == IR::Instruction::Opcode::LOAD && inst->getNumOperands() >= 1) {
            auto* ptr = inst->getOperand(0);
            if (auto* ptrInst = dynamic_cast<IR::Instruction*>(ptr)) {
                if (g_nonNegAllocas.count(ptrInst)) return true;
            }
        }
    }
    return false;
}

// 检查 alloca 是否为非负：所有 STORE 到此 alloca 的值均为非负
// 使用假设归纳：假设此 alloca 非负 → LOAD 非负 → 变换结果非负 → STORE 非负
bool isAllocaNonNeg(IR::Instruction* alloca, const NonNegSet& nonNeg) {
    using Opc = IR::Instruction::Opcode;
    // 临时假设此 alloca 非负
    g_nonNegAllocas.insert(alloca);

    auto* func = alloca->getParent() ? alloca->getParent()->getParent() : nullptr;
    if (!func) {
        g_nonNegAllocas.erase(alloca);
        return false;
    }

    bool allStoresNonNeg = true;
    int storeCount = 0;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == Opc::STORE && inst->getNumOperands() >= 2) {
                // STORE val, ptr
                if (inst->getOperand(1) == alloca) {
                    ++storeCount;
                    if (!isNonNegativeValue(inst->getOperand(0), nonNeg)) {
                        allStoresNonNeg = false;
                        break;
                    }
                }
            }
        }
        if (!allStoresNonNeg) break;
    }

    if (!allStoresNonNeg || storeCount == 0) {
        g_nonNegAllocas.erase(alloca);
        return false;
    }
    // 保留 alloca 在 g_nonNegAllocas 中（假设成立）
    return true;
}

// 计算模块中所有可证明为非负的 Value（固定点迭代）
NonNegSet computeNonNegative(IR::Module* mod) {
    NonNegSet nonNeg;
    g_nonNegAllocas.clear();
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto& inst : bb->getInstructions()) {
                    if (nonNeg.count(inst.get())) continue;
                    using Opc = IR::Instruction::Opcode;
                    auto op = inst->getOpcode();

                    if (op == Opc::PHI) {
                        bool allNN = true;
                        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                            if (!isNonNegativeValue(inst->getOperand(i), nonNeg)) {
                                allNN = false;
                                break;
                            }
                        }
                        if (allNN && inst->getNumOperands() >= 2) {
                            nonNeg.insert(inst.get());
                            changed = true;
                        }
                    } else if (op == Opc::ALLOCA) {
                        // 尝试证明此 alloca 非负
                        // ★ 已证明非负的 alloca 在 g_nonNegAllocas 中，跳过避免重复处理
                        if (g_nonNegAllocas.count(inst.get())) continue;
                        if (isAllocaNonNeg(inst.get(), nonNeg)) {
                            changed = true;
                        }
                    } else if (computeInstNonNeg(inst.get(), nonNeg)) {
                        nonNeg.insert(inst.get());
                        changed = true;
                    }
                }
            }
        }
    }
    return nonNeg;
}

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
bool trySimplify(IR::Instruction* inst, const NonNegSet& nonNeg) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    auto* bb = inst->getParent();
    if (!bb) return false;

    if (inst->getNumOperands() < 2) return false;

    auto* l = inst->getOperand(0);
    auto* r = inst->getOperand(1);
    if (!l || !r) return false;

    // ================================================================
    // 恒等式消除 — 左右操作数相同
    // x - x → 0, x & x → x, x | x → x
    // ================================================================
    if (l == r) {
        struct { Opc opcode; } sameOpIds[] = {
            {Opc::SUB}, {Opc::AND}, {Opc::OR}, {Opc::XOR},
        };
        for (auto& s : sameOpIds) {
            if (op == s.opcode) {
                if (op == Opc::SUB || op == Opc::XOR) {
                    auto* zero = IR::ConstantInt::get(dynamic_cast<IR::IntegerType*>(inst->getType()), 0);
                    inst->replaceAllUsesWith(zero);
                } else {
                    inst->replaceAllUsesWith(l);
                }
                inst->dropAllUses();
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) { bb->erase(it); break; }
                }
                return true;
            }
        }
        // x + x → x << 1
        if (op == Opc::ADD) {
            auto* i32 = dynamic_cast<IR::IntegerType*>(l->getType());
            if (i32) {
                auto* one = IR::ConstantInt::get(i32, 1);
                auto* repl = IR::Instruction::createBinOp(
                    Opc::SHL, inst->getType(), inst->getName() + ".sr", l, one);
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
    // 互反恒等式消除：x + (-x) → 0
    // 其中 -x = SUB(0, x)。GVN 合并等价的 _and(a,b) 后，内联的 _or(a,b) 展开
    // 为 _xor(_xor(a,b), _and(a,b))，其中 _xor(x,y) = -x - y = SUB(0, ADD(x,y))，
    // 最终产生 ADD(t, SUB(0, t)) 模式。此化简将 _or 结果折叠为常量 0。
    // ================================================================
    if (op == Opc::ADD) {
        auto isNegOf = [](IR::Value* maybeNeg, IR::Value* x) -> bool {
            auto* negInst = dynamic_cast<IR::Instruction*>(maybeNeg);
            if (!negInst || negInst->getOpcode() != Opc::SUB) return false;
            if (negInst->getNumOperands() < 2) return false;
            auto* subL = negInst->getOperand(0);
            auto* subR = negInst->getOperand(1);
            auto* ci = dynamic_cast<IR::ConstantInt*>(subL);
            return ci && ci->getValue() == 0 && subR == x;
        };
        if (isNegOf(r, l) || isNegOf(l, r)) {
            auto* zero = IR::ConstantInt::get(
                dynamic_cast<IR::IntegerType*>(inst->getType()), 0);
            inst->replaceAllUsesWith(zero);
            inst->dropAllUses();
            for (auto it = bb->begin(); it != bb->end(); ++it) {
                if (it->get() == inst) { bb->erase(it); break; }
            }
            return true;
        }
    }

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

        // SREM x, 1 → 0  (x % 1 恒为 0)
        if (op == Opc::SREM && rv == 1) {
            auto* zero = IR::ConstantInt::get(
                dynamic_cast<IR::IntegerType*>(inst->getType()), 0);
            inst->replaceAllUsesWith(zero);
            inst->dropAllUses();
            for (auto it = bb->begin(); it != bb->end(); ++it) {
                if (it->get() == inst) { bb->erase(it); break; }
            }
            return true;
        }

        // ================================================================
        // 特殊除法/取模恒等式：除以 -1
        //   x / -1  →  -x    (即 SUB 0, x；INT_MIN/-1 是 UB，可忽略)
        //   x % -1  →  0     (对所有非 INT_MIN 值成立)
        // 这些情况下 MagicDivision 会跳过（返回无效），AlgebraicSimplification 必须处理
        // ================================================================
        if (rv == -1) {
            auto* i32 = dynamic_cast<IR::IntegerType*>(rc->getType());
            if (i32) {
                if (op == Opc::SDIV) {
                    // x / -1 → 0 - x
                    auto* zero = IR::ConstantInt::get(i32, 0);
                    auto* repl = IR::Instruction::createBinOp(
                        Opc::SUB, inst->getType(), inst->getName() + ".neg", zero, l);
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) {
                            replaceWithNewInst(it, inst, repl);
                            return true;
                        }
                    }
                }
                if (op == Opc::SREM) {
                    // x % -1 → 0
                    auto* zero = IR::ConstantInt::get(i32, 0);
                    inst->replaceAllUsesWith(zero);
                    inst->dropAllUses();
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) { bb->erase(it); break; }
                    }
                    return true;
                }
            }
        }

        // ================================================================
        // 除以 INT32_MIN 优化：x / INT32_MIN → 0 (除 x=INT32_MIN 外，那是 UB)
        // x % INT32_MIN → x (对 |x| < |INT32_MIN| 的所有 x 成立)
        // ================================================================
        if (rv == INT32_MIN) {
            auto* i32 = dynamic_cast<IR::IntegerType*>(rc->getType());
            if (i32) {
                if (op == Opc::SDIV) {
                    // x / INT32_MIN → 0
                    auto* zero = IR::ConstantInt::get(i32, 0);
                    inst->replaceAllUsesWith(zero);
                    inst->dropAllUses();
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) { bb->erase(it); break; }
                    }
                    return true;
                }
                if (op == Opc::SREM) {
                    // x % INT32_MIN → x
                    inst->replaceAllUsesWith(l);
                    inst->dropAllUses();
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) { bb->erase(it); break; }
                    }
                    return true;
                }
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

            // 检查左操作数是否可证明为非负
            // 使用非负值分析（含 PHI 归纳、ADD/MUL/SDIV 传播）
            bool lhsNonNeg = isNonNegativeValue(l, nonNeg);

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

        // ================================================================
        // 强度削弱：x * (2^n ± 1) → shift + add/sub
        //   ★ 已禁用：在 BOOM 乱序超标量核心上，mul 是单周期全流水线指令，
        //     shift+add 是 2 条有数据依赖的指令，反而更慢且增加寄存器压力。
        //     该优化仅对无硬件 mul 的简单微控制器有益。
        //   保留代码以备未来在无 mul 目标上启用。
        // ================================================================
        if (false && op == Opc::MUL) {
            auto* i32 = dynamic_cast<IR::IntegerType*>(rc->getType());
            if (i32) {
                int shiftAmt = -1;
                bool isPlusOne = false;
                for (int n = 1; n <= 4; ++n) {
                    int64_t plusVal = (1LL << n) + 1;
                    int64_t minusVal = (1LL << n) - 1;
                    if (rv == plusVal) { shiftAmt = n; isPlusOne = true; break; }
                    if (n >= 2 && rv == minusVal) { shiftAmt = n; isPlusOne = false; break; }
                }
                if (shiftAmt >= 0) {
                    auto* shiftConst = IR::ConstantInt::get(i32, shiftAmt);
                    auto* shlInst = IR::Instruction::createBinOp(
                        Opc::SHL, inst->getType(),
                        inst->getName() + ".shl", l, shiftConst);
                    auto* addSubInst = IR::Instruction::createBinOp(
                        isPlusOne ? Opc::ADD : Opc::SUB,
                        inst->getType(),
                        inst->getName() + ".sr", shlInst, l);
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) {
                            bb->insert(it, shlInst);
                            replaceWithNewInst(it, inst, addSubInst);
                            return true;
                        }
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

            // 左常量 MUL 强度削弱：C * x → (x << n) ± x
            //   ★ 已禁用：同右常量版本，BOOM 核心上 mul 更快
            if (false && op == Opc::MUL) {
                auto* i32 = dynamic_cast<IR::IntegerType*>(lc->getType());
                if (i32) {
                    int shiftAmt = -1;
                    bool isPlusOne = false;
                    for (int n = 1; n <= 4; ++n) {
                        int64_t plusVal = (1LL << n) + 1;
                        int64_t minusVal = (1LL << n) - 1;
                        if (lv == plusVal) {
                            shiftAmt = n;
                            isPlusOne = true;
                            break;
                        }
                        if (n >= 2 && lv == minusVal) {
                            shiftAmt = n;
                            isPlusOne = false;
                            break;
                        }
                    }
                    if (shiftAmt >= 0) {
                        auto* shiftConst = IR::ConstantInt::get(i32, shiftAmt);
                        auto* shlInst = IR::Instruction::createBinOp(
                            Opc::SHL, inst->getType(),
                            inst->getName() + ".shl", r, shiftConst);
                        auto* addSubInst = IR::Instruction::createBinOp(
                            isPlusOne ? Opc::ADD : Opc::SUB,
                            inst->getType(),
                            inst->getName() + ".sr", shlInst, r);
                        for (auto it = bb->begin(); it != bb->end(); ++it) {
                            if (it->get() == inst) {
                                bb->insert(it, shlInst);
                                replaceWithNewInst(it, inst, addSubInst);
                                return true;
                            }
                        }
                    }
                }
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

    // ================================================================
    // 借鉴 Cpl1 InstSimplify：x + (y - x) = y  和  (y - x) + x = y
    // ================================================================
    if (op == Opc::ADD) {
        // 检查 r = (y - x) 且 l == x
        if (auto* rInst = dynamic_cast<IR::Instruction*>(r)) {
            if (rInst->getOpcode() == Opc::SUB && rInst->getNumOperands() >= 2) {
                if (l == rInst->getOperand(1)) {
                    // x + (y - x) = y
                    inst->replaceAllUsesWith(rInst->getOperand(0));
                    inst->dropAllUses();
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) { bb->erase(it); break; }
                    }
                    return true;
                }
            }
        }
        // 检查 l = (y - x) 且 r == x
        if (auto* lInst = dynamic_cast<IR::Instruction*>(l)) {
            if (lInst->getOpcode() == Opc::SUB && lInst->getNumOperands() >= 2) {
                if (r == lInst->getOperand(1)) {
                    // (y - x) + x = y
                    inst->replaceAllUsesWith(lInst->getOperand(0));
                    inst->dropAllUses();
                    for (auto it = bb->begin(); it != bb->end(); ++it) {
                        if (it->get() == inst) { bb->erase(it); break; }
                    }
                    return true;
                }
            }
        }
    }

    // ================================================================
    // 借鉴 Cpl1 InstSimplify：x - (x + y) = -y  →  SUB 0, y
    // ================================================================
    if (op == Opc::SUB) {
        if (auto* rInst = dynamic_cast<IR::Instruction*>(r)) {
            if (rInst->getOpcode() == Opc::ADD && rInst->getNumOperands() >= 2) {
                if (l == rInst->getOperand(0)) {
                    // x - (x + y) = -y  →  SUB 0, y
                    auto* i32 = dynamic_cast<IR::IntegerType*>(inst->getType());
                    if (i32) {
                        auto* zero = IR::ConstantInt::get(i32, 0);
                        auto* repl = IR::Instruction::createBinOp(
                            Opc::SUB, inst->getType(), inst->getName() + ".is", zero, rInst->getOperand(1));
                        for (auto it = bb->begin(); it != bb->end(); ++it) {
                            if (it->get() == inst) {
                                replaceWithNewInst(it, inst, repl);
                                return true;
                            }
                        }
                    }
                }
                if (l == rInst->getOperand(1)) {
                    // x - (y + x) = -y  →  SUB 0, y
                    auto* i32 = dynamic_cast<IR::IntegerType*>(inst->getType());
                    if (i32) {
                        auto* zero = IR::ConstantInt::get(i32, 0);
                        auto* repl = IR::Instruction::createBinOp(
                            Opc::SUB, inst->getType(), inst->getName() + ".is", zero, rInst->getOperand(0));
                        for (auto it = bb->begin(); it != bb->end(); ++it) {
                            if (it->get() == inst) {
                                replaceWithNewInst(it, inst, repl);
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    // ================================================================
    // 借鉴 Cpl1 InstSimplify：x - (0 - y) = x + y
    // ================================================================
    if (op == Opc::SUB) {
        if (auto* rInst = dynamic_cast<IR::Instruction*>(r)) {
            if (rInst->getOpcode() == Opc::SUB && rInst->getNumOperands() >= 2) {
                if (auto* zeroC = dynamic_cast<IR::ConstantInt*>(rInst->getOperand(0))) {
                    if (zeroC->getValue() == 0) {
                        // x - (0 - y) = x + y
                        auto* repl = IR::Instruction::createBinOp(
                            Opc::ADD, inst->getType(), inst->getName() + ".is", l, rInst->getOperand(1));
                        for (auto it = bb->begin(); it != bb->end(); ++it) {
                            if (it->get() == inst) {
                                replaceWithNewInst(it, inst, repl);
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    // ================================================================
    // 借鉴 Cpl1 InstSimplify：(x + c1) + c2 = x + (c1 + c2)
    // 常量重结合，将嵌套的加法常量合并，有助于后续 CSE
    // ================================================================
    if (op == Opc::ADD) {
        if (auto* rc = dynamic_cast<IR::ConstantInt*>(r)) {
            if (auto* lInst = dynamic_cast<IR::Instruction*>(l)) {
                if (lInst->getOpcode() == Opc::ADD && lInst->getNumOperands() >= 2) {
                    if (auto* lrc = dynamic_cast<IR::ConstantInt*>(lInst->getOperand(1))) {
                        // (x + c1) + c2 = x + (c1 + c2)
                        int64_t sum = lrc->getValue() + rc->getValue();
                        auto* i32 = dynamic_cast<IR::IntegerType*>(rc->getType());
                        if (i32) {
                            auto* sumC = IR::ConstantInt::get(i32, sum);
                            auto* repl = IR::Instruction::createBinOp(
                                Opc::ADD, inst->getType(), inst->getName() + ".is",
                                lInst->getOperand(0), sumC);
                            for (auto it = bb->begin(); it != bb->end(); ++it) {
                                if (it->get() == inst) {
                                    replaceWithNewInst(it, inst, repl);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ================================================================
    // 借鉴 Cpl1 GEP Flatten：gep(gep(x, c1), c2) = gep(x, c1 + c2)
    // 将嵌套的 GEP 展开为单层 GEP，合并常量偏移
    // ================================================================
    if (op == Opc::GETELEMENTPTR && inst->getNumOperands() >= 2) {
        if (auto* innerGEP = dynamic_cast<IR::Instruction*>(inst->getOperand(0))) {
            if (innerGEP->getOpcode() == Opc::GETELEMENTPTR && innerGEP->getNumOperands() >= 2) {
                // 检查：当前 GEP 的第二个操作数（第一个索引）和内部 GEP 的第二个操作数是否都是常量
                auto* outerIdx = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
                auto* innerIdx = dynamic_cast<IR::ConstantInt*>(innerGEP->getOperand(1));
                if (outerIdx && innerIdx) {
                    // 仅处理单层索引的情况（最简形式）
                    if (inst->getNumOperands() == 2 && innerGEP->getNumOperands() == 2) {
                        int64_t sum = innerIdx->getValue() + outerIdx->getValue();
                        auto* i32 = dynamic_cast<IR::IntegerType*>(outerIdx->getType());
                        if (i32) {
                            auto* sumC = IR::ConstantInt::get(i32, sum);
                            // 获取内部 GEP 的基址指针类型
                            auto* ptrType = dynamic_cast<IR::PointerType*>(innerGEP->getOperand(0)->getType());
                            IR::Type* pointee = ptrType ? ptrType->getPointeeType() : inst->getType();
                            std::vector<IR::Value*> indices = {sumC};
                            auto* repl = IR::Instruction::createGetElementPtr(
                                pointee, innerGEP->getOperand(0), indices, inst->getName() + ".is");
                            for (auto it = bb->begin(); it != bb->end(); ++it) {
                                if (it->get() == inst) {
                                    replaceWithNewInst(it, inst, repl);
                                    return true;
                                }
                            }
                        }
                    }
                }
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
        // ★ 每轮重算非负值集合：trySimplify 会删除/插入指令，若仅算一次，
        //   nonNeg 中的 IR::Value* 可能指向已释放指令；一旦该地址被新指令
        //   复用，nonNeg.count() 会误判 → SDIV 强度削减产生错码。
        //   每轮重算保证 nonNeg 只含当前存活指令，O(N) 开销可接受。
        NonNegSet nonNeg = computeNonNegative(mod);
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    // 强度削减会修改 BB 结构，需重新扫描
                    if (trySimplify(it->get(), nonNeg)) {
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