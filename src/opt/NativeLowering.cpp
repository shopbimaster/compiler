// ================================================================
// NativeLowering — 原生指令 lowering（合并实现）
// ----------------------------------------------------------------
// 包含 RepeatedDivRemToNative、ModAddRecurrence 等
// 递归或重复计算到原生指令的 lowering pass。

#include "opt/Optimizer.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <limits>
#include <cstdint>

namespace Opt {

// ================================================================
// 第 1 节：RecursiveMulToNative.cpp
// ------------------------------------------------
// 保留独立匿名命名空间，符号在各自作用域内自解析，
// 与其他节同名符号互不冲突（独立内部链接）。
// ================================================================

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

    // 清空所有 BB 的指令，并删除非 entry 的 BB
    // 避免后续 pass 遍历到空 BB（无 terminator）导致崩溃
    for (auto& bb : func->getBlocks()) {
        while (!bb->empty()) {
            auto it = bb->begin();
            it->get()->dropAllUses();
            bb->erase(it);
        }
    }
    // 移除所有非 entry 的 BB（它们现在是空的、无引用的）
    auto& blocks = func->getBlocks();
    blocks.erase(
        std::remove_if(blocks.begin(), blocks.end(),
            [entry](const std::unique_ptr<IR::BasicBlock>& bb) {
                return bb.get() != entry;
            }),
        blocks.end());

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

    // entry → bodyBB 分支
    auto* brInst = IR::Instruction::createBr(bodyBB);
    entry->pushBack(brInst);

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

bool isConstantValue(IR::Value* value, int expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isLoadFrom(IR::Value* value, IR::Value* pointer) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == IR::Instruction::Opcode::LOAD &&
           load->getNumOperands() == 1 && load->getOperand(0) == pointer;
}

bool isStoredTo(IR::Function* function, IR::Value* value, IR::Value* pointer) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::STORE &&
                inst->getNumOperands() == 2 &&
                inst->getOperand(0) == value &&
                inst->getOperand(1) == pointer) {
                return true;
            }
        }
    }
    return false;
}

bool isDirectlyReturned(IR::Function* function, IR::Value* value) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::RET &&
                inst->getNumOperands() == 1 &&
                inst->getOperand(0) == value) {
                return true;
            }
        }
    }
    return false;
}

bool hasEqualityTest(IR::Function* function, IR::Value* value, int constant) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::ICMP ||
                inst->getName() != "eq" || inst->getNumOperands() != 2) {
                continue;
            }
            if ((inst->getOperand(0) == value &&
                 isConstantValue(inst->getOperand(1), constant)) ||
                (inst->getOperand(1) == value &&
                 isConstantValue(inst->getOperand(0), constant))) {
                return true;
            }
        }
    }
    return false;
}

bool hasArgumentEqualityTest(IR::Function* function, IR::Argument* argument,
                             int constant) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::ICMP ||
                inst->getName() != "eq" || inst->getNumOperands() != 2) {
                continue;
            }
            if ((tracesToArg(inst->getOperand(0), argument, function) &&
                 isConstantValue(inst->getOperand(1), constant)) ||
                (tracesToArg(inst->getOperand(1), argument, function) &&
                 isConstantValue(inst->getOperand(0), constant))) {
                return true;
            }
        }
    }
    return false;
}

