#include "opt/AffineRecurrenceAnalysis.h"

#include <utility>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

bool analyzeLoadAccess(
    IR::Instruction* load,
    const AllocaArgumentMap* argumentMap,
    PointerAccess& access) {
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 &&
           collectPointerAccess(
               load->getOperand(0), argumentMap, access);
}

} // namespace

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

} // namespace Opt
