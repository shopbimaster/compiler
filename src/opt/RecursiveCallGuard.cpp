#include "opt/Optimizer.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct EarlyReturnGuard {
    IR::Function* function = nullptr;
    IR::BasicBlock* entry = nullptr;
    IR::Instruction* condition = nullptr;
    bool returnOnTrue = false;
};

bool isPureGuardOpcode(Opc opcode) {
    switch (opcode) {
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::ASHR:
    case Opc::ICMP:
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
    case Opc::SELECT:
        return true;
    default:
        return false;
    }
}

bool isAddressWithinEntryAlloca(IR::Value* value, IR::BasicBlock* entry) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || instruction->getParent() != entry) return false;
    if (instruction->getOpcode() == Opc::ALLOCA) return true;
    if (instruction->getOpcode() != Opc::GETELEMENTPTR ||
        instruction->getNumOperands() == 0) {
        return false;
    }
    return isAddressWithinEntryAlloca(instruction->getOperand(0), entry);
}

int countLocalStoresInUnobservablePrefix(IR::BasicBlock* entry,
                                         IR::Instruction* terminator) {
    int localStores = 0;
    for (const auto& owned : entry->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction == terminator) break;

        switch (instruction->getOpcode()) {
        case Opc::CALL:
        case Opc::RET:
        case Opc::BR:
        case Opc::COND_BR:
            return -1;
        case Opc::STORE:
            if (instruction->getNumOperands() != 2 ||
                !isAddressWithinEntryAlloca(
                    instruction->getOperand(1), entry)) {
                return -1;
            }
            ++localStores;
            break;
        default:
            break;
        }
    }
    return localStores;
}

bool isDirectVoidReturn(IR::BasicBlock* block) {
    if (!block || block->getInstructions().size() != 1) return false;
    auto* instruction = block->getInstructions().front().get();
    return instruction->getOpcode() == Opc::RET &&
           instruction->getNumOperands() == 0;
}

bool canCloneGuardValue(
    IR::Value* value,
    const EarlyReturnGuard& guard,
    std::unordered_set<IR::Value*>& visiting,
    std::unordered_set<IR::Value*>& verified) {
    if (!value) return false;
    if (dynamic_cast<IR::Constant*>(value)) return true;

    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        return argument->getIndex() < guard.function->getNumArgs();
    }

    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || instruction->getParent() != guard.entry ||
        !isPureGuardOpcode(instruction->getOpcode())) {
        return false;
    }
    if (verified.count(instruction)) return true;
    if (!visiting.insert(instruction).second) return false;

    for (unsigned index = 0; index < instruction->getNumOperands(); ++index) {
        if (!canCloneGuardValue(
                instruction->getOperand(index), guard, visiting, verified)) {
            visiting.erase(instruction);
            return false;
        }
    }

    visiting.erase(instruction);
    verified.insert(instruction);
    return true;
}

bool findEarlyReturnGuard(IR::Function* function, EarlyReturnGuard& guard) {
    if (!function || function->isExternal() ||
        !function->getFunctionType()->getReturnType()->isVoid()) {
        return false;
    }

    auto* entry = function->getEntryBlock();
    auto* terminator = entry ? entry->getTerminator() : nullptr;
    if (!terminator || terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }

    // A caller-side guard duplicates the base-case expression on every
    // recursive edge. Require a substantial local initialization prefix so
    // the skipped leaf calls repay that cost. This targets large stack-local
    // scratch buffers without slowing ordinary small recursive functions.
    int localStores =
        countLocalStoresInUnobservablePrefix(entry, terminator);
    if (localStores < 8) {
        return false;
    }

    auto* trueTarget =
        dynamic_cast<IR::BasicBlock*>(terminator->getOperand(1));
    auto* falseTarget =
        dynamic_cast<IR::BasicBlock*>(terminator->getOperand(2));
    bool trueReturns = isDirectVoidReturn(trueTarget);
    bool falseReturns = isDirectVoidReturn(falseTarget);
    if (trueReturns == falseReturns) return false;

    guard.function = function;
    guard.entry = entry;
    guard.condition =
        dynamic_cast<IR::Instruction*>(terminator->getOperand(0));
    guard.returnOnTrue = trueReturns;
    if (!guard.condition) return false;

    std::unordered_set<IR::Value*> visiting;
    std::unordered_set<IR::Value*> verified;
    return canCloneGuardValue(
        guard.condition, guard, visiting, verified);
}

