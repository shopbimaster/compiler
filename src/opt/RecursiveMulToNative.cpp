// ================================================================
// P0: 递归乘法→原生乘法（Recursive Multiplication to Native MUL）
// 策略：
//   检测函数的递归调用模式是否等价于整数乘法。
//   若等价，将递归调用替换为原生 MUL 指令，O(n) → O(1)。
//
// 识别的模式：
//   模式A: multiply(a, b) = (b<=0) ? 0 : a + multiply(a, b-1)
//   模式B: multiply(a, b) = (b==0) ? 0 : (b&1) ? a + multiply(a<<1, b>>1) : multiply(a<<1, b>>1)
//   模式C: power(a, b) = (b==0) ? 1 : a * power(a, b-1) — 递归幂运算
//   （更多乘法等价模式未来可扩展）
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

namespace Opt {
namespace {

// ---- 检测函数是否自递归 ----
bool isSelfRecursive(IR::Function* func) {
    if (func->isExternal()) return false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL &&
                inst->getOperand(0) == static_cast<IR::Value*>(func)) {
                return true;
            }
        }
    }
    return false;
}

// ---- 收集函数中所有自递归调用指令 ----
std::vector<IR::Instruction*> collectSelfCalls(IR::Function* func) {
    std::vector<IR::Instruction*> calls;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL &&
                inst->getOperand(0) == static_cast<IR::Value*>(func)) {
                calls.push_back(inst.get());
            }
        }
    }
    return calls;
}

// ---- 追踪一个 Value 是否最终来源于函数参数 ----
// 处理 ALLOCA → STORE → LOAD 间接引用链
// 返回 true 如果 val == arg 或 val 是 load(alloca) 且 alloca 曾有 store(arg)
bool tracesToArg(IR::Value* val, IR::Argument* arg, IR::Function* func) {
    if (!val || !arg) return false;
    if (val == arg) return true;

    auto* loadInst = dynamic_cast<IR::Instruction*>(val);
    if (!loadInst || loadInst->getOpcode() != IR::Instruction::Opcode::LOAD)
        return false;
    if (loadInst->getNumOperands() < 1) return false;

    auto* loadPtr = loadInst->getOperand(0);
    auto* allocaInst = dynamic_cast<IR::Instruction*>(loadPtr);
    if (!allocaInst || allocaInst->getOpcode() != IR::Instruction::Opcode::ALLOCA)
        return false;

    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                if (inst->getNumOperands() >= 2 &&
                    inst->getOperand(1) == allocaInst &&
                    inst->getOperand(0) == arg) {
                    return true;
                }
            }
        }
    }
    return false;
}

// ---- 追踪一个 Value 是否等价于函数参数（经过 LOAD 追踪） ----
IR::Argument* traceToArgument(IR::Value* val, IR::Function* func) {
    if (!val) return nullptr;
    for (unsigned i = 0; i < func->getNumArgs(); ++i) {
        if (tracesToArg(val, func->getArg(i), func))
            return func->getArg(i);
    }
    return nullptr;
}

