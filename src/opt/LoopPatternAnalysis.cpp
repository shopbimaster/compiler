#include "opt/LoopPatternAnalysis.h"

#include "opt/Optimizer.h"

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isAddOneOf(IR::Instruction* instruction, IR::Value* value) {
    if (!instruction || instruction->getOpcode() != Opc::ADD ||
        instruction->getNumOperands() != 2) {
        return false;
    }
    return (instruction->getOperand(0) == value &&
            isConstant(instruction->getOperand(1), 1)) ||
           (instruction->getOperand(1) == value &&
            isConstant(instruction->getOperand(0), 1));
}

} // namespace

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
    if (!directLessThan && !reversedGreaterThan) return false;
    if (reversedGreaterThan) boundOperand = 0;

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
    bool hasStep = false;
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
        if (!isAddOneOf(add, phi)) return false;
        hasStep = true;
    }
    if (!start || !hasStep) return false;

    result.induction = phi;
    result.compare = compare;
    result.header = header;
    result.start = start;
    result.bound = bound;
    result.step = 1;
    result.boundOperand = boundOperand;
    result.body = naturalLoop->body;
    return true;
}

} // namespace Opt
