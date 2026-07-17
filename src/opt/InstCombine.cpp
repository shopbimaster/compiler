// ================================================================
// 指令组合化简（InstCombine）
// 代数恒等式：x+0=x, x*1=x, x-x=0, x&0=0, x|x=x 等
// Store-to-Load 前推：同 BB 内的 store→load 直接替换
// ================================================================

#include "opt/Optimizer.h"
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ---- 判断是否是 0 常量 ----
bool isZero(IR::Value* v) {
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(v))
        return ci->getValue() == 0;
    if (auto* cf = dynamic_cast<IR::ConstantFloat*>(v))
        return cf->getValue() == 0.0;
    return false;
}

// ---- 判断是否是 1 常量 ----
bool isOne(IR::Value* v) {
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(v))
        return ci->getValue() == 1;
    return false;
}

// ---- 判断是否是 -1 常量 ----
bool isNegOne(IR::Value* v) {
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(v))
        return ci->getValue() == -1;
    return false;
}

// ---- 判断是否是可合并的指令 ----
bool canCombine(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    if (op == Opc::PHI || op == Opc::STORE || op == Opc::CALL ||
        op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
        op == Opc::ALLOCA || op == Opc::LOAD || op == Opc::GETELEMENTPTR)
        return false;
    return true;
}

// ---- 二元运算化简 ----
IR::Value* simplifyBinaryOp(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    auto* lhs = inst->getOperand(0);
    auto* rhs = inst->getOperand(1);
    auto* ty = inst->getType();

    // 常量折叠（两个常量）
    if (auto* lc = dynamic_cast<IR::ConstantInt*>(lhs)) {
        if (auto* rc = dynamic_cast<IR::ConstantInt*>(rhs)) {
            int64_t lv = lc->getValue(), rv = rc->getValue();
            int64_t result = 0;
            switch (op) {
                case Opc::ADD: result = lv + rv; break;
                case Opc::SUB: result = lv - rv; break;
                case Opc::MUL: result = lv * rv; break;
                case Opc::SDIV: if (rv != 0) result = lv / rv; else return nullptr; break;
                case Opc::SREM: if (rv != 0) result = lv % rv; else return nullptr; break;
                case Opc::AND: result = lv & rv; break;
                case Opc::OR:  result = lv | rv; break;
                case Opc::XOR: result = lv ^ rv; break;
                case Opc::SHL: result = lv << rv; break;
                case Opc::ASHR: result = lv >> rv; break;
                default: return nullptr;
            }
            return IR::ConstantInt::get(IR::IntegerType::I32, result);
        }
    }

    // 恒等式化简
    switch (op) {
        case Opc::ADD:
            if (isZero(lhs)) return rhs;       // 0 + x = x
            if (isZero(rhs)) return lhs;       // x + 0 = x
            break;
        case Opc::SUB:
            if (isZero(rhs)) return lhs;       // x - 0 = x
            if (lhs == rhs) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // x - x = 0
            break;
        case Opc::MUL:
            if (isZero(lhs) || isZero(rhs)) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // 0 * x = 0
            if (isOne(lhs)) return rhs;        // 1 * x = x
            if (isOne(rhs)) return lhs;        // x * 1 = x
            if (isNegOne(lhs)) {               // -1 * x = -x
                return IR::Instruction::createBinOp(Opc::SUB, ty, "neg", IR::ConstantInt::get(IR::IntegerType::I32, 0), rhs);
            }
            if (isNegOne(rhs)) {               // x * -1 = -x
                return IR::Instruction::createBinOp(Opc::SUB, ty, "neg", IR::ConstantInt::get(IR::IntegerType::I32, 0), lhs);
            }
            break;
        case Opc::SDIV:
            if (isOne(rhs)) return lhs;        // x / 1 = x
            if (isNegOne(rhs)) {               // x / -1 = -x (for integers)
                return IR::Instruction::createBinOp(Opc::SUB, ty, "neg", IR::ConstantInt::get(IR::IntegerType::I32, 0), lhs);
            }
            if (lhs == rhs) return IR::ConstantInt::get(IR::IntegerType::I32, 1); // x / x = 1
            if (isZero(lhs)) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // 0 / x = 0
            break;
        case Opc::SREM:
            if (isOne(rhs) || isNegOne(rhs)) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // x % 1 = 0
            if (lhs == rhs) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // x % x = 0
            if (isZero(lhs)) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // 0 % x = 0
            break;
        case Opc::AND:
            if (isZero(lhs) || isZero(rhs)) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // 0 & x = 0
            if (lhs == rhs) return lhs;        // x & x = x
            break;
        case Opc::OR:
            if (isZero(lhs)) return rhs;       // 0 | x = x
            if (isZero(rhs)) return lhs;       // x | 0 = x
            if (lhs == rhs) return lhs;        // x | x = x
            break;
        case Opc::XOR:
            if (isZero(lhs)) return rhs;       // 0 ^ x = x
            if (isZero(rhs)) return lhs;       // x ^ 0 = x
            if (lhs == rhs) return IR::ConstantInt::get(IR::IntegerType::I32, 0); // x ^ x = 0
            break;
        case Opc::SHL:
            if (isZero(rhs)) return lhs;       // x << 0 = x
            break;
        case Opc::ASHR:
            if (isZero(rhs)) return lhs;       // x >> 0 = x
            break;
        default:
            break;
    }
    return nullptr;
}

