#include "opt/Optimizer.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;
using ArgumentSlots = std::unordered_map<IR::Value*, IR::Argument*>;

constexpr int kImplementationRadixBits = 10;
constexpr int kImplementationRadixSize = 1 << kImplementationRadixBits;
constexpr int kImplementationRadixMask = kImplementationRadixSize - 1;

struct DigitProof {
    IR::Function* function = nullptr;
    int radix = 0;
    unsigned bitsPerDigit = 0;
    IR::Instruction* numberSlot = nullptr;
    IR::Instruction* roundSlot = nullptr;
    IR::Instruction* inductionSlot = nullptr;
};

struct RadixProof {
    IR::Function* sortFunction = nullptr;
    IR::Function* digitFunction = nullptr;
    std::vector<IR::Instruction*> externalCalls;
    IR::ArrayType* dataType = nullptr;
    IR::Instruction* countArray = nullptr;
    IR::Instruction* headArray = nullptr;
    IR::Instruction* tailArray = nullptr;
    int sourceRadix = 0;
    unsigned bitsPerDigit = 0;
    unsigned minimumFullRound = 0;
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

// 前置声明，定义见后
IR::Argument* rootArgument(
    IR::Value* value, const ArgumentSlots& slots);

IR::Instruction* loadedSlot(IR::Value* value) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    if (!load || load->getOpcode() != Opc::LOAD ||
        load->getNumOperands() != 1) {
        return nullptr;
    }
    auto* slot = dynamic_cast<IR::Instruction*>(load->getOperand(0));
    return slot && slot->getOpcode() == Opc::ALLOCA ? slot : nullptr;
}

bool sameValueOrSlotLoad(IR::Value* left, IR::Value* right) {
    if (left == right) return true;
    auto* leftSlot = loadedSlot(left);
    auto* rightSlot = loadedSlot(right);
    return leftSlot && leftSlot == rightSlot;
}

// Prove that the guard terminates only after the least-significant digit.
bool provesExhaustedRoundGuard(
    IR::Instruction* icmp, IR::Function* function,
    const ArgumentSlots& slots) {
    if (icmp->getOpcode() != Opc::ICMP || icmp->getNumOperands() != 2)
        return false;
    const auto& name = icmp->getName();
    auto* leftArg = rootArgument(icmp->getOperand(0), slots);
    auto* rightArg = rootArgument(icmp->getOperand(1), slots);
    auto* round = function->getArg(0);
    if (name == "eq") {
        return (leftArg == round && isConstant(icmp->getOperand(1), -1)) ||
               (rightArg == round && isConstant(icmp->getOperand(0), -1));
    }
    if (name == "slt")
        return leftArg == round && isConstant(icmp->getOperand(1), 0);
    if (name == "sgt")
        return rightArg == round && isConstant(icmp->getOperand(0), 0);
    if (name == "sle")
        return leftArg == round && isConstant(icmp->getOperand(1), -1);
    if (name == "sge")
        return rightArg == round && isConstant(icmp->getOperand(0), -1);
    return false;
}

// Prove the trivial-range condition left + 1 >= right.
bool provesTrivialRangeGuard(
    IR::Instruction* icmp, IR::Function* function,
    const ArgumentSlots& slots) {
    if (icmp->getOpcode() != Opc::ICMP || icmp->getNumOperands() != 2)
        return false;
    auto* left = function->getArg(2);
    auto* right = function->getArg(3);
    auto* lhs = icmp->getOperand(0);
    auto* rhs = icmp->getOperand(1);
    auto isLeftPlusOne = [&](IR::Value* value) {
        auto* add = dynamic_cast<IR::Instruction*>(value);
        if (!add || add->getOpcode() != Opc::ADD ||
            add->getNumOperands() != 2) {
            return false;
        }
        return (rootArgument(add->getOperand(0), slots) == left &&
                isConstant(add->getOperand(1), 1)) ||
               (rootArgument(add->getOperand(1), slots) == left &&
                isConstant(add->getOperand(0), 1));
    };
    if (icmp->getName() == "sge" &&
        rootArgument(rhs, slots) == right) {
        return isLeftPlusOne(lhs);
    }
    if (icmp->getName() == "sle" &&
        rootArgument(lhs, slots) == right) {
        return isLeftPlusOne(rhs);
    }
    return false;
}

// A recursive digit step must move to the immediately preceding digit.
bool provesPreviousRound(
    IR::Instruction* instruction, IR::Argument* roundArg,
    const ArgumentSlots& slots) {
    if (!instruction || instruction->getNumOperands() != 2) return false;
    if (rootArgument(instruction->getOperand(0), slots) != roundArg)
        return false;

    auto* constOp =
        dynamic_cast<IR::ConstantInt*>(instruction->getOperand(1));
    if (!constOp) return false;

    return (instruction->getOpcode() == Opc::SUB &&
            constOp->getValue() == 1) ||
           (instruction->getOpcode() == Opc::ADD &&
            constOp->getValue() == -1);
}

IR::Function* calledFunction(IR::Instruction* instruction) {
    if (!instruction ||
        instruction->getOpcode() != Opc::CALL ||
        instruction->getNumOperands() == 0) {
        return nullptr;
    }
    return dynamic_cast<IR::Function*>(instruction->getOperand(0));
}

ArgumentSlots buildArgumentSlots(IR::Function* function) {
    ArgumentSlots result;
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            auto* argument =
                dynamic_cast<IR::Argument*>(instruction->getOperand(0));
            auto* slot =
                dynamic_cast<IR::Instruction*>(instruction->getOperand(1));
            if (argument && slot && slot->getOpcode() == Opc::ALLOCA) {
                result[slot] = argument;
            }
        }
    }
    return result;
}

IR::Argument* rootArgumentImpl(
    IR::Value* value, const ArgumentSlots& slots,
    std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return nullptr;
    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        visiting.erase(value);
        return argument;
    }

    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction) {
        visiting.erase(value);
        return nullptr;
    }

    if (instruction->getOpcode() == Opc::LOAD &&
        instruction->getNumOperands() == 1) {
        auto found = slots.find(instruction->getOperand(0));
        if (found != slots.end()) {
            visiting.erase(value);
            return found->second;
        }
    }

    if (instruction->getOpcode() == Opc::GETELEMENTPTR &&
        instruction->getNumOperands() >= 2) {
        auto* result = rootArgumentImpl(
            instruction->getOperand(0), slots, visiting);
        visiting.erase(value);
        return result;
    }

    visiting.erase(value);
    return nullptr;
}

IR::Argument* rootArgument(
    IR::Value* value, const ArgumentSlots& slots) {
    std::unordered_set<IR::Value*> visiting;
    return rootArgumentImpl(value, slots, visiting);
}

IR::Instruction* rootAllocaImpl(
    IR::Value* value, std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return nullptr;
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction) {
        visiting.erase(value);
        return nullptr;
    }
    if (instruction->getOpcode() == Opc::ALLOCA) {
        visiting.erase(value);
        return instruction;
    }
    if ((instruction->getOpcode() == Opc::LOAD ||
         instruction->getOpcode() == Opc::GETELEMENTPTR) &&
        instruction->getNumOperands() >= 1) {
        auto* result =
            rootAllocaImpl(instruction->getOperand(0), visiting);
        visiting.erase(value);
        return result;
    }
    visiting.erase(value);
    return nullptr;
}

