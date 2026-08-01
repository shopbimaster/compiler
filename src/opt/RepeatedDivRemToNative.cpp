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
#include <unordered_set>
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

bool isLoadFrom(IR::Value* value, IR::Value* slot) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 && load->getOperand(0) == slot;
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

bool hasExactDirectUses(
    IR::Value* slot,
    std::initializer_list<IR::Instruction*> expectedLoads,
    std::initializer_list<IR::Instruction*> expectedStores) {
    std::unordered_set<IR::Instruction*> loads(
        expectedLoads.begin(), expectedLoads.end());
    std::unordered_set<IR::Instruction*> stores(
        expectedStores.begin(), expectedStores.end());
    for (const auto& use : slot->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) return false;
        if (instruction->getOpcode() == Opc::LOAD && use.operandNo == 0) {
            if (loads.erase(instruction) != 1) return false;
            continue;
        }
        if (instruction->getOpcode() == Opc::STORE && use.operandNo == 1) {
            if (stores.erase(instruction) != 1) return false;
            continue;
        }
        return false;
    }
    return loads.empty() && stores.empty();
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
    std::vector<IR::Instruction*> returns;
    unsigned conditionalBranches = 0;
    for (auto& block : function->getBlocks()) {
        for (auto& inst : block->getInstructions()) {
            switch (inst->getOpcode()) {
            case Opc::SDIV: divisions.push_back(inst.get()); break;
            case Opc::SREM: remainders.push_back(inst.get()); break;
            case Opc::ADD: additions.push_back(inst.get()); break;
            case Opc::ICMP: comparisons.push_back(inst.get()); break;
            case Opc::RET: returns.push_back(inst.get()); break;
            case Opc::COND_BR: ++conditionalBranches; break;
            case Opc::CALL:
            case Opc::PHI:
            case Opc::SELECT:
                return false;
            case Opc::LOAD: {
                if (inst->getNumOperands() != 1) return false;
                auto* pointer = dynamic_cast<IR::Instruction*>(
                    inst->getOperand(0));
                if (!pointer || pointer->getOpcode() != Opc::ALLOCA) {
                    return false;
                }
                break;
            }
            case Opc::STORE: {
                if (inst->getNumOperands() != 2) return false;
                auto* pointer = dynamic_cast<IR::Instruction*>(
                    inst->getOperand(1));
                if (!pointer || pointer->getOpcode() != Opc::ALLOCA) {
                    return false;
                }
                break;
            }
            case Opc::ALLOCA:
            case Opc::BR:
                break;
            default:
                return false;
            }
        }
    }
    if (function->getBlocks().size() != 4 ||
        divisions.size() != 1 || remainders.size() != 1 ||
        additions.size() != 1 || comparisons.size() != 1 ||
        returns.size() != 1 || conditionalBranches != 1) {
        return false;
    }
    auto* entry = function->getEntryBlock();
    if (!entry) return false;

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
    auto* valueAlloca = dynamic_cast<IR::Instruction*>(valueSlot);
    if (!valueAlloca || valueAlloca->getOpcode() != Opc::ALLOCA ||
        remainderInput->getOperand(0) != valueSlot ||
        !isDirectlyReturned(function, remainder)) {
        return false;
    }

    IR::Value* counterSlot = nullptr;
    IR::Instruction* increment = nullptr;
    IR::Instruction* incrementLoad = nullptr;
    for (auto* addition : additions) {
        IR::Value* loadedCounter = nullptr;
        if (isConstant(addition->getOperand(0), 1))
            loadedCounter = addition->getOperand(1);
        else if (isConstant(addition->getOperand(1), 1))
            loadedCounter = addition->getOperand(0);
        else
            continue;

        auto* load = dynamic_cast<IR::Instruction*>(loadedCounter);
        if (!load || load->getOpcode() != Opc::LOAD ||
            load->getNumOperands() != 1) {
            continue;
        }
        IR::Value* candidateSlot = load->getOperand(0);
        auto* candidateAlloca = dynamic_cast<IR::Instruction*>(candidateSlot);
        if (!candidateAlloca || candidateAlloca->getOpcode() != Opc::ALLOCA)
            continue;
        if (increment) return false;
        increment = addition;
        incrementLoad = load;
        counterSlot = candidateSlot;
    }
    if (!increment || !counterSlot || counterSlot == valueSlot)
        return false;

    IR::Instruction* loopCondition = nullptr;
    IR::Instruction* loopBranch = nullptr;
    IR::Instruction* conditionCounterLoad = nullptr;
    IR::Instruction* positionLoad = nullptr;
    IR::Value* positionSlot = nullptr;
    for (auto* comparison : comparisons) {
        if (comparison->getName() != "slt" ||
            comparison->getNumOperands() != 2) {
            continue;
        }
        auto* controllingBranch =
            findControllingBranch(function, comparison);
        if (!controllingBranch) continue;
        auto* candidateCounterLoad = dynamic_cast<IR::Instruction*>(
            comparison->getOperand(0));
        IR::Value* bound = comparison->getOperand(1);
        auto* candidatePositionLoad = dynamic_cast<IR::Instruction*>(bound);
        bool exactPosition = bound == function->getArg(1);
        if (candidatePositionLoad &&
            candidatePositionLoad->getOpcode() == Opc::LOAD &&
            candidatePositionLoad->getNumOperands() == 1) {
            positionSlot = candidatePositionLoad->getOperand(0);
            auto* positionAlloca =
                dynamic_cast<IR::Instruction*>(positionSlot);
            exactPosition = positionAlloca &&
                positionAlloca->getOpcode() == Opc::ALLOCA;
        }
        bool counterFirst = candidateCounterLoad &&
            isLoadFrom(candidateCounterLoad, counterSlot) && exactPosition;
        if (counterFirst) {
            loopCondition = comparison;
            loopBranch = controllingBranch;
            conditionCounterLoad = candidateCounterLoad;
            positionLoad = candidatePositionLoad;
            break;
        }
    }
    if (!loopCondition || !loopBranch ||
        increment->getParent() != division->getParent()) {
        return false;
    }

    auto* loopBody = division->getParent();
    auto* loopHeader = loopCondition->getParent();
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
    auto* entryTerminator = entry ? entry->getTerminator() : nullptr;
    if (!entry || entry == loopHeader || entry == loopBody ||
        entry == falseTarget ||
        !entryTerminator || entryTerminator->getOpcode() != Opc::BR ||
        entryTerminator->getNumOperands() != 1 ||
        entryTerminator->getOperand(0) != loopHeader ||
        returns.front()->getParent() != falseTarget ||
        returns.front()->getNumOperands() != 1 ||
        returns.front()->getOperand(0) != remainder) {
        return false;
    }
    auto* bodyTerminator = loopBody->getTerminator();
    if (!bodyTerminator || bodyTerminator->getOpcode() != Opc::BR ||
        bodyTerminator->getNumOperands() != 1 ||
        bodyTerminator->getOperand(0) != loopCondition->getParent()) {
        return false;
    }

    IR::Instruction* valueInitializer = nullptr;
    IR::Instruction* valueUpdate = nullptr;
    IR::Instruction* counterInitializer = nullptr;
    IR::Instruction* counterUpdate = nullptr;
    IR::Instruction* positionInitializer = nullptr;
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            auto* storedValue = instruction->getOperand(0);
            auto* destination = instruction->getOperand(1);
            if (destination == valueSlot) {
                if (storedValue == function->getArg(0) &&
                    instruction->getParent() == entry && !valueInitializer) {
                    valueInitializer = instruction;
                } else if (storedValue == division &&
                           instruction->getParent() == loopBody &&
                           !valueUpdate) {
                    valueUpdate = instruction;
                } else {
                    return false;
                }
            } else if (destination == counterSlot) {
                if (isConstant(storedValue, 0) &&
                    instruction->getParent() == entry && !counterInitializer) {
                    counterInitializer = instruction;
                } else if (storedValue == increment &&
                           instruction->getParent() == loopBody &&
                           !counterUpdate) {
                    counterUpdate = instruction;
                } else {
                    return false;
                }
            } else if (positionSlot && destination == positionSlot) {
                if (storedValue == function->getArg(1) &&
                    instruction->getParent() == entry &&
                    !positionInitializer) {
                    positionInitializer = instruction;
                } else {
                    return false;
                }
            }
        }
    }
    if (!valueInitializer || !valueUpdate || !counterInitializer ||
        !counterUpdate || !incrementLoad || !conditionCounterLoad ||
        !hasExactDirectUses(
            valueSlot, {divisionInput, remainderInput},
            {valueInitializer, valueUpdate}) ||
        !hasExactDirectUses(
            counterSlot, {incrementLoad, conditionCounterLoad},
            {counterInitializer, counterUpdate})) {
        return false;
    }
    if (positionSlot &&
        (!positionInitializer || positionSlot == valueSlot ||
         positionSlot == counterSlot ||
         !hasExactDirectUses(
             positionSlot, {positionLoad}, {positionInitializer}))) {
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