// Recognize the overflow-safe recursive modular multiplication:
//   f(a, 0) = 0
//   f(a, 1) = a % M
//   cur = f(a, b / 2)
//   cur = (cur + cur) % M
//   return b % 2 ? (cur + a) % M : cur
//
// This intentionally follows the unoptimized alloca/load/store shape. The
// strict structure check prevents applying 64-bit arithmetic to an unrelated
// recursion whose i32 overflow behavior could differ.
bool matchRecursiveModularMultiply(IR::Function* function, int& modulus) {
    using Opc = IR::Instruction::Opcode;
    if (function->isExternal() || function->getNumArgs() != 2 ||
        function->getFunctionType()->getReturnType() != IR::IntegerType::I32 ||
        function->getArg(0)->getType() != IR::IntegerType::I32 ||
        function->getArg(1)->getType() != IR::IntegerType::I32) {
        return false;
    }

    std::vector<IR::Instruction*> calls;
    std::vector<IR::Instruction*> remainders;
    bool returnsZero = false;
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == Opc::CALL) calls.push_back(inst.get());
            if (inst->getOpcode() == Opc::SREM) remainders.push_back(inst.get());
            if (inst->getOpcode() == Opc::WIDE_SMOD_MUL) return false;
            if (inst->getOpcode() == Opc::RET && inst->getNumOperands() == 1 &&
                isConstantValue(inst->getOperand(0), 0)) {
                returnsZero = true;
            }
        }
    }
    if (calls.size() != 1 || remainders.size() != 4 || !returnsZero) return false;

    auto* selfCall = calls.front();
    if (selfCall->getNumOperands() != 3 ||
        selfCall->getOperand(0) != static_cast<IR::Value*>(function) ||
        !tracesToArg(selfCall->getOperand(1), function->getArg(0), function)) {
        return false;
    }
    auto* half = dynamic_cast<IR::Instruction*>(selfCall->getOperand(2));
    if (!half || half->getOpcode() != Opc::SDIV ||
        half->getNumOperands() != 2 ||
        !tracesToArg(half->getOperand(0), function->getArg(1), function) ||
        !isConstantValue(half->getOperand(1), 2)) {
        return false;
    }

    IR::Value* currentSlot = nullptr;
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == Opc::STORE &&
                inst->getNumOperands() == 2 &&
                inst->getOperand(0) == selfCall) {
                if (currentSlot) return false;
                currentSlot = inst->getOperand(1);
            }
        }
    }
    auto* currentAlloca = dynamic_cast<IR::Instruction*>(currentSlot);
    if (!currentAlloca || currentAlloca->getOpcode() != Opc::ALLOCA) return false;

    IR::Instruction* parityRemainder = nullptr;
    std::vector<IR::Instruction*> modularRemainders;
    int detectedModulus = 0;
    for (auto* remainder : remainders) {
        auto* divisor = dynamic_cast<IR::ConstantInt*>(remainder->getOperand(1));
        if (!divisor) return false;
        int64_t value = divisor->getValue();
        if (value == 2 &&
            tracesToArg(remainder->getOperand(0), function->getArg(1), function)) {
            if (parityRemainder) return false;
            parityRemainder = remainder;
            continue;
        }
        if (value <= 1 || value > std::numeric_limits<int>::max() / 2)
            return false;
        if (detectedModulus == 0) detectedModulus = static_cast<int>(value);
        if (detectedModulus != value) return false;
        modularRemainders.push_back(remainder);
    }
    if (!parityRemainder || modularRemainders.size() != 3 ||
        !hasEqualityTest(function, parityRemainder, 1) ||
        !hasArgumentEqualityTest(function, function->getArg(1), 0) ||
        !hasArgumentEqualityTest(function, function->getArg(1), 1)) {
        return false;
    }

    bool foundBase = false;
    bool foundDouble = false;
    bool foundOdd = false;
    for (auto* remainder : modularRemainders) {
        IR::Value* dividend = remainder->getOperand(0);
        if (tracesToArg(dividend, function->getArg(0), function) &&
            isDirectlyReturned(function, remainder)) {
            foundBase = true;
            continue;
        }

        auto* add = dynamic_cast<IR::Instruction*>(dividend);
        if (!add || add->getOpcode() != Opc::ADD ||
            add->getNumOperands() != 2) {
            return false;
        }
        if (isLoadFrom(add->getOperand(0), currentSlot) &&
            isLoadFrom(add->getOperand(1), currentSlot) &&
            isStoredTo(function, remainder, currentSlot)) {
            foundDouble = true;
            continue;
        }

        bool lhsCurrent = isLoadFrom(add->getOperand(0), currentSlot);
        bool rhsCurrent = isLoadFrom(add->getOperand(1), currentSlot);
        bool lhsArgument =
            tracesToArg(add->getOperand(0), function->getArg(0), function);
        bool rhsArgument =
            tracesToArg(add->getOperand(1), function->getArg(0), function);
        if (((lhsCurrent && rhsArgument) || (rhsCurrent && lhsArgument)) &&
            isDirectlyReturned(function, remainder)) {
            foundOdd = true;
            continue;
        }
        return false;
    }
    if (!foundBase || !foundDouble || !foundOdd) return false;

    modulus = detectedModulus;
    return true;
}

void addGuardedWideModularMultiply(IR::Function* function, int modulus) {
    using Opc = IR::Instruction::Opcode;
    auto* slowEntry = function->getEntryBlock();
    auto* guard = function->insertBlock("modmul.guard", slowEntry);
    auto* fast = function->insertBlock("modmul.fast", slowEntry);
    auto* zero = IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto* modulusValue =
        IR::ConstantInt::get(IR::IntegerType::I32, modulus);

    auto* firstNonNegative = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(0), zero, "sge");
    auto* firstBelowModulus = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(0), modulusValue, "slt");
    auto* secondNonNegative = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(1), zero, "sge");
    auto* firstInRange = IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1, "modmul.first.in.range",
        firstNonNegative, firstBelowModulus);
    auto* inputsSafe = IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1, "modmul.inputs.safe",
        firstInRange, secondNonNegative);
    guard->pushBack(firstNonNegative);
    guard->pushBack(firstBelowModulus);
    guard->pushBack(secondNonNegative);
    guard->pushBack(firstInRange);
    guard->pushBack(inputsSafe);
    guard->pushBack(IR::Instruction::createCondBr(
        inputsSafe, fast, slowEntry));

    auto* nativeResult = IR::Instruction::createTernaryOp(
        Opc::WIDE_SMOD_MUL, IR::IntegerType::I32, "modmul.wide",
        function->getArg(0), function->getArg(1), modulusValue);
    fast->pushBack(nativeResult);
    fast->pushBack(IR::Instruction::createRet(nativeResult));
}

} // namespace

