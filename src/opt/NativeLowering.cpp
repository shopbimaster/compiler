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

bool isConstantValue(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

IR::Instruction* modularInstructionAt(
    IR::BasicBlock* block, size_t index, IR::Instruction::Opcode opcode) {
    if (!block || index >= block->getInstructions().size()) return nullptr;
    auto* instruction = block->getInstructions()[index].get();
    return instruction->getOpcode() == opcode ? instruction : nullptr;
}

bool isEqualityWithConstant(IR::Instruction* comparison, IR::Value* value,
                            int64_t constant) {
    if (!comparison ||
        comparison->getOpcode() != IR::Instruction::Opcode::ICMP ||
        comparison->getName() != "eq" ||
        comparison->getNumOperands() != 2) {
        return false;
    }
    return (comparison->getOperand(0) == value &&
            isConstantValue(comparison->getOperand(1), constant)) ||
           (comparison->getOperand(1) == value &&
            isConstantValue(comparison->getOperand(0), constant));
}

bool isBranchTo(IR::Instruction* branch, IR::BasicBlock* target) {
    return branch && branch->getOpcode() == IR::Instruction::Opcode::BR &&
           branch->getNumOperands() == 1 && branch->getOperand(0) == target;
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
    if (!function || function->isExternal() || function->getNumArgs() != 2 ||
        function->getFunctionType()->getReturnType() != IR::IntegerType::I32 ||
        function->getArg(0)->getType() != IR::IntegerType::I32 ||
        function->getArg(1)->getType() != IR::IntegerType::I32) {
        return false;
    }

    auto* entry = function->getEntryBlock();
    if (!entry || entry->size() != 7) return false;
    auto* firstSlot = modularInstructionAt(entry, 0, Opc::ALLOCA);
    auto* firstInit = modularInstructionAt(entry, 1, Opc::STORE);
    auto* secondSlot = modularInstructionAt(entry, 2, Opc::ALLOCA);
    auto* secondInit = modularInstructionAt(entry, 3, Opc::STORE);
    auto* secondZeroLoad = modularInstructionAt(entry, 4, Opc::LOAD);
    auto* zeroComparison = modularInstructionAt(entry, 5, Opc::ICMP);
    auto* zeroBranch = modularInstructionAt(entry, 6, Opc::COND_BR);
    if (!firstSlot || !firstInit || firstInit->getNumOperands() != 2 ||
        firstInit->getOperand(0) != function->getArg(0) ||
        firstInit->getOperand(1) != firstSlot ||
        !secondSlot || secondSlot == firstSlot ||
        !secondInit || secondInit->getNumOperands() != 2 ||
        secondInit->getOperand(0) != function->getArg(1) ||
        secondInit->getOperand(1) != secondSlot ||
        !secondZeroLoad || secondZeroLoad->getNumOperands() != 1 ||
        secondZeroLoad->getOperand(0) != secondSlot ||
        !isEqualityWithConstant(zeroComparison, secondZeroLoad, 0) ||
        !zeroBranch || zeroBranch->getNumOperands() != 3 ||
        zeroBranch->getOperand(0) != zeroComparison) {
        return false;
    }

    auto* zeroReturnBlock =
        dynamic_cast<IR::BasicBlock*>(zeroBranch->getOperand(1));
    auto* zeroFallthrough =
        dynamic_cast<IR::BasicBlock*>(zeroBranch->getOperand(2));
    auto* zeroReturn = modularInstructionAt(zeroReturnBlock, 0, Opc::RET);
    auto* zeroNext = modularInstructionAt(zeroFallthrough, 0, Opc::BR);
    if (!zeroReturnBlock || zeroReturnBlock->size() != 1 ||
        !zeroReturn || zeroReturn->getNumOperands() != 1 ||
        !isConstantValue(zeroReturn->getOperand(0), 0) ||
        !zeroFallthrough || zeroFallthrough->size() != 1 || !zeroNext) {
        return false;
    }

    auto* oneTest = zeroNext && zeroNext->getNumOperands() == 1
        ? dynamic_cast<IR::BasicBlock*>(zeroNext->getOperand(0))
        : nullptr;
    if (!isBranchTo(zeroNext, oneTest) || !oneTest || oneTest->size() != 3)
        return false;
    auto* secondOneLoad = modularInstructionAt(oneTest, 0, Opc::LOAD);
    auto* oneComparison = modularInstructionAt(oneTest, 1, Opc::ICMP);
    auto* oneBranch = modularInstructionAt(oneTest, 2, Opc::COND_BR);
    if (!secondOneLoad || secondOneLoad->getNumOperands() != 1 ||
        secondOneLoad->getOperand(0) != secondSlot ||
        !isEqualityWithConstant(oneComparison, secondOneLoad, 1) ||
        !oneBranch || oneBranch->getNumOperands() != 3 ||
        oneBranch->getOperand(0) != oneComparison) {
        return false;
    }

    auto* oneReturnBlock =
        dynamic_cast<IR::BasicBlock*>(oneBranch->getOperand(1));
    auto* oneFallthrough =
        dynamic_cast<IR::BasicBlock*>(oneBranch->getOperand(2));
    if (!oneReturnBlock || oneReturnBlock->size() != 3 ||
        !oneFallthrough || oneFallthrough->size() != 1) {
        return false;
    }
    auto* firstBaseLoad = modularInstructionAt(oneReturnBlock, 0, Opc::LOAD);
    auto* baseRemainder = modularInstructionAt(oneReturnBlock, 1, Opc::SREM);
    auto* baseReturn = modularInstructionAt(oneReturnBlock, 2, Opc::RET);
    if (!firstBaseLoad || firstBaseLoad->getNumOperands() != 1 ||
        firstBaseLoad->getOperand(0) != firstSlot ||
        !baseRemainder || baseRemainder->getNumOperands() != 2 ||
        baseRemainder->getOperand(0) != firstBaseLoad ||
        !baseReturn || baseReturn->getNumOperands() != 1 ||
        baseReturn->getOperand(0) != baseRemainder) {
        return false;
    }

    auto* recursiveBranch =
        modularInstructionAt(oneFallthrough, 0, Opc::BR);
    auto* recursiveBlock = recursiveBranch &&
                           recursiveBranch->getNumOperands() == 1
        ? dynamic_cast<IR::BasicBlock*>(recursiveBranch->getOperand(0))
        : nullptr;
    if (!isBranchTo(recursiveBranch, recursiveBlock) || !recursiveBlock ||
        recursiveBlock->size() != 14) {
        return false;
    }

    auto* currentSlot = modularInstructionAt(recursiveBlock, 0, Opc::ALLOCA);
    auto* firstRecursiveLoad = modularInstructionAt(recursiveBlock, 1, Opc::LOAD);
    auto* secondRecursiveLoad = modularInstructionAt(recursiveBlock, 2, Opc::LOAD);
    auto* half = modularInstructionAt(recursiveBlock, 3, Opc::SDIV);
    auto* selfCall = modularInstructionAt(recursiveBlock, 4, Opc::CALL);
    auto* callStore = modularInstructionAt(recursiveBlock, 5, Opc::STORE);
    auto* currentDoubleLoad = modularInstructionAt(recursiveBlock, 6, Opc::LOAD);
    auto* doubled = modularInstructionAt(recursiveBlock, 7, Opc::ADD);
    auto* doubleRemainder = modularInstructionAt(recursiveBlock, 8, Opc::SREM);
    auto* doubleStore = modularInstructionAt(recursiveBlock, 9, Opc::STORE);
    auto* secondParityLoad = modularInstructionAt(recursiveBlock, 10, Opc::LOAD);
    auto* parityRemainder = modularInstructionAt(recursiveBlock, 11, Opc::SREM);
    auto* parityComparison = modularInstructionAt(recursiveBlock, 12, Opc::ICMP);
    auto* parityBranch = modularInstructionAt(recursiveBlock, 13, Opc::COND_BR);
    if (!currentSlot || currentSlot == firstSlot || currentSlot == secondSlot ||
        !firstRecursiveLoad || firstRecursiveLoad->getNumOperands() != 1 ||
        firstRecursiveLoad->getOperand(0) != firstSlot ||
        !secondRecursiveLoad || secondRecursiveLoad->getNumOperands() != 1 ||
        secondRecursiveLoad->getOperand(0) != secondSlot ||
        !half || half->getNumOperands() != 2 ||
        half->getOperand(0) != secondRecursiveLoad ||
        !isConstantValue(half->getOperand(1), 2) ||
        !selfCall || selfCall->getNumOperands() != 3 ||
        selfCall->getOperand(0) != static_cast<IR::Value*>(function) ||
        selfCall->getOperand(1) != firstRecursiveLoad ||
        selfCall->getOperand(2) != half ||
        !callStore || callStore->getNumOperands() != 2 ||
        callStore->getOperand(0) != selfCall ||
        callStore->getOperand(1) != currentSlot ||
        !currentDoubleLoad || currentDoubleLoad->getNumOperands() != 1 ||
        currentDoubleLoad->getOperand(0) != currentSlot ||
        !doubled || doubled->getNumOperands() != 2 ||
        doubled->getOperand(0) != currentDoubleLoad ||
        doubled->getOperand(1) != currentDoubleLoad ||
        !doubleRemainder || doubleRemainder->getNumOperands() != 2 ||
        doubleRemainder->getOperand(0) != doubled ||
        !doubleStore || doubleStore->getNumOperands() != 2 ||
        doubleStore->getOperand(0) != doubleRemainder ||
        doubleStore->getOperand(1) != currentSlot ||
        !secondParityLoad || secondParityLoad->getNumOperands() != 1 ||
        secondParityLoad->getOperand(0) != secondSlot ||
        !parityRemainder || parityRemainder->getNumOperands() != 2 ||
        parityRemainder->getOperand(0) != secondParityLoad ||
        !isConstantValue(parityRemainder->getOperand(1), 2) ||
        !isEqualityWithConstant(parityComparison, parityRemainder, 1) ||
        !parityBranch || parityBranch->getNumOperands() != 3 ||
        parityBranch->getOperand(0) != parityComparison) {
        return false;
    }

    auto* oddBlock = dynamic_cast<IR::BasicBlock*>(parityBranch->getOperand(1));
    auto* evenTarget = dynamic_cast<IR::BasicBlock*>(parityBranch->getOperand(2));
    IR::BasicBlock* evenBridge = nullptr;
    IR::BasicBlock* evenBlock = evenTarget;
    if (evenTarget && evenTarget->size() == 1) {
        auto* bridgeBranch = modularInstructionAt(evenTarget, 0, Opc::BR);
        if (!bridgeBranch || bridgeBranch->getNumOperands() != 1) return false;
        evenBridge = evenTarget;
        evenBlock = dynamic_cast<IR::BasicBlock*>(bridgeBranch->getOperand(0));
        if (!isBranchTo(bridgeBranch, evenBlock)) return false;
    }
    if (!oddBlock || oddBlock->size() != 5 ||
        !evenBlock || evenBlock->size() != 2) {
        return false;
    }
    auto* currentOddLoad = modularInstructionAt(oddBlock, 0, Opc::LOAD);
    auto* firstOddLoad = modularInstructionAt(oddBlock, 1, Opc::LOAD);
    auto* oddAdd = modularInstructionAt(oddBlock, 2, Opc::ADD);
    auto* oddRemainder = modularInstructionAt(oddBlock, 3, Opc::SREM);
    auto* oddReturn = modularInstructionAt(oddBlock, 4, Opc::RET);
    if (!currentOddLoad || currentOddLoad->getNumOperands() != 1 ||
        currentOddLoad->getOperand(0) != currentSlot ||
        !firstOddLoad || firstOddLoad->getNumOperands() != 1 ||
        firstOddLoad->getOperand(0) != firstSlot ||
        !oddAdd || oddAdd->getNumOperands() != 2 ||
        !((oddAdd->getOperand(0) == currentOddLoad &&
           oddAdd->getOperand(1) == firstOddLoad) ||
          (oddAdd->getOperand(1) == currentOddLoad &&
           oddAdd->getOperand(0) == firstOddLoad)) ||
        !oddRemainder || oddRemainder->getNumOperands() != 2 ||
        oddRemainder->getOperand(0) != oddAdd ||
        !oddReturn || oddReturn->getNumOperands() != 1 ||
        oddReturn->getOperand(0) != oddRemainder) {
        return false;
    }
    auto* currentEvenLoad = modularInstructionAt(evenBlock, 0, Opc::LOAD);
    auto* evenReturn = modularInstructionAt(evenBlock, 1, Opc::RET);
    if (!currentEvenLoad || currentEvenLoad->getNumOperands() != 1 ||
        currentEvenLoad->getOperand(0) != currentSlot ||
        !evenReturn || evenReturn->getNumOperands() != 1 ||
        evenReturn->getOperand(0) != currentEvenLoad) {
        return false;
    }

    auto* baseModulus =
        dynamic_cast<IR::ConstantInt*>(baseRemainder->getOperand(1));
    auto* doubleModulus =
        dynamic_cast<IR::ConstantInt*>(doubleRemainder->getOperand(1));
    auto* oddModulus =
        dynamic_cast<IR::ConstantInt*>(oddRemainder->getOperand(1));
    if (!baseModulus || !doubleModulus || !oddModulus ||
        baseModulus->getValue() != doubleModulus->getValue() ||
        baseModulus->getValue() != oddModulus->getValue() ||
        baseModulus->getValue() <= 1 ||
        baseModulus->getValue() > std::numeric_limits<int>::max() / 2) {
        return false;
    }

    std::unordered_set<IR::BasicBlock*> accounted = {
        entry, zeroReturnBlock, zeroFallthrough, oneTest, oneReturnBlock,
        oneFallthrough, recursiveBlock, oddBlock, evenBlock};
    if (evenBridge) accounted.insert(evenBridge);
    if (accounted.size() != (evenBridge ? 10u : 9u)) return false;
    for (const auto& block : function->getBlocks()) {
        if (!accounted.count(block.get()) && !block->empty()) return false;
    }

    modulus = static_cast<int>(baseModulus->getValue());
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

IR::Instruction* digitInstruction(IR::Value* value, Opc opcode) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    return instruction && instruction->getOpcode() == opcode
               ? instruction
               : nullptr;
}

bool isEntryAlloca(IR::Value* value, IR::BasicBlock* entry) {
    auto* instruction = digitInstruction(value, Opc::ALLOCA);
    return instruction && instruction->getParent() == entry;
}

bool isBinaryWithConstant(IR::Instruction* instruction, Opc opcode,
                          IR::Value* other, int64_t constant) {
    if (!instruction || instruction->getOpcode() != opcode ||
        instruction->getNumOperands() != 2) {
        return false;
    }
    return (instruction->getOperand(0) == other &&
            isConstant(instruction->getOperand(1), constant)) ||
           (instruction->getOperand(1) == other &&
            isConstant(instruction->getOperand(0), constant));
}

bool matchRepeatedDivRem(IR::Function* function,
                         DigitExtractionMatch& match) {
    if (!function || function->isExternal() ||
        function->getNumArgs() != 2 ||
        function->getBlocks().size() != 4 ||
        function->getFunctionType()->getReturnType() !=
            IR::IntegerType::I32 ||
        function->getArg(0)->getType() != IR::IntegerType::I32 ||
        function->getArg(1)->getType() != IR::IntegerType::I32) {
        return false;
    }

    auto* entry = function->getEntryBlock();
    if (!entry || entry->empty()) return false;
    auto* entryBranch = entry->getTerminator();
    if (!entryBranch || entryBranch->getOpcode() != Opc::BR ||
        entryBranch->getNumOperands() != 1) {
        return false;
    }
    auto* header = dynamic_cast<IR::BasicBlock*>(entryBranch->getOperand(0));
    if (!header || header == entry || header->size() != 4) return false;

    auto headerIt = header->getInstructions().begin();
    auto* counterHeaderLoad = digitInstruction(headerIt++->get(), Opc::LOAD);
    auto* boundLoad = digitInstruction(headerIt++->get(), Opc::LOAD);
    auto* loopCondition = digitInstruction(headerIt++->get(), Opc::ICMP);
    auto* loopBranch = digitInstruction(headerIt++->get(), Opc::COND_BR);
    if (!counterHeaderLoad || counterHeaderLoad->getNumOperands() != 1 ||
        !boundLoad || boundLoad->getNumOperands() != 1 ||
        !loopCondition || loopCondition->getName() != "slt" ||
        loopCondition->getNumOperands() != 2 ||
        loopCondition->getOperand(0) != counterHeaderLoad ||
        loopCondition->getOperand(1) != boundLoad ||
        !loopBranch || loopBranch->getNumOperands() != 3 ||
        loopBranch->getOperand(0) != loopCondition) {
        return false;
    }

    auto* loopBody = dynamic_cast<IR::BasicBlock*>(loopBranch->getOperand(1));
    auto* exit = dynamic_cast<IR::BasicBlock*>(loopBranch->getOperand(2));
    if (!loopBody || !exit || loopBody == entry || loopBody == header ||
        exit == entry || exit == header || exit == loopBody ||
        loopBody->size() != 7 || exit->size() != 3) {
        return false;
    }

    auto bodyIt = loopBody->getInstructions().begin();
    auto* divisionInput = digitInstruction(bodyIt++->get(), Opc::LOAD);
    auto* division = digitInstruction(bodyIt++->get(), Opc::SDIV);
    auto* valueStore = digitInstruction(bodyIt++->get(), Opc::STORE);
    auto* counterBodyLoad = digitInstruction(bodyIt++->get(), Opc::LOAD);
    auto* increment = digitInstruction(bodyIt++->get(), Opc::ADD);
    auto* counterStore = digitInstruction(bodyIt++->get(), Opc::STORE);
    auto* bodyBranch = digitInstruction(bodyIt++->get(), Opc::BR);
    if (!divisionInput || divisionInput->getNumOperands() != 1 ||
        !division || division->getNumOperands() != 2 ||
        division->getOperand(0) != divisionInput ||
        !valueStore || valueStore->getNumOperands() != 2 ||
        valueStore->getOperand(0) != division ||
        !counterBodyLoad || counterBodyLoad->getNumOperands() != 1 ||
        !isBinaryWithConstant(increment, Opc::ADD, counterBodyLoad, 1) ||
        !counterStore || counterStore->getNumOperands() != 2 ||
        counterStore->getOperand(0) != increment ||
        !bodyBranch || bodyBranch->getNumOperands() != 1 ||
        bodyBranch->getOperand(0) != header) {
        return false;
    }

    auto exitIt = exit->getInstructions().begin();
    auto* remainderInput = digitInstruction(exitIt++->get(), Opc::LOAD);
    auto* remainder = digitInstruction(exitIt++->get(), Opc::SREM);
    auto* returnInstruction = digitInstruction(exitIt++->get(), Opc::RET);
    if (!remainderInput || remainderInput->getNumOperands() != 1 ||
        !remainder || remainder->getNumOperands() != 2 ||
        remainder->getOperand(0) != remainderInput ||
        !returnInstruction || returnInstruction->getNumOperands() != 1 ||
        returnInstruction->getOperand(0) != remainder) {
        return false;
    }

    IR::Value* valueSlot = divisionInput->getOperand(0);
    IR::Value* counterSlot = counterBodyLoad->getOperand(0);
    IR::Value* boundSlot = boundLoad->getOperand(0);
    if (remainderInput->getOperand(0) != valueSlot ||
        valueStore->getOperand(1) != valueSlot ||
        counterHeaderLoad->getOperand(0) != counterSlot ||
        counterStore->getOperand(1) != counterSlot ||
        valueSlot == counterSlot || valueSlot == boundSlot ||
        counterSlot == boundSlot ||
        !isEntryAlloca(valueSlot, entry) ||
        !isEntryAlloca(counterSlot, entry) ||
        !isEntryAlloca(boundSlot, entry)) {
        return false;
    }

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
    if (bits == 0 || bits >= 32 || 32 % bits != 0) return false;

    // Replacing the whole function is only valid after accounting for every
    // entry instruction. Required state is initialized exactly once. Extra
    // front-end temporaries may only be write-only local constants.
    std::unordered_set<IR::Value*> allocas;
    std::unordered_map<IR::Value*, IR::Value*> initialValues;
    for (const auto& owned : entry->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction == entryBranch) continue;
        if (instruction->getOpcode() == Opc::ALLOCA) {
            allocas.insert(instruction);
            continue;
        }
        if (instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            !allocas.count(instruction->getOperand(1)) ||
            initialValues.count(instruction->getOperand(1))) {
            return false;
        }
        initialValues.emplace(instruction->getOperand(1),
                              instruction->getOperand(0));
    }
    if (allocas.size() != initialValues.size() ||
        initialValues[valueSlot] != function->getArg(0) ||
        initialValues[boundSlot] != function->getArg(1) ||
        !isConstant(initialValues[counterSlot], 0)) {
        return false;
    }
    for (IR::Value* slot : allocas) {
        if (slot == valueSlot || slot == boundSlot || slot == counterSlot)
            continue;
        if (!dynamic_cast<IR::ConstantInt*>(initialValues[slot])) return false;
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
