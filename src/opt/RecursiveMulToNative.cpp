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
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <limits>

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

// ================================================================
// recursiveMulToNative 入口
// ================================================================
bool recursiveMulToNative(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (tryConvertFunction(func.get())) {
                changed = true;
                anyChanged = true;
            }
        }
    }
    return anyChanged;
}

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

} // namespace Opt