bool recursiveModularMulToNative(IR::Module* mod) {
    bool changed = false;
    for (auto& function : mod->getFunctions()) {
        int modulus = 0;
        if (!matchRecursiveModularMultiply(function.get(), modulus)) continue;
        addGuardedWideModularMultiply(function.get(), modulus);
        changed = true;
    }
    return changed;
}


// ================================================================
// 第 2 节：RepeatedDivRemToNative.cpp
// ------------------------------------------------
// 保留独立匿名命名空间，符号在各自作用域内自解析，
// 与其他节同名符号互不冲突（独立内部链接）。
// ================================================================

namespace {

using Opc = IR::Instruction::Opcode;

struct DigitExtractionMatch {
    unsigned bitsPerDigit = 0;
    int base = 0;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isPowerOfTwo(int64_t value) {
    return value > 1 && (value & (value - 1)) == 0;
}

unsigned exactLog2(int64_t value) {
    unsigned bits = 0;
    while (value > 1) {
        value >>= 1;
        ++bits;
    }
    return bits;
}

bool tracesToArgument(IR::Value* value, IR::Argument* argument,
                      IR::Function* function) {
    if (!value || !argument) return false;
    if (value == argument) return true;

    auto* load = dynamic_cast<IR::Instruction*>(value);
    if (!load || load->getOpcode() != Opc::LOAD ||
        load->getNumOperands() != 1) {
        return false;
    }
    IR::Value* slot = load->getOperand(0);
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == Opc::STORE &&
                inst->getNumOperands() == 2 &&
                inst->getOperand(0) == argument &&
                inst->getOperand(1) == slot) {
                return true;
            }
        }
    }
    return false;
}

// 注：isLoadFrom / isDirectlyReturned 已由第 1 节（RecursiveMulToNative）
// 提供，C++ 匿名命名空间在同文件内合并，此处不再重复定义。

bool hasStore(IR::Function* function, IR::Value* value, IR::Value* slot) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == Opc::STORE &&
                inst->getNumOperands() == 2 &&
                inst->getOperand(0) == value &&
                inst->getOperand(1) == slot) {
                return true;
            }
        }
    }
    return false;
}

bool hasStoreInBlock(IR::BasicBlock* block, IR::Value* value,
                     IR::Value* slot) {
    if (!block) return false;
    for (auto& inst : block->getInstructions()) {
        if (inst->getOpcode() == Opc::STORE &&
            inst->getNumOperands() == 2 &&
            inst->getOperand(0) == value &&
            inst->getOperand(1) == slot) {
            return true;
        }
    }
    return false;
}

IR::Instruction* findControllingBranch(IR::Function* function,
                                       IR::Instruction* condition) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == Opc::COND_BR &&
                inst->getNumOperands() == 3 &&
                inst->getOperand(0) == condition) {
                return inst.get();
            }
        }
    }
    return nullptr;
}

