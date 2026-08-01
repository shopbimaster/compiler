// ================================================================
// P0: 位运算模式识别（Bit Operation Pattern Recognition）
// 策略：
//   识别并融合连续的基本位运算指令，降序指令数并暴露更多优化机会。
//   所有变换只依赖 IR 语义和数据流，不依赖函数名、测试名或输入特征。
//
// 识别的模式：
//   模式1: 连续同向移位融合 — (x >> a) >> b → x >> (a+b)  (a,b 常量)
//   模式2: 连续同向移位融合 — (x << a) << b → x << (a+b)  (a,b 常量)
//   模式3: 连续 AND 融合 — (x & m1) & m2 → x & (m1 & m2)  (m1,m2 常量)
//   模式4: 移位后 AND 优化 — ((x >> a) & m) → 优化为 (x >> a) 若 m 覆盖所有剩余位
//   模式5: 连续位操作折叠 — XOR/OR 常量合并
//   模式6: 循环中逐位 ASHR 模式 → 合并为多位移位
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// Recognize 32-iteration software bit operations without relying on names.
// Native operations are only used for non-negative inputs; the original loop
// remains as the exact fallback for signed division/remainder semantics.
using Opcode = IR::Instruction::Opcode;

bool isIntConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

IR::Value* getLoadedPointer(IR::Value* value) {
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

IR::Instruction* findStoredValue(
    const std::vector<IR::Instruction*>& instructions,
    IR::Value* value,
    IR::Value* pointer = nullptr) {
    for (auto* instruction : instructions) {
        if (instruction->getOpcode() != Opcode::STORE ||
            instruction->getNumOperands() != 2 ||
            instruction->getOperand(0) != value) {
            continue;
        }
        if (!pointer || instruction->getOperand(1) == pointer) {
            return instruction;
        }
    }
    return nullptr;
}

bool hasConstantStore(const std::vector<IR::Instruction*>& instructions,
                      IR::Value* pointer, int64_t value) {
    for (auto* instruction : instructions) {
        if (instruction->getOpcode() == Opcode::STORE &&
            instruction->getNumOperands() == 2 &&
            instruction->getOperand(1) == pointer &&
            isIntConstant(instruction->getOperand(0), value)) {
            return true;
        }
    }
    return false;
}

IR::Instruction* findConditionalBranch(
    const std::vector<IR::Instruction*>& instructions, IR::Value* condition) {
    for (auto* instruction : instructions) {
        if (instruction->getOpcode() == Opcode::COND_BR &&
            instruction->getNumOperands() == 3 &&
            instruction->getOperand(0) == condition) {
            return instruction;
        }
    }
    return nullptr;
}

struct SoftwareBitLoop {
    Opcode nativeOpcode = Opcode::XOR;
    IR::Value* argumentSlots[2] = {nullptr, nullptr};
    IR::Value* bitSlots[2] = {nullptr, nullptr};
    IR::Value* lengthSlot = nullptr;
    IR::Value* powerSlot = nullptr;
    IR::Value* resultSlot = nullptr;
    IR::Value* temporarySlot = nullptr;
    IR::Instruction* resultAdd = nullptr;
};

bool matchSoftwareBitLoop(IR::Function* function, SoftwareBitLoop& match) {
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

    std::vector<IR::Instruction*> instructions;
    for (const auto& block : function->getBlocks()) {
        for (const auto& instruction : block->getInstructions()) {
            instructions.push_back(instruction.get());
            if (instruction->getOpcode() == Opcode::CALL) return false;
            if (instruction->getOpcode() == Opcode::STORE &&
                instruction->getNumOperands() == 2) {
                auto* pointer = dynamic_cast<IR::Instruction*>(
                    instruction->getOperand(1));
                if (!pointer || pointer->getOpcode() != Opcode::ALLOCA) {
                    return false;
                }
            }
        }
    }

    IR::Value* argumentSlots[2] = {nullptr, nullptr};
    for (int index = 0; index < 2; ++index) {
        for (const auto& instruction : entry->getInstructions()) {
            if (instruction->getOpcode() == Opcode::STORE &&
                instruction->getNumOperands() == 2 &&
                instruction->getOperand(0) == function->getArg(index)) {
                argumentSlots[index] = instruction->getOperand(1);
                break;
            }
        }
        if (!argumentSlots[index]) return false;
    }

    IR::Value* bitSlots[2] = {nullptr, nullptr};
    int divideCount = 0;
    int remainderCount = 0;
    for (auto* instruction : instructions) {
        if ((instruction->getOpcode() != Opcode::SDIV &&
             instruction->getOpcode() != Opcode::SREM) ||
            instruction->getNumOperands() != 2 ||
            !isIntConstant(instruction->getOperand(1), 2)) {
            continue;
        }

        IR::Value* sourceSlot = getLoadedPointer(instruction->getOperand(0));
        int argumentIndex = sourceSlot == argumentSlots[0] ? 0 :
                            sourceSlot == argumentSlots[1] ? 1 : -1;
        if (argumentIndex < 0) return false;

        auto* store = findStoredValue(instructions, instruction);
        if (!store) return false;
        if (instruction->getOpcode() == Opcode::SDIV) {
            if (store->getOperand(1) != sourceSlot) return false;
            ++divideCount;
        } else {
            if (bitSlots[argumentIndex] &&
                bitSlots[argumentIndex] != store->getOperand(1)) {
                return false;
            }
            bitSlots[argumentIndex] = store->getOperand(1);
            ++remainderCount;
        }
    }
    if (divideCount != 2 || remainderCount != 2 ||
        !bitSlots[0] || !bitSlots[1] || bitSlots[0] == bitSlots[1]) {
        return false;
    }

    IR::Value* lengthSlot = nullptr;
    IR::Value* powerSlot = nullptr;
    IR::Instruction* resultAdd = nullptr;
    IR::Value* resultSlot = nullptr;

    for (auto* instruction : instructions) {
        if (instruction->getOpcode() == Opcode::SUB &&
            instruction->getNumOperands() == 2 &&
            isIntConstant(instruction->getOperand(1), 1)) {
            IR::Value* slot = getLoadedPointer(instruction->getOperand(0));
            if (slot && findStoredValue(instructions, instruction, slot) &&
                hasConstantStore(instructions, slot, 32)) {
                if (lengthSlot && lengthSlot != slot) return false;
                lengthSlot = slot;
            }
        }
        if (instruction->getOpcode() == Opcode::MUL &&
            instruction->getNumOperands() == 2 &&
            isIntConstant(instruction->getOperand(1), 2)) {
            IR::Value* slot = getLoadedPointer(instruction->getOperand(0));
            if (slot && findStoredValue(instructions, instruction, slot) &&
                hasConstantStore(instructions, slot, 1)) {
                if (powerSlot && powerSlot != slot) return false;
                powerSlot = slot;
            }
        }
    }
    if (!lengthSlot || !powerSlot) return false;

    for (auto* instruction : instructions) {
        if (instruction->getOpcode() != Opcode::RET ||
            instruction->getNumOperands() != 1) {
            continue;
        }
        IR::Value* candidateResult = getLoadedPointer(instruction->getOperand(0));
        if (!candidateResult ||
            !hasConstantStore(instructions, candidateResult, 0)) {
            continue;
        }
        for (auto* candidateAdd : instructions) {
            if (candidateAdd->getOpcode() != Opcode::ADD ||
                candidateAdd->getNumOperands() != 2 ||
                !findStoredValue(instructions, candidateAdd, candidateResult)) {
                continue;
            }
            IR::Value* left = getLoadedPointer(candidateAdd->getOperand(0));
            IR::Value* right = getLoadedPointer(candidateAdd->getOperand(1));
            if ((left == candidateResult && right == powerSlot) ||
                (right == candidateResult && left == powerSlot)) {
                resultSlot = candidateResult;
                resultAdd = candidateAdd;
                break;
            }
        }
    }
    if (!resultSlot || !resultAdd) return false;

    match.argumentSlots[0] = argumentSlots[0];
    match.argumentSlots[1] = argumentSlots[1];
    match.bitSlots[0] = bitSlots[0];
    match.bitSlots[1] = bitSlots[1];
    match.lengthSlot = lengthSlot;
    match.powerSlot = powerSlot;
    match.resultSlot = resultSlot;
    match.resultAdd = resultAdd;

    for (auto* instruction : instructions) {
        if (instruction->getOpcode() != Opcode::ICMP ||
            instruction->getName() != "ne" ||
            instruction->getNumOperands() != 2) {
            continue;
        }
        IR::Value* left = getLoadedPointer(instruction->getOperand(0));
        IR::Value* right = getLoadedPointer(instruction->getOperand(1));
        if (!((left == bitSlots[0] && right == bitSlots[1]) ||
              (left == bitSlots[1] && right == bitSlots[0]))) {
            continue;
        }
        auto* branch = findConditionalBranch(instructions, instruction);
        auto* trueBlock = branch
                              ? dynamic_cast<IR::BasicBlock*>(
                                    branch->getOperand(1))
                              : nullptr;
        if (containsInstruction(trueBlock, resultAdd)) {
            match.nativeOpcode = Opcode::XOR;
            return true;
        }
    }

    IR::Instruction* bitTests[2] = {nullptr, nullptr};
    for (auto* instruction : instructions) {
        if (instruction->getOpcode() != Opcode::ICMP ||
            instruction->getName() != "eq" ||
            instruction->getNumOperands() != 2) {
            continue;
        }
        IR::Value* slot = nullptr;
        if (isIntConstant(instruction->getOperand(1), 1)) {
            slot = getLoadedPointer(instruction->getOperand(0));
        } else if (isIntConstant(instruction->getOperand(0), 1)) {
            slot = getLoadedPointer(instruction->getOperand(1));
        }
        if (slot == bitSlots[0]) bitTests[0] = instruction;
        if (slot == bitSlots[1]) bitTests[1] = instruction;
    }
    if (!bitTests[0] || !bitTests[1]) return false;

    IR::Instruction* storedTest = nullptr;
    IR::Value* temporarySlot = nullptr;
    int storedIndex = -1;
    for (int index = 0; index < 2; ++index) {
        auto* store = findStoredValue(instructions, bitTests[index]);
        if (store) {
            storedTest = bitTests[index];
            temporarySlot = store->getOperand(1);
            storedIndex = index;
            break;
        }
    }
    if (!storedTest || !temporarySlot) return false;
    match.temporarySlot = temporarySlot;

    IR::Instruction* finalTest = nullptr;
    for (auto* instruction : instructions) {
        if (instruction->getOpcode() != Opcode::ICMP ||
            instruction->getName() != "ne" ||
            instruction->getNumOperands() != 2) {
            continue;
        }
        if ((getLoadedPointer(instruction->getOperand(0)) == temporarySlot &&
             isIntConstant(instruction->getOperand(1), 0)) ||
            (getLoadedPointer(instruction->getOperand(1)) == temporarySlot &&
             isIntConstant(instruction->getOperand(0), 0))) {
            finalTest = instruction;
            break;
        }
    }
    auto* finalBranch = findConditionalBranch(instructions, finalTest);
    auto* resultBlock = finalBranch
                            ? dynamic_cast<IR::BasicBlock*>(
                                  finalBranch->getOperand(1))
                            : nullptr;
    if (!containsInstruction(resultBlock, resultAdd)) return false;

    int firstIndex = 1 - storedIndex;
    auto* firstBranch = findConditionalBranch(instructions,
                                               bitTests[firstIndex]);
    if (!firstBranch) return false;
    auto* trueBlock = dynamic_cast<IR::BasicBlock*>(firstBranch->getOperand(1));
    auto* falseBlock = dynamic_cast<IR::BasicBlock*>(firstBranch->getOperand(2));
    auto* storedTestBlock = storedTest->getParent();

    if (hasConstantStore(instructions, temporarySlot, 0) &&
        trueBlock == storedTestBlock) {
        match.nativeOpcode = Opcode::AND;
        return true;
    }
    if (hasConstantStore(instructions, temporarySlot, 1) &&
        falseBlock == storedTestBlock) {
        match.nativeOpcode = Opcode::OR;
        return true;
    }
    return false;
}

template <typename Predicate>
IR::Instruction* findUniqueInstruction(IR::Function* function,
                                       Predicate predicate) {
    IR::Instruction* result = nullptr;
    for (const auto& block : function->getBlocks()) {
        for (const auto& instruction : block->getInstructions()) {
            if (!predicate(instruction.get())) continue;
            if (result) return nullptr;
            result = instruction.get();
        }
    }
    return result;
}

bool hasExactInstructionUses(
    IR::Value* value, const std::vector<IR::Instruction*>& expectedUses) {
    std::unordered_set<IR::Instruction*> remaining(
        expectedUses.begin(), expectedUses.end());
    if (remaining.size() != expectedUses.size()) return false;
    for (const auto& use : value->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction || remaining.erase(instruction) != 1) return false;
    }
    return remaining.empty();
}