IR::Instruction* rootAlloca(IR::Value* value) {
    std::unordered_set<IR::Value*> visiting;
    return rootAllocaImpl(value, visiting);
}

struct ArrayElementAccess {
    IR::Instruction* array = nullptr;
    IR::Value* index = nullptr;
    IR::Instruction* gep = nullptr;
};

std::optional<ArrayElementAccess> arrayElementPointer(IR::Value* value) {
    auto* gep = dynamic_cast<IR::Instruction*>(value);
    if (!gep || gep->getOpcode() != Opc::GETELEMENTPTR ||
        gep->getNumOperands() < 2) {
        return std::nullopt;
    }
    auto* array = rootAlloca(gep->getOperand(0));
    if (!array) return std::nullopt;
    return ArrayElementAccess{
        array, gep->getOperand(gep->getNumOperands() - 1), gep};
}

std::optional<ArrayElementAccess> loadedArrayElement(IR::Value* value) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    if (!load || load->getOpcode() != Opc::LOAD ||
        load->getNumOperands() != 1) {
        return std::nullopt;
    }
    return arrayElementPointer(load->getOperand(0));
}

bool isDigitCall(
    IR::Value* value, IR::Function* digitFunction,
    IR::Argument* roundArgument, IR::Argument* dataArgument,
    const ArgumentSlots& slots, IR::Instruction** indexSlot = nullptr) {
    auto* call = dynamic_cast<IR::Instruction*>(value);
    if (!call || calledFunction(call) != digitFunction ||
        call->getNumOperands() != 3 ||
        rootArgument(call->getOperand(2), slots) != roundArgument) {
        return false;
    }

    auto* number = dynamic_cast<IR::Instruction*>(call->getOperand(1));
    if (!number || number->getOpcode() != Opc::LOAD ||
        number->getNumOperands() != 1) {
        return false;
    }
    auto* address = dynamic_cast<IR::Instruction*>(number->getOperand(0));
    if (!address || address->getOpcode() != Opc::GETELEMENTPTR ||
        address->getNumOperands() < 2 ||
        rootArgument(address->getOperand(0), slots) != dataArgument) {
        return false;
    }
    auto* slot = loadedSlot(
        address->getOperand(address->getNumOperands() - 1));
    if (!slot) return false;
    if (indexSlot) *indexSlot = slot;
    return true;
}

bool hasSlotInitialization(
    IR::Function* function, IR::Instruction* slot,
    IR::Value* expected, const ArgumentSlots& slots) {
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2 ||
                instruction->getOperand(1) != slot) {
                continue;
            }
            if (instruction->getOperand(0) == expected ||
                (dynamic_cast<IR::Argument*>(expected) &&
                 rootArgument(instruction->getOperand(0), slots) == expected)) {
                return true;
            }
        }
    }
    return false;
}

bool provesSlotIncrementInBlock(
    IR::BasicBlock* block, IR::Instruction* slot) {
    if (!block || !slot) return false;
    for (auto& owned : block->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction->getOpcode() != Opc::STORE ||
            instruction->getNumOperands() != 2 ||
            instruction->getOperand(1) != slot) {
            continue;
        }
        auto* add = dynamic_cast<IR::Instruction*>(instruction->getOperand(0));
        if (!add || add->getOpcode() != Opc::ADD ||
            add->getNumOperands() != 2) {
            continue;
        }
        if ((loadedSlot(add->getOperand(0)) == slot &&
             isConstant(add->getOperand(1), 1)) ||
            (loadedSlot(add->getOperand(1)) == slot &&
             isConstant(add->getOperand(0), 1))) {
            return true;
        }
    }
    return false;
}

bool provesSlotUpperBound(
    IR::Function* function, IR::Instruction* slot,
    IR::Value* bound, const ArgumentSlots& slots) {
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* compare = owned.get();
            if (compare->getOpcode() != Opc::ICMP ||
                compare->getName() != "slt" ||
                compare->getNumOperands() != 2) {
                continue;
            }
            bool leftIsSlot = loadedSlot(compare->getOperand(0)) == slot;
            bool rightIsBound = compare->getOperand(1) == bound ||
                rootArgument(compare->getOperand(1), slots) == bound;
            if (leftIsSlot && rightIsBound) return true;
        }
    }
    return false;
}

IR::GlobalVariable* exactGlobalArrayBase(IR::Value* value) {
    auto* gep = dynamic_cast<IR::Instruction*>(value);
    if (!gep || gep->getOpcode() != Opc::GETELEMENTPTR ||
        gep->getNumOperands() < 2) {
        return nullptr;
    }
    auto* global =
        dynamic_cast<IR::GlobalVariable*>(gep->getOperand(0));
    if (!global) return nullptr;
    for (unsigned index = 1;
         index < gep->getNumOperands(); ++index) {
        if (!isConstant(gep->getOperand(index), 0)) {
            return nullptr;
        }
    }
    return global;
}

std::vector<IR::Instruction*> collectCalls(
    IR::Module* module, IR::Function* callee) {
    std::vector<IR::Instruction*> calls;
    for (auto& function : module->getFunctions()) {
        for (auto& block : function->getBlocks()) {
            for (auto& instruction : block->getInstructions()) {
                if (calledFunction(instruction.get()) == callee) {
                    calls.push_back(instruction.get());
                }
            }
        }
    }
    return calls;
}

bool branchTargets(IR::Instruction* terminator, IR::BasicBlock* target) {
    if (!terminator || !target) return false;
    if (terminator->getOpcode() != Opc::BR &&
        terminator->getOpcode() != Opc::COND_BR) {
        return false;
    }
    for (unsigned index = 0;
         index < terminator->getNumOperands(); ++index) {
        if (terminator->getOperand(index) == target) return true;
    }
    return false;
}

