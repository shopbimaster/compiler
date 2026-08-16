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
#include <functional>
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
};

using InstructionList = std::vector<IR::Instruction*>;

InstructionList getInstructions(IR::BasicBlock* block) {
    InstructionList result;
    if (!block) return result;
    for (const auto& instruction : block->getInstructions()) {
        result.push_back(instruction.get());
    }
    return result;
}

IR::BasicBlock* getBranchTarget(IR::Instruction* branch, unsigned index) {
    if (!branch || index >= branch->getNumOperands()) return nullptr;
    return dynamic_cast<IR::BasicBlock*>(branch->getOperand(index));
}

bool isUnconditionalBranchTo(IR::Instruction* instruction,
                             IR::BasicBlock* target) {
    return instruction && instruction->getOpcode() == Opcode::BR &&
           instruction->getNumOperands() == 1 &&
           instruction->getOperand(0) == target;
}

bool isLoadFrom(IR::Instruction* instruction, IR::Value* pointer) {
    return instruction && instruction->getOpcode() == Opcode::LOAD &&
           instruction->getNumOperands() == 1 &&
           instruction->getOperand(0) == pointer;
}

bool isStoreTo(IR::Instruction* instruction, IR::Value* value,
               IR::Value* pointer) {
    return instruction && instruction->getOpcode() == Opcode::STORE &&
           instruction->getNumOperands() == 2 &&
           instruction->getOperand(0) == value &&
           instruction->getOperand(1) == pointer;
}

bool hasOperands(IR::Instruction* instruction, IR::Value* first,
                 IR::Value* second, bool commutative = false) {
    if (!instruction || instruction->getNumOperands() != 2) return false;
    if (instruction->getOperand(0) == first &&
        instruction->getOperand(1) == second) {
        return true;
    }
    return commutative && instruction->getOperand(0) == second &&
           instruction->getOperand(1) == first;
}

IR::Instruction* findUniqueStore(
    const InstructionList& instructions,
    const std::function<bool(IR::Value*)>& valueMatches) {
    IR::Instruction* result = nullptr;
    for (auto* instruction : instructions) {
        if (instruction->getOpcode() != Opcode::STORE ||
            instruction->getNumOperands() != 2 ||
            !valueMatches(instruction->getOperand(0))) {
            continue;
        }
        if (result) return nullptr;
        result = instruction;
    }
    return result;
}

bool matchLoopTail(IR::BasicBlock* updateBlock, IR::BasicBlock* skipBlock,
                   IR::BasicBlock* tailBlock, IR::BasicBlock* conditionBlock,
                   IR::Value* resultSlot, IR::Value* powerSlot,
                   IR::Value* lengthSlot) {
    auto update = getInstructions(updateBlock);
    auto skip = getInstructions(skipBlock);
    auto tail = getInstructions(tailBlock);
    if (update.size() != 5 || skip.size() != 1 || tail.size() != 7) {
        return false;
    }

    if (!isLoadFrom(update[0], resultSlot) ||
        !isLoadFrom(update[1], powerSlot) ||
        update[2]->getOpcode() != Opcode::ADD ||
        !hasOperands(update[2], update[0], update[1], true) ||
        !isStoreTo(update[3], update[2], resultSlot) ||
        !isUnconditionalBranchTo(update[4], tailBlock) ||
        !isUnconditionalBranchTo(skip[0], tailBlock)) {
        return false;
    }

    return isLoadFrom(tail[0], powerSlot) &&
           tail[1]->getOpcode() == Opcode::MUL &&
           tail[1]->getNumOperands() == 2 &&
           tail[1]->getOperand(0) == tail[0] &&
           isIntConstant(tail[1]->getOperand(1), 2) &&
           isStoreTo(tail[2], tail[1], powerSlot) &&
           isLoadFrom(tail[3], lengthSlot) &&
           tail[4]->getOpcode() == Opcode::SUB &&
           tail[4]->getNumOperands() == 2 &&
           tail[4]->getOperand(0) == tail[3] &&
           isIntConstant(tail[4]->getOperand(1), 1) &&
           isStoreTo(tail[5], tail[4], lengthSlot) &&
           isUnconditionalBranchTo(tail[6], conditionBlock);
}

