#include "opt/LoopPatternAnalysis.h"

#include "opt/Optimizer.h"

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

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