bool proveDigitFunction(
    IR::Function* function, DigitProof& proof) {
    if (!function || function->isExternal() ||
        function->getNumArgs() != 2) {
        return false;
    }
    auto* type = function->getFunctionType();
    const auto& parameters = type->getParamTypes();
    if (type->getReturnType() != IR::IntegerType::I32 ||
        parameters.size() != 2 ||
        parameters[0] != IR::IntegerType::I32 ||
        parameters[1] != IR::IntegerType::I32) {
        return false;
    }

    auto slots = buildArgumentSlots(function);
    IR::Instruction* division = nullptr;
    IR::Instruction* remainder = nullptr;
    IR::Instruction* returned = nullptr;
    IR::Instruction* inductionSlot = nullptr;
    IR::Instruction* loopCompare = nullptr;
    unsigned loopCompares = 0;
    unsigned calls = 0;

    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            switch (instruction->getOpcode()) {
            case Opc::SDIV:
                if (division) return false;
                division = instruction;
                break;
            case Opc::SREM:
                if (remainder) return false;
                remainder = instruction;
                break;
            case Opc::ICMP:
                if (instruction->getName() == "slt") {
                    auto* leftSlot = loadedSlot(instruction->getOperand(0));
                    auto* rightSlot = loadedSlot(instruction->getOperand(1));
                    auto* left = rootArgument(instruction->getOperand(0), slots);
                    auto* right = rootArgument(instruction->getOperand(1), slots);
                    if (leftSlot && right == function->getArg(1)) {
                        inductionSlot = leftSlot;
                        loopCompare = instruction;
                        ++loopCompares;
                    } else if (rightSlot && left == function->getArg(1)) {
                        inductionSlot = rightSlot;
                        loopCompare = instruction;
                        ++loopCompares;
                    }
                }
                break;
            case Opc::CALL:
                ++calls;
                break;
            case Opc::RET:
                if (instruction->getNumOperands() == 1) {
                    returned = dynamic_cast<IR::Instruction*>(
                        instruction->getOperand(0));
                }
                break;
            default:
                break;
            }
        }
    }

    if (!division || !remainder || returned != remainder ||
        loopCompares != 1 || calls != 0) {
        return false;
    }

    auto* divisor = division->getNumOperands() == 2
        ? dynamic_cast<IR::ConstantInt*>(
              division->getOperand(1))
        : nullptr;
    auto* remainderDivisor = remainder->getNumOperands() == 2
        ? dynamic_cast<IR::ConstantInt*>(
              remainder->getOperand(1))
        : nullptr;
    if (!divisor || !remainderDivisor ||
        divisor->getValue() != remainderDivisor->getValue() ||
        !isPowerOfTwo(divisor->getValue()) ||
        divisor->getValue() >
            std::numeric_limits<int>::max()) {
        return false;
    }

    auto* divisionInput =
        dynamic_cast<IR::Instruction*>(division->getOperand(0));
    auto* remainderInput =
        dynamic_cast<IR::Instruction*>(remainder->getOperand(0));
    if (!divisionInput || !remainderInput ||
        divisionInput->getOpcode() != Opc::LOAD ||
        remainderInput->getOpcode() != Opc::LOAD ||
        divisionInput->getNumOperands() != 1 ||
        remainderInput->getNumOperands() != 1 ||
        divisionInput->getOperand(0) != remainderInput->getOperand(0)) {
        return false;
    }

    auto* numberSlot = divisionInput->getOperand(0);
    auto number = slots.find(numberSlot);
    if (number == slots.end() ||
        number->second != function->getArg(0)) {
        return false;
    }

    unsigned divisionStores = 0;
    for (const auto& use : division->getUses()) {
        auto* store = dynamic_cast<IR::Instruction*>(use.user);
        if (store && store->getOpcode() == Opc::STORE &&
            store->getNumOperands() == 2 &&
            store->getOperand(0) == division &&
            store->getOperand(1) == numberSlot) {
            ++divisionStores;
        }
    }
    if (divisionStores != 1) return false;
    IR::Instruction* roundSlot = nullptr;
    for (const auto& [slot, argument] : slots) {
        if (argument == function->getArg(1))
            roundSlot = dynamic_cast<IR::Instruction*>(slot);
    }
    if (!roundSlot || !inductionSlot || !loopCompare ||
        !hasSlotInitialization(
            function, inductionSlot,
            IR::ConstantInt::get(IR::IntegerType::I32, 0), slots) ||
        !provesSlotIncrementInBlock(division->getParent(), inductionSlot) ||
        !branchTargets(division->getParent()->getTerminator(),
                       loopCompare->getParent()) ||
        !branchTargets(loopCompare->getParent()->getTerminator(),
                       division->getParent())) {
        return false;
    }

    proof.function = function;
    proof.radix = static_cast<int>(divisor->getValue());
    proof.bitsPerDigit = exactLog2(divisor->getValue());
    proof.numberSlot = dynamic_cast<IR::Instruction*>(numberSlot);
    proof.roundSlot = roundSlot;
    proof.inductionSlot = inductionSlot;
    bool proven = proof.numberSlot && proof.bitsPerDigit > 0 &&
                  proof.bitsPerDigit < 31;
    return proven;
}

bool extractArrayIncrement(
    IR::Instruction* store, ArrayElementAccess& destination,
    ArrayElementAccess& source) {
    if (!store || store->getOpcode() != Opc::STORE ||
        store->getNumOperands() != 2) {
        return false;
    }
    auto destinationAccess = arrayElementPointer(store->getOperand(1));
    auto* add = dynamic_cast<IR::Instruction*>(store->getOperand(0));
    if (!destinationAccess || !add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2) {
        return false;
    }
    IR::Value* oldValue = nullptr;
    if (isConstant(add->getOperand(0), 1)) oldValue = add->getOperand(1);
    if (isConstant(add->getOperand(1), 1)) oldValue = add->getOperand(0);
    auto sourceAccess = loadedArrayElement(oldValue);
    if (!sourceAccess || sourceAccess->array != destinationAccess->array)
        return false;
    destination = *destinationAccess;
    source = *sourceAccess;
    return true;
}

bool sameDigitApplication(
    IR::Value* left, IR::Value* right,
    IR::Function* digitFunction, IR::Argument* roundArgument,
    const ArgumentSlots& slots) {
    if (sameValueOrSlotLoad(left, right)) return true;
    auto* leftCall = dynamic_cast<IR::Instruction*>(left);
    auto* rightCall = dynamic_cast<IR::Instruction*>(right);
    if (!leftCall || !rightCall ||
        calledFunction(leftCall) != digitFunction ||
        calledFunction(rightCall) != digitFunction ||
        leftCall->getNumOperands() != 3 ||
        rightCall->getNumOperands() != 3 ||
        rootArgument(leftCall->getOperand(2), slots) != roundArgument ||
        rootArgument(rightCall->getOperand(2), slots) != roundArgument) {
        return false;
    }
    if (sameValueOrSlotLoad(
            leftCall->getOperand(1), rightCall->getOperand(1))) {
        return true;
    }
    auto* leftValue = dynamic_cast<IR::Instruction*>(leftCall->getOperand(1));
    auto* rightValue = dynamic_cast<IR::Instruction*>(rightCall->getOperand(1));
    if (!leftValue || !rightValue ||
        leftValue->getOpcode() != Opc::LOAD ||
        rightValue->getOpcode() != Opc::LOAD) {
        return false;
    }
    auto* leftAddress = dynamic_cast<IR::Instruction*>(leftValue->getOperand(0));
    auto* rightAddress = dynamic_cast<IR::Instruction*>(rightValue->getOperand(0));
    if (!leftAddress || !rightAddress ||
        leftAddress->getOpcode() != Opc::GETELEMENTPTR ||
        rightAddress->getOpcode() != Opc::GETELEMENTPTR ||
        rootArgument(leftAddress->getOperand(0), slots) !=
            rootArgument(rightAddress->getOperand(0), slots)) {
        return false;
    }
    return sameValueOrSlotLoad(
        leftAddress->getOperand(leftAddress->getNumOperands() - 1),
        rightAddress->getOperand(rightAddress->getNumOperands() - 1));
}

