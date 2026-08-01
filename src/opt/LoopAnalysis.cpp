// ================================================================
// LoopAnalysis — 循环分析基础设施（合并实现）
// ----------------------------------------------------------------
// 合并自：MemoryAccessAnalysis / AffineRecurrenceAnalysis /
//         ScalarReductionAnalysis / LoopPatternAnalysis
// 四个模块语义同族、互相紧耦合（AffineRecurrence 依赖 MemoryAccess 的
// PointerAccess/collectPointerAccess；LoopPattern 依赖 Optimizer 的
// findNaturalLoops）。合并后单一头文件 LoopAnalysis.h 统一对外。
// ----------------------------------------------------------------
// 合并说明：仅做物理拼接 + 去重 `using Opc` 与 #include，
//           未改动任何函数体逻辑。
// ================================================================

#include "opt/LoopAnalysis.h"

#include "opt/Optimizer.h"  // findNaturalLoops / NaturalLoop（LoopPatternAnalysis 依赖）

#include <unordered_set>
#include <utility>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

// ================================================================
// 一、MemoryAccessAnalysis — 内部辅助
// ================================================================

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool collectPointerAccessImpl(
    IR::Value* value,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access,
    std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return false;

    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        access.root = argument;
        visiting.erase(value);
        return true;
    }
    if (auto* global = dynamic_cast<IR::GlobalVariable*>(value)) {
        access.root = global;
        visiting.erase(value);
        return true;
    }

    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction) {
        visiting.erase(value);
        return false;
    }

    if (instruction->getOpcode() == Opc::LOAD && argumentMap &&
        instruction->getNumOperands() == 1) {
        auto found = argumentMap->find(instruction->getOperand(0));
        if (found != argumentMap->end()) {
            access.root = found->second;
            visiting.erase(value);
            return true;
        }
    }

    if (instruction->getOpcode() != Opc::GETELEMENTPTR ||
        instruction->getNumOperands() < 2 ||
        !collectPointerAccessImpl(
            instruction->getOperand(0), argumentMap,
            access, visiting)) {
        visiting.erase(value);
        return false;
    }

    for (unsigned index = 1;
         index < instruction->getNumOperands(); ++index) {
        auto* operand = instruction->getOperand(index);
        if (isConstant(operand, 0) &&
            index + 1 < instruction->getNumOperands()) {
            continue;
        }
        access.indices.push_back(operand);
    }
    visiting.erase(value);
    return true;
}

// ================================================================
// 二、AffineRecurrenceAnalysis — 内部辅助
// ================================================================

bool analyzeLoadAccess(
    IR::Instruction* load,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access) {
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 &&
           collectPointerAccess(
               load->getOperand(0), argumentMap, access);
}

// ================================================================
// 三、ScalarReductionAnalysis — 内部辅助
// ================================================================

bool isIdentity(
    ScalarReductionKind kind, IR::Value* value) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    if (!constant) return false;
    if (kind == ScalarReductionKind::Add) {
        return constant->getValue() == 0;
    }
    return constant->getValue() == 1;
}

bool classifyReduction(
    IR::Instruction* update, ScalarReductionKind& kind) {
    if (!update || update->getNumOperands() != 2) return false;
    if (update->getOpcode() == Opc::ADD) {
        kind = ScalarReductionKind::Add;
        return true;
    }
    if (update->getOpcode() == Opc::MUL) {
        kind = ScalarReductionKind::Multiply;
        return true;
    }
    return false;
}

// ================================================================
// 四、LoopPatternAnalysis — 内部辅助
// ================================================================

bool getPositiveConstantStep(
    IR::Instruction* instruction,
    IR::Value* induction,
    int64_t& step) {
    if (!instruction || instruction->getOpcode() != Opc::ADD ||
        instruction->getNumOperands() != 2) {
        return false;
    }
    IR::Value* stepValue = nullptr;
    if (instruction->getOperand(0) == induction) {
        stepValue = instruction->getOperand(1);
    } else if (instruction->getOperand(1) == induction) {
        stepValue = instruction->getOperand(0);
    } else {
        return false;
    }
    auto* constant =
        dynamic_cast<IR::ConstantInt*>(stepValue);
    if (!constant || constant->getValue() <= 0) return false;
    step = constant->getValue();
    return true;
}

} // namespace

// ================================================================
// 一、MemoryAccessAnalysis — 公开 API
// ================================================================

