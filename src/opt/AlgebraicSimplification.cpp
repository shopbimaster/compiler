// ================================================================
// O3: 代数化简 + 强度削减（Algebraic Simplification & Strength Reduction）
// 策略：
//   强度削减：SDIV/SREM/MUL 除以/乘以 2 的幂 → ASHR/AND/SHL
//   恒等式消除：x+0→x, x-0→x, x*1→x, x&0→0, x|0→x, x^0→x 等
// ================================================================

#include "opt/Optimizer.h"
#include <climits>
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

    // ================================================================
    // SELECT → 布尔 OR/AND 化简
    //   select(cond, 1, x) → or(cond, x)    （对应 a || b）
    //   select(cond, x, 0) → and(cond, x)   （对应 a && b）
    // 前提：cond 与非常量分支 x 均为 i1（值域 {0,1}），此时按位 or/and 与
    //   select 逐位等价。IfConversion 把 `||`/`&&` 降级成的 select 正是此形，
    //   否则后端把 select 展开成 seqz/neg/and/or 多条指令；递归基例中的
    //   短路条件会频繁执行。化简为单条 or/and。
    // 安全性：纯函数式；要求 cond 和 x 都是 i1，结果类型沿用 select 的 i1。
    // ================================================================
    if (op == Opc::SELECT && inst->getNumOperands() >= 3) {
        auto* cond = inst->getOperand(0);
        auto* tv = inst->getOperand(1);
        auto* fv = inst->getOperand(2);
        auto isI1 = [](IR::Value* v) {
            return v && v->getType() == IR::IntegerType::I1;
        };
        auto isConstVal = [](IR::Value* v, int64_t k) {
            auto* c = dynamic_cast<IR::ConstantInt*>(v);
            return c && c->getValue() == k;
        };
        // 结果类型沿用 select 的类型（常见为 i32，因结果会存入 int 变量）：
        // cond/x 为 i1（值 0/1），or/and 逐位运算结果仍是 0/1，存入更宽整数
        // 位模式一致，故安全。只需 cond 与非常量分支 x 都是 i1。
        if (isI1(cond)) {
            IR::Instruction* repl = nullptr;

            if (isConstVal(tv, 1) && isI1(fv)) {
                repl = IR::Instruction::createBinOp(
                    Opc::OR, inst->getType(), inst->getName() + ".sor", cond, fv);
            } else if (isConstVal(fv, 0) && isI1(tv)) {
                repl = IR::Instruction::createBinOp(
                    Opc::AND, inst->getType(), inst->getName() + ".sand", cond, tv);
            }
            if (repl) {
                for (auto it = bb->begin(); it != bb->end(); ++it) {
                    if (it->get() == inst) {
                        replaceWithNewInst(it, inst, repl);
                        return true;
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