bool isI32Alloca(IR::Value* value) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    auto* pointerType = instruction
        ? dynamic_cast<IR::PointerType*>(instruction->getType())
        : nullptr;
    return instruction && instruction->getOpcode() == Opcode::ALLOCA &&
           pointerType &&
           pointerType->getPointeeType() == IR::IntegerType::I32;
}

bool isBranchTo(IR::BasicBlock* block, IR::BasicBlock* target) {
    auto* terminator = block ? block->getTerminator() : nullptr;
    return terminator && terminator->getOpcode() == Opcode::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == target;
}

bool hasExactInstructionOrder(
    IR::BasicBlock* block,
    const std::vector<IR::Instruction*>& expectedInstructions) {
    if (!block || block->getInstructions().size() !=
                      expectedInstructions.size()) {
        return false;
    }
    std::size_t index = 0;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction.get() != expectedInstructions[index++]) return false;
    }
    return true;
}

bool validateExactSoftwareBitLoop(IR::Function* function,
                                  const SoftwareBitLoop& match) {
    auto* entry = function ? function->getEntryBlock() : nullptr;
    if (!entry || !match.resultAdd ||
        !isI32Alloca(match.argumentSlots[0]) ||
        !isI32Alloca(match.argumentSlots[1]) ||
        !isI32Alloca(match.bitSlots[0]) ||
        !isI32Alloca(match.bitSlots[1]) ||
        !isI32Alloca(match.lengthSlot) ||
        !isI32Alloca(match.powerSlot) ||
        !isI32Alloca(match.resultSlot)) {
        return false;
    }

    auto isStore = [](IR::Instruction* instruction, IR::Value* value,
                      IR::Value* pointer) {
        return instruction->getOpcode() == Opcode::STORE &&
               instruction->getNumOperands() == 2 &&
               instruction->getOperand(0) == value &&
               instruction->getOperand(1) == pointer;
    };
    auto isConstantStore = [](IR::Instruction* instruction,
                              IR::Value* pointer, int64_t value) {
        return instruction->getOpcode() == Opcode::STORE &&
               instruction->getNumOperands() == 2 &&
               instruction->getOperand(1) == pointer &&
               isIntConstant(instruction->getOperand(0), value);
    };
    auto isLoad = [](IR::Instruction* instruction, IR::Value* pointer) {
        return instruction->getOpcode() == Opcode::LOAD &&
               instruction->getNumOperands() == 1 &&
               instruction->getOperand(0) == pointer;
    };

    IR::Instruction* argumentInitializers[2] = {nullptr, nullptr};
    IR::Instruction* remainderLoads[2] = {nullptr, nullptr};
    IR::Instruction* remainders[2] = {nullptr, nullptr};
    IR::Instruction* remainderStores[2] = {nullptr, nullptr};
    IR::Instruction* divisionLoads[2] = {nullptr, nullptr};
    IR::Instruction* divisions[2] = {nullptr, nullptr};
    IR::Instruction* divisionStores[2] = {nullptr, nullptr};

    for (int index = 0; index < 2; ++index) {
        argumentInitializers[index] = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return isStore(instruction, function->getArg(index),
                               match.argumentSlots[index]);
            });
        remainders[index] = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return instruction->getOpcode() == Opcode::SREM &&
                       instruction->getNumOperands() == 2 &&
                       getLoadedPointer(instruction->getOperand(0)) ==
                           match.argumentSlots[index] &&
                       isIntConstant(instruction->getOperand(1), 2);
            });
        divisions[index] = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return instruction->getOpcode() == Opcode::SDIV &&
                       instruction->getNumOperands() == 2 &&
                       getLoadedPointer(instruction->getOperand(0)) ==
                           match.argumentSlots[index] &&
                       isIntConstant(instruction->getOperand(1), 2);
            });
        if (!argumentInitializers[index] || !remainders[index] ||
            !divisions[index]) {
            return false;
        }
        remainderLoads[index] = dynamic_cast<IR::Instruction*>(
            remainders[index]->getOperand(0));
        divisionLoads[index] = dynamic_cast<IR::Instruction*>(
            divisions[index]->getOperand(0));
        remainderStores[index] = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return isStore(instruction, remainders[index],
                               match.bitSlots[index]);
            });
        divisionStores[index] = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return isStore(instruction, divisions[index],
                               match.argumentSlots[index]);
            });
        if (!remainderStores[index] || !divisionStores[index] ||
            argumentInitializers[index]->getParent() != entry ||
            !hasExactInstructionUses(
                match.argumentSlots[index],
                {argumentInitializers[index], remainderLoads[index],
                 divisionLoads[index], divisionStores[index]})) {
            return false;
        }
    }

    auto* lengthInitializer = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            return isConstantStore(instruction, match.lengthSlot, 32);
        });
    auto* resultInitializer = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            return isConstantStore(instruction, match.resultSlot, 0);
        });
    auto* powerInitializer = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            return isConstantStore(instruction, match.powerSlot, 1);
        });
    if (!lengthInitializer || !resultInitializer || !powerInitializer ||
        lengthInitializer->getParent() != entry ||
        resultInitializer->getParent() != entry ||
        powerInitializer->getParent() != entry) {
        return false;
    }

    auto* lengthCondition = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            if (instruction->getOpcode() != Opcode::ICMP ||
                instruction->getName() != "ne" ||
                instruction->getNumOperands() != 2) {
                return false;
            }
            return (getLoadedPointer(instruction->getOperand(0)) ==
                        match.lengthSlot &&
                    isIntConstant(instruction->getOperand(1), 0)) ||
                   (getLoadedPointer(instruction->getOperand(1)) ==
                        match.lengthSlot &&
                    isIntConstant(instruction->getOperand(0), 0));
        });
    auto* lengthBranch = lengthCondition
        ? findUniqueInstruction(function, [&](IR::Instruction* instruction) {
              return instruction->getOpcode() == Opcode::COND_BR &&
                     instruction->getNumOperands() == 3 &&
                     instruction->getOperand(0) == lengthCondition;
          })
        : nullptr;
    auto* lengthConditionLoad = lengthCondition
        ? dynamic_cast<IR::Instruction*>(
              getLoadedPointer(lengthCondition->getOperand(0)) ==
                      match.lengthSlot
                  ? lengthCondition->getOperand(0)
                  : lengthCondition->getOperand(1))
        : nullptr;
    if (!lengthBranch || !lengthConditionLoad) return false;

    auto* header = lengthCondition->getParent();
    auto* body = dynamic_cast<IR::BasicBlock*>(lengthBranch->getOperand(1));
    auto* exit = dynamic_cast<IR::BasicBlock*>(lengthBranch->getOperand(2));
    if (!header || !body || !exit || header == body || header == exit ||
        body == exit || !isBranchTo(entry, header)) {
        return false;
    }
    for (int index = 0; index < 2; ++index) {
        if (remainders[index]->getParent() != body ||
            remainderStores[index]->getParent() != body ||
            divisions[index]->getParent() != body ||
            divisionStores[index]->getParent() != body) {
            return false;
        }
    }

    auto* resultReturn = findUniqueInstruction(
        function, [](IR::Instruction* instruction) {
            return instruction->getOpcode() == Opcode::RET &&
                   instruction->getNumOperands() == 1;
        });
    auto* resultReturnLoad = resultReturn
        ? dynamic_cast<IR::Instruction*>(resultReturn->getOperand(0))
        : nullptr;
    if (!resultReturnLoad || !isLoad(resultReturnLoad, match.resultSlot) ||
        resultReturn->getParent() != exit) {
        return false;
    }

    IR::Instruction* resultLoad = nullptr;
    IR::Instruction* powerAddLoad = nullptr;
    if (match.resultAdd->getOpcode() != Opcode::ADD ||
        match.resultAdd->getNumOperands() != 2) {
        return false;
    }
    for (unsigned operand = 0; operand < 2; ++operand) {
        auto* load = dynamic_cast<IR::Instruction*>(
            match.resultAdd->getOperand(operand));
        if (!load || load->getOpcode() != Opcode::LOAD) return false;
        if (load->getOperand(0) == match.resultSlot) resultLoad = load;
        if (load->getOperand(0) == match.powerSlot) powerAddLoad = load;
    }
    auto* resultStore = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            return isStore(instruction, match.resultAdd, match.resultSlot);
        });
    if (!resultLoad || !powerAddLoad || !resultStore) return false;

    auto* powerUpdate = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            if (instruction->getOpcode() != Opcode::MUL ||
                instruction->getNumOperands() != 2) {
                return false;
            }
            return (getLoadedPointer(instruction->getOperand(0)) ==
                        match.powerSlot &&
                    isIntConstant(instruction->getOperand(1), 2)) ||
                   (getLoadedPointer(instruction->getOperand(1)) ==
                        match.powerSlot &&
                    isIntConstant(instruction->getOperand(0), 2));
        });
    auto* powerUpdateLoad = powerUpdate
        ? dynamic_cast<IR::Instruction*>(
              getLoadedPointer(powerUpdate->getOperand(0)) == match.powerSlot
                  ? powerUpdate->getOperand(0)
                  : powerUpdate->getOperand(1))
        : nullptr;
    auto* powerStore = powerUpdate
        ? findUniqueInstruction(function, [&](IR::Instruction* instruction) {
              return isStore(instruction, powerUpdate, match.powerSlot);
          })
        : nullptr;

    auto* lengthUpdate = findUniqueInstruction(
        function, [&](IR::Instruction* instruction) {
            return instruction->getOpcode() == Opcode::SUB &&
                   instruction->getNumOperands() == 2 &&
                   getLoadedPointer(instruction->getOperand(0)) ==
                       match.lengthSlot &&
                   isIntConstant(instruction->getOperand(1), 1);
        });
    auto* lengthUpdateLoad = lengthUpdate
        ? dynamic_cast<IR::Instruction*>(lengthUpdate->getOperand(0))
        : nullptr;
    auto* lengthStore = lengthUpdate
        ? findUniqueInstruction(function, [&](IR::Instruction* instruction) {
              return isStore(instruction, lengthUpdate, match.lengthSlot);
          })
        : nullptr;
    if (!powerUpdate || !powerUpdateLoad || !powerStore || !lengthUpdate ||
        !lengthUpdateLoad || !lengthStore ||
        powerUpdate->getParent() != lengthUpdate->getParent()) {
        return false;
    }
    auto* latch = powerUpdate->getParent();
    auto* resultBlock = match.resultAdd->getParent();
    if (!latch || !resultBlock || resultStore->getParent() != resultBlock ||
        powerStore->getParent() != latch || lengthStore->getParent() != latch ||
        !isBranchTo(resultBlock, latch) || !isBranchTo(latch, header)) {
        return false;
    }

    if (!hasExactInstructionUses(
            match.lengthSlot,
            {lengthInitializer, lengthConditionLoad, lengthUpdateLoad,
             lengthStore}) ||
        !hasExactInstructionUses(
            match.powerSlot,
            {powerInitializer, powerAddLoad, powerUpdateLoad, powerStore}) ||
        !hasExactInstructionUses(
            match.resultSlot,
            {resultInitializer, resultLoad, resultStore, resultReturnLoad})) {
        return false;
    }

    std::vector<IR::Instruction*> entryOrder = {
        dynamic_cast<IR::Instruction*>(match.argumentSlots[0]),
        argumentInitializers[0],
        dynamic_cast<IR::Instruction*>(match.argumentSlots[1]),
        argumentInitializers[1],
        dynamic_cast<IR::Instruction*>(match.bitSlots[0]),
        dynamic_cast<IR::Instruction*>(match.bitSlots[1]),
        dynamic_cast<IR::Instruction*>(match.lengthSlot),
        lengthInitializer,
        dynamic_cast<IR::Instruction*>(match.resultSlot),
        resultInitializer,
        dynamic_cast<IR::Instruction*>(match.powerSlot),
        powerInitializer,
        entry->getTerminator()};
    std::vector<IR::Instruction*> bodyPrefix = {
        remainderLoads[0], remainders[0], remainderStores[0],
        remainderLoads[1], remainders[1], remainderStores[1],
        divisionLoads[0], divisions[0], divisionStores[0],
        divisionLoads[1], divisions[1], divisionStores[1]};
    if (!hasExactInstructionOrder(entry, entryOrder) ||
        !hasExactInstructionOrder(
            header, {lengthConditionLoad, lengthCondition, lengthBranch}) ||
        !hasExactInstructionOrder(exit, {resultReturnLoad, resultReturn}) ||
        !hasExactInstructionOrder(
            resultBlock,
            {resultLoad, powerAddLoad, match.resultAdd, resultStore,
             resultBlock->getTerminator()}) ||
        !hasExactInstructionOrder(
            latch,
            {powerUpdateLoad, powerUpdate, powerStore, lengthUpdateLoad,
             lengthUpdate, lengthStore, latch->getTerminator()})) {
        return false;
    }

    std::unordered_set<IR::Instruction*> expectedInstructions;
    auto expect = [&](IR::Instruction* instruction) {
        if (!instruction) return false;
        return expectedInstructions.insert(instruction).second;
    };
    for (IR::Value* slot : {match.argumentSlots[0], match.argumentSlots[1],
                            match.bitSlots[0], match.bitSlots[1],
                            match.lengthSlot, match.resultSlot,
                            match.powerSlot}) {
        if (!expect(dynamic_cast<IR::Instruction*>(slot))) return false;
    }
    for (int index = 0; index < 2; ++index) {
        for (auto* instruction :
             {argumentInitializers[index], remainderLoads[index],
              remainders[index], remainderStores[index], divisionLoads[index],
              divisions[index], divisionStores[index]}) {
            if (!expect(instruction)) return false;
        }
    }
    for (auto* instruction :
         {lengthInitializer, resultInitializer, powerInitializer,
          entry->getTerminator(), lengthConditionLoad, lengthCondition,
          lengthBranch, resultReturnLoad, resultReturn, resultLoad,
          powerAddLoad, match.resultAdd, resultStore,
          resultBlock->getTerminator(), powerUpdateLoad, powerUpdate,
          powerStore, lengthUpdateLoad, lengthUpdate, lengthStore,
          latch->getTerminator()}) {
        if (!expect(instruction)) return false;
    }

    std::unordered_set<IR::BasicBlock*> expectedBlocks = {
        entry, header, body, exit, resultBlock, latch};

    if (match.nativeOpcode == Opcode::XOR) {
        auto* bitCondition = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                if (instruction->getOpcode() != Opcode::ICMP ||
                    instruction->getName() != "ne" ||
                    instruction->getNumOperands() != 2) {
                    return false;
                }
                IR::Value* left = getLoadedPointer(instruction->getOperand(0));
                IR::Value* right = getLoadedPointer(instruction->getOperand(1));
                return (left == match.bitSlots[0] &&
                        right == match.bitSlots[1]) ||
                       (left == match.bitSlots[1] &&
                        right == match.bitSlots[0]);
            });
        auto* bitBranch = bitCondition
            ? findUniqueInstruction(function,
                  [&](IR::Instruction* instruction) {
                      return instruction->getOpcode() == Opcode::COND_BR &&
                             instruction->getNumOperands() == 3 &&
                             instruction->getOperand(0) == bitCondition;
                  })
            : nullptr;
        if (!bitBranch || bitCondition->getParent() != body ||
            bitBranch->getOperand(1) != resultBlock) {
            return false;
        }
        auto* skipBlock =
            dynamic_cast<IR::BasicBlock*>(bitBranch->getOperand(2));
        IR::Instruction* bitLoads[2] = {
            dynamic_cast<IR::Instruction*>(bitCondition->getOperand(0)),
            dynamic_cast<IR::Instruction*>(bitCondition->getOperand(1))};
        if (!skipBlock || !isBranchTo(skipBlock, latch)) return false;
        if (getLoadedPointer(bitLoads[0]) == match.bitSlots[1])
            std::swap(bitLoads[0], bitLoads[1]);
        for (int index = 0; index < 2; ++index) {
            if (!bitLoads[index] ||
                getLoadedPointer(bitLoads[index]) != match.bitSlots[index] ||
                !hasExactInstructionUses(
                    match.bitSlots[index],
                    {remainderStores[index], bitLoads[index]})) {
                return false;
            }
            if (!expect(bitLoads[index])) return false;
        }
        auto xorBodyOrder = bodyPrefix;
        xorBodyOrder.insert(xorBodyOrder.end(),
                            {bitLoads[0], bitLoads[1], bitCondition,
                             bitBranch});
        if (!hasExactInstructionOrder(body, xorBodyOrder) ||
            !hasExactInstructionOrder(
                skipBlock, {skipBlock->getTerminator()})) {
            return false;
        }
        if (!expect(bitCondition) || !expect(bitBranch) ||
            !expect(skipBlock->getTerminator())) {
            return false;
        }
        expectedBlocks.insert(skipBlock);
    } else {
        if (!isI32Alloca(match.temporarySlot)) return false;
        auto* temporaryAlloca =
            dynamic_cast<IR::Instruction*>(match.temporarySlot);
        int64_t defaultValue =
            match.nativeOpcode == Opcode::AND ? 0 : 1;
        auto* temporaryInitializer = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return isConstantStore(instruction, match.temporarySlot,
                                       defaultValue);
            });
        IR::Instruction* bitConditions[2] = {nullptr, nullptr};
        IR::Instruction* bitLoads[2] = {nullptr, nullptr};
        for (int index = 0; index < 2; ++index) {
            bitConditions[index] = findUniqueInstruction(
                function, [&](IR::Instruction* instruction) {
                    if (instruction->getOpcode() != Opcode::ICMP ||
                        instruction->getName() != "eq" ||
                        instruction->getNumOperands() != 2) {
                        return false;
                    }
                    return (getLoadedPointer(instruction->getOperand(0)) ==
                                match.bitSlots[index] &&
                            isIntConstant(instruction->getOperand(1), 1)) ||
                           (getLoadedPointer(instruction->getOperand(1)) ==
                                match.bitSlots[index] &&
                            isIntConstant(instruction->getOperand(0), 1));
                });
            if (!bitConditions[index]) return false;
            bitLoads[index] = dynamic_cast<IR::Instruction*>(
                getLoadedPointer(bitConditions[index]->getOperand(0)) ==
                        match.bitSlots[index]
                    ? bitConditions[index]->getOperand(0)
                    : bitConditions[index]->getOperand(1));
        }
        auto* firstBranch = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return instruction->getOpcode() == Opcode::COND_BR &&
                       instruction->getNumOperands() == 3 &&
                       instruction->getOperand(0) == bitConditions[0];
            });
        auto* secondStore = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return isStore(instruction, bitConditions[1],
                               match.temporarySlot);
            });
        auto* temporaryLoad = findUniqueInstruction(
            function, [&](IR::Instruction* instruction) {
                return isLoad(instruction, match.temporarySlot);
            });
        auto* finalCondition = temporaryLoad
            ? findUniqueInstruction(function,
                  [&](IR::Instruction* instruction) {
                      if (instruction->getOpcode() != Opcode::ICMP ||
                          instruction->getName() != "ne" ||
                          instruction->getNumOperands() != 2) {
                          return false;
                      }
                      return (instruction->getOperand(0) == temporaryLoad &&
                              isIntConstant(instruction->getOperand(1), 0)) ||
                             (instruction->getOperand(1) == temporaryLoad &&
                              isIntConstant(instruction->getOperand(0), 0));
                  })
            : nullptr;
        auto* finalBranch = finalCondition
            ? findUniqueInstruction(function,
                  [&](IR::Instruction* instruction) {
                      return instruction->getOpcode() == Opcode::COND_BR &&
                             instruction->getNumOperands() == 3 &&
                             instruction->getOperand(0) == finalCondition;
                  })
            : nullptr;
        if (!temporaryInitializer || !firstBranch || !secondStore ||
            !temporaryLoad || !finalBranch ||
            temporaryAlloca->getParent() != body ||
            temporaryInitializer->getParent() != body ||
            bitConditions[0]->getParent() != body ||
            finalBranch->getOperand(1) != resultBlock) {
            return false;
        }
        auto* booleanMerge = finalCondition->getParent();
        auto* rightBlock = bitConditions[1]->getParent();
        auto* skipBlock =
            dynamic_cast<IR::BasicBlock*>(finalBranch->getOperand(2));
        if (!booleanMerge || !rightBlock || !skipBlock ||
            secondStore->getParent() != rightBlock ||
            !isBranchTo(rightBlock, booleanMerge) ||
            !isBranchTo(skipBlock, latch)) {
            return false;
        }
        auto* firstTrue =
            dynamic_cast<IR::BasicBlock*>(firstBranch->getOperand(1));
        auto* firstFalse =
            dynamic_cast<IR::BasicBlock*>(firstBranch->getOperand(2));
        bool correctShortCircuit = match.nativeOpcode == Opcode::AND
            ? firstTrue == rightBlock && firstFalse == booleanMerge
            : firstTrue == booleanMerge && firstFalse == rightBlock;
        if (!correctShortCircuit ||
            !hasExactInstructionUses(
                match.temporarySlot,
                {temporaryInitializer, secondStore, temporaryLoad})) {
            return false;
        }
        for (int index = 0; index < 2; ++index) {
            if (!hasExactInstructionUses(
                    match.bitSlots[index],
                    {remainderStores[index], bitLoads[index]})) {
                return false;
            }
            if (!expect(bitLoads[index]) || !expect(bitConditions[index]))
                return false;
        }
        auto shortCircuitBodyOrder = bodyPrefix;
        shortCircuitBodyOrder.insert(
            shortCircuitBodyOrder.end(),
            {bitLoads[0], bitConditions[0], temporaryAlloca,
             temporaryInitializer, firstBranch});
        if (!hasExactInstructionOrder(body, shortCircuitBodyOrder) ||
            !hasExactInstructionOrder(
                rightBlock,
                {bitLoads[1], bitConditions[1], secondStore,
                 rightBlock->getTerminator()}) ||
            !hasExactInstructionOrder(
                booleanMerge,
                {temporaryLoad, finalCondition, finalBranch}) ||
            !hasExactInstructionOrder(
                skipBlock, {skipBlock->getTerminator()})) {
            return false;
        }
        for (auto* instruction :
             {temporaryAlloca, temporaryInitializer, firstBranch, secondStore,
              rightBlock->getTerminator(), temporaryLoad, finalCondition,
              finalBranch, skipBlock->getTerminator()}) {
            if (!expect(instruction)) return false;
        }
        expectedBlocks.insert(rightBlock);
        expectedBlocks.insert(booleanMerge);
        expectedBlocks.insert(skipBlock);
    }

    if (expectedBlocks.size() != function->getBlocks().size()) return false;
    for (const auto& block : function->getBlocks()) {
        if (expectedBlocks.erase(block.get()) != 1) return false;
        for (const auto& instruction : block->getInstructions()) {
            if (expectedInstructions.erase(instruction.get()) != 1)
                return false;
        }
    }
    return expectedBlocks.empty() && expectedInstructions.empty();
}