IR::Instruction* proveHistogramArray(
    IR::Function* function, IR::Function* digitFunction,
    const std::vector<IR::Instruction*>& bucketArrays,
    const ArgumentSlots& slots) {
    IR::Instruction* provenArray = nullptr;
    auto* round = function->getArg(0);
    auto* data = function->getArg(1);
    auto* left = function->getArg(2);
    auto* right = function->getArg(3);

    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            ArrayElementAccess destination;
            ArrayElementAccess source;
            if (!extractArrayIncrement(
                    owned.get(), destination, source) ||
                std::find(bucketArrays.begin(), bucketArrays.end(),
                          destination.array) == bucketArrays.end()) {
                continue;
            }
            IR::Instruction* destinationIndexSlot = nullptr;
            IR::Instruction* sourceIndexSlot = nullptr;
            if (!isDigitCall(destination.index, digitFunction, round,
                             data, slots, &destinationIndexSlot) ||
                !isDigitCall(source.index, digitFunction, round,
                             data, slots, &sourceIndexSlot) ||
                destinationIndexSlot != sourceIndexSlot ||
                !hasSlotInitialization(
                    function, destinationIndexSlot, left, slots) ||
                !provesSlotUpperBound(
                    function, destinationIndexSlot, right, slots) ||
                !provesSlotIncrementInBlock(
                    owned->getParent(), destinationIndexSlot)) {
                continue;
            }
            if (provenArray && provenArray != destination.array)
                return nullptr;
            provenArray = destination.array;
        }
    }
    return provenArray;
}

bool provesPrefixPartition(
    IR::Function* function, IR::Instruction* countArray,
    IR::Instruction* headArray, IR::Instruction* tailArray,
    int radix, const ArgumentSlots& slots) {
    bool headZero = false;
    bool tailZero = false;
    std::unordered_set<IR::Instruction*> headRecurrenceSlots;
    std::unordered_set<IR::Instruction*> tailRecurrenceSlots;
    auto* left = function->getArg(2);

    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* store = owned.get();
            if (store->getOpcode() != Opc::STORE ||
                store->getNumOperands() != 2) {
                continue;
            }
            auto destination = arrayElementPointer(store->getOperand(1));
            if (!destination) continue;

            if (destination->array == headArray &&
                isConstant(destination->index, 0) &&
                rootArgument(store->getOperand(0), slots) == left) {
                headZero = true;
            }

            if (destination->array == tailArray &&
                isConstant(destination->index, 0)) {
                auto* add = dynamic_cast<IR::Instruction*>(store->getOperand(0));
                if (add && add->getOpcode() == Opc::ADD &&
                    add->getNumOperands() == 2) {
                    auto first = loadedArrayElement(add->getOperand(0));
                    auto second = loadedArrayElement(add->getOperand(1));
                    bool hasLeft = rootArgument(add->getOperand(0), slots) == left ||
                                   rootArgument(add->getOperand(1), slots) == left;
                    bool hasCountZero =
                        (first && first->array == countArray &&
                         isConstant(first->index, 0)) ||
                        (second && second->array == countArray &&
                         isConstant(second->index, 0));
                    tailZero |= hasLeft && hasCountZero;
                }
            }

            auto* slot = loadedSlot(destination->index);
            if (!slot) continue;
            if (destination->array == headArray) {
                auto source = loadedArrayElement(store->getOperand(0));
                auto* previous = source
                    ? dynamic_cast<IR::Instruction*>(source->index)
                    : nullptr;
                if (source && source->array == tailArray && previous &&
                    previous->getOpcode() == Opc::SUB &&
                    previous->getNumOperands() == 2 &&
                    loadedSlot(previous->getOperand(0)) == slot &&
                    isConstant(previous->getOperand(1), 1)) {
                    headRecurrenceSlots.insert(slot);
                }
            }
            if (destination->array == tailArray) {
                auto* add = dynamic_cast<IR::Instruction*>(store->getOperand(0));
                if (!add || add->getOpcode() != Opc::ADD ||
                    add->getNumOperands() != 2) {
                    continue;
                }
                auto first = loadedArrayElement(add->getOperand(0));
                auto second = loadedArrayElement(add->getOperand(1));
                bool headAtIndex =
                    (first && first->array == headArray &&
                     sameValueOrSlotLoad(first->index, destination->index)) ||
                    (second && second->array == headArray &&
                     sameValueOrSlotLoad(second->index, destination->index));
                bool countAtIndex =
                    (first && first->array == countArray &&
                     sameValueOrSlotLoad(first->index, destination->index)) ||
                    (second && second->array == countArray &&
                     sameValueOrSlotLoad(second->index, destination->index));
                if (headAtIndex && countAtIndex) {
                    tailRecurrenceSlots.insert(slot);
                }
            }
        }
    }

    auto* one = IR::ConstantInt::get(IR::IntegerType::I32, 1);
    auto* radixValue = IR::ConstantInt::get(IR::IntegerType::I32, radix);
    if (!headZero || !tailZero) return false;
    for (auto* slot : headRecurrenceSlots) {
        if (tailRecurrenceSlots.count(slot) &&
            hasSlotInitialization(function, slot, one, slots) &&
            provesSlotUpperBound(function, slot, radixValue, slots)) {
            return true;
        }
    }
    return false;
}

bool provesPermutationWrites(
    IR::Function* function, IR::Function* digitFunction,
    IR::Instruction* headArray, const ArgumentSlots& slots) {
    unsigned dataStores = 0;
    auto* data = function->getArg(1);
    auto* round = function->getArg(0);
    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* store = owned.get();
            if (store->getOpcode() != Opc::STORE ||
                store->getNumOperands() != 2 ||
                rootArgument(store->getOperand(1), slots) != data) {
                continue;
            }
            auto* pointer = dynamic_cast<IR::Instruction*>(store->getOperand(1));
            if (!pointer || pointer->getOpcode() != Opc::GETELEMENTPTR)
                return false;
            auto headPosition = loadedArrayElement(
                pointer->getOperand(pointer->getNumOperands() - 1));
            if (!headPosition || headPosition->array != headArray)
                return false;

            bool advancesSameHead = false;
            for (auto& peer : block->getInstructions()) {
                ArrayElementAccess destination;
                ArrayElementAccess source;
                if (!extractArrayIncrement(
                        peer.get(), destination, source) ||
                    destination.array != headArray) {
                    continue;
                }
                if (sameValueOrSlotLoad(
                        headPosition->index, destination.index) ||
                    sameDigitApplication(
                        headPosition->index, destination.index,
                        digitFunction, round, slots)) {
                    advancesSameHead = true;
                    break;
                }
            }
            if (!advancesSameHead) return false;
            ++dataStores;
        }
    }
    return dataStores == 2;
}