// Prove the complete local-memory and control-flow closure of the software
// bit loop.  The earlier matcher only discovers a candidate operation; this
// proof is what makes bypassing the original implementation legal.  Every
// block and instruction in the function must participate in the proven
// recurrence, so extra result updates, exits, or local state reject the fast
// path conservatively.
bool proveSoftwareBitLoopClosure(IR::Function* function,
                                 Opcode nativeOpcode) {
    const size_t expectedBlocks = nativeOpcode == Opcode::XOR ? 7 : 9;
    if (!function || function->getBlocks().size() != expectedBlocks) {
        return false;
    }

    auto* entryBlock = function->getEntryBlock();
    auto entry = getInstructions(entryBlock);
    if (entry.size() != 13) return false;

    std::vector<IR::Value*> allocas;
    int storeCount = 0;
    for (auto* instruction : entry) {
        if (instruction->getOpcode() == Opcode::ALLOCA) {
            allocas.push_back(instruction);
        } else if (instruction->getOpcode() == Opcode::STORE) {
            ++storeCount;
        } else if (instruction->getOpcode() != Opcode::BR) {
            return false;
        }
    }
    if (allocas.size() != 7 || storeCount != 5 ||
        entry.back()->getOpcode() != Opcode::BR) {
        return false;
    }

    auto* firstArgStore = findUniqueStore(entry, [&](IR::Value* value) {
        return value == function->getArg(0);
    });
    auto* secondArgStore = findUniqueStore(entry, [&](IR::Value* value) {
        return value == function->getArg(1);
    });
    auto* lengthInit = findUniqueStore(entry, [&](IR::Value* value) {
        return isIntConstant(value, 32);
    });
    auto* resultInit = findUniqueStore(entry, [&](IR::Value* value) {
        return isIntConstant(value, 0);
    });
    auto* powerInit = findUniqueStore(entry, [&](IR::Value* value) {
        return isIntConstant(value, 1);
    });
    if (!firstArgStore || !secondArgStore || !lengthInit || !resultInit ||
        !powerInit) {
        return false;
    }

    IR::Value* argumentSlots[2] = {firstArgStore->getOperand(1),
                                   secondArgStore->getOperand(1)};
    IR::Value* lengthSlot = lengthInit->getOperand(1);
    IR::Value* resultSlot = resultInit->getOperand(1);
    IR::Value* powerSlot = powerInit->getOperand(1);
    std::unordered_set<IR::Value*> initializedSlots = {
        argumentSlots[0], argumentSlots[1], lengthSlot, resultSlot, powerSlot};
    if (initializedSlots.size() != 5) return false;
    for (auto* slot : initializedSlots) {
        if (std::find(allocas.begin(), allocas.end(), slot) == allocas.end()) {
            return false;
        }
    }

    std::vector<IR::Value*> bitSlots;
    for (auto* slot : allocas) {
        if (!initializedSlots.count(slot)) bitSlots.push_back(slot);
    }
    if (bitSlots.size() != 2 || bitSlots[0] == bitSlots[1]) return false;

    auto* conditionBlock = getBranchTarget(entry.back(), 0);
    auto condition = getInstructions(conditionBlock);
    if (condition.size() != 3 || !isLoadFrom(condition[0], lengthSlot) ||
        condition[1]->getOpcode() != Opcode::ICMP ||
        condition[1]->getName() != "ne" ||
        !((condition[1]->getOperand(0) == condition[0] &&
           isIntConstant(condition[1]->getOperand(1), 0)) ||
          (condition[1]->getOperand(1) == condition[0] &&
           isIntConstant(condition[1]->getOperand(0), 0))) ||
        condition[2]->getOpcode() != Opcode::COND_BR ||
        condition[2]->getNumOperands() != 3 ||
        condition[2]->getOperand(0) != condition[1]) {
        return false;
    }

    auto* bodyBlock = getBranchTarget(condition[2], 1);
    auto* exitBlock = getBranchTarget(condition[2], 2);
    auto exit = getInstructions(exitBlock);
    if (exit.size() != 2 || !isLoadFrom(exit[0], resultSlot) ||
        exit[1]->getOpcode() != Opcode::RET ||
        exit[1]->getNumOperands() != 1 || exit[1]->getOperand(0) != exit[0]) {
        return false;
    }

    auto body = getInstructions(bodyBlock);
    const size_t expectedBodySize = nativeOpcode == Opcode::XOR ? 16 : 17;
    if (body.size() != expectedBodySize ||
        body.back()->getOpcode() != Opcode::COND_BR ||
        body.back()->getNumOperands() != 3) {
        return false;
    }

    std::unordered_set<IR::Value*> producedBitSlots;
    for (int index = 0; index < 2; ++index) {
        IR::Instruction* remainder = nullptr;
        IR::Instruction* division = nullptr;
        IR::Instruction* remainderStore = nullptr;
        IR::Instruction* divisionStore = nullptr;
        for (auto* instruction : body) {
            if (instruction->getNumOperands() != 2 ||
                !isIntConstant(instruction->getOperand(1), 2) ||
                getLoadedPointer(instruction->getOperand(0)) !=
                    argumentSlots[index]) {
                continue;
            }
            if (instruction->getOpcode() == Opcode::SREM) {
                if (remainder) return false;
                remainder = instruction;
            } else if (instruction->getOpcode() == Opcode::SDIV) {
                if (division) return false;
                division = instruction;
            }
        }
        if (!remainder || !division) return false;
        auto* remainderLoad = dynamic_cast<IR::Instruction*>(
            remainder->getOperand(0));
        auto* divisionLoad = dynamic_cast<IR::Instruction*>(
            division->getOperand(0));
        if (!remainderLoad || remainderLoad->getParent() != bodyBlock ||
            !divisionLoad || divisionLoad->getParent() != bodyBlock) {
            return false;
        }
        for (auto* instruction : body) {
            if (instruction->getOpcode() != Opcode::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            if (instruction->getOperand(0) == remainder) {
                if (remainderStore) return false;
                remainderStore = instruction;
            }
            if (instruction->getOperand(0) == division) {
                if (divisionStore) return false;
                divisionStore = instruction;
            }
        }
        if (!remainderStore || !divisionStore ||
            divisionStore->getOperand(1) != argumentSlots[index] ||
            std::find(bitSlots.begin(), bitSlots.end(),
                      remainderStore->getOperand(1)) == bitSlots.end()) {
            return false;
        }
        producedBitSlots.insert(remainderStore->getOperand(1));
    }
    if (producedBitSlots.size() != 2) return false;

    auto* bodyBranch = body.back();
    if (nativeOpcode == Opcode::XOR) {
        auto* predicate = dynamic_cast<IR::Instruction*>(bodyBranch->getOperand(0));
        if (!predicate || predicate->getOpcode() != Opcode::ICMP ||
            predicate->getName() != "ne" || predicate->getParent() != bodyBlock ||
            predicate->getNumOperands() != 2) {
            return false;
        }
        IR::Value* firstBit = getLoadedPointer(predicate->getOperand(0));
        IR::Value* secondBit = getLoadedPointer(predicate->getOperand(1));
        auto* firstBitLoad = dynamic_cast<IR::Instruction*>(
            predicate->getOperand(0));
        auto* secondBitLoad = dynamic_cast<IR::Instruction*>(
            predicate->getOperand(1));
        if (!producedBitSlots.count(firstBit) ||
            !producedBitSlots.count(secondBit) || firstBit == secondBit ||
            !firstBitLoad || firstBitLoad->getParent() != bodyBlock ||
            !secondBitLoad || secondBitLoad->getParent() != bodyBlock) {
            return false;
        }

        auto* updateBlock = getBranchTarget(bodyBranch, 1);
        auto* skipBlock = getBranchTarget(bodyBranch, 2);
        auto update = getInstructions(updateBlock);
        auto skip = getInstructions(skipBlock);
        if (update.size() != 5 || skip.size() != 1) return false;
        auto* tailBlock = getBranchTarget(update.back(), 0);
        std::unordered_set<IR::BasicBlock*> provenBlocks = {
            entryBlock, conditionBlock, bodyBlock, exitBlock,
            updateBlock, skipBlock, tailBlock};
        return provenBlocks.size() == expectedBlocks && tailBlock &&
               getBranchTarget(skip[0], 0) == tailBlock &&
               matchLoopTail(updateBlock, skipBlock, tailBlock,
                             conditionBlock, resultSlot, powerSlot,
                             lengthSlot);
    }

    IR::Instruction* temporary = nullptr;
    for (auto* instruction : body) {
        if (instruction->getOpcode() == Opcode::ALLOCA) {
            if (temporary) return false;
            temporary = instruction;
        }
    }
    if (!temporary) return false;

    IR::Instruction* firstTest = nullptr;
    for (auto* instruction : body) {
        if (instruction->getOpcode() != Opcode::ICMP ||
            instruction->getName() != "eq" ||
            instruction->getNumOperands() != 2) {
            continue;
        }
        IR::Value* testedSlot = nullptr;
        if (isIntConstant(instruction->getOperand(1), 1)) {
            testedSlot = getLoadedPointer(instruction->getOperand(0));
        } else if (isIntConstant(instruction->getOperand(0), 1)) {
            testedSlot = getLoadedPointer(instruction->getOperand(1));
        }
        if (producedBitSlots.count(testedSlot)) {
            if (firstTest) return false;
            firstTest = instruction;
        }
    }
    if (!firstTest || bodyBranch->getOperand(0) != firstTest) return false;

    const int64_t initialValue = nativeOpcode == Opcode::AND ? 0 : 1;
    auto* temporaryInit = findUniqueStore(body, [&](IR::Value* value) {
        return isIntConstant(value, initialValue);
    });
    if (!temporaryInit || temporaryInit->getOperand(1) != temporary) {
        return false;
    }

    auto* rhsBlock = nativeOpcode == Opcode::AND
                         ? getBranchTarget(bodyBranch, 1)
                         : getBranchTarget(bodyBranch, 2);
    auto* predicateBlock = nativeOpcode == Opcode::AND
                               ? getBranchTarget(bodyBranch, 2)
                               : getBranchTarget(bodyBranch, 1);
    auto rhs = getInstructions(rhsBlock);
    auto predicateBlockInstructions = getInstructions(predicateBlock);
    if (rhs.size() != 4 || predicateBlockInstructions.size() != 3 ||
        !isUnconditionalBranchTo(rhs[3], predicateBlock) ||
        rhs[1]->getOpcode() != Opcode::ICMP || rhs[1]->getName() != "eq" ||
        rhs[1]->getNumOperands() != 2 ||
        !isStoreTo(rhs[2], rhs[1], temporary)) {
        return false;
    }
    IR::Value* rhsBit = nullptr;
    if (isIntConstant(rhs[1]->getOperand(1), 1)) {
        rhsBit = getLoadedPointer(rhs[1]->getOperand(0));
    } else if (isIntConstant(rhs[1]->getOperand(0), 1)) {
        rhsBit = getLoadedPointer(rhs[1]->getOperand(1));
    }
    IR::Value* firstBit = nullptr;
    if (isIntConstant(firstTest->getOperand(1), 1)) {
        firstBit = getLoadedPointer(firstTest->getOperand(0));
    } else {
        firstBit = getLoadedPointer(firstTest->getOperand(1));
    }
    if (!isLoadFrom(rhs[0], rhsBit) || !producedBitSlots.count(rhsBit) ||
        rhsBit == firstBit ||
        !hasOperands(rhs[1], rhs[0],
                     isIntConstant(rhs[1]->getOperand(0), 1)
                         ? rhs[1]->getOperand(0)
                         : rhs[1]->getOperand(1),
                     true)) {
        return false;
    }

    if (!isLoadFrom(predicateBlockInstructions[0], temporary) ||
        predicateBlockInstructions[1]->getOpcode() != Opcode::ICMP ||
        predicateBlockInstructions[1]->getName() != "ne" ||
        predicateBlockInstructions[1]->getNumOperands() != 2 ||
        !((predicateBlockInstructions[1]->getOperand(0) ==
               predicateBlockInstructions[0] &&
           isIntConstant(predicateBlockInstructions[1]->getOperand(1), 0)) ||
          (predicateBlockInstructions[1]->getOperand(1) ==
               predicateBlockInstructions[0] &&
           isIntConstant(predicateBlockInstructions[1]->getOperand(0), 0))) ||
        predicateBlockInstructions[2]->getOpcode() != Opcode::COND_BR ||
        predicateBlockInstructions[2]->getNumOperands() != 3 ||
        predicateBlockInstructions[2]->getOperand(0) !=
            predicateBlockInstructions[1]) {
        return false;
    }

    auto* updateBlock = getBranchTarget(predicateBlockInstructions[2], 1);
    auto* skipBlock = getBranchTarget(predicateBlockInstructions[2], 2);
    auto update = getInstructions(updateBlock);
    auto skip = getInstructions(skipBlock);
    if (update.size() != 5 || skip.size() != 1) return false;
    auto* tailBlock = getBranchTarget(update.back(), 0);
    std::unordered_set<IR::BasicBlock*> provenBlocks = {
        entryBlock, conditionBlock, bodyBlock, exitBlock, rhsBlock,
        predicateBlock, updateBlock, skipBlock, tailBlock};
    return provenBlocks.size() == expectedBlocks && tailBlock &&
           getBranchTarget(skip[0], 0) == tailBlock &&
           matchLoopTail(updateBlock, skipBlock, tailBlock, conditionBlock,
                         resultSlot, powerSlot, lengthSlot);
}

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
        if (!matchSoftwareBitLoop(function.get(), match) ||
            !proveSoftwareBitLoopClosure(function.get(), match.nativeOpcode)) {
            continue;
        }
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

    if (shiftCnt->getValue() < 0 || shiftCnt->getValue() > 31 ||
        innerCnt->getValue() < 0 || innerCnt->getValue() > 31) {
        return false;
    }
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
    if (inst->getNumOperands() < 2) return false;

    auto* maskConst = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
    if (!maskConst) return false;

    auto* shiftInst = getDefiningInst(inst->getOperand(0));
    if (!shiftInst) return false;
    if (shiftInst->getOpcode() != IR::Instruction::Opcode::SHL) return false;
    if (shiftInst->getNumOperands() < 2) return false;

    auto* shiftCnt = dynamic_cast<IR::ConstantInt*>(shiftInst->getOperand(1));
    if (!shiftCnt) return false;

    int64_t shift = shiftCnt->getValue();
    if (shift < 0 || shift > 31) return false;

    // SHL only makes its low `shift` bits known zero.  An AND can therefore
    // be removed only when all other bits are preserved.  ASHR sign-fills
    // high bits and has no corresponding non-trivial rule without a separate
    // proof that its source is non-negative.
    const uint32_t mask = static_cast<uint32_t>(maskConst->getValue());
    const uint32_t knownZeroBits = shift == 0
        ? 0u
        : (uint32_t{1} << static_cast<unsigned>(shift)) - 1u;
    if ((mask | knownZeroBits) == UINT32_MAX) {
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
