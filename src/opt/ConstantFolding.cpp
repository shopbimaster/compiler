// ================================================================
// O1: IR 常量折叠 —— 编译期计算所有常量表达式
// ================================================================

#include "opt/Optimizer.h"
#include <cmath>
#include <unordered_set>

namespace Opt {
namespace {

bool isConstant(IR::Value* v) {
    return dynamic_cast<IR::ConstantInt*>(v) || dynamic_cast<IR::ConstantFloat*>(v);
}

int64_t getIntVal(IR::Value* v) {
    if (auto* ci = dynamic_cast<IR::ConstantInt*>(v)) return ci->getValue();
    return 0;
}

double getFloatVal(IR::Value* v) {
    if (auto* cf = dynamic_cast<IR::ConstantFloat*>(v)) return cf->getValue();
    return 0.0;
}

bool tryFold(IR::Instruction* inst) {
    if (inst->getNumOperands() == 0) return false;
    if (isConstant(inst)) return false;

    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;

    // ---- 二元整数运算 ----
    if ((op == Opc::ADD || op == Opc::SUB || op == Opc::MUL ||
         op == Opc::SDIV || op == Opc::SREM ||
         op == Opc::AND || op == Opc::OR || op == Opc::XOR ||
         op == Opc::SHL || op == Opc::ASHR) &&
        inst->getNumOperands() >= 2) {
        auto* l = inst->getOperand(0);
        auto* r = inst->getOperand(1);
        if (!isConstant(l) || !isConstant(r)) return false;
        int64_t lv = getIntVal(l), rv = getIntVal(r);
        int64_t result = 0;
        bool valid = true;
        switch (op) {
            case Opc::ADD:  result = lv + rv; break;
            case Opc::SUB:  result = lv - rv; break;
            case Opc::MUL:  result = lv * rv; break;
            case Opc::SDIV: if (rv == 0) valid = false; else result = lv / rv; break;
            case Opc::SREM: if (rv == 0) valid = false; else result = lv % rv; break;
            case Opc::AND:  result = lv & rv; break;
            case Opc::OR:   result = lv | rv; break;
            case Opc::XOR:  result = lv ^ rv; break;
            case Opc::SHL:  result = lv << rv; break;
            case Opc::ASHR: result = lv >> rv; break;
            default: valid = false;
        }
        if (!valid) return false;
        auto* folded = IR::ConstantInt::get(IR::IntegerType::I32, result);
        inst->replaceAllUsesWith(folded);
        inst->dropAllUses();
        return true;
    }

    // ---- 二元浮点运算 ----
    if ((op == Opc::FADD || op == Opc::FSUB || op == Opc::FMUL || op == Opc::FDIV) &&
        inst->getNumOperands() >= 2) {
        auto* l = inst->getOperand(0);
        auto* r = inst->getOperand(1);
        if (!isConstant(l) || !isConstant(r)) return false;
        float lv = static_cast<float>(getFloatVal(l)), rv = static_cast<float>(getFloatVal(r));
        float result = 0.0f;
        bool valid = true;
        switch (op) {
            case Opc::FADD: result = lv + rv; break;
            case Opc::FSUB: result = lv - rv; break;
            case Opc::FMUL: result = lv * rv; break;
            case Opc::FDIV: if (rv == 0.0f) valid = false; else result = lv / rv; break;
            default: valid = false;
        }
        if (!valid) return false;
        auto* folded = IR::ConstantFloat::get(IR::FloatType::get(), static_cast<double>(result));
        inst->replaceAllUsesWith(folded);
        inst->dropAllUses();
        return true;
    }

    // ---- ICMP 常量比较 ----
    if (op == Opc::ICMP && inst->getNumOperands() >= 2) {
        auto* l = inst->getOperand(0);
        auto* r = inst->getOperand(1);
        std::string cond = inst->getName();
        bool result = false;
        if (isConstant(l) && isConstant(r)) {
            auto* lt = l->getType();
            if (lt && lt->isFloat()) {
                float lv = static_cast<float>(getFloatVal(l)), rv = static_cast<float>(getFloatVal(r));
                if (cond == "eq")       result = (lv == rv);
                else if (cond == "ne")  result = (lv != rv);
                else if (cond == "slt") result = (lv < rv);
                else if (cond == "sle") result = (lv <= rv);
                else if (cond == "sgt") result = (lv > rv);
                else if (cond == "sge") result = (lv >= rv);
                else                    result = (lv < rv);
            } else {
                int64_t lv = getIntVal(l), rv = getIntVal(r);
                if (cond == "eq")       result = (lv == rv);
                else if (cond == "ne")  result = (lv != rv);
                else if (cond == "slt") result = (lv < rv);
                else if (cond == "sle") result = (lv <= rv);
                else if (cond == "sgt") result = (lv > rv);
                else if (cond == "sge") result = (lv >= rv);
                else                    result = (lv < rv);
            }
            auto* folded = IR::ConstantInt::get(IR::IntegerType::I1, result ? 1 : 0);
            inst->replaceAllUsesWith(folded);
            inst->dropAllUses();
            return true;
        }
        return false;
    }

    // ---- LOAD const 全局变量 ----
    // const int KSIZE = 5; load i32, i32* @KSIZE → ConstantInt(5)
    // 这是极其重要的优化：const 全局变量值在编译时已知，所有 LOAD 都应被替换为常量。
    // 不处理此情况会导致循环中每次访问 KSIZE 都生成 la+lw 指令，阻塞 LICM/CSE/常量传播。
    if (op == Opc::LOAD && inst->getNumOperands() >= 1) {
        auto* ptr = inst->getOperand(0);
        if (auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr)) {
            if (gv->isConstant()) {
                auto* init = gv->getInitializer();
                if (auto* ci = dynamic_cast<IR::ConstantInt*>(init)) {
                    inst->replaceAllUsesWith(ci);
                    inst->dropAllUses();
                    return true;
                }
                if (auto* cf = dynamic_cast<IR::ConstantFloat*>(init)) {
                    inst->replaceAllUsesWith(cf);
                    inst->dropAllUses();
                    return true;
                }
            }
        }
    }