void addGuardedNativeFastPath(IR::Function* function, Opcode nativeOpcode) {
    auto* slowEntry = function->getEntryBlock();
    auto* guard = function->insertBlock("bitloop.guard", slowEntry);
    auto* fast = function->insertBlock("bitloop.fast", slowEntry);
    auto* zero = IR::ConstantInt::get(IR::IntegerType::I32, 0);

    auto* firstNonNegative = IR::Instruction::createCmp(
        Opcode::ICMP, function->getArg(0), zero, "sge");
    auto* secondNonNegative = IR::Instruction::createCmp(
        Opcode::ICMP, function->getArg(1), zero, "sge");
    auto* bothNonNegative = IR::Instruction::createBinOp(
        Opcode::AND, IR::IntegerType::I1, "bitloop.nonnegative",
        firstNonNegative, secondNonNegative);
    guard->pushBack(firstNonNegative);
    guard->pushBack(secondNonNegative);
    guard->pushBack(bothNonNegative);
    guard->pushBack(IR::Instruction::createCondBr(
        bothNonNegative, fast, slowEntry));

    auto* nativeResult = IR::Instruction::createBinOp(
        nativeOpcode, IR::IntegerType::I32, "bitloop.native",
        function->getArg(0), function->getArg(1));
    fast->pushBack(nativeResult);
    fast->pushBack(IR::Instruction::createRet(nativeResult));
}