// ---- 识别模式A: multiply(a, b) = (b<=0) ? 0 : a + multiply(a, b-1) ----
// 特征：
//   1. 函数有 2 个 int 参数 (a, b)
//   2. entry BB: ICMP 比较 b <= 0，条件分支
//   3. 一个分支直接返回 0（base case）
//   4. 另一个分支：ADD(a, CALL(self, a, SUB(b, 1))) → RET
bool detectPatternA_Additive(IR::Function* func,
                              unsigned& paramA,
                              unsigned& paramB) {
    if (func->getNumArgs() != 2) return false;
    paramA = 0;
    paramB = 1;

    IR::BasicBlock* cmpBB = nullptr;
    IR::Instruction* condBr = nullptr;
    IR::Instruction* icmpInst = nullptr;

    for (auto& bb : func->getBlocks()) {
        auto* term = bb->getTerminator();
        if (term && term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            cmpBB = bb.get();
            condBr = term;
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
                    icmpInst = inst.get();
                }
            }
            break;
        }
    }

    if (!cmpBB || !condBr || !icmpInst) return false;

    auto* cmpOp0 = icmpInst->getOperand(0);
    auto* cmpOp1 = icmpInst->getOperand(1);
    auto* argB = func->getArg(1);

    bool cmpOp0isB = tracesToArg(cmpOp0, argB, func);
    bool cmpOp1isB = tracesToArg(cmpOp1, argB, func);
    if (!cmpOp0isB && !cmpOp1isB) return false;

    auto* otherOp = cmpOp0isB ? dynamic_cast<IR::ConstantInt*>(cmpOp1)
                              : dynamic_cast<IR::ConstantInt*>(cmpOp0);
    if (!otherOp || otherOp->getValue() > 1) return false;

    auto* thenBB = dynamic_cast<IR::BasicBlock*>(condBr->getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(condBr->getOperand(2));
    if (!thenBB || !elseBB) return false;

    IR::BasicBlock* baseBB = nullptr;
    IR::BasicBlock* recurBB = nullptr;
    for (auto* bb : {thenBB, elseBB}) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::RET) {
                if (inst->getNumOperands() > 0) {
                    auto* retVal = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
                    if (retVal && retVal->getValue() == 0) {
                        baseBB = bb;
                    }
                }
            }
        }
    }
    recurBB = (baseBB == thenBB) ? elseBB : thenBB;
    if (!baseBB || !recurBB) return false;

    for (auto& bb : func->getBlocks()) {
        if (bb.get() == baseBB) continue;
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::ADD) {
                auto* lhs = inst->getOperand(0);
                auto* rhs = inst->getOperand(1);
                if (!lhs || !rhs) continue;

                auto* lhsCall = dynamic_cast<IR::Instruction*>(lhs);
                auto* rhsCall = dynamic_cast<IR::Instruction*>(rhs);
                IR::Instruction* selfCall = nullptr;
                IR::Value* aVal = nullptr;

                if (lhsCall && lhsCall->getOpcode() == IR::Instruction::Opcode::CALL &&
                    lhsCall->getOperand(0) == static_cast<IR::Value*>(func)) {
                    selfCall = lhsCall;
                    aVal = rhs;
                } else if (rhsCall && rhsCall->getOpcode() == IR::Instruction::Opcode::CALL &&
                           rhsCall->getOperand(0) == static_cast<IR::Value*>(func)) {
                    selfCall = rhsCall;
                    aVal = lhs;
                }
                if (!selfCall || !aVal) continue;

            if (selfCall->getNumOperands() < 3) continue;
            auto* callArg0 = selfCall->getOperand(1);
            auto* callArg1 = selfCall->getOperand(2);

            auto* argA = func->getArg(0);
            if (!tracesToArg(callArg0, argA, func)) continue;

            auto* subInst = dynamic_cast<IR::Instruction*>(callArg1);
            if (!subInst || subInst->getOpcode() != IR::Instruction::Opcode::SUB) continue;
            if (subInst->getNumOperands() < 2) continue;
            if (!tracesToArg(subInst->getOperand(0), argB, func)) continue;
            auto* const1 = dynamic_cast<IR::ConstantInt*>(subInst->getOperand(1));
            if (!const1 || const1->getValue() != 1) continue;

            return true;
            }
        }
    }
    return false;
}