bool proveRecursiveSort(
    IR::Module* module, IR::Function* function,
    IR::Function* digitFunction,
    const DigitProof& digitProof, RadixProof& proof) {
    if (!function || function->isExternal() ||
        function->getNumArgs() != 4) {
        return false;
    }

    auto* type = function->getFunctionType();
    const auto& parameters = type->getParamTypes();
    auto* dataPointer = parameters.size() == 4
        ? dynamic_cast<IR::PointerType*>(parameters[1])
        : nullptr;
    if (!type->getReturnType()->isVoid() ||
        parameters.size() != 4 ||
        parameters[0] != IR::IntegerType::I32 ||
        !dataPointer ||
        dataPointer->getPointeeType() != IR::IntegerType::I32 ||
        parameters[2] != IR::IntegerType::I32 ||
        parameters[3] != IR::IntegerType::I32) {
        return false;
    }

    auto slots = buildArgumentSlots(function);
    std::vector<IR::Instruction*> bucketArrays;
    std::vector<IR::Instruction*> recursiveCalls;
    unsigned digitCalls = 0;
    unsigned otherCalls = 0;
    bool hasRoundGuard = false;
    bool hasRangeGuard = false;
    bool hasUnknownWrite = false;

    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() == Opc::ALLOCA) {
                auto* pointer =
                    dynamic_cast<IR::PointerType*>(instruction->getType());
                auto* array = pointer
                    ? dynamic_cast<IR::ArrayType*>(
                          pointer->getPointeeType())
                    : nullptr;
                if (array &&
                    array->getElementType() == IR::IntegerType::I32 &&
                    array->getNumElements() ==
                        static_cast<unsigned>(digitProof.radix)) {
                    bucketArrays.push_back(instruction);
                }
            }

            if (instruction->getOpcode() == Opc::CALL) {
                auto* callee = calledFunction(instruction);
                if (callee == function) {
                    recursiveCalls.push_back(instruction);
                } else if (callee == digitFunction) {
                    ++digitCalls;
                } else {
                    ++otherCalls;
                }
            }

            if (instruction->getOpcode() == Opc::ICMP) {
                if (provesExhaustedRoundGuard(
                        instruction, function, slots)) {
                    hasRoundGuard = true;
                }
                if (provesTrivialRangeGuard(
                        instruction, function, slots)) {
                    hasRangeGuard = true;
                }
            }

            if (instruction->getOpcode() == Opc::STORE &&
                instruction->getNumOperands() == 2 &&
                rootArgument(instruction->getOperand(1), slots) !=
                    function->getArg(1) &&
                !rootAlloca(instruction->getOperand(1))) {
                hasUnknownWrite = true;
            }

        }
    }

    // 硬约束：合并所有结构条件
    if (bucketArrays.size() != 3 ||
        recursiveCalls.size() != 1 ||
        digitCalls == 0 ||
        otherCalls != 0 ||
        !hasRoundGuard || !hasRangeGuard || hasUnknownWrite) {
        return false;
    }

    auto* countArray = proveHistogramArray(
        function, digitFunction, bucketArrays, slots);
    if (!countArray) return false;

    auto* recursive = recursiveCalls.front();
    if (recursive->getNumOperands() != 5) return false;
    auto* nextRound =
        dynamic_cast<IR::Instruction*>(recursive->getOperand(1));
    if (!nextRound ||
        !provesPreviousRound(nextRound, function->getArg(0), slots) ||
        rootArgument(recursive->getOperand(2), slots) !=
            function->getArg(1)) {
        return false;
    }
    auto* leftArray = rootAlloca(recursive->getOperand(3));
    auto* rightArray = rootAlloca(recursive->getOperand(4));
    if (!leftArray || !rightArray || leftArray == rightArray ||
        leftArray == countArray || rightArray == countArray ||
        std::find(bucketArrays.begin(), bucketArrays.end(), leftArray) ==
            bucketArrays.end() ||
        std::find(bucketArrays.begin(), bucketArrays.end(), rightArray) ==
            bucketArrays.end()) {
        return false;
    }

    auto leftElement = loadedArrayElement(recursive->getOperand(3));
    auto rightElement = loadedArrayElement(recursive->getOperand(4));
    auto* bucketSlot = leftElement ? loadedSlot(leftElement->index) : nullptr;
    auto* zero = IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto* radixValue = IR::ConstantInt::get(
        IR::IntegerType::I32, digitProof.radix);
    bool prefixProven = provesPrefixPartition(
        function, countArray, leftArray, rightArray,
        digitProof.radix, slots);
    bool permutationProven = provesPermutationWrites(
        function, digitFunction, leftArray, slots);
    if (!leftElement || !rightElement ||
        leftElement->array != leftArray ||
        rightElement->array != rightArray ||
        !sameValueOrSlotLoad(leftElement->index, rightElement->index) ||
        !bucketSlot ||
        !hasSlotInitialization(function, bucketSlot, zero, slots) ||
        !provesSlotUpperBound(function, bucketSlot, radixValue, slots) ||
        !provesSlotIncrementInBlock(recursive->getParent(), bucketSlot) ||
        !prefixProven || !permutationProven) {
        return false;
    }

    auto calls = collectCalls(module, function);
    std::vector<IR::Instruction*> externalCalls;
    for (auto* call : calls) {
        if (call != recursive) externalCalls.push_back(call);
    }
    if (externalCalls.empty()) return false;

    IR::ArrayType* array = nullptr;
    for (auto* external : externalCalls) {
        if (external->getNumOperands() != 5) return false;
        auto* global =
            exactGlobalArrayBase(external->getOperand(2));
        auto* globalPointer = global
            ? dynamic_cast<IR::PointerType*>(global->getType())
            : nullptr;
        auto* candidate = globalPointer
            ? dynamic_cast<IR::ArrayType*>(
                  globalPointer->getPointeeType())
            : nullptr;
        if (!candidate ||
            candidate->getElementType() != IR::IntegerType::I32 ||
            candidate->getNumElements() < 2 ||
            candidate->getNumElements() >
                static_cast<unsigned>(
                    std::numeric_limits<int32_t>::max()) ||
            (array && candidate != array)) {
            return false;
        }
        array = candidate;
    }

    auto digitCallsAll = collectCalls(module, digitFunction);
    if (digitCallsAll.size() != digitCalls) return false;
    for (auto* call : digitCallsAll) {
        if (call->getParent()->getParent() != function) return false;
    }

    proof.sortFunction = function;
    proof.digitFunction = digitFunction;
    proof.externalCalls = std::move(externalCalls);
    proof.dataType = array;
    proof.countArray = countArray;
    proof.headArray = leftArray;
    proof.tailArray = rightArray;
    proof.sourceRadix = digitProof.radix;
    proof.bitsPerDigit = digitProof.bitsPerDigit;
    proof.minimumFullRound =
        (31u + digitProof.bitsPerDigit - 1u) /
        digitProof.bitsPerDigit - 1u;
    return true;
}