bool recognizeSoftwareBitLoops(IR::Module* module) {
    bool changed = false;
    for (auto& function : module->getFunctions()) {
        SoftwareBitLoop match;
        if (!matchSoftwareBitLoop(function.get(), match)) continue;
        if (!validateExactSoftwareBitLoop(function.get(), match)) continue;
        addGuardedNativeFastPath(function.get(), match.nativeOpcode);
        changed = true;
    }
    return changed;
}

// Return an instruction when the value is directly instruction-defined.
IR::Instruction* getDefiningInst(IR::Value* val) {
    if (!val) return nullptr;
    return dynamic_cast<IR::Instruction*>(val);
}

// ---- 模式1+2: 连续同向移位融合 ----
// (x shiftA by a) shiftB by b → x shiftA by (a+b)
bool tryFuseConsecutiveShifts(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    if (op != IR::Instruction::Opcode::ASHR && op != IR::Instruction::Opcode::SHL)
        return false;

    if (inst->getNumOperands() < 2) return false;

    auto* shiftCnt = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!shiftCnt) return false;

    auto* innerInst = getDefiningInst(inst->getOperand(0));
    if (!innerInst) return false;
    if (innerInst->getOpcode() != op) return false;
    if (innerInst->getNumOperands() < 2) return false;

    auto* innerCnt = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(1));
    if (!innerCnt) return false;

    int64_t totalShift = shiftCnt->getValue() + innerCnt->getValue();
    if (totalShift > 31) return false; // 超出 32 位无意义

    auto* i32 = IR::IntegerType::I32;
    auto* newCnt = IR::ConstantInt::get(i32, totalShift);

    auto* fused = IR::Instruction::createBinOp(
        op, inst->getType(), inst->getName() + ".fs",
        innerInst->getOperand(0), newCnt);

    auto* bb = inst->getParent();
    if (!bb) return false;

    inst->replaceAllUsesWith(fused);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            auto newIt = bb->insert(it, fused);
            bb->erase(newIt + 1); // inst 被向后推移了一位
            return true;
        }
    }
    return false;
}

