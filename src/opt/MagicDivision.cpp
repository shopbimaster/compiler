// ================================================================
// MagicDivision — 常量除法转乘法+移位
// 将 sdiv %n, C 替换为 (smulh+M)>>s 序列，消除除法指令
// 参考：Hacker's Delight 第10章，Granlund & Montgomery PLDI 1994
// ================================================================

#include "opt/Optimizer.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ================================================================
// canProveNonNegative — 保守的非负推断
// 用于 srem x, 2^k → x & (2^k-1) 的安全条件检查
// 仅当能确信 x >= 0 时返回 true，否则返回 false（保守）
// ================================================================
bool canProveNonNegative(IR::Value* v, int depth = 0) {
    if (depth > 4) return false;  // 防止无限递归

    // 1. 非负常量
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(v)) {
        return ci->getValue() >= 0;
    }

    auto* inst = dynamic_cast<IR::Instruction*>(v);
    if (!inst) return false;

    // 2. AND 正常量（清除符号位）：x & c, c > 0 && c < 2^31
    if (inst->getOpcode() == Opc::AND && inst->getNumOperands() >= 2) {
        auto* c = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
        if (c && c->getValue() > 0 && c->getValue() < 0x80000000LL) {
            return true;
        }
        // x & x 也非负（如果 x 非负）
        c = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
        if (c && c->getValue() > 0 && c->getValue() < 0x80000000LL) {
            return true;
        }
    }

    // 3. ZEXT（零扩展总是非负）
    if (inst->getOpcode() == Opc::ZEXT) {
        return true;
    }

    // 4. SHL 大位移（移出符号位）：x << c, c >= 31 → 结果为 0（非负）
    if (inst->getOpcode() == Opc::SHL && inst->getNumOperands() >= 2) {
        auto* c = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
        if (c && c->getValue() >= 31) {
            return true;
        }
    }

    // 5. PHI：所有 incoming 非负才安全
    if (inst->getOpcode() == Opc::PHI) {
        // 简单检查：至少一个 incoming 是 0，且所有 incoming 都能证明非负
        // 为了避免复杂分析，仅检查所有 incoming 都是 ConstantInt >= 0 的情况
        // 这覆盖了 "常量 PHI" 但不覆盖循环计数器（需递归）
        // 循环计数器（PHI [0, preheader] [iv+1, latch]）需要更复杂的分析
        return false;  // 保守：不处理 PHI
    }

    return false;
}

// ---- 有符号魔法数计算结果 ----
struct SignedMagic {
    int32_t magic;   // 32-bit magic number
    int     shift;   // shift amount (>= 0)
};

// 计算有符号除法的魔法数（Hacker's Delight 算法）
// 返回 magic number 和 shift amount
// 始终针对 |d| 计算，magic 不根据 d 符号取反
// 对于 d = 0, 1, -1, INT32_MIN 返回 {0, -1} 表示无效
SignedMagic computeSignedMagic(int32_t d) {
    if (d == 0 || d == 1 || d == -1) return {0, -1};
    // INT32_MIN: abs would overflow
    if (d == INT32_MIN) return {0, -1};

    const unsigned N = 32;
    uint32_t ad = (uint32_t)(d >= 0 ? d : -d);
    // 始终针对正除数 |d|，用 t = 0x80000000
    // （原代码 (uint32_t)(d >> 31) 对负 d 给出 0xFFFFFFFF 是 BUG）
    uint32_t t = 0x80000000u;
    uint32_t anc = t - 1 - t % ad;
    unsigned p = N - 1;
    uint32_t q1 = 0x80000000u / anc;
    uint32_t r1 = 0x80000000u - q1 * anc;
    uint32_t q2 = 0x80000000u / ad;
    uint32_t r2 = 0x80000000u - q2 * ad;

    uint32_t delta;
    do {
        p++;
        q1 <<= 1;
        r1 <<= 1;
        if (r1 >= anc) {
            q1++;
            r1 -= anc;
        }
        q2 <<= 1;
        r2 <<= 1;
        if (r2 >= ad) {
            q2++;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    SignedMagic m;
    m.magic = (int32_t)(q2 + 1);
    // 不取反 magic：始终针对 |d|，负除数在 buildQuotientSequence 中取反商
    m.shift = (int)(p - N);
    return m;
}

// 在指令之前插入新指令
void insertBefore(IR::Instruction* beforeInst, IR::Instruction* newInst) {
    auto* bb = beforeInst->getParent();
    if (!bb) return;
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == beforeInst) {
            bb->insert(it, newInst);
            return;
        }
    }
}