IR::Value* cloneGuardValue(
    IR::Value* value,
    const EarlyReturnGuard& guard,
    IR::Instruction* call,
    IR::BasicBlock* destination,
    std::unordered_map<IR::Value*, IR::Value*>& cloned) {
    if (dynamic_cast<IR::Constant*>(value)) return value;

    if (auto* argument = dynamic_cast<IR::Argument*>(value)) {
        return call->getOperand(argument->getIndex() + 1);
    }

    auto found = cloned.find(value);
    if (found != cloned.end()) return found->second;

    auto* source = dynamic_cast<IR::Instruction*>(value);
    if (!source) return nullptr;

    std::vector<IR::Value*> operands;
    operands.reserve(source->getNumOperands());
    for (unsigned index = 0; index < source->getNumOperands(); ++index) {
        auto* operand = cloneGuardValue(
            source->getOperand(index), guard, call, destination, cloned);
        if (!operand) return nullptr;
        operands.push_back(operand);
    }

    static unsigned guardValueId = 0;
    std::string name = source->getName();
    if (source->getOpcode() != Opc::ICMP) {
        name += ".call_guard." + std::to_string(guardValueId++);
    }

    IR::Instruction* copy = nullptr;
    switch (source->getOpcode()) {
    case Opc::ICMP:
        copy = IR::Instruction::createCmp(
            Opc::ICMP, operands[0], operands[1], source->getName());
        break;
    case Opc::SELECT:
        copy = IR::Instruction::createSelect(
            operands[0], operands[1], operands[2], name);
        break;
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
        copy = IR::Instruction::createCast(
            source->getOpcode(), source->getType(), operands[0], name);
        break;
    default:
        copy = IR::Instruction::createBinOp(
            source->getOpcode(), source->getType(), name,
            operands[0], operands[1]);
        break;
    }

    destination->pushBack(copy);
    cloned[source] = copy;
    return copy;
}

void replacePhiPredecessor(IR::Function* function,
                           IR::BasicBlock* oldPredecessor,
                           IR::BasicBlock* newPredecessor) {
    for (auto& block : function->getBlocks()) {
        for (auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() != Opc::PHI) continue;
            for (unsigned index = 1;
                 index < instruction->getNumOperands(); index += 2) {
                if (instruction->getOperand(index) == oldPredecessor) {
                    instruction->setOperand(index, newPredecessor);
                }
            }
        }
    }
}

bool guardCall(IR::Instruction* call,
               const EarlyReturnGuard& guard,
               unsigned callSiteId) {
    auto* callBlock = call ? call->getParent() : nullptr;
    auto* caller = callBlock ? callBlock->getParent() : nullptr;
    if (!caller || call->getNumOperands() !=
                       guard.function->getNumArgs() + 1) {
        return false;
    }
    for (unsigned index = 1; index < call->getNumOperands(); ++index) {
        if (!call->getOperand(index)) return false;
    }

    std::vector<std::unique_ptr<IR::Instruction>> afterCall;
    std::unique_ptr<IR::Instruction> ownedCall;
    bool foundCall = false;
    for (auto iterator = callBlock->begin();
         iterator != callBlock->end();) {
        if (iterator->get() == call) {
            foundCall = true;
            ownedCall = std::move(*iterator);
            iterator = callBlock->erase(iterator);
            continue;
        }
        if (foundCall) {
            afterCall.push_back(std::move(*iterator));
            iterator = callBlock->erase(iterator);
        } else {
            ++iterator;
        }
    }
    if (!ownedCall) return false;

    std::unordered_map<IR::Value*, IR::Value*> cloned;
    auto* condition = cloneGuardValue(
        guard.condition, guard, call, callBlock, cloned);
    if (!condition) return false;

    std::string suffix = std::to_string(callSiteId);
    auto* continuation =
        caller->createBlock("call_guard.cont." + suffix);
    auto* guardedCall =
        caller->insertBlock("call_guard.invoke." + suffix, continuation);

    for (auto& instruction : afterCall) {
        continuation->pushBack(instruction.release());
    }
    replacePhiPredecessor(caller, callBlock, continuation);

    guardedCall->pushBack(ownedCall.release());
    guardedCall->pushBack(IR::Instruction::createBr(continuation));

    if (guard.returnOnTrue) {
        callBlock->pushBack(IR::Instruction::createCondBr(
            condition, continuation, guardedCall));
    } else {
        callBlock->pushBack(IR::Instruction::createCondBr(
            condition, guardedCall, continuation));
    }
    return true;
}

} // namespace

bool hoistRecursiveCallGuards(IR::Module* module) {
    bool changed = false;
    unsigned callSiteId = 0;

    for (auto& ownedFunction : module->getFunctions()) {
        auto* function = ownedFunction.get();
        EarlyReturnGuard guard;
        if (!findEarlyReturnGuard(function, guard)) continue;

        std::vector<IR::Instruction*> recursiveCalls;
        for (auto& block : function->getBlocks()) {
            for (auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() == Opc::CALL &&
                    instruction->getNumOperands() > 0 &&
                    instruction->getOperand(0) == function) {
                    recursiveCalls.push_back(instruction.get());
                }
            }
        }

        for (auto* call : recursiveCalls) {
            if (guardCall(call, guard, callSiteId++)) changed = true;
        }
    }
    return changed;
}

} // namespace Opt