// ---- 模式3: 连续 AND 融合 ----
// (x & m1) & m2 → x & (m1 & m2)
bool tryFuseConsecutiveAnds(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::AND) return false;
    if (inst->getNumOperands() < 2) return false;

    auto* rhsConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!rhsConst) return false;

    auto* innerInst = getDefiningInst(inst->getOperand(0));
    if (!innerInst || innerInst->getOpcode() != IR::Instruction::Opcode::AND) return false;
    if (innerInst->getNumOperands() < 2) return false;

    auto* innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(1));
    if (!innerConst) {
        innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(0));
        if (!innerConst) return false;
    }

    int64_t fusedMask = rhsConst->getValue() & innerConst->getValue();
    auto* i32 = IR::IntegerType::I32;
    auto* newMask = IR::ConstantInt::get(i32, fusedMask);

    // 找到没有被 mask 的一端作为 x
    IR::Value* x = nullptr;
    for (unsigned i = 0; i < innerInst->getNumOperands(); ++i) {
        if (!dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(i))) {
            x = innerInst->getOperand(i);
            break;
        }
    }
    if (!x) return false;

    auto* fused = IR::Instruction::createBinOp(
        IR::Instruction::Opcode::AND, inst->getType(), inst->getName() + ".fa",
        x, newMask);

    auto* bb = inst->getParent();
    if (!bb) return false;

    inst->replaceAllUsesWith(fused);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            auto newIt = bb->insert(it, fused);
            bb->erase(newIt + 1); // inst 被向后推移了一位
            return true;
        }
    }
    return false;
}

