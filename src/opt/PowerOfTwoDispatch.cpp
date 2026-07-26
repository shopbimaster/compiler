#include "opt/Optimizer.h"

#include <algorithm>
#include <unordered_set>

namespace Opt {
namespace {

using Opcode = IR::Instruction::Opcode;

enum class DispatchKind {
    LeftShift,
    SignedDivide,
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
                            int64_t expected, IR::Instruction*& branch) {
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

    return (loadedPointer(compare->getOperand(0)) == amountSlot &&
            isConstant(compare->getOperand(1), expected)) ||
           (loadedPointer(compare->getOperand(1)) == amountSlot &&
            isConstant(compare->getOperand(0), expected));
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

bool matchPowerOfTwoDispatch(IR::Function* function, DispatchKind& kind) {
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
        }
    }

    bool kindInitialized = false;
    std::unordered_set<IR::BasicBlock*> visited;
    IR::BasicBlock* current = entry;

    for (int amount = 1; amount <= 8; ++amount) {
        if (!current || !visited.insert(current).second) return false;

        IR::Instruction* branch = nullptr;
        if (!matchDispatchCondition(current, amountSlot, amount, branch)) {
            return false;
        }

        auto* operationBlock = branchTarget(branch, 1);
        if (!operationBlock || !visited.insert(operationBlock).second ||
            !matchOperationReturn(operationBlock, valueSlot, amount,
                                  kind, kindInitialized)) {
            return false;
        }

        current = skipEmptyTrampolines(branchTarget(branch, 2), visited);
    }

    if (!kindInitialized || !current || !visited.insert(current).second ||
        !matchDefaultReturn(current, valueSlot)) {
        return false;
    }
    return visited.size() == function->getBlocks().size();
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

void buildDynamicDispatch(IR::Function* function, DispatchKind kind) {
    auto* entry = function->getEntryBlock();
    clearFunctionBody(function, entry);

    auto* value = function->getArg(0);
    auto* amount = function->getArg(1);
    auto* i32 = IR::IntegerType::I32;
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* eight = IR::ConstantInt::get(i32, 8);
    auto* thirtyOne = IR::ConstantInt::get(i32, 31);

    auto* dynamicBlock = function->createBlock("pow2.dynamic");
    auto* defaultBlock = function->createBlock("pow2.default");

    auto* atLeastOne = IR::Instruction::createCmp(
        Opcode::ICMP, amount, one, "sge");
    auto* atMostEight = IR::Instruction::createCmp(
        Opcode::ICMP, amount, eight, "sle");
    auto* inRange = IR::Instruction::createBinOp(
        Opcode::AND, IR::IntegerType::I1, "pow2.inrange",
        atLeastOne, atMostEight);
    entry->pushBack(atLeastOne);
    entry->pushBack(atMostEight);
    entry->pushBack(inRange);
    entry->pushBack(IR::Instruction::createCondBr(
        inRange, dynamicBlock, defaultBlock));

    if (kind == DispatchKind::LeftShift) {
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
        DispatchKind kind;
        if (!matchPowerOfTwoDispatch(function.get(), kind)) continue;
        buildDynamicDispatch(function.get(), kind);
        changed = true;
    }
    return changed;
}

} // namespace Opt