// ---- ICMP/FCMP 化简 ----
IR::Value* simplifyCmp(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    if (op != Opc::ICMP && op != Opc::FCMP) return nullptr;

    auto* lhs = inst->getOperand(0);
    auto* rhs = inst->getOperand(1);
    const auto& cond = inst->getName();

    // 两个常量比较
    if (auto* lc = dynamic_cast<IR::ConstantInt*>(lhs)) {
        if (auto* rc = dynamic_cast<IR::ConstantInt*>(rhs)) {
            int64_t lv = lc->getValue(), rv = rc->getValue();
            bool result = false;
            if (cond == "eq")  result = (lv == rv);
            else if (cond == "ne")  result = (lv != rv);
            else if (cond == "slt") result = (lv < rv);
            else if (cond == "sle") result = (lv <= rv);
            else if (cond == "sgt") result = (lv > rv);
            else if (cond == "sge") result = (lv >= rv);
            else return nullptr;
            return IR::ConstantInt::get(IR::IntegerType::I1, result ? 1 : 0);
        }
    }

    // 相同操作数化简
    if (lhs == rhs) {
        if (cond == "eq" || cond == "sle" || cond == "sge")
            return IR::ConstantInt::get(IR::IntegerType::I1, 1);
        if (cond == "ne" || cond == "slt" || cond == "sgt")
            return IR::ConstantInt::get(IR::IntegerType::I1, 0);
    }

    return nullptr;
}

// ---- 获取指针的基地址（剥离 GEP）----
IR::Value* getBasePointer(IR::Value* v) {
    if (auto* inst = dynamic_cast<IR::Instruction*>(v)) {
        if (inst->getOpcode() == Opc::GETELEMENTPTR) {
            return getBasePointer(inst->getOperand(0));
        }
    }
    return v;
}

// ---- 判断两个指针是否可能别名 ----
// 同一基址的 GEP 可能别名（索引可能相同），不同基址绝不别名
bool mayAlias(IR::Value* a, IR::Value* b) {
    if (a == b) return true;
    IR::Value* baseA = getBasePointer(a);
    IR::Value* baseB = getBasePointer(b);
    return baseA == baseB;
}

// ---- Store-to-Load 前推：同 BB 内，store 后紧跟的 load 可直接替换 ----
// 安全检查：
//   1. 遇到 CALL → 停止（可能修改任意内存）
//   2. 遇到 STORE 到同一指针 → 前推
//   3. 遇到 STORE 到可能别名的指针 → 停止（可能覆盖 LOAD 值）
//   4. 遇到 STORE 到绝不别名的指针 → 继续
IR::Value* forwardStoreToLoad(IR::Instruction* loadInst) {
    if (loadInst->getOpcode() != Opc::LOAD) return nullptr;

    auto* loadPtr = loadInst->getOperand(0);
    auto* bb = loadInst->getParent();
    if (!bb) return nullptr;

    // 在 LOAD 之前反向查找同一 BB 内最近的 STORE
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == loadInst) {
            // 反向遍历
            while (it != bb->begin()) {
                --it;
                auto* inst = it->get();

                // 如果遇到 CALL，保守停止（可能修改任意内存）
                if (inst->getOpcode() == Opc::CALL) return nullptr;

                // 如果遇到 STORE
                if (inst->getOpcode() == Opc::STORE) {
                    auto* storePtr = inst->getOperand(1);
                    if (storePtr == loadPtr) {
                        return inst->getOperand(0); // store 的值
                    }
                    // STORE 到可能别名的指针 → 停止（可能覆盖 LOAD 值）
                    if (mayAlias(storePtr, loadPtr)) return nullptr;
                    // STORE 到绝不别名的指针 → 继续
                }
            }
            break;
        }
    }
    return nullptr;
}

// ---- 单函数 InstCombine ----
bool instCombineOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    bool changed = false;

    std::vector<IR::Instruction*> toErase;

    for (auto& bb : func->getBlocks()) {
        for (auto it = bb->begin(); it != bb->end(); ++it) {
            auto* inst = it->get();

            IR::Value* simplified = nullptr;

            if (inst->getOpcode() == Opc::LOAD) {
                // ★ Store-to-Load 前推：同 BB 内 store 后的 load 可直接替换
                simplified = forwardStoreToLoad(inst);
            } else if (canCombine(inst)) {
                // 尝试二元运算化简
                simplified = simplifyBinaryOp(inst);
                if (!simplified) {
                    // 尝试比较运算化简
                    simplified = simplifyCmp(inst);
                }
            }

            if (simplified && simplified != inst) {
                inst->replaceAllUsesWith(simplified);
                toErase.push_back(inst);
                changed = true;
            }
        }
    }

    // 批量删除被替换的指令
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

bool instCombine(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (instCombineOnFunction(func.get()))
                changed = true;
        }
        if (changed) anyChanged = true;
    }
    return anyChanged;
}

} // namespace Opt