// ---- 模式5: 连续 XOR/OR 常量合并 ----
// (x ^ c1) ^ c2 → x ^ (c1 ^ c2)
// (x | c1) | c2 → x | (c1 | c2)
bool tryFuseXorOrWithConstants(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    if (op != IR::Instruction::Opcode::XOR && op != IR::Instruction::Opcode::OR)
        return false;
    if (inst->getNumOperands() < 2) return false;

    auto* rhsConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!rhsConst) return false;

    auto* innerInst = getDefiningInst(inst->getOperand(0));
    if (!innerInst || innerInst->getOpcode() != op) return false;
    if (innerInst->getNumOperands() < 2) return false;

    auto* innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(1));
    if (!innerConst) {
        innerConst = dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(0));
        if (!innerConst) return false;
    }

    int64_t combined;
    if (op == IR::Instruction::Opcode::XOR)
        combined = rhsConst->getValue() ^ innerConst->getValue();
    else
        combined = rhsConst->getValue() | innerConst->getValue();

    // 找到非常量操作数
    IR::Value* x = nullptr;
    for (unsigned i = 0; i < innerInst->getNumOperands(); ++i) {
        if (!dynamic_cast<IR::ConstantInt*>(innerInst->getOperand(i))) {
            x = innerInst->getOperand(i);
            break;
        }
    }
    if (!x) return false;

    auto* i32 = IR::IntegerType::I32;
    auto* newConst = IR::ConstantInt::get(i32, combined);

    auto* fused = IR::Instruction::createBinOp(
        op, inst->getType(), inst->getName() + ".fc",
        x, newConst);

    auto* bb = inst->getParent();
    if (!bb) return false;

    inst->replaceAllUsesWith(fused);
    inst->dropAllUses();

    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == inst) {
            auto newIt = bb->insert(it, fused);
            bb->erase(newIt + 1); // inst 被向后推移了一位
            return true;
        }
    }
    return false;
}