bool findProof(IR::Module* module, RadixProof& proof) {
    for (auto& global : module->getGlobals()) {
        if (global->getName() == "__opt_radix_scratch" ||
            global->getName() == "__opt_radix_counts") {
            return false;
        }
    }

    struct DigitCandidate {
        IR::Function* function = nullptr;
        DigitProof proof;
    };
    std::vector<DigitCandidate> digitFunctions;
    for (auto& function : module->getFunctions()) {
        DigitProof digitProof;
        if (proveDigitFunction(function.get(), digitProof)) {
            digitFunctions.push_back(
                {function.get(), digitProof});
        }
    }
    for (auto& function : module->getFunctions()) {
        if (function->getName() == "__opt_radix_slow") {
            return false;
        }
    }

    std::vector<RadixProof> proofs;
    for (const auto& digit : digitFunctions) {
        for (auto& function : module->getFunctions()) {
            RadixProof candidate;
            if (proveRecursiveSort(
                    module, function.get(), digit.function,
                    digit.proof, candidate)) {
                proofs.push_back(candidate);
            }
        }
    }
    if (proofs.size() != 1) return false;
    proof = proofs.front();
    return true;
}

IR::Instruction* makePhi(
    IR::Type* type, const char* name,
    IR::Value* firstValue, IR::BasicBlock* firstBlock,
    IR::Value* secondValue, IR::BasicBlock* secondBlock) {
    auto* phi = IR::Instruction::createPhi(type, name, 4);
    phi->addOperand(firstValue);
    phi->addOperand(firstBlock);
    phi->addOperand(secondValue);
    phi->addOperand(secondBlock);
    return phi;
}

IR::Instruction* elementAddress(
    IR::Value* pointer, IR::Value* index, const char* name) {
    return IR::Instruction::createGetElementPtr(
        IR::IntegerType::I32, pointer, {index}, name);
}

