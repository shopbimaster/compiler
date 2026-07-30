#include "opt/ScalarReductionAnalysis.h"

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

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

} // namespace

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

} // namespace Opt