bool matchRepeatedDivRem(IR::Function* function,
                         DigitExtractionMatch& match) {
    if (!function || function->isExternal() ||
        function->getNumArgs() != 2 ||
        function->getFunctionType()->getReturnType() !=
            IR::IntegerType::I32 ||
        function->getArg(0)->getType() != IR::IntegerType::I32 ||
        function->getArg(1)->getType() != IR::IntegerType::I32) {
        return false;
    }

    std::vector<IR::Instruction*> divisions;
    std::vector<IR::Instruction*> remainders;
    std::vector<IR::Instruction*> additions;
    std::vector<IR::Instruction*> comparisons;
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            switch (inst->getOpcode()) {
            case Opc::SDIV: divisions.push_back(inst.get()); break;
            case Opc::SREM: remainders.push_back(inst.get()); break;
            case Opc::ADD: additions.push_back(inst.get()); break;
            case Opc::ICMP: comparisons.push_back(inst.get()); break;
            case Opc::CALL:
            case Opc::PHI:
            case Opc::SELECT:
                return false;
            default:
                break;
            }
        }
    }
    if (divisions.size() != 1 || remainders.size() != 1)
        return false;

    auto* division = divisions.front();
    auto* remainder = remainders.front();
    auto* divisor =
        dynamic_cast<IR::ConstantInt*>(division->getOperand(1));
    auto* remainderDivisor =
        dynamic_cast<IR::ConstantInt*>(remainder->getOperand(1));
    if (!divisor || !remainderDivisor ||
        divisor->getValue() != remainderDivisor->getValue() ||
        !isPowerOfTwo(divisor->getValue())) {
        return false;
    }

    unsigned bits = exactLog2(divisor->getValue());
    if (bits == 0 || bits >= 32 || 32 % bits != 0)
        return false;

    auto* divisionInput =
        dynamic_cast<IR::Instruction*>(division->getOperand(0));
    auto* remainderInput =
        dynamic_cast<IR::Instruction*>(remainder->getOperand(0));
    if (!divisionInput || divisionInput->getOpcode() != Opc::LOAD ||
        !remainderInput || remainderInput->getOpcode() != Opc::LOAD) {
        return false;
    }
    IR::Value* valueSlot = divisionInput->getOperand(0);
    if (remainderInput->getOperand(0) != valueSlot ||
        !hasStore(function, function->getArg(0), valueSlot) ||
        !hasStore(function, division, valueSlot) ||
        !isDirectlyReturned(function, remainder)) {
        return false;
    }

    IR::Value* counterSlot = nullptr;
    IR::Instruction* increment = nullptr;
    for (auto* addition : additions) {
        IR::Value* loadedCounter = nullptr;
        if (isConstant(addition->getOperand(0), 1))
            loadedCounter = addition->getOperand(1);
        else if (isConstant(addition->getOperand(1), 1))
            loadedCounter = addition->getOperand(0);
        else
            continue;

        auto* load = dynamic_cast<IR::Instruction*>(loadedCounter);
        if (!load || load->getOpcode() != Opc::LOAD) continue;
        IR::Value* candidateSlot = load->getOperand(0);
        if (!hasStore(function, addition, candidateSlot) ||
            !hasStore(function,
                      IR::ConstantInt::get(IR::IntegerType::I32, 0),
                      candidateSlot)) {
            continue;
        }
        if (increment) return false;
        increment = addition;
        counterSlot = candidateSlot;
    }
    if (!increment || !counterSlot || counterSlot == valueSlot)
        return false;

    IR::Instruction* loopCondition = nullptr;
    IR::Instruction* loopBranch = nullptr;
    for (auto* comparison : comparisons) {
        if (comparison->getName() != "slt" ||
            comparison->getNumOperands() != 2) {
            continue;
        }
        auto* controllingBranch =
            findControllingBranch(function, comparison);
        if (!controllingBranch) continue;
        bool counterFirst =
            isLoadFrom(comparison->getOperand(0), counterSlot) &&
            tracesToArgument(comparison->getOperand(1),
                             function->getArg(1), function);
        if (counterFirst) {
            loopCondition = comparison;
            loopBranch = controllingBranch;
            break;
        }
    }
    if (!loopCondition || !loopBranch ||
        increment->getParent() != division->getParent()) {
        return false;
    }

    auto* loopBody = division->getParent();
    auto* trueTarget =
        dynamic_cast<IR::BasicBlock*>(loopBranch->getOperand(1));
    auto* falseTarget =
        dynamic_cast<IR::BasicBlock*>(loopBranch->getOperand(2));
    if (trueTarget != loopBody || !falseTarget ||
        remainder->getParent() != falseTarget ||
        !hasStoreInBlock(loopBody, division, valueSlot) ||
        !hasStoreInBlock(loopBody, increment, counterSlot)) {
        return false;
    }
    auto* bodyTerminator = loopBody->getTerminator();
    if (!bodyTerminator || bodyTerminator->getOpcode() != Opc::BR ||
        bodyTerminator->getNumOperands() != 1 ||
        bodyTerminator->getOperand(0) != loopCondition->getParent()) {
        return false;
    }

    match.bitsPerDigit = bits;
    match.base = static_cast<int>(divisor->getValue());
    return true;
}

