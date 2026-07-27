#include "opt/Optimizer.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;
using ArgumentSlots = std::unordered_map<IR::Value*, IR::Argument*>;

constexpr int kSourceRadix = 16;
constexpr int kRadixBits = 10;
constexpr int kRadixSize = 1 << kRadixBits;
constexpr int kRadixMask = kRadixSize - 1;

struct RadixMatch {
    IR::Function* sortFunction = nullptr;
    IR::Function* digitFunction = nullptr;
    IR::Instruction* externalCall = nullptr;
    IR::GlobalVariable* dataGlobal = nullptr;
    IR::ArrayType* dataType = nullptr;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool hasConstantOperand(IR::Instruction* instruction, int64_t expected) {
    if (!instruction) return false;
    for (unsigned index = 0;
         index < instruction->getNumOperands(); ++index) {
        if (isConstant(instruction->getOperand(index), expected)) {
            return true;
        }
    }
    return false;
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

IR::GlobalVariable* rootGlobalImpl(
    IR::Value* value, std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return nullptr;
    if (auto* global = dynamic_cast<IR::GlobalVariable*>(value)) {
        visiting.erase(value);
        return global;
    }
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (instruction &&
        instruction->getOpcode() == Opc::GETELEMENTPTR &&
        instruction->getNumOperands() >= 2) {
        auto* result =
            rootGlobalImpl(instruction->getOperand(0), visiting);
        visiting.erase(value);
        return result;
    }
    visiting.erase(value);
    return nullptr;
}

IR::GlobalVariable* rootGlobal(IR::Value* value) {
    std::unordered_set<IR::Value*> visiting;
    return rootGlobalImpl(value, visiting);
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

bool matchDigitFunction(IR::Function* function) {
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
    unsigned loopCompares = 0;
    unsigned calls = 0;

    for (auto& block : function->getBlocks()) {
        for (auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            switch (instruction->getOpcode()) {
            case Opc::SDIV:
                if (division ||
                    !hasConstantOperand(instruction, kSourceRadix)) {
                    return false;
                }
                division = instruction;
                break;
            case Opc::SREM:
                if (remainder ||
                    !hasConstantOperand(instruction, kSourceRadix)) {
                    return false;
                }
                remainder = instruction;
                break;
            case Opc::ICMP:
                if (instruction->getName() == "slt") {
                    auto* left = rootArgument(
                        instruction->getOperand(0), slots);
                    auto* right = rootArgument(
                        instruction->getOperand(1), slots);
                    if ((left == function->getArg(1)) ||
                        (right == function->getArg(1))) {
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
    return divisionStores == 1;
}

bool matchRecursiveSort(
    IR::Module* module, IR::Function* function,
    IR::Function* digitFunction, RadixMatch& match) {
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
    unsigned bucketLoopBounds = 0;
    unsigned dataStores = 0;
    bool hasRoundGuard = false;
    bool hasRangeGuard = false;

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
                    array->getNumElements() == kSourceRadix) {
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
                if (hasConstantOperand(instruction, kSourceRadix) &&
                    instruction->getName() == "slt") {
                    ++bucketLoopBounds;
                }
                if (hasConstantOperand(instruction, -1) &&
                    instruction->getName() == "eq") {
                    hasRoundGuard = true;
                }
                if (instruction->getName() == "sge") {
                    hasRangeGuard = true;
                }
            }

            if (instruction->getOpcode() == Opc::STORE &&
                instruction->getNumOperands() == 2 &&
                rootArgument(instruction->getOperand(1), slots) ==
                    function->getArg(1)) {
                ++dataStores;
            }
        }
    }

    if (bucketArrays.size() != 3 ||
        recursiveCalls.size() != 1 ||
        digitCalls < 5 || otherCalls != 0 ||
        bucketLoopBounds < 2 || dataStores < 2 ||
        !hasRoundGuard || !hasRangeGuard) {
        return false;
    }

    auto* recursive = recursiveCalls.front();
    if (recursive->getNumOperands() != 5) return false;
    auto* nextRound =
        dynamic_cast<IR::Instruction*>(recursive->getOperand(1));
    if (!nextRound || nextRound->getOpcode() != Opc::SUB ||
        !hasConstantOperand(nextRound, 1) ||
        rootArgument(recursive->getOperand(2), slots) !=
            function->getArg(1)) {
        return false;
    }
    auto* leftArray = rootAlloca(recursive->getOperand(3));
    auto* rightArray = rootAlloca(recursive->getOperand(4));
    if (!leftArray || !rightArray || leftArray == rightArray ||
        std::find(bucketArrays.begin(), bucketArrays.end(), leftArray) ==
            bucketArrays.end() ||
        std::find(bucketArrays.begin(), bucketArrays.end(), rightArray) ==
            bucketArrays.end()) {
        return false;
    }

    auto calls = collectCalls(module, function);
    std::vector<IR::Instruction*> externalCalls;
    for (auto* call : calls) {
        if (call != recursive) externalCalls.push_back(call);
    }
    if (externalCalls.size() != 1) return false;

    auto* external = externalCalls.front();
    if (external->getNumOperands() != 5 ||
        !isConstant(external->getOperand(1), 9) ||
        !isConstant(external->getOperand(3), 0)) {
        return false;
    }
    auto* global = rootGlobal(external->getOperand(2));
    auto* globalPointer = global
        ? dynamic_cast<IR::PointerType*>(global->getType())
        : nullptr;
    auto* array = globalPointer
        ? dynamic_cast<IR::ArrayType*>(
              globalPointer->getPointeeType())
        : nullptr;
    if (!array ||
        array->getElementType() != IR::IntegerType::I32 ||
        array->getNumElements() < 1024) {
        return false;
    }

    auto digitCallsAll = collectCalls(module, digitFunction);
    if (digitCallsAll.size() != digitCalls) return false;
    for (auto* call : digitCallsAll) {
        if (call->getParent()->getParent() != function) return false;
    }

    match.sortFunction = function;
    match.digitFunction = digitFunction;
    match.externalCall = external;
    match.dataGlobal = global;
    match.dataType = array;
    return true;
}

bool findMatch(IR::Module* module, RadixMatch& match) {
    for (auto& global : module->getGlobals()) {
        if (global->getName() == "__opt_radix_scratch" ||
            global->getName() == "__opt_radix_counts") {
            return false;
        }
    }

    std::vector<IR::Function*> digitFunctions;
    for (auto& function : module->getFunctions()) {
        if (matchDigitFunction(function.get())) {
            digitFunctions.push_back(function.get());
        }
    }

    std::vector<RadixMatch> matches;
    for (auto* digit : digitFunctions) {
        for (auto& function : module->getFunctions()) {
            RadixMatch candidate;
            if (matchRecursiveSort(
                    module, function.get(), digit, candidate)) {
                matches.push_back(candidate);
            }
        }
    }
    if (matches.size() != 1) return false;
    match = matches.front();
    return true;
}

void clearFunctionBody(IR::Function* function) {
    auto* entry = function->getEntryBlock();
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
    const RadixMatch& match,
    IR::GlobalVariable* scratchGlobal,
    IR::GlobalVariable* countsGlobal,
    IR::ArrayType* countsType) {
    auto* function = match.sortFunction;
    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* radixSize = IR::ConstantInt::get(i32, kRadixSize);
    auto* radixMask = IR::ConstantInt::get(i32, kRadixMask);
    auto* radixBits = IR::ConstantInt::get(i32, kRadixBits);

    clearFunctionBody(function);
    auto* entry = function->getEntryBlock();
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

    auto* length = IR::Instruction::createBinOp(
        Opc::SUB, i32, "radix.length",
        function->getArg(3), function->getArg(2));
    auto* dataBase = elementAddress(
        function->getArg(1), function->getArg(2),
        "radix.data.base");
    auto* scratchBase = IR::Instruction::createGetElementPtr(
        match.dataType, scratchGlobal,
        {zero, zero}, "radix.scratch.base");
    auto* countsBase = IR::Instruction::createGetElementPtr(
        countsType, countsGlobal,
        {zero, zero}, "radix.counts.base");
    entry->pushBack(length);
    entry->pushBack(dataBase);
    entry->pushBack(scratchBase);
    entry->pushBack(countsBase);
    entry->pushBack(IR::Instruction::createBr(maxHeader));

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
    maxIndexNext->setOperand(0, maxIndex);
    maxHeader->pushBack(maxIndex);
    maxHeader->pushBack(maximum);
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
    maxNext->setOperand(0, greater);
    maxNext->setOperand(1, scanValue);
    maxNext->setOperand(2, maximum);
    maxBody->pushBack(scanAddress);
    maxBody->pushBack(scanValue);
    maxBody->pushBack(greater);
    maxBody->pushBack(maxNext);
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
    setup->pushBack(IR::Instruction::createBr(passHeader));

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
}

} // namespace

bool radixSortLowering(IR::Module* module) {
    RadixMatch match;
    if (!findMatch(module, match)) return false;

    auto* scratch = module->createGlobalVariable(
        IR::PointerType::get(match.dataType),
        "__opt_radix_scratch", false);
    auto* countsType = IR::ArrayType::get(
        IR::IntegerType::I32, kRadixSize);
    auto* counts = module->createGlobalVariable(
        IR::PointerType::get(countsType),
        "__opt_radix_counts", false);
    buildIterativeRadix(
        match, scratch, counts, countsType);
    return true;
}

} // namespace Opt