AllocaArgumentMap buildAllocaArgumentMap(IR::Function* function) {
    AllocaArgumentMap result;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            auto* argument =
                dynamic_cast<IR::Argument*>(instruction->getOperand(0));
            auto* alloca =
                dynamic_cast<IR::Instruction*>(instruction->getOperand(1));
            if (argument && alloca &&
                alloca->getOpcode() == Opc::ALLOCA) {
                result[alloca] = argument;
            }
        }
    }
    return result;
}

bool collectPointerAccess(
    IR::Value* value,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access) {
    std::unordered_set<IR::Value*> visiting;
    return collectPointerAccessImpl(
        value, argumentMap, access, visiting);
}

IR::GlobalVariable* rootGlobal(IR::Value* value) {
    PointerAccess access;
    if (!collectPointerAccess(value, nullptr, access)) return nullptr;
    return dynamic_cast<IR::GlobalVariable*>(access.root);
}

// ================================================================
// 二、AffineRecurrenceAnalysis — 公开 API
// ================================================================

bool analyzeAffineRecurrence(
    IR::Instruction* store,
    const AllocaArgumentMap* argumentMap,
    AffineRecurrence& result) {
    if (!store || store->getOpcode() != Opc::STORE ||
        store->getNumOperands() != 2) {
        return false;
    }

    PointerAccess destination;
    if (!collectPointerAccess(
            store->getOperand(1), argumentMap, destination)) {
        return false;
    }

    auto* update =
        dynamic_cast<IR::Instruction*>(store->getOperand(0));
    if (!update || update->getOpcode() != Opc::ADD ||
        update->getNumOperands() != 2) {
        return false;
    }

    IR::Instruction* multiply = nullptr;
    IR::Instruction* addendLoad = nullptr;
    for (unsigned index = 0; index < 2; ++index) {
        auto* operand =
            dynamic_cast<IR::Instruction*>(update->getOperand(index));
        if (!operand) return false;
        if (operand->getOpcode() == Opc::MUL) {
            if (multiply) return false;
            multiply = operand;
        } else if (operand->getOpcode() == Opc::LOAD) {
            if (addendLoad) return false;
            addendLoad = operand;
        } else {
            return false;
        }
    }
    if (!multiply || !addendLoad ||
        multiply->getNumOperands() != 2 ||
        addendLoad->getNumOperands() != 1) {
        return false;
    }

    IR::Instruction* previousLoad = nullptr;
    IR::Instruction* scaleLoad = nullptr;
    PointerAccess previous;
    PointerAccess scale;
    for (unsigned index = 0; index < 2; ++index) {
        auto* load = dynamic_cast<IR::Instruction*>(
            multiply->getOperand(index));
        PointerAccess access;
        if (!analyzeLoadAccess(load, argumentMap, access)) {
            return false;
        }
        if (access.root == destination.root &&
            access.indices == destination.indices) {
            if (previousLoad) return false;
            previousLoad = load;
            previous = std::move(access);
        } else {
            if (scaleLoad) return false;
            scaleLoad = load;
            scale = std::move(access);
        }
    }
    if (!previousLoad || !scaleLoad) return false;

    PointerAccess addend;
    if (!analyzeLoadAccess(addendLoad, argumentMap, addend)) {
        return false;
    }

    result.store = store;
    result.update = update;
    result.multiply = multiply;
    result.previousLoad = previousLoad;
    result.scaleLoad = scaleLoad;
    result.addendLoad = addendLoad;
    result.destination = std::move(destination);
    result.previous = std::move(previous);
    result.scale = std::move(scale);
    result.addend = std::move(addend);
    return true;
}

// ================================================================
// 三、ScalarReductionAnalysis — 公开 API
// ================================================================