void buildIterativeRadix(
    IR::Function* function,
    IR::Function* slowFunction,
    const RadixProof& proof,
    IR::GlobalVariable* scratchGlobal,
    IR::GlobalVariable* countsGlobal,
    IR::ArrayType* countsType) {
    auto* i32 = IR::IntegerType::I32;
    auto* i1 = IR::IntegerType::I1;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* trueValue = IR::ConstantInt::get(i1, 1);
    auto* radixSize = IR::ConstantInt::get(
        i32, kImplementationRadixSize);
    auto* radixMask = IR::ConstantInt::get(
        i32, kImplementationRadixMask);
    auto* radixBits = IR::ConstantInt::get(
        i32, kImplementationRadixBits);

    auto* entry = function->createBlock("entry");
    auto* maxHeader = function->createBlock("radix.max.cond");
    auto* maxBody = function->createBlock("radix.max.body");
    auto* setup = function->createBlock("radix.setup");
    auto* passHeader = function->createBlock("radix.pass.cond");
    auto* zeroHeader = function->createBlock("radix.zero.cond");
    auto* zeroBody = function->createBlock("radix.zero.body");
    auto* countHeader = function->createBlock("radix.count.cond");
    auto* countBody = function->createBlock("radix.count.body");
    auto* prefixHeader = function->createBlock("radix.prefix.cond");
    auto* prefixBody = function->createBlock("radix.prefix.body");
    auto* scatterHeader = function->createBlock("radix.scatter.cond");
    auto* scatterBody = function->createBlock("radix.scatter.body");
    auto* passLatch = function->createBlock("radix.pass.latch");
    auto* afterPass = function->createBlock("radix.after");
    auto* copyHeader = function->createBlock("radix.copy.cond");
    auto* copyBody = function->createBlock("radix.copy.body");
    auto* exit = function->createBlock("radix.exit");
    auto* slow = function->createBlock("radix.slow");

    auto* length = IR::Instruction::createBinOp(
        Opc::SUB, i32, "radix.length",
        function->getArg(3), function->getArg(2));
    auto* dataBase = elementAddress(
        function->getArg(1), function->getArg(2),
        "radix.data.base");
    auto* scratchBase = IR::Instruction::createGetElementPtr(
        proof.dataType, scratchGlobal,
        {zero, zero}, "radix.scratch.base");
    auto* countsBase = IR::Instruction::createGetElementPtr(
        countsType, countsGlobal,
        {zero, zero}, "radix.counts.base");
    entry->pushBack(length);
    entry->pushBack(dataBase);
    entry->pushBack(scratchBase);
    entry->pushBack(countsBase);
    auto* leftNonnegative = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(2), zero, "sge");
    auto* rangeOrdered = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(3),
        function->getArg(2), "sge");
    auto* withinArray = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(3),
        IR::ConstantInt::get(
            i32, static_cast<int32_t>(
                proof.dataType->getNumElements())),
        "sle");
    auto* lowerBoundsSafe = IR::Instruction::createBinOp(
        Opc::AND, i1, "radix.bounds.lower",
        leftNonnegative, rangeOrdered);
    auto* boundsSafe = IR::Instruction::createBinOp(
        Opc::AND, i1, "radix.bounds.safe",
        lowerBoundsSafe, withinArray);
    auto* roundCoversAllDigits = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(0),
        IR::ConstantInt::get(
            i32, static_cast<int32_t>(proof.minimumFullRound)),
        "sge");
    auto* fastPathSafe = IR::Instruction::createBinOp(
        Opc::AND, i1, "radix.fast.safe",
        boundsSafe, roundCoversAllDigits);
    entry->pushBack(leftNonnegative);
    entry->pushBack(rangeOrdered);
    entry->pushBack(withinArray);
    entry->pushBack(lowerBoundsSafe);
    entry->pushBack(boundsSafe);
    entry->pushBack(roundCoversAllDigits);
    entry->pushBack(fastPathSafe);
    entry->pushBack(IR::Instruction::createCondBr(
        fastPathSafe, maxHeader, slow));

    auto* maxIndexNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.max.i.next", nullptr, one);
    auto* maxNext = IR::Instruction::createSelect(
        zero, zero, zero, "radix.max.next");
    auto* maxIndex = makePhi(
        i32, "radix.max.i", zero, entry,
        maxIndexNext, maxBody);
    auto* maximum = makePhi(
        i32, "radix.max", zero, entry,
        maxNext, maxBody);
    auto* nonnegativeNext = IR::Instruction::createBinOp(
        Opc::AND, i1, "radix.nonnegative.next",
        nullptr, nullptr);
    auto* allNonnegative = makePhi(
        i1, "radix.nonnegative", trueValue, entry,
        nonnegativeNext, maxBody);
    maxIndexNext->setOperand(0, maxIndex);
    maxHeader->pushBack(maxIndex);
    maxHeader->pushBack(maximum);
    maxHeader->pushBack(allNonnegative);
    auto* maxCompare = IR::Instruction::createCmp(
        Opc::ICMP, maxIndex, length, "slt");
    maxHeader->pushBack(maxCompare);
    maxHeader->pushBack(IR::Instruction::createCondBr(
        maxCompare, maxBody, setup));

    auto* scanAddress = elementAddress(
        dataBase, maxIndex, "radix.max.addr");
    auto* scanValue = IR::Instruction::createLoad(
        i32, scanAddress, "radix.max.value");
    auto* greater = IR::Instruction::createCmp(
        Opc::ICMP, scanValue, maximum, "sgt");
    auto* valueNonnegative = IR::Instruction::createCmp(
        Opc::ICMP, scanValue, zero, "sge");
    maxNext->setOperand(0, greater);
    maxNext->setOperand(1, scanValue);
    maxNext->setOperand(2, maximum);
    nonnegativeNext->setOperand(0, allNonnegative);
    nonnegativeNext->setOperand(1, valueNonnegative);
    maxBody->pushBack(scanAddress);
    maxBody->pushBack(scanValue);
    maxBody->pushBack(greater);
    maxBody->pushBack(valueNonnegative);
    maxBody->pushBack(maxNext);
    maxBody->pushBack(nonnegativeNext);
    maxBody->pushBack(maxIndexNext);
    maxBody->pushBack(IR::Instruction::createBr(maxHeader));

    auto* below20 = IR::Instruction::createCmp(
        Opc::ICMP, maximum,
        IR::ConstantInt::get(i32, 1 << 20), "slt");
    auto* below30 = IR::Instruction::createCmp(
        Opc::ICMP, maximum,
        IR::ConstantInt::get(i32, 1 << 30), "slt");
    auto* highPasses = IR::Instruction::createSelect(
        below30, IR::ConstantInt::get(i32, 3),
        IR::ConstantInt::get(i32, 4), "radix.passes.high");
    auto* remainingPasses = IR::Instruction::createSelect(
        below20, IR::ConstantInt::get(i32, 2),
        highPasses, "radix.passes.remaining");
    auto* below10 = IR::Instruction::createCmp(
        Opc::ICMP, maximum,
        IR::ConstantInt::get(i32, 1 << 10), "slt");
    auto* passCount = IR::Instruction::createSelect(
        below10, one, remainingPasses, "radix.passes");
    setup->pushBack(below20);
    setup->pushBack(below30);
    setup->pushBack(highPasses);
    setup->pushBack(remainingPasses);
    setup->pushBack(below10);
    setup->pushBack(passCount);

    IR::Value* requiredRound = zero;
    int requiredDigit = 1;
    int64_t threshold = 1LL << proof.bitsPerDigit;
    while (threshold <= std::numeric_limits<int32_t>::max()) {
        auto* reachesThreshold = IR::Instruction::createCmp(
            Opc::ICMP, maximum,
            IR::ConstantInt::get(i32, threshold), "sge");
        auto* nextRequiredRound =
            IR::Instruction::createSelect(
                reachesThreshold,
                IR::ConstantInt::get(i32, requiredDigit),
                requiredRound, "radix.required.round");
        setup->pushBack(reachesThreshold);
        setup->pushBack(nextRequiredRound);
        requiredRound = nextRequiredRound;

        if (threshold >
            std::numeric_limits<int32_t>::max() /
                (1LL << proof.bitsPerDigit)) {
            break;
        }
        threshold <<= proof.bitsPerDigit;
        ++requiredDigit;
    }
    auto* roundEnough = IR::Instruction::createCmp(
        Opc::ICMP, function->getArg(0),
        requiredRound, "sge");
    auto* canUseFastPath = IR::Instruction::createBinOp(
        Opc::AND, i1, "radix.fast.path",
        allNonnegative, roundEnough);
    setup->pushBack(roundEnough);
    setup->pushBack(canUseFastPath);
    setup->pushBack(IR::Instruction::createCondBr(
        canUseFastPath, passHeader, slow));

    auto* passNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.pass.next", nullptr, one);
    auto* pass = makePhi(
        i32, "radix.pass", zero, setup,
        passNext, passLatch);
    passNext->setOperand(0, pass);
    passHeader->pushBack(pass);
    auto* passCompare = IR::Instruction::createCmp(
        Opc::ICMP, pass, passCount, "slt");
    passHeader->pushBack(passCompare);
    auto* parity = IR::Instruction::createBinOp(
        Opc::AND, i32, "radix.parity", pass, one);
    auto* even = IR::Instruction::createCmp(
        Opc::ICMP, parity, zero, "eq");
    auto* source = IR::Instruction::createSelect(
        even, dataBase, scratchBase, "radix.source");
    auto* destination = IR::Instruction::createSelect(
        even, scratchBase, dataBase, "radix.destination");
    auto* shift = IR::Instruction::createBinOp(
        Opc::MUL, i32, "radix.shift", pass, radixBits);
    passHeader->pushBack(parity);
    passHeader->pushBack(even);
    passHeader->pushBack(source);
    passHeader->pushBack(destination);
    passHeader->pushBack(shift);
    passHeader->pushBack(IR::Instruction::createCondBr(
        passCompare, zeroHeader, afterPass));

    auto* zeroNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.zero.i.next", nullptr, one);
    auto* zeroIndex = makePhi(
        i32, "radix.zero.i", zero, passHeader,
        zeroNext, zeroBody);
    zeroNext->setOperand(0, zeroIndex);
    zeroHeader->pushBack(zeroIndex);
    auto* zeroCompare = IR::Instruction::createCmp(
        Opc::ICMP, zeroIndex, radixSize, "slt");
    zeroHeader->pushBack(zeroCompare);
    zeroHeader->pushBack(IR::Instruction::createCondBr(
        zeroCompare, zeroBody, countHeader));
    auto* zeroAddress = elementAddress(
        countsBase, zeroIndex, "radix.zero.addr");
    zeroBody->pushBack(zeroAddress);
    zeroBody->pushBack(
        IR::Instruction::createStore(zero, zeroAddress));
    zeroBody->pushBack(zeroNext);
    zeroBody->pushBack(IR::Instruction::createBr(zeroHeader));

    auto* countNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.count.i.next", nullptr, one);
    auto* countIndex = makePhi(
        i32, "radix.count.i", zero, zeroHeader,
        countNext, countBody);
    countNext->setOperand(0, countIndex);
    countHeader->pushBack(countIndex);
    auto* countCompare = IR::Instruction::createCmp(
        Opc::ICMP, countIndex, length, "slt");
    countHeader->pushBack(countCompare);
    countHeader->pushBack(IR::Instruction::createCondBr(
        countCompare, countBody, prefixHeader));
    auto* countSourceAddress = elementAddress(
        source, countIndex, "radix.count.source.addr");
    auto* countValue = IR::Instruction::createLoad(
        i32, countSourceAddress, "radix.count.value");
    auto* countShifted = IR::Instruction::createBinOp(
        Opc::ASHR, i32, "radix.count.shifted",
        countValue, shift);
    auto* countDigit = IR::Instruction::createBinOp(
        Opc::AND, i32, "radix.count.digit",
        countShifted, radixMask);
    auto* countAddress = elementAddress(
        countsBase, countDigit, "radix.count.addr");
    auto* oldCount = IR::Instruction::createLoad(
        i32, countAddress, "radix.count.old");
    auto* newCount = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.count.new", oldCount, one);
    countBody->pushBack(countSourceAddress);
    countBody->pushBack(countValue);
    countBody->pushBack(countShifted);
    countBody->pushBack(countDigit);
    countBody->pushBack(countAddress);
    countBody->pushBack(oldCount);
    countBody->pushBack(newCount);
    countBody->pushBack(
        IR::Instruction::createStore(newCount, countAddress));
    countBody->pushBack(countNext);
    countBody->pushBack(IR::Instruction::createBr(countHeader));

    auto* prefixNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.prefix.i.next", nullptr, one);
    auto* prefixSumNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.prefix.sum.next",
        nullptr, nullptr);
    auto* prefixIndex = makePhi(
        i32, "radix.prefix.i", zero, countHeader,
        prefixNext, prefixBody);
    auto* prefixSum = makePhi(
        i32, "radix.prefix.sum", zero, countHeader,
        prefixSumNext, prefixBody);
    prefixNext->setOperand(0, prefixIndex);
    prefixHeader->pushBack(prefixIndex);
    prefixHeader->pushBack(prefixSum);
    auto* prefixCompare = IR::Instruction::createCmp(
        Opc::ICMP, prefixIndex, radixSize, "slt");
    prefixHeader->pushBack(prefixCompare);
    prefixHeader->pushBack(IR::Instruction::createCondBr(
        prefixCompare, prefixBody, scatterHeader));
    auto* prefixAddress = elementAddress(
        countsBase, prefixIndex, "radix.prefix.addr");
    auto* bucketCount = IR::Instruction::createLoad(
        i32, prefixAddress, "radix.prefix.count");
    prefixSumNext->setOperand(0, prefixSum);
    prefixSumNext->setOperand(1, bucketCount);
    prefixBody->pushBack(prefixAddress);
    prefixBody->pushBack(bucketCount);
    prefixBody->pushBack(
        IR::Instruction::createStore(prefixSum, prefixAddress));
    prefixBody->pushBack(prefixSumNext);
    prefixBody->pushBack(prefixNext);
    prefixBody->pushBack(IR::Instruction::createBr(prefixHeader));

    auto* scatterNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.scatter.i.next", nullptr, one);
    auto* scatterIndex = makePhi(
        i32, "radix.scatter.i", zero, prefixHeader,
        scatterNext, scatterBody);
    scatterNext->setOperand(0, scatterIndex);
    scatterHeader->pushBack(scatterIndex);
    auto* scatterCompare = IR::Instruction::createCmp(
        Opc::ICMP, scatterIndex, length, "slt");
    scatterHeader->pushBack(scatterCompare);
    scatterHeader->pushBack(IR::Instruction::createCondBr(
        scatterCompare, scatterBody, passLatch));
    auto* scatterSourceAddress = elementAddress(
        source, scatterIndex, "radix.scatter.source.addr");
    auto* scatterValue = IR::Instruction::createLoad(
        i32, scatterSourceAddress, "radix.scatter.value");
    auto* scatterShifted = IR::Instruction::createBinOp(
        Opc::ASHR, i32, "radix.scatter.shifted",
        scatterValue, shift);
    auto* scatterDigit = IR::Instruction::createBinOp(
        Opc::AND, i32, "radix.scatter.digit",
        scatterShifted, radixMask);
    auto* positionAddress = elementAddress(
        countsBase, scatterDigit, "radix.scatter.position.addr");
    auto* position = IR::Instruction::createLoad(
        i32, positionAddress, "radix.scatter.position");
    auto* scatterDestinationAddress = elementAddress(
        destination, position, "radix.scatter.destination.addr");
    auto* positionNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.scatter.position.next",
        position, one);
    scatterBody->pushBack(scatterSourceAddress);
    scatterBody->pushBack(scatterValue);
    scatterBody->pushBack(scatterShifted);
    scatterBody->pushBack(scatterDigit);
    scatterBody->pushBack(positionAddress);
    scatterBody->pushBack(position);
    scatterBody->pushBack(scatterDestinationAddress);
    scatterBody->pushBack(IR::Instruction::createStore(
        scatterValue, scatterDestinationAddress));
    scatterBody->pushBack(positionNext);
    scatterBody->pushBack(IR::Instruction::createStore(
        positionNext, positionAddress));
    scatterBody->pushBack(scatterNext);
    scatterBody->pushBack(IR::Instruction::createBr(scatterHeader));

    passLatch->pushBack(passNext);
    passLatch->pushBack(IR::Instruction::createBr(passHeader));

    auto* finalParity = IR::Instruction::createBinOp(
        Opc::AND, i32, "radix.final.parity", passCount, one);
    auto* needsCopy = IR::Instruction::createCmp(
        Opc::ICMP, finalParity, one, "eq");
    afterPass->pushBack(finalParity);
    afterPass->pushBack(needsCopy);
    afterPass->pushBack(IR::Instruction::createCondBr(
        needsCopy, copyHeader, exit));

    auto* copyNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "radix.copy.i.next", nullptr, one);
    auto* copyIndex = makePhi(
        i32, "radix.copy.i", zero, afterPass,
        copyNext, copyBody);
    copyNext->setOperand(0, copyIndex);
    copyHeader->pushBack(copyIndex);
    auto* copyCompare = IR::Instruction::createCmp(
        Opc::ICMP, copyIndex, length, "slt");
    copyHeader->pushBack(copyCompare);
    copyHeader->pushBack(IR::Instruction::createCondBr(
        copyCompare, copyBody, exit));
    auto* copySourceAddress = elementAddress(
        scratchBase, copyIndex, "radix.copy.source.addr");
    auto* copyValue = IR::Instruction::createLoad(
        i32, copySourceAddress, "radix.copy.value");
    auto* copyDestinationAddress = elementAddress(
        dataBase, copyIndex, "radix.copy.destination.addr");
    copyBody->pushBack(copySourceAddress);
    copyBody->pushBack(copyValue);
    copyBody->pushBack(copyDestinationAddress);
    copyBody->pushBack(IR::Instruction::createStore(
        copyValue, copyDestinationAddress));
    copyBody->pushBack(copyNext);
    copyBody->pushBack(IR::Instruction::createBr(copyHeader));

    exit->pushBack(IR::Instruction::createRet(nullptr));
    slow->pushBack(IR::Instruction::createCall(
        slowFunction->getFunctionType(), slowFunction,
        {function->getArg(0), function->getArg(1),
         function->getArg(2), function->getArg(3)},
        ""));
    slow->pushBack(IR::Instruction::createRet(nullptr));
}

} // namespace

bool radixSortLowering(IR::Module* module) {
    RadixProof proof;
    if (!findProof(module, proof)) return false;

    auto* scratch = module->createGlobalVariable(
        IR::PointerType::get(proof.dataType),
        "__opt_radix_scratch", false);
    auto* countsType = IR::ArrayType::get(
        IR::IntegerType::I32, kImplementationRadixSize);
    auto* counts = module->createGlobalVariable(
        IR::PointerType::get(countsType),
        "__opt_radix_counts", false);

    auto* slowFunction = proof.sortFunction;
    auto* functionType = slowFunction->getFunctionType();
    auto originalName = slowFunction->getName();
    slowFunction->setName("__opt_radix_slow");
    auto* fastFunction = module->createFunction(
        functionType, originalName, false);
    for (auto* call : proof.externalCalls) {
        call->setOperand(0, fastFunction);
    }
    buildIterativeRadix(
        fastFunction, slowFunction, proof,
        scratch, counts, countsType);
    return true;
}

} // namespace Opt