    // ---- Cast 运算 ----
    if ((op == Opc::ZEXT || op == Opc::SEXT || op == Opc::TRUNC) &&
        inst->getNumOperands() >= 1) {
        auto* src = inst->getOperand(0);
        if (!isConstant(src)) return false;
        int64_t sv = getIntVal(src);
        auto* folded = IR::ConstantInt::get(IR::IntegerType::I32, sv);
        inst->replaceAllUsesWith(folded);
        inst->dropAllUses();
        return true;
    }

    if (op == Opc::SITOFP && inst->getNumOperands() >= 1) {
        auto* src = inst->getOperand(0);
        if (!isConstant(src)) return false;
        auto* folded = IR::ConstantFloat::get(IR::FloatType::get(),
                                               static_cast<double>(getIntVal(src)));
        inst->replaceAllUsesWith(folded);
        inst->dropAllUses();
        return true;
    }

    if (op == Opc::FPTOSI && inst->getNumOperands() >= 1) {
        auto* src = inst->getOperand(0);
        if (!isConstant(src)) return false;
        auto* folded = IR::ConstantInt::get(IR::IntegerType::I32,
                                             static_cast<int64_t>(getFloatVal(src)));
        inst->replaceAllUsesWith(folded);
        inst->dropAllUses();
        return true;
    }

    return false;
}

} // namespace

// ================================================================
// constantFolding 入口 — 反复扫描直到收敛
// ================================================================
void constantFolding(IR::Module* mod) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    if (tryFold(it->get())) {
                        it = bb->erase(it);
                        changed = true;
                    } else {
                        ++it;
                    }
                }
            }
        }
    }
}

} // namespace Opt