bool analyzeAllocaScalarReduction(
    IR::Function* function,
    IR::Instruction* update,
    IR::Value* contribution,
    ScalarReduction& result) {
    ScalarReductionKind kind;
    if (!function || !contribution ||
        !classifyReduction(update, kind) ||
        update->getNumUses() != 1) {
        return false;
    }

    IR::Instruction* accumulatorLoad = nullptr;
    if (update->getOperand(0) == contribution) {
        accumulatorLoad =
            dynamic_cast<IR::Instruction*>(update->getOperand(1));
    } else if (update->getOperand(1) == contribution) {
        accumulatorLoad =
            dynamic_cast<IR::Instruction*>(update->getOperand(0));
    } else {
        return false;
    }

    auto* accumulatorAddress =
        accumulatorLoad &&
                accumulatorLoad->getOpcode() == Opc::LOAD &&
                accumulatorLoad->getNumOperands() == 1
            ? dynamic_cast<IR::Instruction*>(
                  accumulatorLoad->getOperand(0))
            : nullptr;
    auto* updateStore =
        dynamic_cast<IR::Instruction*>(update->getUses().front().user);
    if (!accumulatorAddress ||
        accumulatorAddress->getOpcode() != Opc::ALLOCA ||
        !updateStore ||
        updateStore->getOpcode() != Opc::STORE ||
        updateStore->getNumOperands() != 2 ||
        updateStore->getOperand(0) != update ||
        updateStore->getOperand(1) != accumulatorAddress) {
        return false;
    }

    unsigned storeCount = 0;
    IR::Instruction* initializationStore = nullptr;
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2 ||
                instruction->getOperand(1) != accumulatorAddress) {
                continue;
            }
            ++storeCount;
            if (instruction.get() != updateStore &&
                isIdentity(kind, instruction->getOperand(0))) {
                initializationStore = instruction.get();
            }
        }
    }
    if (storeCount != 2 || !initializationStore) return false;

    result.kind = kind;
    result.accumulatorAddress = accumulatorAddress;
    result.accumulatorLoad = accumulatorLoad;
    result.initializationStore = initializationStore;
    result.update = update;
    result.updateStore = updateStore;
    result.contribution = contribution;
    return true;
}

// ================================================================
// 四、LoopPatternAnalysis — 公开 API
// ================================================================

bool analyzeCanonicalCountedLoop(
    IR::Function* function,
    IR::Value* induction,
    IR::Value* bound,
    IR::BasicBlock* containedBlock,
    CanonicalCountedLoop& result) {
    auto* phi = dynamic_cast<IR::Instruction*>(induction);
    if (!phi || phi->getOpcode() != Opc::PHI ||
        phi->getNumOperands() < 4 ||
        phi->getNumOperands() % 2 != 0) {
        return false;
    }

    auto* header = phi->getParent();
    auto* terminator = header ? header->getTerminator() : nullptr;
    if (!terminator || terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }

    auto* compare =
        dynamic_cast<IR::Instruction*>(terminator->getOperand(0));
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getNumOperands() != 2) {
        return false;
    }

    unsigned boundOperand = 1;
    const bool directLessThan =
        compare->getName() == "slt" &&
        compare->getOperand(0) == phi &&
        compare->getOperand(1) == bound;
    const bool reversedGreaterThan =
        compare->getName() == "sgt" &&
        compare->getOperand(0) == bound &&
        compare->getOperand(1) == phi;
    const bool directLessEqual =
        compare->getName() == "sle" &&
        compare->getOperand(0) == phi &&
        compare->getOperand(1) == bound;
    const bool reversedGreaterEqual =
        compare->getName() == "sge" &&
        compare->getOperand(0) == bound &&
        compare->getOperand(1) == phi;
    if (!directLessThan && !reversedGreaterThan &&
        !directLessEqual && !reversedGreaterEqual) {
        return false;
    }
    if (reversedGreaterThan || reversedGreaterEqual) {
        boundOperand = 0;
    }

    const NaturalLoop* naturalLoop = nullptr;
    auto loops = findNaturalLoops(function);
    for (auto& loop : loops) {
        if (loop.header == header &&
            loop.body.count(containedBlock)) {
            naturalLoop = &loop;
            break;
        }
    }
    if (!naturalLoop) return false;

    IR::Value* start = nullptr;
    int64_t step = 0;
    for (unsigned index = 0;
         index < phi->getNumOperands(); index += 2) {
        auto* incoming = phi->getOperand(index);
        auto* incomingBlock = dynamic_cast<IR::BasicBlock*>(
            phi->getOperand(index + 1));
        if (!incomingBlock) return false;
        if (!naturalLoop->body.count(incomingBlock)) {
            if (start) return false;
            start = incoming;
            continue;
        }
        auto* add = dynamic_cast<IR::Instruction*>(incoming);
        int64_t incomingStep = 0;
        if (!getPositiveConstantStep(
                add, phi, incomingStep)) {
            return false;
        }
        if (step != 0 && step != incomingStep) return false;
        step = incomingStep;
    }
    if (!start || step == 0) return false;

    result.induction = phi;
    result.compare = compare;
    result.header = header;
    result.start = start;
    result.bound = bound;
    result.step = step;
    result.boundOperand = boundOperand;
    result.inclusiveUpperBound =
        directLessEqual || reversedGreaterEqual;
    result.body = naturalLoop->body;
    return true;
}

} // namespace Opt
