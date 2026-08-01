// ================================================================
// src/opt/StencilInteriorSpecialization.cpp — 模板内部边界特化
// ----------------------------------------------------------------
// 所属模块：opt（O2 阶段 6 全局清理）
// 关键依赖：opt/LoopAnalysis.h（循环模式分析）、opt/Optimizer.h
// ================================================================

#include "opt/LoopAnalysis.h"
#include "opt/Optimizer.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct PhiIncoming {
    IR::Value* initialValue = nullptr;
    IR::Value* backedgeValue = nullptr;
};

struct AffineCoordinate {
    IR::Value* outer = nullptr;
    int64_t offset = 0;
};

struct StencilCandidate {
    IR::Function* function = nullptr;
    NaturalLoop innerLoop;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* conditionBlock = nullptr;
    IR::BasicBlock* computeBlock = nullptr;
    IR::BasicBlock* skipBlock = nullptr;
    IR::BasicBlock* latch = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Instruction* induction = nullptr;
    IR::Instruction* accumulator = nullptr;
    IR::Instruction* computedAccumulator = nullptr;
    IR::Instruction* inductionNext = nullptr;
    IR::Value* innerBound = nullptr;
    IR::Value* imageBound = nullptr;
    IR::Value* outerCoordinate = nullptr;
    std::vector<IR::Value*> invariantConditions;
    int64_t lowerMargin = 0;
    int64_t upperMargin = 0;
    PhiIncoming inductionIncoming;
    PhiIncoming accumulatorIncoming;
};

IR::ConstantInt* asConstant(IR::Value* value) {
    return dynamic_cast<IR::ConstantInt*>(value);
}

IR::Instruction* asInstruction(
    IR::Value* value, Opc opcode) {
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    return instruction &&
                   instruction->getOpcode() == opcode
        ? instruction
        : nullptr;
}

bool isZero(IR::Value* value) {
    auto* constant = asConstant(value);
    return constant && constant->getValue() == 0;
}