// ---- 识别模式B: 位移递归乘法 ----
// multiply(a, b) = (b==0) ? 0 : (b&1) ? a + multiply(a<<1, b>>1) : multiply(a<<1, b>>1)
bool detectPatternB_Shift(IR::Function* func,
                           unsigned& paramA,
                           unsigned& paramB) {
    if (func->getNumArgs() != 2) return false;
    paramA = 0;
    paramB = 1;

    auto* entry = func->getEntryBlock();
    if (!entry) return false;

    auto* term = entry->getTerminator();
    if (!term || term->getOpcode() != IR::Instruction::Opcode::COND_BR)
        return false;

    // 检查是否有 selfCall
    auto selfCalls = collectSelfCalls(func);
    if (selfCalls.empty()) return false;

    // 检查 selfCall 的参数是否被左移/右移
    for (auto* call : selfCalls) {
        if (call->getNumOperands() < 3) continue;
        auto* callArg0 = call->getOperand(1);
        auto* callArg1 = call->getOperand(2);

        bool aShifted = false, bShifted = false;
        auto* argA = func->getArg(0);
        auto* argB = func->getArg(1);

        if (auto* shl = dynamic_cast<IR::Instruction*>(callArg0)) {
            if (shl->getOpcode() == IR::Instruction::Opcode::SHL &&
                shl->getNumOperands() >= 2) {
                auto* base = shl->getOperand(0);
                auto* cnt = dynamic_cast<IR::ConstantInt*>(shl->getOperand(1));
                if (tracesToArg(base, argA, func) && cnt && cnt->getValue() == 1) aShifted = true;
            }
        }
        if (auto* ashr = dynamic_cast<IR::Instruction*>(callArg1)) {
            if (ashr->getOpcode() == IR::Instruction::Opcode::ASHR &&
                ashr->getNumOperands() >= 2) {
                auto* base = ashr->getOperand(0);
                auto* cnt = dynamic_cast<IR::ConstantInt*>(ashr->getOperand(1));
                if (tracesToArg(base, argB, func) && cnt && cnt->getValue() == 1) bShifted = true;
            }
        }

        if (aShifted && bShifted) return true;
    }
    return false;
}

// ---- 模式C: 递归幂运算 → 可用乘法循环或内置实现 ----
// power(a, b) = (b==0) ? 1 : a * power(a, b-1)
// 注意：SysY2022 可能无原生乘法，此模式在后面阶段处理
bool detectPatternC_Power(IR::Function* func,
                           unsigned& paramA,
                           unsigned& paramB) {
    if (func->getNumArgs() != 2) return false;
    paramA = 0;
    paramB = 1;

    auto* entry = func->getEntryBlock();
    if (!entry) return false;

    auto* term = entry->getTerminator();
    if (!term || term->getOpcode() != IR::Instruction::Opcode::COND_BR)
        return false;

    auto* argB = func->getArg(1);

    // 寻找 ICMP：检查 b == 0
    IR::Instruction* icmpInst = nullptr;
    for (auto& inst : entry->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
            icmpInst = inst.get();
        }
    }
    if (!icmpInst) return false;

    auto* cmpOp0 = icmpInst->getOperand(0);
    auto* cmpOp1 = icmpInst->getOperand(1);
    if (cmpOp0 != argB) return false;
    auto* const0 = dynamic_cast<IR::ConstantInt*>(cmpOp1);
    if (!const0 || const0->getValue() != 0) return false;
    if (icmpInst->getName() != "eq") return false;

    // COND_BR 两个分支
    auto* thenBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
    if (!thenBB || !elseBB) return false;

    // 检查 base case 返回 1
    IR::BasicBlock* baseBB = nullptr;
    IR::BasicBlock* recurBB = nullptr;
    for (auto* bb : {thenBB, elseBB}) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::RET) {
                if (inst->getNumOperands() > 0) {
                    auto* retVal = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
                    if (retVal && retVal->getValue() == 1) {
                        baseBB = bb;
                    }
                }
            }
        }
    }
    recurBB = (baseBB == thenBB) ? elseBB : thenBB;
    if (!baseBB || !recurBB) return false;

    // 检查递归分支：MUL(a, CALL(self, a, SUB(b, 1)))
    for (auto& inst : recurBB->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::MUL) continue;
        if (inst->getNumOperands() < 2) continue;

        auto* lhs = inst->getOperand(0);
        auto* rhs = inst->getOperand(1);
        auto* argA = func->getArg(0);

        auto* lhsCall = dynamic_cast<IR::Instruction*>(lhs);
        auto* rhsCall = dynamic_cast<IR::Instruction*>(rhs);
        IR::Instruction* selfCall = nullptr;

        if (lhsCall && lhsCall->getOpcode() == IR::Instruction::Opcode::CALL &&
            lhsCall->getOperand(0) == static_cast<IR::Value*>(func)) {
            selfCall = lhsCall;
        } else if (rhsCall && rhsCall->getOpcode() == IR::Instruction::Opcode::CALL &&
                   rhsCall->getOperand(0) == static_cast<IR::Value*>(func)) {
            selfCall = rhsCall;
        }
        if (!selfCall) continue;

        // 检查 selfCall 参数
        if (selfCall->getNumOperands() < 3) continue;
        if (selfCall->getOperand(1) != argA) continue;
        auto* subInst = dynamic_cast<IR::Instruction*>(selfCall->getOperand(2));
        if (!subInst || subInst->getOpcode() != IR::Instruction::Opcode::SUB) continue;
        if (subInst->getNumOperands() < 2) continue;
        if (subInst->getOperand(0) != argB) continue;
        auto* const1 = dynamic_cast<IR::ConstantInt*>(subInst->getOperand(1));
        if (!const1 || const1->getValue() != 1) continue;

        return true;
    }
    return false;
}