// ================================================================
// 构建常量除法商计算序列
// 对于 n / d（d 为常量），生成等价的指令序列计算商 q。
// 返回 q 的 Instruction*，所有新指令被加入 toInsert（{before, newInst} 对）。
// 若 d 无效（0, 1, -1, INT32_MIN），返回 nullptr。
// ================================================================
IR::Instruction* buildQuotientSequence(
    IR::Value* n, int32_t d, IR::IntegerType* i32,
    IR::Instruction* beforeInst,
    std::vector<std::pair<IR::Instruction*, IR::Instruction*>>& toInsert) {
    // 2 的幂次方特殊处理：使用移位+符号修正，避免 magic number 算法溢出 bug
    // 对于 d = 2^k (k > 0):
    //   q = (n + (n >> 31 & (d-1))) >> k
    // 对于 d = -2^k (k > 0):
    //   q = -((n + (n >> 31 & (d-1))) >> k)
    {
        uint32_t absD = (d >= 0) ? (uint32_t)d : (uint32_t)(-d);
        bool isPow2 = (absD != 0) && ((absD & (absD - 1)) == 0);
        if (isPow2 && absD > 1) {
            unsigned k = 0;
            uint32_t tmp = absD;
            while (tmp > 1) { tmp >>= 1; k++; }

            auto* sign = IR::Instruction::createBinOp(
                Opc::ASHR, i32, "div.pow2.sign", n,
                IR::ConstantInt::get(i32, 31));
            auto* corr = IR::Instruction::createBinOp(
                Opc::AND, i32, "div.pow2.corr", sign,
                IR::ConstantInt::get(i32, (int32_t)(absD - 1)));
            auto* sum = IR::Instruction::createBinOp(
                Opc::ADD, i32, "div.pow2.sum", n, corr);
            auto* q = IR::Instruction::createBinOp(
                Opc::ASHR, i32, "div.pow2.q", sum,
                IR::ConstantInt::get(i32, (int32_t)k));

            IR::Instruction* result = q;
            if (d < 0) {
                result = IR::Instruction::createBinOp(
                    Opc::SUB, i32, "div.pow2.neg",
                    IR::ConstantInt::get(i32, 0), q);
            }

            toInsert.push_back({beforeInst, sign});
            toInsert.push_back({beforeInst, corr});
            toInsert.push_back({beforeInst, sum});
            toInsert.push_back({beforeInst, q});
            if (d < 0) {
                toInsert.push_back({beforeInst, result});
            }
            return result;
        }
    }

    // 非 2 的幂次方：使用 magic number 算法
    // computeSignedMagic 始终针对 |d| 计算，magic 不取反
    auto magic = computeSignedMagic(d);
    if (magic.shift < 0) return nullptr; // 无效除数

    auto* magicConst = IR::ConstantInt::get(i32, magic.magic);
    auto* hi = IR::Instruction::createBinOp(Opc::SMULH, i32, "magic.hi", n, magicConst);
    toInsert.push_back({beforeInst, hi});

    // M < 0 时（如 d=7 的 M=-1840700269）：q = (smulh(n, M) + n) >> s
    // M > 0 时：q = smulh(n, M) >> s
    IR::Instruction* preShift;
    if (magic.magic < 0) {
        auto* add = IR::Instruction::createBinOp(Opc::ADD, i32, "magic.add", hi, n);
        toInsert.push_back({beforeInst, add});
        preShift = add;
    } else {
        preShift = hi;
    }

    IR::Instruction* shifted;
    if (magic.shift > 0) {
        auto* shiftConst = IR::ConstantInt::get(i32, magic.shift);
        shifted = IR::Instruction::createBinOp(Opc::ASHR, i32, "magic.div", preShift, shiftConst);
        toInsert.push_back({beforeInst, shifted});
    } else {
        shifted = preShift;
    }

    // 符号修正：q = q - (n >> 31)
    // smulh + ashr 给出 floor(n/|d|)，C 除法是 trunc(n/d)
    // n < 0 且非整除时 floor = trunc - 1，需 +1 修正
    // n >> 31 = -1 (n<0) 或 0 (n>=0)，q - (n>>31) 实现修正
    auto* sign = IR::Instruction::createBinOp(
        Opc::ASHR, i32, "magic.sign", n,
        IR::ConstantInt::get(i32, 31));
    auto* corrected = IR::Instruction::createBinOp(
        Opc::SUB, i32, "magic.cor", shifted, sign);
    toInsert.push_back({beforeInst, sign});
    toInsert.push_back({beforeInst, corrected});

    // 负除数取反：n / d = -(n / |d|)
    IR::Instruction* result = corrected;
    if (d < 0) {
        result = IR::Instruction::createBinOp(
            Opc::SUB, i32, "magic.neg",
            IR::ConstantInt::get(i32, 0), corrected);
        toInsert.push_back({beforeInst, result});
    }

    return result;
}

