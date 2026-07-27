// ================================================================
// Repeated division/remainder digit extraction to native shifts
//
// Recognizes:
//   while (i < position) {
//       value = value / BASE;
//       i = i + 1;
//   }
//   return value % BASE;
//
// for a power-of-two BASE whose log2 divides 32. The loop is replaced
// with a constant-size signed digit extraction. This preserves the
// original behavior for negative values, position <= 0, INT_MIN, and
// positions large enough to reduce every i32 value to zero.
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Opt {
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

bool isLoadFrom(IR::Value* value, IR::Value* slot) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 && load->getOperand(0) == slot;
}

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

bool isDirectlyReturned(IR::Function* function, IR::Value* value) {
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            if (inst->getOpcode() == Opc::RET &&
                inst->getNumOperands() == 1 &&
                inst->getOperand(0) == value) {
                return true;
            }
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

} // namespace Opt
