// ================================================================
// src/opt/PowerOfTwoDispatch.cpp — 2 的幂次 switch 分派化简
// ----------------------------------------------------------------
// 所属模块：opt（O2 结构化变换）
// 关键依赖：opt/Optimizer.h
// ================================================================

#include "opt/Optimizer.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opcode = IR::Instruction::Opcode;

enum class DispatchKind {
    LeftShift,
    SignedDivide,
};

struct DispatchMatch {
    DispatchKind kind = DispatchKind::LeftShift;
    int minimumAmount = 0;
    int maximumAmount = 0;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

IR::Value* loadedPointer(IR::Value* value) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    if (!load || load->getOpcode() != Opcode::LOAD ||
        load->getNumOperands() != 1) {
        return nullptr;
    }
    return load->getOperand(0);
}

bool containsInstruction(IR::BasicBlock* block, IR::Instruction* target) {
    if (!block || !target) return false;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction.get() == target) return true;
    }
    return false;
}

IR::Value* findArgumentSlot(IR::BasicBlock* entry, IR::Argument* argument) {
    for (const auto& instruction : entry->getInstructions()) {
        if (instruction->getOpcode() == Opcode::STORE &&
            instruction->getNumOperands() == 2 &&
            instruction->getOperand(0) == argument) {
            auto* slot = dynamic_cast<IR::Instruction*>(
                instruction->getOperand(1));
            if (slot && slot->getOpcode() == Opcode::ALLOCA) {
                return slot;
            }
        }
    }
    return nullptr;
}

IR::BasicBlock* branchTarget(IR::Instruction* branch, unsigned index) {
    if (!branch || branch->getNumOperands() <= index) return nullptr;
    return dynamic_cast<IR::BasicBlock*>(branch->getOperand(index));
}

IR::BasicBlock* skipEmptyTrampolines(
    IR::BasicBlock* block,
    std::unordered_set<IR::BasicBlock*>& visited) {
    while (block) {
        auto* terminator = block->getTerminator();
        if (!terminator || terminator->getOpcode() != Opcode::BR ||
            block->size() != 1) {
            return block;
        }
        if (!visited.insert(block).second) return nullptr;
        block = branchTarget(terminator, 0);
    }
    return nullptr;
}

bool matchDispatchCondition(IR::BasicBlock* block, IR::Value* amountSlot,
                            int& amount, IR::Instruction*& branch) {
    if (!block) return false;
    branch = block ? block->getTerminator() : nullptr;
    if (!branch || branch->getOpcode() != Opcode::COND_BR ||
        branch->getNumOperands() != 3) {
        return false;
    }

    auto* compare = dynamic_cast<IR::Instruction*>(branch->getOperand(0));
    if (!compare || compare->getOpcode() != Opcode::ICMP ||
        compare->getName() != "eq" || compare->getNumOperands() != 2 ||
        !containsInstruction(block, compare)) {
        return false;
    }

    IR::ConstantInt* constant = nullptr;
    IR::Instruction* load = nullptr;
    if (loadedPointer(compare->getOperand(0)) == amountSlot) {
        load = dynamic_cast<IR::Instruction*>(compare->getOperand(0));
        constant = dynamic_cast<IR::ConstantInt*>(compare->getOperand(1));
    } else if (loadedPointer(compare->getOperand(1)) == amountSlot) {
        load = dynamic_cast<IR::Instruction*>(compare->getOperand(1));
        constant = dynamic_cast<IR::ConstantInt*>(compare->getOperand(0));
    }
    if (!load || load->getParent() != block || !constant ||
        constant->getValue() < 0 || constant->getValue() > 30) {
        return false;
    }

    amount = static_cast<int>(constant->getValue());
    return true;
}