bool magicDivisionOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    bool changed = false;

    std::vector<IR::Instruction*> toErase;
    std::vector<std::pair<IR::Instruction*, IR::Instruction*>> toInsert; // {before, new}

    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            // 同时处理 SDIV 和 SREM
            if (op != Opc::SDIV && op != Opc::SREM) continue;
            if (inst->getNumOperands() < 2) continue;

            auto* rhs = inst->getOperand(1);
            auto* ci = dynamic_cast<IR::ConstantInt*>(rhs);
            if (!ci) continue;

            int32_t d = (int32_t)ci->getValue();
            auto* n = inst->getOperand(0);
            auto* i32 = IR::IntegerType::I32;

            // 构建商计算序列 q = n / d
            // 对于 SDIV：直接用 q 替换
            // 对于 SREM：r = n - q * d
            IR::Instruction* q = buildQuotientSequence(n, d, i32, inst.get(), toInsert);
            if (!q) continue; // 无效除数（0, 1, -1, INT32_MIN 由 AlgebraicSimplification 处理）

            if (op == Opc::SDIV) {
                inst->replaceAllUsesWith(q);
                toErase.push_back(inst.get());
                changed = true;
            } else {
                // SREM: r = n - q * d
                // ★ 快速路径：srem x, 2^k 当 x 可证明非负时 → x & (2^k - 1)
                //   1 条 AND 指令替代 6 条 div.pow2 序列
                //   安全条件：x >= 0（C 取余符号与被除数相同，非负数取余结果非负）
                {
                    uint32_t absD = (d >= 0) ? (uint32_t)d : (uint32_t)(-d);
                    bool isPow2 = (absD != 0) && ((absD & (absD - 1)) == 0);
                    if (isPow2 && absD > 1 && canProveNonNegative(n)) {
                        auto* maskConst = IR::ConstantInt::get(i32, (int32_t)(absD - 1));
                        auto* andInst = IR::Instruction::createBinOp(
                            Opc::AND, i32, "magic.rem.and", n, maskConst);
                        toInsert.push_back({inst.get(), andInst});

                        // 负除数取反：n % -2^k = -(n % 2^k)
                        IR::Instruction* result = andInst;
                        if (d < 0) {
                            result = IR::Instruction::createBinOp(
                                Opc::SUB, i32, "magic.rem.neg",
                                IR::ConstantInt::get(i32, 0), andInst);
                            toInsert.push_back({inst.get(), result});
                        }

                        inst->replaceAllUsesWith(result);
                        toErase.push_back(inst.get());
                        changed = true;
                        continue;  // 跳过下面的通用 SREM 处理
                    }
                }

                // 通用 SREM: r = n - q * d
                // 生成 mul q, d 和 sub n, mul
                auto* mulConst = IR::ConstantInt::get(i32, d);
                auto* mul = IR::Instruction::createBinOp(Opc::MUL, i32, "magic.rem.mul", q, mulConst);
                auto* rem = IR::Instruction::createBinOp(Opc::SUB, i32, "magic.rem", n, mul);
                toInsert.push_back({inst.get(), mul});
                toInsert.push_back({inst.get(), rem});

                inst->replaceAllUsesWith(rem);
                toErase.push_back(inst.get());
                changed = true;
            }
        }
    }

    // 批量插入新指令
    for (auto& [before, newInst] : toInsert) {
        insertBefore(before, newInst);
    }

    // 批量删除旧指令
    for (auto* inst : toErase) {
        inst->dropAllUses();
        auto* parent = inst->getParent();
        if (parent) {
            for (auto it = parent->begin(); it != parent->end(); ++it) {
                if (it->get() == inst) {
                    parent->erase(it);
                    break;
                }
            }
        }
    }

    return changed;
}

} // namespace

bool magicDivision(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (magicDivisionOnFunction(func.get()))
            changed = true;
    }
    return changed;
}

} // namespace Opt