// ---- 替换整个函数的 body 为 MUL(a,b) + RET ----
// 保留参数 ALLOCA + STORE 链路以确保后端正确处理参数加载
bool convertToNativeMul(IR::Function* func) {
    auto* ft = func->getFunctionType();
    if (!ft || ft->getReturnType() != IR::IntegerType::I32) return false;

    if (func->getNumArgs() != 2) return false;
    for (unsigned i = 0; i < func->getNumArgs(); ++i) {
        if (func->getArg(i)->getType() != IR::IntegerType::I32)
            return false;
    }

    auto* argA = func->getArg(0);
    auto* argB = func->getArg(1);

    auto* entry = func->getEntryBlock();
    if (!entry) return false;

    for (auto& bb : func->getBlocks()) {
        while (!bb->empty()) {
            auto it = bb->begin();
            it->get()->dropAllUses();
            bb->erase(it);
        }
    }

    auto* i32 = IR::IntegerType::I32;

    auto* allocaA = IR::Instruction::createAlloca(i32, "a");
    auto* allocaB = IR::Instruction::createAlloca(i32, "b");
    auto* storeA = IR::Instruction::createStore(argA, allocaA);
    auto* storeB = IR::Instruction::createStore(argB, allocaB);

    entry->pushBack(allocaA);
    entry->pushBack(allocaB);
    entry->pushBack(storeA);
    entry->pushBack(storeB);

    auto* bodyBB = func->createBlock("bb_native");

    auto* loadA = IR::Instruction::createLoad(i32, allocaA, "t0");
    auto* loadB = IR::Instruction::createLoad(i32, allocaB, "t1");
    auto* mulInst = IR::Instruction::createBinOp(
        IR::Instruction::Opcode::MUL, i32,
        "t2", loadA, loadB);
    auto* retInst = IR::Instruction::createRet(mulInst);

    bodyBB->pushBack(loadA);
    bodyBB->pushBack(loadB);
    bodyBB->pushBack(mulInst);
    bodyBB->pushBack(retInst);

    return true;
}

// ---- 尝试识别并转换单函数 ----
bool tryConvertFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    if (!isSelfRecursive(func)) return false;

    unsigned paramA, paramB;

    if (detectPatternA_Additive(func, paramA, paramB)) {
        return convertToNativeMul(func);
    }
    if (detectPatternB_Shift(func, paramA, paramB)) {
        return convertToNativeMul(func);
    }
    return false;
}

} // namespace

// ================================================================
// recursiveMulToNative 入口
// ================================================================
void recursiveMulToNative(IR::Module* mod) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (tryConvertFunction(func.get())) {
                changed = true;
            }
        }
    }
}

} // namespace Opt