bool matchOperationReturn(IR::BasicBlock* block, IR::Value* valueSlot,
                          int shiftAmount, DispatchKind& kind,
                          bool& kindInitialized) {
    if (!block || block->size() != 3) return false;

    IR::Instruction* load = nullptr;
    IR::Instruction* operation = nullptr;
    IR::Instruction* ret = nullptr;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opcode::LOAD) {
            if (load) return false;
            load = instruction.get();
        } else if (instruction->getOpcode() == Opcode::MUL ||
                   instruction->getOpcode() == Opcode::SDIV) {
            if (operation) return false;
            operation = instruction.get();
        } else if (instruction->getOpcode() == Opcode::RET) {
            if (ret) return false;
            ret = instruction.get();
        } else {
            return false;
        }
    }
    if (!load || !operation || !ret ||
        load->getNumOperands() != 1 ||
        load->getOperand(0) != valueSlot ||
        operation->getNumOperands() != 2 ||
        ret->getNumOperands() != 1 ||
        ret->getOperand(0) != operation) {
        return false;
    }

    int64_t factor = int64_t{1} << shiftAmount;
    DispatchKind operationKind;
    if (operation->getOpcode() == Opcode::MUL &&
        ((operation->getOperand(0) == load &&
          isConstant(operation->getOperand(1), factor)) ||
         (operation->getOperand(1) == load &&
          isConstant(operation->getOperand(0), factor)))) {
        operationKind = DispatchKind::LeftShift;
    } else if (operation->getOpcode() == Opcode::SDIV &&
               operation->getOperand(0) == load &&
               isConstant(operation->getOperand(1), factor)) {
        operationKind = DispatchKind::SignedDivide;
    } else {
        return false;
    }

    if (kindInitialized && kind != operationKind) return false;
    kind = operationKind;
    kindInitialized = true;
    return true;
}

bool matchDefaultReturn(IR::BasicBlock* block, IR::Value* valueSlot) {
    if (!block || block->size() != 2) return false;
    auto iterator = block->getInstructions().begin();
    auto* load = iterator->get();
    ++iterator;
    auto* ret = iterator->get();
    return load->getOpcode() == Opcode::LOAD &&
           load->getNumOperands() == 1 &&
           load->getOperand(0) == valueSlot &&
           ret->getOpcode() == Opcode::RET &&
           ret->getNumOperands() == 1 &&
           ret->getOperand(0) == load;
}

bool matchPowerOfTwoDispatch(IR::Function* function, DispatchMatch& match) {
    if (!function || function->isExternal() || function->getNumArgs() != 2) {
        return false;
    }
    auto* functionType = function->getFunctionType();
    if (!functionType ||
        functionType->getReturnType() != IR::IntegerType::I32 ||
        function->getArg(0)->getType() != IR::IntegerType::I32 ||
        function->getArg(1)->getType() != IR::IntegerType::I32) {
        return false;
    }

    auto* entry = function->getEntryBlock();
    if (!entry) return false;
    auto* valueSlot = findArgumentSlot(entry, function->getArg(0));
    auto* amountSlot = findArgumentSlot(entry, function->getArg(1));
    if (!valueSlot || !amountSlot || valueSlot == amountSlot) return false;

    int argumentStoreCount = 0;
    for (const auto& block : function->getBlocks()) {
        for (const auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opcode::CALL) return false;
            if (instruction->getOpcode() == Opcode::STORE &&
                !(instruction->getNumOperands() == 2 &&
                  ((instruction->getOperand(0) == function->getArg(0) &&
                    instruction->getOperand(1) == valueSlot) ||
                   (instruction->getOperand(0) == function->getArg(1) &&
                    instruction->getOperand(1) == amountSlot)))) {
                return false;
            }
            if (instruction->getOpcode() == Opcode::STORE) {
                ++argumentStoreCount;
            } else if (instruction->getOpcode() == Opcode::ALLOCA) {
                if (instruction.get() != valueSlot &&
                    instruction.get() != amountSlot) {
                    return false;
                }
            } else if (instruction->getOpcode() == Opcode::LOAD) {
                if (instruction->getNumOperands() != 1 ||
                    (instruction->getOperand(0) != valueSlot &&
                     instruction->getOperand(0) != amountSlot)) {
                    return false;
                }
            } else if (instruction->getOpcode() != Opcode::ICMP &&
                       instruction->getOpcode() != Opcode::MUL &&
                       instruction->getOpcode() != Opcode::SDIV &&
                       instruction->getOpcode() != Opcode::RET &&
                       instruction->getOpcode() != Opcode::BR &&
                       instruction->getOpcode() != Opcode::COND_BR) {
                return false;
            }
        }
    }
    if (argumentStoreCount != 2) return false;

    bool kindInitialized = false;
    DispatchKind kind = DispatchKind::LeftShift;
    std::unordered_set<IR::BasicBlock*> visited;
    std::unordered_set<int> seenAmounts;
    std::vector<int> amounts;
    IR::BasicBlock* current = entry;

    while (current && !matchDefaultReturn(current, valueSlot)) {
        if (!current || !visited.insert(current).second) return false;

        // The entry additionally owns the two argument allocas and their
        // initial stores. Every later condition block must contain exactly
        // one load, one comparison, and one conditional branch. Together
        // with the opcode closure above, this proves that no discarded
        // computation or side effect is hidden in the dispatch chain.
        const size_t expectedConditionSize = current == entry ? 7 : 3;
        if (current->size() != expectedConditionSize) return false;

        IR::Instruction* branch = nullptr;
        int amount = 0;
        if (!matchDispatchCondition(current, amountSlot, amount, branch) ||
            !seenAmounts.insert(amount).second) {
            return false;
        }
        amounts.push_back(amount);

        auto* operationBlock = branchTarget(branch, 1);
        if (!operationBlock || !visited.insert(operationBlock).second ||
            !matchOperationReturn(operationBlock, valueSlot, amount,
                                  kind, kindInitialized)) {
            return false;
        }

        current = skipEmptyTrampolines(branchTarget(branch, 2), visited);
        if (amounts.size() > 31) return false;
    }

    if (!kindInitialized || amounts.size() < 2 || !current ||
        !visited.insert(current).second ||
        !matchDefaultReturn(current, valueSlot)) {
        return false;
    }
    std::sort(amounts.begin(), amounts.end());
    for (size_t index = 1; index < amounts.size(); ++index) {
        if (amounts[index] != amounts[index - 1] + 1) return false;
    }
    if (visited.size() != function->getBlocks().size()) return false;

    match.kind = kind;
    match.minimumAmount = amounts.front();
    match.maximumAmount = amounts.back();
    return true;
}