bool replaceWithNativeDigitExtraction(IR::Function* function,
                                      const DigitExtractionMatch& match) {
    auto* functionType = function->getFunctionType();
    if (!functionType ||
        functionType->getReturnType() != IR::IntegerType::I32) {
        return false;
    }
    auto* entry = function->getEntryBlock();
    if (!entry) return false;

    // Detach every operand before destroying any instruction.  Later
    // instructions in the old body may still reference values defined near
    // the entry, so erasing one instruction at a time can leave dangling
    // operands for the remaining cleanup.
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            for (unsigned index = 0;
                 index < instruction->getNumOperands(); ++index) {
                instruction->setOperand(index, nullptr);
            }
        }
    }
    for (auto& block : function->getBlocks()) {
        while (!block->empty()) {
            block->erase(block->begin());
        }
    }
    auto& blocks = function->getBlocks();
    blocks.erase(
        std::remove_if(
            blocks.begin(), blocks.end(),
            [entry](const std::unique_ptr<IR::BasicBlock>& block) {
                return block.get() != entry;
            }),
        blocks.end());

    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* signShift = IR::ConstantInt::get(i32, 31);
    auto* digitBits =
        IR::ConstantInt::get(i32, match.bitsPerDigit);
    int maxPosition = 32 / static_cast<int>(match.bitsPerDigit);
    auto* maximumPosition =
        IR::ConstantInt::get(i32, maxPosition);
    auto* positionMask =
        IR::ConstantInt::get(i32, maxPosition - 1);
    auto* digitMask =
        IR::ConstantInt::get(i32, match.base - 1);

    auto* positionPositive = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(1), zero, "sgt");
    auto* effectivePosition = IR::Instruction::createSelect(
        positionPositive, function->getArg(1), zero,
        "digit.position.nonnegative");
    auto* maskedPosition = IR::Instruction::createBinOp(
        Opc::AND, i32, "digit.position.masked",
        effectivePosition, positionMask);
    auto* shiftAmount = IR::Instruction::createBinOp(
        Opc::MUL, i32, "digit.shift",
        maskedPosition, digitBits);

    auto* sign = IR::Instruction::createBinOp(
        Opc::ASHR, i32, "digit.sign",
        function->getArg(0), signShift);
    auto* magnitudeXor = IR::Instruction::createBinOp(
        Opc::XOR, i32, "digit.magnitude.xor",
        function->getArg(0), sign);
    auto* magnitude = IR::Instruction::createBinOp(
        Opc::SUB, i32, "digit.magnitude",
        magnitudeXor, sign);
    auto* shifted = IR::Instruction::createBinOp(
        Opc::ASHR, i32, "digit.shifted",
        magnitude, shiftAmount);
    auto* unsignedDigit = IR::Instruction::createBinOp(
        Opc::AND, i32, "digit.unsigned",
        shifted, digitMask);
    auto* signedXor = IR::Instruction::createBinOp(
        Opc::XOR, i32, "digit.signed.xor",
        unsignedDigit, sign);
    auto* signedDigit = IR::Instruction::createBinOp(
        Opc::SUB, i32, "digit.signed",
        signedXor, sign);

    auto* positionInRange = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(1), maximumPosition, "slt");
    auto* result = IR::Instruction::createSelect(
        positionInRange, signedDigit, zero, "digit.result");

    entry->pushBack(positionPositive);
    entry->pushBack(effectivePosition);
    entry->pushBack(maskedPosition);
    entry->pushBack(shiftAmount);
    entry->pushBack(sign);
    entry->pushBack(magnitudeXor);
    entry->pushBack(magnitude);
    entry->pushBack(shifted);
    entry->pushBack(unsignedDigit);
    entry->pushBack(signedXor);
    entry->pushBack(signedDigit);
    entry->pushBack(positionInRange);
    entry->pushBack(result);
    entry->pushBack(IR::Instruction::createRet(result));
    return true;
}

} // namespace

bool repeatedDivRemToNative(IR::Module* module) {
    bool changed = false;
    for (auto& function : module->getFunctions()) {
        DigitExtractionMatch match;
        if (!matchRepeatedDivRem(function.get(), match)) continue;
        if (replaceWithNativeDigitExtraction(function.get(), match))
            changed = true;
    }
    return changed;
}


// ================================================================
// 第 3 节：ModAddRecurrence.cpp
// ------------------------------------------------
// 保留独立匿名命名空间，符号在各自作用域内自解析，
// 与其他节同名符号互不冲突（独立内部链接）。
// ================================================================

namespace {

using Opc = IR::Instruction::Opcode;

struct ModAddLoop {
    IR::Function* function = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* body = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Value* inductionPointer = nullptr;
    IR::Value* bound = nullptr;
    IR::Value* boundPointer = nullptr;
    IR::Value* recurrencePointer = nullptr;
    int64_t increment = 0;
    int64_t modulus = 0;
};

IR::ConstantInt* asConstant(IR::Value* value) {
    return dynamic_cast<IR::ConstantInt*>(value);
}

IR::Instruction* asInstruction(
    IR::Value* value, Opc opcode) {
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    return instruction &&
                   instruction->getOpcode() == opcode
        ? instruction
        : nullptr;
}

bool isScalarStorage(IR::Value* pointer) {
    if (!pointer) return false;
    auto* type =
        dynamic_cast<IR::PointerType*>(pointer->getType());
    if (!type ||
        type->getPointeeType() != IR::IntegerType::I32) {
        return false;
    }
    auto* instruction =
        dynamic_cast<IR::Instruction*>(pointer);
    return dynamic_cast<IR::GlobalVariable*>(pointer) ||
           (instruction &&
            instruction->getOpcode() == Opc::ALLOCA);
}

IR::Instruction* getLoadPointer(
    IR::Value* value, IR::Value*& pointer) {
    auto* load = asInstruction(value, Opc::LOAD);
    if (!load || load->getNumOperands() != 1) return nullptr;
    pointer = load->getOperand(0);
    return load;
}

bool matchAddConstant(
    IR::Instruction* add, IR::Value* variable,
    int64_t& constant) {
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2) {
        return false;
    }
    IR::ConstantInt* value = nullptr;
    if (add->getOperand(0) == variable) {
        value = asConstant(add->getOperand(1));
    } else if (add->getOperand(1) == variable) {
        value = asConstant(add->getOperand(0));
    }
    if (!value) return false;
    constant = value->getValue();
    return true;
}