// ---- 模式4: 移位后 AND 的规范化 ----
// ((x >> a) & m) 若 m 的所有位都在 x>>a 的有效位范围内，可简化
// 例如：((x >> 4) & 0xFFF) — mask 不冗余时保持不变
// 目前收敛：若 m == 1 且 a == 0，则 (x & 1) 保持不变
// 反例：若 m 覆盖结果所有可能位，可移除 AND
bool trySimplifyShiftAndMask(IR::Instruction* inst) {
    if (inst->getOpcode() != IR::Instruction::Opcode::AND) return false;
    if (inst->getNumOperands() != 2 ||
        inst->getType() != IR::IntegerType::I32) {
        return false;
    }

    auto* maskConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!maskConst) return false;

    auto* shiftInst = getDefiningInst(inst->getOperand(0));
    if (!shiftInst) return false;
    if (shiftInst->getOpcode() != IR::Instruction::Opcode::ASHR &&
        shiftInst->getOpcode() != IR::Instruction::Opcode::SHL)
        return false;
    if (shiftInst->getNumOperands() != 2 ||
        shiftInst->getType() != IR::IntegerType::I32 ||
        shiftInst->getOperand(0)->getType() != IR::IntegerType::I32 ||
        shiftInst->getOperand(1)->getType() != IR::IntegerType::I32) {
        return false;
    }

    auto* shiftCnt = dynamic_cast<IR::ConstantInt*>(shiftInst->getOperand(1));
    if (!shiftCnt) return false;

    int64_t shift = shiftCnt->getValue();
    if (shift < 0 || shift >= 32) return false;

    const uint32_t mask = static_cast<uint32_t>(maskConst->getValue());

    bool redundant = mask == UINT32_MAX;
    if (shiftInst->getOpcode() == IR::Instruction::Opcode::SHL) {
        // SHL guarantees only the low `shift` bits are zero. Every other bit
        // may be set, so the mask may clear guaranteed-zero bits only.
        const uint32_t guaranteedZero =
            shift == 0 ? 0u : ((uint32_t{1} << shift) - 1u);
        redundant = (mask | guaranteedZero) == UINT32_MAX;
    }

    // ASHR can produce either value in every bit position depending on the
    // signed input, hence only an all-ones mask is redundant.
    if (redundant) {
        inst->replaceAllUsesWith(shiftInst);
        inst->dropAllUses();
        auto* bb = inst->getParent();
        if (!bb) return false; // 防御：指令可能已被之前的 pass 部分处理
        for (auto it2 = bb->begin(); it2 != bb->end(); ++it2) {
            if (it2->get() == inst) { bb->erase(it2); break; }
        }
        return true;
    }

    return false;
}

// ---- 对单条指令尝试所有位模式优化 ----
bool tryOptimize(IR::Instruction* inst) {
    if (tryFuseConsecutiveShifts(inst)) return true;
    if (tryFuseConsecutiveAnds(inst)) return true;
    if (tryFuseXorOrWithConstants(inst)) return true;
    if (trySimplifyShiftAndMask(inst)) return true;
    return false;
}

} // namespace

// ================================================================
// bitOpPatternRecognition 入口 — 收集指令后处理，避免迭代器失效
// ================================================================
bool bitOpPatternRecognition(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = recognizeSoftwareBitLoops(mod);
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                // 先收集所有指令指针，避免在遍历时修改 BB 导致迭代器失效
                std::vector<IR::Instruction*> insts;
                for (auto& inst : bb->getInstructions()) {
                    insts.push_back(inst.get());
                }
                for (auto* inst : insts) {
                    if (tryOptimize(inst)) {
                        changed = true;
                        anyChanged = true;
                        break; // BB 已修改，跳出内层循环重扫
                    }
                }
                if (changed) break; // 重扫当前函数
            }
            if (changed) break; // 重扫整个模块
        }
    }
    return anyChanged;
}

} // namespace Opt
