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
        compare->getName() != "slt" ||
        compare->getNumOperands() != 2 ||
        compare->getOperand(0) != phi ||
        compare->getOperand(1) != bound) {
        return false;
    }

    bool hasZero = false;
    bool hasStep = false;
    for (unsigned index = 0;
         index < phi->getNumOperands(); index += 2) {
        auto* incoming = phi->getOperand(index);
        if (isConstant(incoming, 0)) {
            hasZero = true;
            continue;
        }
        auto* add = dynamic_cast<IR::Instruction*>(incoming);
        if (!isAddOneOf(add, phi)) return false;
        hasStep = true;
    }
    if (!hasZero || !hasStep) return false;

    for (auto& loop : findNaturalLoops(function)) {
        if (loop.header != header ||
            !loop.body.count(containedBlock)) {
            continue;
        }
        result.induction = phi;
        result.compare = compare;
        result.header = header;
        result.bound = bound;
        result.body = loop.body;
        return true;
    }
    return false;
}

} // namespace Opt