bool getUniquePreheader(
    const NaturalLoop& loop, const PredMap& predecessors,
    IR::BasicBlock*& preheader) {
    preheader = nullptr;
    auto found = predecessors.find(loop.header);
    if (found == predecessors.end()) return false;
    for (auto* predecessor : found->second) {
        if (loop.body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator =
        preheader ? preheader->getTerminator() : nullptr;
    return terminator &&
           terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == loop.header;
}

bool getPhiIncoming(
    IR::Instruction* phi, IR::BasicBlock* preheader,
    IR::BasicBlock* latch, PhiIncoming& result) {
    if (!phi || phi->getOpcode() != Opc::PHI ||
        phi->getNumOperands() % 2 != 0) {
        return false;
    }
    for (unsigned index = 0;
         index < phi->getNumOperands(); index += 2) {
        auto* block = dynamic_cast<IR::BasicBlock*>(
            phi->getOperand(index + 1));
        if (block == preheader) {
            if (result.initialValue) return false;
            result.initialValue = phi->getOperand(index);
        } else if (block == latch) {
            if (result.backedgeValue) return false;
            result.backedgeValue = phi->getOperand(index);
        } else {
            return false;
        }
    }
    return result.initialValue && result.backedgeValue;
}

bool matchLoopHeader(
    const NaturalLoop& loop,
    IR::Instruction*& induction,
    IR::Value*& bound,
    IR::BasicBlock*& conditionBlock,
    IR::BasicBlock*& exit) {
    auto* terminator = loop.header->getTerminator();
    if (!terminator ||
        terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        return false;
    }
    auto* compare =
        asInstruction(terminator->getOperand(0), Opc::ICMP);
    if (!compare || compare->getName() != "slt" ||
        compare->getNumOperands() != 2) {
        return false;
    }
    induction =
        asInstruction(compare->getOperand(0), Opc::PHI);
    bound = compare->getOperand(1);
    conditionBlock = dynamic_cast<IR::BasicBlock*>(
        terminator->getOperand(1));
    exit = dynamic_cast<IR::BasicBlock*>(
        terminator->getOperand(2));
    return induction && conditionBlock && exit &&
           loop.body.count(conditionBlock) &&
           !loop.body.count(exit);
}

bool collectConjunctionTerms(
    IR::Value* value, IR::BasicBlock* conditionBlock,
    std::vector<IR::Value*>& terms,
    std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return false;
    auto release = [&]() { visiting.erase(value); };
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    if (!instruction ||
        instruction->getParent() != conditionBlock) {
        terms.push_back(value);
        release();
        return true;
    }

    if (instruction->getOpcode() == Opc::ICMP &&
        instruction->getName() == "ne" &&
        instruction->getNumOperands() == 2) {
        IR::Value* nested = nullptr;
        if (isZero(instruction->getOperand(0))) {
            nested = instruction->getOperand(1);
        } else if (isZero(instruction->getOperand(1))) {
            nested = instruction->getOperand(0);
        }
        if (nested) {
            bool ok = collectConjunctionTerms(
                nested, conditionBlock, terms, visiting);
            release();
            return ok;
        }
    }

    if (instruction->getOpcode() == Opc::SELECT &&
        instruction->getNumOperands() == 3 &&
        isZero(instruction->getOperand(2))) {
        bool ok = collectConjunctionTerms(
                      instruction->getOperand(0),
                      conditionBlock, terms, visiting) &&
                  collectConjunctionTerms(
                      instruction->getOperand(1),
                      conditionBlock, terms, visiting);
        release();
        return ok;
    }

    terms.push_back(value);
    release();
    return true;
}

bool flattenAffine(
    IR::Value* value, IR::Value* induction,
    int coefficient,
    std::unordered_map<IR::Value*, int>& variables,
    int64_t& constant,
    std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return false;
    auto release = [&]() { visiting.erase(value); };
    if (auto* literal = asConstant(value)) {
        constant += coefficient * literal->getValue();
        release();
        return true;
    }
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    if (instruction && instruction->getNumOperands() == 2 &&
        (instruction->getOpcode() == Opc::ADD ||
         instruction->getOpcode() == Opc::SUB)) {
        bool ok = flattenAffine(
            instruction->getOperand(0), induction,
            coefficient, variables, constant, visiting);
        if (ok) {
            ok = flattenAffine(
                instruction->getOperand(1), induction,
                instruction->getOpcode() == Opc::ADD
                    ? coefficient
                    : -coefficient,
                variables, constant, visiting);
        }
        release();
        return ok;
    }
    variables[value] += coefficient;
    release();
    return true;
}

bool matchAffineCoordinate(
    IR::Value* value, IR::Value* induction,
    AffineCoordinate& result) {
    std::unordered_map<IR::Value*, int> variables;
    std::unordered_set<IR::Value*> visiting;
    int64_t constant = 0;
    if (!flattenAffine(
            value, induction, 1,
            variables, constant, visiting) ||
        variables[induction] != 1) {
        return false;
    }
    variables.erase(induction);
    if (variables.size() != 1 ||
        variables.begin()->second != 1) {
        return false;
    }
    result.outer = variables.begin()->first;
    result.offset = constant;
    return true;
}

bool matchColumnBounds(
    IR::BasicBlock* conditionBlock,
    IR::Instruction* induction,
    IR::Value*& imageBound,
    AffineCoordinate& coordinate,
    IR::Instruction*& lowerCompare,
    IR::Instruction*& upperCompare) {
    lowerCompare = nullptr;
    upperCompare = nullptr;
    IR::Value* coordinateValue = nullptr;
    for (auto& owned : conditionBlock->getInstructions()) {
        auto* compare = owned.get();
        if (compare->getOpcode() != Opc::ICMP ||
            compare->getNumOperands() != 2) {
            continue;
        }
        if (compare->getName() == "sge" &&
            isZero(compare->getOperand(1))) {
            AffineCoordinate candidate;
            if (matchAffineCoordinate(
                    compare->getOperand(0),
                    induction, candidate)) {
                lowerCompare = compare;
                coordinate = candidate;
                coordinateValue = compare->getOperand(0);
            }
        }
    }
    if (!lowerCompare) return false;
    for (auto& owned : conditionBlock->getInstructions()) {
        auto* compare = owned.get();
        if (compare->getOpcode() != Opc::ICMP ||
            compare->getName() != "slt" ||
            compare->getNumOperands() != 2 ||
            compare->getOperand(0) != coordinateValue) {
            continue;
        }
        upperCompare = compare;
        imageBound = compare->getOperand(1);
        break;
    }
    return upperCompare && imageBound;
}

bool valueDominatesBlock(
    IR::Value* value, IR::BasicBlock* block,
    const DomMap& dominators) {
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    if (!instruction) return true;
    if (instruction->getParent() == block) {
        auto* terminator = block->getTerminator();
        for (auto& owned : block->getInstructions()) {
            if (owned.get() == instruction) return true;
            if (owned.get() == terminator) break;
        }
        return false;
    }
    auto found = dominators.find(block);
    return found != dominators.end() &&
           found->second.count(instruction->getParent());
}

bool matchAccumulatorMerge(
    const NaturalLoop& loop,
    IR::BasicBlock* conditionBlock,
    IR::Instruction* accumulator,
    IR::Instruction* backedge,
    IR::BasicBlock*& computeBlock,
    IR::BasicBlock*& skipBlock,
    IR::Instruction*& computedAccumulator) {
    auto* branch = conditionBlock->getTerminator();
    if (!branch || branch->getOpcode() != Opc::COND_BR ||
        branch->getNumOperands() != 3) {
        return false;
    }
    auto* first = dynamic_cast<IR::BasicBlock*>(
        branch->getOperand(1));
    auto* second = dynamic_cast<IR::BasicBlock*>(
        branch->getOperand(2));
    if (!first || !second ||
        !loop.body.count(first) ||
        !loop.body.count(second)) {
        return false;
    }
    if (!backedge || backedge->getOpcode() != Opc::PHI ||
        backedge->getParent() != loop.latch ||
        backedge->getNumOperands() != 4) {
        return false;
    }

    for (unsigned index = 0;
         index < backedge->getNumOperands(); index += 2) {
        auto* predecessor = dynamic_cast<IR::BasicBlock*>(
            backedge->getOperand(index + 1));
        auto* value = dynamic_cast<IR::Instruction*>(
            backedge->getOperand(index));
        if (backedge->getOperand(index) == accumulator) {
            skipBlock = predecessor;
        } else {
            computeBlock = predecessor;
            computedAccumulator = value;
        }
    }
    if (!computeBlock || !skipBlock ||
        !computedAccumulator ||
        ((computeBlock != first || skipBlock != second) &&
         (computeBlock != second || skipBlock != first))) {
        return false;
    }
    auto* computeTerminator = computeBlock->getTerminator();
    auto* skipTerminator = skipBlock->getTerminator();
    return computeTerminator &&
           computeTerminator->getOpcode() == Opc::BR &&
           computeTerminator->getOperand(0) == loop.latch &&
           skipBlock->size() == 1 && skipTerminator &&
           skipTerminator->getOpcode() == Opc::BR &&
           skipTerminator->getOperand(0) == loop.latch;
}

bool isPureCloneable(Opc opcode) {
    switch (opcode) {
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::ASHR:
    case Opc::GETELEMENTPTR:
    case Opc::LOAD:
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
        return true;
    default:
        return false;
    }
}

IR::Value* cloneValue(
    IR::Value* value, IR::BasicBlock* destination,
    const std::unordered_set<IR::BasicBlock*>& sourceBlocks,
    std::unordered_map<IR::Value*, IR::Value*>& mapping,
    bool& valid) {
    auto mapped = mapping.find(value);
    if (mapped != mapping.end()) return mapped->second;
    auto* instruction =
        dynamic_cast<IR::Instruction*>(value);
    if (!instruction ||
        !sourceBlocks.count(instruction->getParent())) {
        return value;
    }
    if (!isPureCloneable(instruction->getOpcode())) {
        valid = false;
        return nullptr;
    }

    std::vector<IR::Value*> operands;
    operands.reserve(instruction->getNumOperands());
    for (unsigned index = 0;
         index < instruction->getNumOperands(); ++index) {
        auto* operand = cloneValue(
            instruction->getOperand(index), destination,
            sourceBlocks, mapping, valid);
        if (!valid || !operand) return nullptr;
        operands.push_back(operand);
    }

    std::string name = instruction->getName();
    if (!name.empty()) name += ".stencil";
    IR::Instruction* cloned = nullptr;
    switch (instruction->getOpcode()) {
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::ASHR:
        cloned = IR::Instruction::createBinOp(
            instruction->getOpcode(), instruction->getType(),
            name, operands[0], operands[1]);
        break;
    case Opc::GETELEMENTPTR: {
        auto* pointerType =
            dynamic_cast<IR::PointerType*>(
                operands[0]->getType());
        if (!pointerType) {
            valid = false;
            return nullptr;
        }
        std::vector<IR::Value*> indices(
            operands.begin() + 1, operands.end());
        cloned = IR::Instruction::createGetElementPtr(
            pointerType->getPointeeType(), operands[0],
            indices, name);
        break;
    }
    case Opc::LOAD:
        cloned = IR::Instruction::createLoad(
            instruction->getType(), operands[0], name);
        break;
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC:
        cloned = IR::Instruction::createCast(
            instruction->getOpcode(), instruction->getType(),
            operands[0], name);
        break;
    default:
        valid = false;
        return nullptr;
    }
    destination->pushBack(cloned);
    mapping[instruction] = cloned;
    return cloned;
}

bool buildCandidate(
    IR::Function* function, const NaturalLoop& loop,
    const PredMap& predecessors, const DomMap& dominators,
    StencilCandidate& candidate) {
    IR::BasicBlock* preheader = nullptr;
    if (!getUniquePreheader(
            loop, predecessors, preheader)) {
        return false;
    }

    IR::Instruction* induction = nullptr;
    IR::Value* innerBound = nullptr;
    IR::BasicBlock* conditionBlock = nullptr;
    IR::BasicBlock* exit = nullptr;
    if (!matchLoopHeader(
            loop, induction, innerBound,
            conditionBlock, exit)) {
        return false;
    }

    std::vector<IR::Instruction*> phis;
    for (auto& instruction : loop.header->getInstructions()) {
        if (instruction->getOpcode() == Opc::PHI) {
            phis.push_back(instruction.get());
        }
    }
    if (phis.size() != 2) return false;
    auto* accumulator =
        phis[0] == induction ? phis[1] : phis[0];
    if (accumulator == induction) return false;

    PhiIncoming inductionIncoming;
    PhiIncoming accumulatorIncoming;
    if (!getPhiIncoming(
            induction, preheader, loop.latch,
            inductionIncoming) ||
        !getPhiIncoming(
            accumulator, preheader, loop.latch,
            accumulatorIncoming)) {
        return false;
    }
    auto* inductionNext = asInstruction(
        inductionIncoming.backedgeValue, Opc::ADD);
    int64_t step = 0;
    if (!inductionNext ||
        !((inductionNext->getOperand(0) == induction &&
           asConstant(inductionNext->getOperand(1)) &&
           (step = asConstant(
                       inductionNext->getOperand(1))
                       ->getValue()) == 1) ||
          (inductionNext->getOperand(1) == induction &&
           asConstant(inductionNext->getOperand(0)) &&
           (step = asConstant(
                       inductionNext->getOperand(0))
                       ->getValue()) == 1))) {
        return false;
    }

    IR::BasicBlock* computeBlock = nullptr;
    IR::BasicBlock* skipBlock = nullptr;
    IR::Instruction* computedAccumulator = nullptr;
    auto* accumulatorBackedge =
        asInstruction(
            accumulatorIncoming.backedgeValue, Opc::PHI);
    if (!matchAccumulatorMerge(
            loop, conditionBlock, accumulator,
            accumulatorBackedge, computeBlock,
            skipBlock, computedAccumulator)) {
        return false;
    }

    auto* condition = conditionBlock->getTerminator()
        ? conditionBlock->getTerminator()->getOperand(0)
        : nullptr;
    std::vector<IR::Value*> terms;
    std::unordered_set<IR::Value*> visiting;
    if (!collectConjunctionTerms(
            condition, conditionBlock, terms, visiting)) {
        return false;
    }

    IR::Value* imageBound = nullptr;
    AffineCoordinate coordinate;
    IR::Instruction* lowerCompare = nullptr;
    IR::Instruction* upperCompare = nullptr;
    if (!matchColumnBounds(
            conditionBlock, induction, imageBound,
            coordinate, lowerCompare, upperCompare)) {
        return false;
    }

    bool foundLower = false;
    bool foundUpper = false;
    std::vector<IR::Value*> invariantConditions;
    for (auto* term : terms) {
        if (term == lowerCompare) {
            foundLower = true;
        } else if (term == upperCompare) {
            foundUpper = true;
        } else {
            auto* instruction =
                dynamic_cast<IR::Instruction*>(term);
            if (instruction &&
                loop.body.count(instruction->getParent())) {
                return false;
            }
            if (!valueDominatesBlock(
                    term, preheader, dominators)) {
                return false;
            }
            invariantConditions.push_back(term);
        }
    }
    if (!foundLower || !foundUpper ||
        invariantConditions.empty() ||
        !valueDominatesBlock(
            coordinate.outer, preheader, dominators) ||
        !valueDominatesBlock(
            imageBound, preheader, dominators)) {
        return false;
    }

    auto* innerStart =
        asConstant(inductionIncoming.initialValue);
    auto* innerLimit = asConstant(innerBound);
    if (!innerStart || !innerLimit ||
        innerStart->getValue() < 0 ||
        innerLimit->getValue() <=
            innerStart->getValue()) {
        return false;
    }
    int64_t lowerMargin =
        -(innerStart->getValue() + coordinate.offset);
    int64_t upperMargin =
        innerLimit->getValue() - 1 + coordinate.offset;
    if (lowerMargin < 0 || upperMargin < 0 ||
        lowerMargin >
            std::numeric_limits<int32_t>::max() ||
        upperMargin >
            std::numeric_limits<int32_t>::max()) {
        return false;
    }

    CanonicalCountedLoop outerLoop;
    if (!analyzeCanonicalCountedLoop(
            function, coordinate.outer, imageBound,
            preheader, outerLoop) ||
        outerLoop.step != 1 ||
        outerLoop.inclusiveUpperBound ||
        !isZero(outerLoop.start)) {
        return false;
    }

    candidate.function = function;
    candidate.innerLoop = loop;
    candidate.preheader = preheader;
    candidate.header = loop.header;
    candidate.conditionBlock = conditionBlock;
    candidate.computeBlock = computeBlock;
    candidate.skipBlock = skipBlock;
    candidate.latch = loop.latch;
    candidate.exit = exit;
    candidate.induction = induction;
    candidate.accumulator = accumulator;
    candidate.computedAccumulator = computedAccumulator;
    candidate.inductionNext = inductionNext;
    candidate.innerBound = innerBound;
    candidate.imageBound = imageBound;
    candidate.outerCoordinate = coordinate.outer;
    candidate.invariantConditions =
        std::move(invariantConditions);
    candidate.lowerMargin = lowerMargin;
    candidate.upperMargin = upperMargin;
    candidate.inductionIncoming = inductionIncoming;
    candidate.accumulatorIncoming = accumulatorIncoming;
    return true;
}

IR::BasicBlock::iterator findTerminator(
    IR::BasicBlock* block) {
    auto* terminator = block->getTerminator();
    for (auto iterator = block->begin();
         iterator != block->end(); ++iterator) {
        if (iterator->get() == terminator) return iterator;
    }
    return block->end();
}

IR::Instruction* createPhi(
    IR::Type* type, const std::string& name,
    IR::Value* initial, IR::BasicBlock* preheader,
    IR::Value* backedge, IR::BasicBlock* latch) {
    auto* phi = IR::Instruction::createPhi(type, name, 4);
    phi->addOperand(initial);
    phi->addOperand(preheader);
    phi->addOperand(backedge);
    phi->addOperand(latch);
    return phi;
}

void replaceExternalUses(
    IR::Instruction* original, IR::Instruction* replacement,
    const std::unordered_set<IR::BasicBlock*>& loopBody,
    IR::Instruction* joinPhi) {
    auto uses = original->getUses();
    for (const auto& use : uses) {
        auto* user =
            dynamic_cast<IR::Instruction*>(use.user);
        if (!user || user == joinPhi ||
            loopBody.count(user->getParent())) {
            continue;
        }
        user->setOperand(use.operandNo, replacement);
    }
}

bool applyCandidate(
    const StencilCandidate& candidate,
    unsigned specializationId) {
    auto insertion = findTerminator(candidate.preheader);
    if (insertion == candidate.preheader->end()) {
        return false;
    }
    auto insert = [&](IR::Instruction* instruction) {
        insertion = candidate.preheader->insert(
            insertion, instruction);
        ++insertion;
        return instruction;
    };

    auto* i32 = IR::IntegerType::I32;
    auto* lower = IR::ConstantInt::get(
        i32, candidate.lowerMargin);
    auto* upperMargin = IR::ConstantInt::get(
        i32, candidate.upperMargin);
    auto* lowerGuard = insert(IR::Instruction::createCmp(
        Opc::ICMP, candidate.outerCoordinate,
        lower, "sge"));
    IR::Value* upperLimit = candidate.imageBound;
    if (candidate.upperMargin != 0) {
        upperLimit = insert(IR::Instruction::createBinOp(
            Opc::SUB, i32, "stencil.upper.limit",
            candidate.imageBound, upperMargin));
    }
    auto* upperGuard = insert(IR::Instruction::createCmp(
        Opc::ICMP, candidate.outerCoordinate,
        upperLimit, "slt"));
    IR::Value* guard = insert(IR::Instruction::createBinOp(
        Opc::AND, IR::IntegerType::I1,
        "stencil.column.interior",
        lowerGuard, upperGuard));
    for (auto* invariant :
         candidate.invariantConditions) {
        guard = insert(IR::Instruction::createBinOp(
            Opc::AND, IR::IntegerType::I1,
            "stencil.interior", guard, invariant));
    }

    std::string suffix =
        std::to_string(specializationId);
    auto* fastHeader = candidate.function->insertBlock(
        "stencil.fast.header." + suffix,
        candidate.exit);
    auto* fastBody = candidate.function->insertBlock(
        "stencil.fast.body." + suffix,
        candidate.exit);
    auto* join = candidate.function->insertBlock(
        "stencil.join." + suffix,
        candidate.exit);

    auto* fastInduction = createPhi(
        candidate.induction->getType(),
        candidate.induction->getName() + ".stencil",
        candidate.inductionIncoming.initialValue,
        candidate.preheader, nullptr, fastBody);
    auto* fastAccumulator = createPhi(
        candidate.accumulator->getType(),
        candidate.accumulator->getName() + ".stencil",
        candidate.accumulatorIncoming.initialValue,
        candidate.preheader, nullptr, fastBody);
    fastHeader->pushBack(fastInduction);
    fastHeader->pushBack(fastAccumulator);
    auto* fastCompare = IR::Instruction::createCmp(
        Opc::ICMP, fastInduction,
        candidate.innerBound, "slt");
    fastHeader->pushBack(fastCompare);
    fastHeader->pushBack(
        IR::Instruction::createCondBr(
            fastCompare, fastBody, join));

    std::unordered_map<IR::Value*, IR::Value*> mapping = {
        {candidate.induction, fastInduction},
        {candidate.accumulator, fastAccumulator}};
    std::unordered_set<IR::BasicBlock*> cloneBlocks = {
        candidate.conditionBlock,
        candidate.computeBlock};
    bool valid = true;
    auto* fastUpdated = cloneValue(
        candidate.computedAccumulator, fastBody,
        cloneBlocks, mapping, valid);
    if (!valid || !fastUpdated) return false;
    auto* fastNext = IR::Instruction::createBinOp(
        Opc::ADD, i32,
        candidate.inductionNext->getName() + ".stencil",
        fastInduction, IR::ConstantInt::get(i32, 1));
    fastBody->pushBack(fastNext);
    fastBody->pushBack(IR::Instruction::createBr(
        fastHeader));
    fastInduction->setOperand(2, fastNext);
    fastAccumulator->setOperand(2, fastUpdated);

    auto* joinAccumulator = IR::Instruction::createPhi(
        candidate.accumulator->getType(),
        candidate.accumulator->getName() +
            ".stencil.join",
        4);
    joinAccumulator->addOperand(candidate.accumulator);
    joinAccumulator->addOperand(candidate.header);
    joinAccumulator->addOperand(fastAccumulator);
    joinAccumulator->addOperand(fastHeader);
    join->pushBack(joinAccumulator);
    join->pushBack(IR::Instruction::createBr(
        candidate.exit));

    auto* originalHeaderTerminator =
        candidate.header->getTerminator();
    originalHeaderTerminator->setOperand(2, join);
    for (auto& instruction :
         candidate.exit->getInstructions()) {
        if (instruction->getOpcode() != Opc::PHI) continue;
        for (unsigned index = 1;
             index < instruction->getNumOperands();
             index += 2) {
            if (instruction->getOperand(index) ==
                candidate.header) {
                instruction->setOperand(index, join);
            }
        }
    }
    replaceExternalUses(
        candidate.accumulator, joinAccumulator,
        candidate.innerLoop.body, joinAccumulator);

    insertion = candidate.preheader->erase(insertion);
    candidate.preheader->insert(
        insertion,
        IR::Instruction::createCondBr(
            guard, fastHeader, candidate.header));
    return true;
}

} // namespace

bool stencilInteriorSpecialization(IR::Module* module) {
    bool changed = false;
    unsigned specializationId = 0;
    for (auto& ownedFunction : module->getFunctions()) {
        auto* function = ownedFunction.get();
        if (!function || function->isExternal()) continue;

        auto loops = getLoopsInnermostFirst(function);
        auto predecessors = buildPredecessors(function);
        auto dominators = computeDominators(function);
        std::vector<StencilCandidate> candidates;
        for (const auto& loop : loops) {
            StencilCandidate candidate;
            if (buildCandidate(
                    function, loop, predecessors,
                    dominators, candidate)) {
                candidates.push_back(std::move(candidate));
            }
        }
        for (const auto& candidate : candidates) {
            if (applyCandidate(
                    candidate, specializationId++)) {
                changed = true;
                break;
            }
        }
    }
    return changed;
}

} // namespace Opt