bool blockContainsPhi(IR::BasicBlock* block) {
    for (auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opc::PHI) return true;
    }
    return false;
}

bool findPreheader(
    const NaturalLoop& loop, const PredMap& predecessors,
    IR::BasicBlock*& preheader) {
    preheader = nullptr;
    auto found = predecessors.find(loop.header);
    if (found == predecessors.end()) return false;
    for (auto* predecessor : found->second) {
        if (loop.body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator =
        preheader ? preheader->getTerminator() : nullptr;
    return terminator &&
           terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == loop.header;
}

bool hasZeroInitialization(
    IR::BasicBlock* preheader, IR::Value* pointer) {
    IR::Instruction* initialization = nullptr;
    for (auto& instruction : preheader->getInstructions()) {
        if (instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            instruction->getOperand(1) != pointer) {
            continue;
        }
        initialization = instruction.get();
    }
    auto* zero = initialization
        ? asConstant(initialization->getOperand(0))
        : nullptr;
    return zero && zero->getValue() == 0;
}

bool matchHeader(
    const NaturalLoop& loop,
    IR::BasicBlock*& body,
    IR::BasicBlock*& exit,
    IR::Value*& inductionPointer,
    IR::Value*& bound,
    IR::Value*& boundPointer) {
    auto* terminator = loop.header->getTerminator();
    if (!terminator ||
        terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }
    auto* compare =
        asInstruction(terminator->getOperand(0), Opc::ICMP);
    if (!compare || compare->getName() != "slt" ||
        compare->getNumOperands() != 2) {
        return false;
    }

    IR::Value* inductionLoadPointer = nullptr;
    if (!getLoadPointer(
            compare->getOperand(0),
            inductionLoadPointer) ||
        !isScalarStorage(inductionLoadPointer)) {
        return false;
    }

    auto* trueTarget = dynamic_cast<IR::BasicBlock*>(
        terminator->getOperand(1));
    auto* falseTarget = dynamic_cast<IR::BasicBlock*>(
        terminator->getOperand(2));
    if (!trueTarget || !falseTarget ||
        !loop.body.count(trueTarget) ||
        loop.body.count(falseTarget)) {
        return false;
    }

    IR::Value* loadedBoundPointer = nullptr;
    auto* boundLoad = getLoadPointer(
        compare->getOperand(1), loadedBoundPointer);
    if (boundLoad) {
        if (!isScalarStorage(loadedBoundPointer)) return false;
        bound = nullptr;
        boundPointer = loadedBoundPointer;
    } else {
        auto* boundInstruction =
            dynamic_cast<IR::Instruction*>(
                compare->getOperand(1));
        if (boundInstruction &&
            boundInstruction->getParent() !=
                loop.header->getParent()->getEntryBlock()) {
            return false;
        }
        bound = compare->getOperand(1);
        boundPointer = nullptr;
    }

    body = trueTarget;
    exit = falseTarget;
    inductionPointer = inductionLoadPointer;
    return !blockContainsPhi(loop.header) &&
           !blockContainsPhi(exit);
}

bool matchInductionUpdate(
    IR::BasicBlock* body,
    IR::Value* inductionPointer,
    std::unordered_set<IR::Instruction*>& matched) {
    for (auto& owned : body->getInstructions()) {
        auto* store = owned.get();
        if (store->getOpcode() != Opc::STORE ||
            store->getNumOperands() != 2 ||
            store->getOperand(1) != inductionPointer) {
            continue;
        }
        auto* add = asInstruction(
            store->getOperand(0), Opc::ADD);
        if (!add) return false;
        IR::Value* loadPointer = nullptr;
        IR::Instruction* load = nullptr;
        int64_t step = 0;
        for (unsigned index = 0; index < 2; ++index) {
            auto* candidate = getLoadPointer(
                add->getOperand(index), loadPointer);
            if (candidate &&
                loadPointer == inductionPointer) {
                load = candidate;
                auto* constant =
                    asConstant(add->getOperand(1 - index));
                step = constant ? constant->getValue() : 0;
                break;
            }
        }
        if (!load || step != 1) return false;
        matched.insert(load);
        matched.insert(add);
        matched.insert(store);
        return true;
    }
    return false;
}

bool matchRecurrence(
    IR::BasicBlock* body,
    IR::Value* inductionPointer,
    ModAddLoop& result,
    std::unordered_set<IR::Instruction*>& matched) {
    std::unordered_map<IR::Instruction*, size_t> position;
    size_t current = 0;
    for (auto& instruction : body->getInstructions()) {
        position[instruction.get()] = current++;
    }

    for (auto& owned : body->getInstructions()) {
        auto* finalStore = owned.get();
        if (finalStore->getOpcode() != Opc::STORE ||
            finalStore->getNumOperands() != 2 ||
            finalStore->getOperand(1) == inductionPointer) {
            continue;
        }
        auto* remainder = asInstruction(
            finalStore->getOperand(0), Opc::SREM);
        auto* modulus = remainder &&
                                remainder->getNumOperands() == 2
            ? asConstant(remainder->getOperand(1))
            : nullptr;
        if (!modulus || modulus->getValue() <= 0 ||
            modulus->getValue() >
                std::numeric_limits<int32_t>::max()) {
            continue;
        }

        auto* add = asInstruction(
            remainder->getOperand(0), Opc::ADD);
        IR::Instruction* reload = nullptr;
        IR::Instruction* intermediateStore = nullptr;
        IR::Value* recurrencePointer =
            finalStore->getOperand(1);
        if (!add) {
            IR::Value* reloadPointer = nullptr;
            reload = getLoadPointer(
                remainder->getOperand(0), reloadPointer);
            if (!reload ||
                reloadPointer != recurrencePointer) {
                continue;
            }
            for (auto& candidate : body->getInstructions()) {
                auto* store = candidate.get();
                if (store->getOpcode() != Opc::STORE ||
                    store->getNumOperands() != 2 ||
                    store->getOperand(1) !=
                        recurrencePointer ||
                    position[store] >= position[reload]) {
                    continue;
                }
                auto* candidateAdd = asInstruction(
                    store->getOperand(0), Opc::ADD);
                if (candidateAdd &&
                    (!intermediateStore ||
                     position[store] >
                         position[intermediateStore])) {
                    intermediateStore = store;
                    add = candidateAdd;
                }
            }
        }
        if (!add || !isScalarStorage(recurrencePointer) ||
            recurrencePointer == inductionPointer) {
            continue;
        }

        IR::Instruction* recurrenceLoad = nullptr;
        int64_t increment = 0;
        for (unsigned index = 0; index < 2; ++index) {
            IR::Value* pointer = nullptr;
            auto* candidate = getLoadPointer(
                add->getOperand(index), pointer);
            auto* constant =
                asConstant(add->getOperand(1 - index));
            if (candidate && pointer == recurrencePointer &&
                constant) {
                recurrenceLoad = candidate;
                increment = constant->getValue();
                break;
            }
        }
        if (!recurrenceLoad || increment <= 0 ||
            increment >
                std::numeric_limits<int32_t>::max()) {
            continue;
        }
        if (position[recurrenceLoad] >= position[add] ||
            position[add] >= position[remainder] ||
            position[remainder] >= position[finalStore]) {
            continue;
        }
        if (reload &&
            (!intermediateStore ||
             position[add] >= position[intermediateStore] ||
             position[intermediateStore] >= position[reload] ||
             position[reload] >= position[remainder])) {
            continue;
        }

        result.recurrencePointer = recurrencePointer;
        result.increment = increment;
        result.modulus = modulus->getValue();
        matched.insert(recurrenceLoad);
        matched.insert(add);
        if (intermediateStore) {
            matched.insert(intermediateStore);
            matched.insert(reload);
        }
        matched.insert(remainder);
        matched.insert(finalStore);
        return true;
    }
    return false;
}

bool matchLoop(
    IR::Function* function, const NaturalLoop& loop,
    const PredMap& predecessors, ModAddLoop& result) {
    if (!function || loop.body.size() != 2 ||
        loop.latch == loop.header) {
        return false;
    }
    IR::BasicBlock* preheader = nullptr;
    if (!findPreheader(loop, predecessors, preheader)) {
        return false;
    }

    IR::BasicBlock* body = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Value* inductionPointer = nullptr;
    IR::Value* bound = nullptr;
    IR::Value* boundPointer = nullptr;
    if (!matchHeader(
            loop, body, exit, inductionPointer,
            bound, boundPointer) ||
        body != loop.latch ||
        !hasZeroInitialization(
            preheader, inductionPointer)) {
        return false;
    }
    if (boundPointer == inductionPointer) return false;

    auto* bodyTerminator = body->getTerminator();
    if (!bodyTerminator ||
        bodyTerminator->getOpcode() != Opc::BR ||
        bodyTerminator->getNumOperands() != 1 ||
        bodyTerminator->getOperand(0) != loop.header) {
        return false;
    }

    result.function = function;
    result.preheader = preheader;
    result.header = loop.header;
    result.body = body;
    result.exit = exit;
    result.inductionPointer = inductionPointer;
    result.bound = bound;
    result.boundPointer = boundPointer;

    std::unordered_set<IR::Instruction*> matched = {
        bodyTerminator};
    if (!matchInductionUpdate(
            body, inductionPointer, matched) ||
        !matchRecurrence(
            body, inductionPointer, result, matched) ||
        result.recurrencePointer == boundPointer) {
        return false;
    }
    for (auto& instruction : body->getInstructions()) {
        if (!matched.count(instruction.get())) return false;
    }
    return true;
}

IR::BasicBlock::iterator findTerminator(
    IR::BasicBlock* block) {
    auto* terminator = block->getTerminator();
    for (auto iterator = block->begin();
         iterator != block->end(); ++iterator) {
        if (iterator->get() == terminator) return iterator;
    }
    return block->end();
}

bool applyLoopVersioning(const ModAddLoop& loop) {
    auto insertion = findTerminator(loop.preheader);
    if (insertion == loop.preheader->end()) return false;

    auto insert = [&](IR::Instruction* instruction) {
        insertion =
            loop.preheader->insert(insertion, instruction);
        ++insertion;
        return instruction;
    };

    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* intMax = IR::ConstantInt::get(
        i32, std::numeric_limits<int32_t>::max());
    auto* increment =
        IR::ConstantInt::get(i32, loop.increment);
    auto* modulus =
        IR::ConstantInt::get(i32, loop.modulus);

    IR::Value* tripCount = loop.bound;
    if (loop.boundPointer) {
        tripCount = insert(IR::Instruction::createLoad(
            i32, loop.boundPointer, "modrec.trip"));
    }
    auto* initial = insert(IR::Instruction::createLoad(
        i32, loop.recurrencePointer, "modrec.initial"));
    auto* positiveTrip = insert(IR::Instruction::createCmp(
        Opc::ICMP, tripCount, zero, "sgt"));
    auto* nonnegativeInitial =
        insert(IR::Instruction::createCmp(
            Opc::ICMP, initial, zero, "sge"));
    auto* remaining = insert(
        IR::Instruction::createBinOp(
            Opc::SUB, i32, "modrec.remaining",
            intMax, initial));
    auto* maxTrips = insert(
        IR::Instruction::createBinOp(
            Opc::SDIV, i32, "modrec.max_trips",
            remaining, increment));
    auto* tripFits = insert(IR::Instruction::createCmp(
        Opc::ICMP, tripCount, maxTrips, "sle"));
    auto* nonnegativeAndFits = insert(
        IR::Instruction::createBinOp(
            Opc::AND, IR::IntegerType::I1,
            "modrec.nonnegative_fits",
            nonnegativeInitial, tripFits));
    auto* safe = insert(IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1, "modrec.safe",
        positiveTrip, nonnegativeAndFits));
    auto* scaled = insert(IR::Instruction::createBinOp(
        Opc::MUL, i32, "modrec.scaled",
        tripCount, increment));
    auto* sum = insert(IR::Instruction::createBinOp(
        Opc::ADD, i32, "modrec.sum", initial, scaled));
    auto* closed = insert(IR::Instruction::createBinOp(
        Opc::SREM, i32, "modrec.closed", sum, modulus));

    auto* fast = loop.function->createBlock(
        "modrec.fast");
    fast->pushBack(IR::Instruction::createStore(
        closed, loop.recurrencePointer));
    fast->pushBack(IR::Instruction::createStore(
        tripCount, loop.inductionPointer));
    fast->pushBack(IR::Instruction::createBr(loop.exit));

    insertion = loop.preheader->erase(insertion);
    loop.preheader->insert(
        insertion,
        IR::Instruction::createCondBr(
            safe, fast, loop.header));
    return true;
}

} // namespace

bool modAddRecurrenceStrengthReduce(IR::Module* module) {
    bool changed = false;
    for (auto& ownedFunction : module->getFunctions()) {
        auto* function = ownedFunction.get();
        if (!function || function->isExternal()) continue;

        auto loops = findNaturalLoops(function);
        auto predecessors = buildPredecessors(function);
        std::vector<ModAddLoop> candidates;
        for (const auto& loop : loops) {
            ModAddLoop candidate;
            if (matchLoop(
                    function, loop, predecessors, candidate)) {
                candidates.push_back(candidate);
            }
        }
        for (const auto& candidate : candidates) {
            if (applyLoopVersioning(candidate)) {
                changed = true;
            }
        }
    }
    return changed;
}


} // namespace Opt