void clearFunctionBody(IR::Function* function, IR::BasicBlock* entry) {
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
            auto iterator = block->begin();
            block->erase(iterator);
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
}

void buildDynamicDispatch(IR::Function* function, const DispatchMatch& match) {
    auto* entry = function->getEntryBlock();
    clearFunctionBody(function, entry);

    auto* value = function->getArg(0);
    auto* amount = function->getArg(1);
    auto* i32 = IR::IntegerType::I32;
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* minimum = IR::ConstantInt::get(i32, match.minimumAmount);
    auto* maximum = IR::ConstantInt::get(i32, match.maximumAmount);
    auto* thirtyOne = IR::ConstantInt::get(i32, 31);

    auto* dynamicBlock = function->createBlock("pow2.dynamic");
    auto* defaultBlock = function->createBlock("pow2.default");

    auto* atLeastMinimum = IR::Instruction::createCmp(
        Opcode::ICMP, amount, minimum, "sge");
    auto* atMostMaximum = IR::Instruction::createCmp(
        Opcode::ICMP, amount, maximum, "sle");
    auto* inRange = IR::Instruction::createBinOp(
        Opcode::AND, IR::IntegerType::I1, "pow2.inrange",
        atLeastMinimum, atMostMaximum);
    entry->pushBack(atLeastMinimum);
    entry->pushBack(atMostMaximum);
    entry->pushBack(inRange);
    entry->pushBack(IR::Instruction::createCondBr(
        inRange, dynamicBlock, defaultBlock));

    if (match.kind == DispatchKind::LeftShift) {
        auto* shifted = IR::Instruction::createBinOp(
            Opcode::SHL, i32, "pow2.shift", value, amount);
        dynamicBlock->pushBack(shifted);
        dynamicBlock->pushBack(IR::Instruction::createRet(shifted));
    } else {
        auto* sign = IR::Instruction::createBinOp(
            Opcode::ASHR, i32, "pow2.sign", value, thirtyOne);
        auto* divisor = IR::Instruction::createBinOp(
            Opcode::SHL, i32, "pow2.divisor", one, amount);
        auto* mask = IR::Instruction::createBinOp(
            Opcode::SUB, i32, "pow2.mask", divisor, one);
        auto* correction = IR::Instruction::createBinOp(
            Opcode::AND, i32, "pow2.correction", sign, mask);
        auto* corrected = IR::Instruction::createBinOp(
            Opcode::ADD, i32, "pow2.corrected", value, correction);
        auto* quotient = IR::Instruction::createBinOp(
            Opcode::ASHR, i32, "pow2.quotient", corrected, amount);
        dynamicBlock->pushBack(sign);
        dynamicBlock->pushBack(divisor);
        dynamicBlock->pushBack(mask);
        dynamicBlock->pushBack(correction);
        dynamicBlock->pushBack(corrected);
        dynamicBlock->pushBack(quotient);
        dynamicBlock->pushBack(IR::Instruction::createRet(quotient));
    }

    defaultBlock->pushBack(IR::Instruction::createRet(value));
}

} // namespace

bool powerOfTwoDispatchSimplification(IR::Module* module) {
    bool changed = false;
    for (auto& function : module->getFunctions()) {
        DispatchMatch match;
        if (!matchPowerOfTwoDispatch(function.get(), match)) continue;
        buildDynamicDispatch(function.get(), match);
        changed = true;
    }
    return changed;
}

} // namespace Opt
