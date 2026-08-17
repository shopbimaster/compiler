#include "opt/VectorPlan.h"

#include "opt/Optimizer.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct Failure {
    VectorRejectReason reason = VectorRejectReason::None;
    const char* detail = "";
};

struct CanonicalLoop {
    IR::Instruction* induction = nullptr;
    IR::Value* inductionStorage = nullptr;
    IR::Instruction* compare = nullptr;
    IR::Value* start = nullptr;
    IR::Value* bound = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* latch = nullptr;
    IR::BasicBlock* exit = nullptr;
    bool inclusiveUpperBound = false;
    std::unordered_set<IR::Instruction*> controlOnly;
    std::unordered_set<IR::Instruction*> currentInductionLoads;
};

struct DecomposedPointer {
    IR::Value* root = nullptr;
    std::vector<IR::Value*> indices;
};

struct AffineIndex {
    bool valid = false;
    bool hasSymbolicInvariant = false;
    int64_t coefficient = 0;
    int64_t constant = 0;
};

struct CheckedAccess {
    VectorMemoryAccess access;
};

using ArgumentSpills =
    std::unordered_map<IR::Value*, IR::Argument*>;

bool isConstantInt(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool addChecked(int64_t lhs, int64_t rhs, int64_t& result) {
    if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs)) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool multiplyChecked(int64_t lhs, int64_t rhs, int64_t& result) {
    if (lhs == 0 || rhs == 0) {
        result = 0;
        return true;
    }
    if ((lhs == -1 && rhs == std::numeric_limits<int64_t>::min()) ||
        (rhs == -1 && lhs == std::numeric_limits<int64_t>::min())) {
        return false;
    }
    if (lhs > 0) {
        if (rhs > 0) {
            if (lhs > std::numeric_limits<int64_t>::max() / rhs) return false;
        } else if (rhs < std::numeric_limits<int64_t>::min() / lhs) {
            return false;
        }
    } else if (rhs > 0) {
        if (lhs < std::numeric_limits<int64_t>::min() / rhs) return false;
    } else if (lhs < std::numeric_limits<int64_t>::max() / rhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool instructionInLoop(
        IR::Instruction* instruction, const NaturalLoop& loop) {
    return instruction && instruction->getParent() &&
           loop.body.count(instruction->getParent()) != 0;
}

bool pointerTreeDoesNotEscape(
        IR::Value* pointer, std::unordered_set<IR::Value*>& visited) {
    if (!pointer || !visited.insert(pointer).second) return true;
    for (const auto& use : pointer->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) return false;
        switch (instruction->getOpcode()) {
            case Opc::GETELEMENTPTR:
                if (use.operandNo != 0 ||
                    !pointerTreeDoesNotEscape(instruction, visited)) {
                    return false;
                }
                break;
            case Opc::LOAD:
                if (use.operandNo != 0) return false;
                break;
            case Opc::STORE:
                if (use.operandNo != 1) return false;
                break;
            default:
                return false;
        }
    }
    return true;
}

bool pointerTreeDoesNotEscape(IR::Value* pointer) {
    std::unordered_set<IR::Value*> visited;
    return pointerTreeDoesNotEscape(pointer, visited);
}

ArgumentSpills buildArgumentSpills(IR::Function* function) {
    struct Candidate {
        IR::Argument* argument = nullptr;
        IR::Instruction* store = nullptr;
        bool conflict = false;
    };
    std::unordered_map<IR::Value*, Candidate> candidates;
    for (const auto& block : function->getBlocks()) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2) {
                continue;
            }
            auto* argument =
                dynamic_cast<IR::Argument*>(instruction->getOperand(0));
            auto* alloca =
                dynamic_cast<IR::Instruction*>(instruction->getOperand(1));
            if (!argument || !alloca || alloca->getOpcode() != Opc::ALLOCA ||
                !argument->getType()->isPointer()) {
                continue;
            }
            auto& candidate = candidates[alloca];
            if (candidate.store) {
                candidate.conflict = true;
            } else {
                candidate.argument = argument;
                candidate.store = instruction;
            }
        }
    }

    ArgumentSpills result;
    for (const auto& entry : candidates) {
        auto* alloca = entry.first;
        const auto& candidate = entry.second;
        if (candidate.conflict || !candidate.store) continue;
        bool valid = true;
        for (const auto& use : alloca->getUses()) {
            auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
            if (!instruction) {
                valid = false;
                break;
            }
            if (instruction == candidate.store && use.operandNo == 1) {
                continue;
            }
            if (instruction->getOpcode() == Opc::LOAD && use.operandNo == 0) {
                continue;
            }
            valid = false;
            break;
        }
        if (valid) result.emplace(alloca, candidate.argument);
    }
    return result;
}

bool decomposePointerImpl(
        IR::Value* value, const ArgumentSpills& argumentSpills,
        DecomposedPointer& result,
        std::unordered_set<IR::Value*>& visiting) {
    if (!value || !visiting.insert(value).second) return false;

    if (dynamic_cast<IR::Argument*>(value) ||
        dynamic_cast<IR::GlobalVariable*>(value)) {
        result.root = value;
        visiting.erase(value);
        return true;
    }
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction) {
        visiting.erase(value);
        return false;
    }
    if (instruction->getOpcode() == Opc::ALLOCA) {
        result.root = instruction;
        visiting.erase(value);
        return true;
    }
    if (instruction->getOpcode() == Opc::LOAD &&
        instruction->getNumOperands() == 1) {
        auto found = argumentSpills.find(instruction->getOperand(0));
        if (found != argumentSpills.end()) {
            result.root = found->second;
            visiting.erase(value);
            return true;
        }
    }
    if (instruction->getOpcode() != Opc::GETELEMENTPTR ||
        instruction->getNumOperands() < 2 ||
        !decomposePointerImpl(
            instruction->getOperand(0), argumentSpills,
            result, visiting)) {
        visiting.erase(value);
        return false;
    }
    for (unsigned index = 1; index < instruction->getNumOperands(); ++index) {
        auto* operand = instruction->getOperand(index);
        if (isConstantInt(operand, 0) &&
            index + 1 < instruction->getNumOperands()) {
            continue;
        }
        result.indices.push_back(operand);
    }
    visiting.erase(value);
    return true;
}

bool decomposePointer(
        IR::Value* value, const ArgumentSpills& argumentSpills,
        DecomposedPointer& result) {
    std::unordered_set<IR::Value*> visiting;
    return decomposePointerImpl(value, argumentSpills, result, visiting);
}

bool pointerTreeHasStoreInLoop(
        IR::Value* pointer, const NaturalLoop& loop,
        std::unordered_set<IR::Value*>& visited) {
    if (!pointer || !visited.insert(pointer).second) return false;
    for (const auto& use : pointer->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (instruction && instruction->getOpcode() == Opc::STORE &&
            use.operandNo == 1 && instructionInLoop(instruction, loop)) {
            return true;
        }
        if (instruction && instruction->getOpcode() == Opc::GETELEMENTPTR &&
            use.operandNo == 0 &&
            pointerTreeHasStoreInLoop(instruction, loop, visited)) {
            return true;
        }
    }
    return false;
}

bool pointerTreeHasStoreInLoop(
        IR::Value* pointer, const NaturalLoop& loop) {
    std::unordered_set<IR::Value*> visited;
    return pointerTreeHasStoreInLoop(pointer, loop, visited);
}

bool isLoopInvariantValueImpl(
        IR::Value* value, const NaturalLoop& loop,
        std::unordered_set<IR::Value*>& visiting) {
    if (!value) return false;
    if (dynamic_cast<IR::Constant*>(value) ||
        dynamic_cast<IR::Argument*>(value) ||
        dynamic_cast<IR::GlobalVariable*>(value) ||
        dynamic_cast<IR::Function*>(value)) {
        return true;
    }
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction) return false;
    if (!instructionInLoop(instruction, loop)) return true;
    if (!visiting.insert(value).second) return false;

    bool invariant = false;
    if (instruction->getOpcode() == Opc::LOAD &&
        instruction->getNumOperands() == 1) {
        auto* alloca = dynamic_cast<IR::Instruction*>(
            instruction->getOperand(0));
        invariant = alloca && alloca->getOpcode() == Opc::ALLOCA &&
                    pointerTreeDoesNotEscape(alloca) &&
                    !pointerTreeHasStoreInLoop(alloca, loop);
    } else {
        switch (instruction->getOpcode()) {
            case Opc::ADD:
            case Opc::SUB:
            case Opc::MUL:
            case Opc::SDIV:
            case Opc::SREM:
            case Opc::FADD:
            case Opc::FSUB:
            case Opc::FMUL:
            case Opc::FDIV:
            case Opc::AND:
            case Opc::OR:
            case Opc::XOR:
            case Opc::SHL:
            case Opc::ASHR:
            case Opc::SMULH:
            case Opc::WIDE_SMOD_MUL:
            case Opc::ICMP:
            case Opc::FCMP:
            case Opc::ZEXT:
            case Opc::SEXT:
            case Opc::TRUNC:
            case Opc::SITOFP:
            case Opc::FPTOSI:
            case Opc::SELECT:
                invariant = true;
                for (unsigned i = 0; i < instruction->getNumOperands(); ++i) {
                    if (!isLoopInvariantValueImpl(
                            instruction->getOperand(i), loop, visiting)) {
                        invariant = false;
                        break;
                    }
                }
                break;
            default:
                invariant = false;
                break;
        }
    }
    visiting.erase(value);
    return invariant;
}

bool isLoopInvariantValue(IR::Value* value, const NaturalLoop& loop) {
    std::unordered_set<IR::Value*> visiting;
    return isLoopInvariantValueImpl(value, loop, visiting);
}

void collectControlDefinitions(
        IR::Value* value, const NaturalLoop& loop,
        std::unordered_set<IR::Instruction*>& result) {
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || !instructionInLoop(instruction, loop) ||
        !result.insert(instruction).second) {
        return;
    }
    for (unsigned i = 0; i < instruction->getNumOperands(); ++i) {
        collectControlDefinitions(instruction->getOperand(i), loop, result);
    }
}

bool matchPhiInduction(
        IR::Value* value, const NaturalLoop& loop,
        IR::BasicBlock* preheader, IR::BasicBlock* latch,
        CanonicalLoop& canonical, Failure& failure) {
    auto* phi = dynamic_cast<IR::Instruction*>(value);
    if (!phi || phi->getOpcode() != Opc::PHI ||
        phi->getParent() != loop.header ||
        phi->getNumOperands() != 4) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "loop comparison does not use a canonical induction phi"};
        return false;
    }

    IR::Value* start = nullptr;
    IR::Value* update = nullptr;
    for (unsigned i = 0; i < 4; i += 2) {
        auto* incomingBlock =
            dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
        if (incomingBlock == preheader) {
            if (start) return false;
            start = phi->getOperand(i);
        } else if (incomingBlock == latch) {
            if (update) return false;
            update = phi->getOperand(i);
        } else {
            failure = {VectorRejectReason::MissingCanonicalInduction,
                       "induction phi has an unexpected incoming edge"};
            return false;
        }
    }
    if (!start || !update) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "induction phi is missing its entry or back-edge value"};
        return false;
    }
    auto* add = dynamic_cast<IR::Instruction*>(update);
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2 || add->getParent() != latch) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "induction back edge is not a canonical add"};
        return false;
    }
    IR::Value* step = nullptr;
    if (add->getOperand(0) == phi) step = add->getOperand(1);
    if (add->getOperand(1) == phi) step = add->getOperand(0);
    if (!step) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "induction update does not consume its phi"};
        return false;
    }
    auto* stepConstant = dynamic_cast<IR::ConstantInt*>(step);
    if (!stepConstant || stepConstant->getValue() != 1) {
        failure = {VectorRejectReason::NonUnitStep,
                   "induction step is not the constant one"};
        return false;
    }
    canonical.induction = phi;
    canonical.start = start;
    canonical.controlOnly.insert(phi);
    canonical.controlOnly.insert(add);
    return true;
}

bool isLoadFrom(IR::Value* value, IR::Value* pointer) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 && load->getOperand(0) == pointer;
}

bool precedesInBlock(IR::Instruction* first, IR::Instruction* second) {
    if (!first || !second || first->getParent() != second->getParent()) {
        return false;
    }
    for (const auto& owned : first->getParent()->getInstructions()) {
        if (owned.get() == first) return true;
        if (owned.get() == second) return false;
    }
    return false;
}

bool matchAllocaInduction(
        IR::Value* value, IR::Function* function,
        const NaturalLoop& loop, IR::BasicBlock* preheader,
        IR::BasicBlock* latch, CanonicalLoop& canonical,
        Failure& failure) {
    auto* compareLoad = dynamic_cast<IR::Instruction*>(value);
    auto* alloca = compareLoad && compareLoad->getOpcode() == Opc::LOAD &&
                           compareLoad->getNumOperands() == 1
                       ? dynamic_cast<IR::Instruction*>(
                             compareLoad->getOperand(0))
                       : nullptr;
    if (!alloca || compareLoad->getParent() != loop.header ||
        alloca->getOpcode() != Opc::ALLOCA ||
        !pointerTreeDoesNotEscape(alloca)) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "loop comparison does not use a canonical induction value"};
        return false;
    }

    IR::Instruction* initialization = nullptr;
    IR::Instruction* updateStore = nullptr;
    unsigned storeCount = 0;
    for (const auto& block : function->getBlocks()) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getOpcode() != Opc::STORE ||
                instruction->getNumOperands() != 2 ||
                instruction->getOperand(1) != alloca) {
                continue;
            }
            ++storeCount;
            if (instruction->getParent() == preheader) {
                initialization = instruction;
            } else if (instruction->getParent() == latch) {
                updateStore = instruction;
            }
        }
    }
    if (storeCount != 2 || !initialization || !updateStore) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "induction storage is not initialized and updated exactly once"};
        return false;
    }
    auto* add = dynamic_cast<IR::Instruction*>(updateStore->getOperand(0));
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2 || add->getParent() != latch) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "induction store does not contain a canonical add"};
        return false;
    }
    IR::Value* step = nullptr;
    IR::Instruction* updateLoad = nullptr;
    if (isLoadFrom(add->getOperand(0), alloca)) {
        updateLoad = dynamic_cast<IR::Instruction*>(add->getOperand(0));
        step = add->getOperand(1);
    } else if (isLoadFrom(add->getOperand(1), alloca)) {
        updateLoad = dynamic_cast<IR::Instruction*>(add->getOperand(1));
        step = add->getOperand(0);
    }
    auto* stepConstant = dynamic_cast<IR::ConstantInt*>(step);
    if (!updateLoad || !stepConstant || stepConstant->getValue() != 1) {
        failure = {VectorRejectReason::NonUnitStep,
                   "induction step is not the constant one"};
        return false;
    }
    if (updateLoad->getParent() != latch ||
        !precedesInBlock(updateLoad, add) ||
        !precedesInBlock(add, updateStore)) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "induction update order is not provably load-add-store"};
        return false;
    }
    for (const auto& use : alloca->getUses()) {
        auto* instruction = dynamic_cast<IR::Instruction*>(use.user);
        if (!instruction) {
            failure = {VectorRejectReason::LiveOutValue,
                       "induction storage has an unknown user"};
            return false;
        }
        if (instruction->getOpcode() == Opc::STORE && use.operandNo == 1) {
            if (instruction != initialization && instruction != updateStore) {
                failure = {VectorRejectReason::LoopCarriedDependence,
                           "induction storage has an extra store"};
                return false;
            }
            continue;
        }
        if (instruction->getOpcode() == Opc::LOAD && use.operandNo == 0 &&
            instructionInLoop(instruction, loop)) {
            if (instruction->getParent() == loop.header &&
                instruction != compareLoad) {
                failure = {VectorRejectReason::LoopCarriedDependence,
                           "loop header reloads induction outside the comparison"};
                return false;
            }
            if (instruction->getParent() == latch &&
                instruction != updateLoad &&
                !precedesInBlock(instruction, updateStore)) {
                failure = {VectorRejectReason::LoopCarriedDependence,
                           "induction is reloaded after its update"};
                return false;
            }
            canonical.currentInductionLoads.insert(instruction);
            continue;
        }
        failure = {VectorRejectReason::LiveOutValue,
                   "induction storage is observed outside the loop"};
        return false;
    }
    canonical.induction = compareLoad;
    canonical.inductionStorage = alloca;
    canonical.start = initialization->getOperand(0);
    canonical.controlOnly.insert(compareLoad);
    canonical.controlOnly.insert(updateLoad);
    canonical.controlOnly.insert(add);
    canonical.controlOnly.insert(updateStore);
    canonical.currentInductionLoads.insert(compareLoad);
    canonical.currentInductionLoads.insert(updateLoad);
    return true;
}

bool analyzeCanonicalLoop(
        IR::Function* function, const NaturalLoop& loop,
        CanonicalLoop& canonical, Failure& failure) {
    auto predecessors = buildPredecessors(function);
    std::vector<IR::BasicBlock*> outsidePreds;
    std::vector<IR::BasicBlock*> insidePreds;
    for (auto* predecessor : predecessors[loop.header]) {
        (loop.body.count(predecessor) ? insidePreds : outsidePreds)
            .push_back(predecessor);
    }
    if (outsidePreds.size() != 1 || insidePreds.size() != 1) {
        failure = {VectorRejectReason::UnsupportedControlFlow,
                   "loop header does not have one entry and one back edge"};
        return false;
    }
    canonical.preheader = outsidePreds.front();
    canonical.latch = insidePreds.front();
    canonical.exit = loop.exitBlocks.front();

    auto* terminator = loop.header->getTerminator();
    if (!terminator || terminator->getOpcode() != Opc::COND_BR ||
        terminator->getNumOperands() != 3) {
        failure = {VectorRejectReason::UnsupportedControlFlow,
                   "loop header is not controlled by one conditional branch"};
        return false;
    }
    auto* trueBlock =
        dynamic_cast<IR::BasicBlock*>(terminator->getOperand(1));
    auto* falseBlock =
        dynamic_cast<IR::BasicBlock*>(terminator->getOperand(2));
    if (!trueBlock || !falseBlock || !loop.body.count(trueBlock) ||
        falseBlock != canonical.exit) {
        failure = {VectorRejectReason::UnsupportedControlFlow,
                   "loop continuation is not the true branch of its condition"};
        return false;
    }
    auto* compare =
        dynamic_cast<IR::Instruction*>(terminator->getOperand(0));
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getParent() != loop.header ||
        compare->getNumOperands() != 2) {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "loop condition is not a header integer comparison"};
        return false;
    }

    IR::Value* inductionValue = nullptr;
    IR::Value* bound = nullptr;
    const auto& predicate = compare->getName();
    if (predicate == "slt" || predicate == "sle") {
        inductionValue = compare->getOperand(0);
        bound = compare->getOperand(1);
        canonical.inclusiveUpperBound = predicate == "sle";
    } else if (predicate == "sgt" || predicate == "sge") {
        inductionValue = compare->getOperand(1);
        bound = compare->getOperand(0);
        canonical.inclusiveUpperBound = predicate == "sge";
    } else {
        failure = {VectorRejectReason::MissingCanonicalInduction,
                   "loop condition is not an ascending range comparison"};
        return false;
    }

    Failure phiFailure;
    if (!matchPhiInduction(
            inductionValue, loop, canonical.preheader,
            canonical.latch, canonical, phiFailure)) {
        Failure allocaFailure;
        if (!matchAllocaInduction(
                inductionValue, function, loop, canonical.preheader,
                canonical.latch, canonical, allocaFailure)) {
            failure = allocaFailure.reason == VectorRejectReason::NonUnitStep
                          ? allocaFailure
                          : phiFailure.reason == VectorRejectReason::NonUnitStep
                                ? phiFailure
                                : allocaFailure;
            return false;
        }
    }
    if (!isLoopInvariantValue(canonical.start, loop) ||
        !isLoopInvariantValue(bound, loop)) {
        failure = {VectorRejectReason::NonInvariantBound,
                   "loop start or bound is not provably invariant"};
        return false;
    }
    if (canonical.inclusiveUpperBound) {
        failure = {VectorRejectReason::PotentialInductionOverflow,
                   "inclusive upper bound requires a no-wrap guard"};
        return false;
    }
    canonical.bound = bound;
    canonical.compare = compare;
    canonical.controlOnly.insert(compare);
    canonical.controlOnly.insert(terminator);
    collectControlDefinitions(bound, loop, canonical.controlOnly);
    return true;
}

bool isInductionValue(IR::Value* value, const CanonicalLoop& canonical) {
    if (value == canonical.induction) return true;
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    return canonical.inductionStorage && instruction &&
           canonical.currentInductionLoads.count(instruction) != 0;
}

AffineIndex analyzeAffineIndexImpl(
        IR::Value* value, const NaturalLoop& loop,
        const CanonicalLoop& canonical,
        std::unordered_set<IR::Value*>& visiting) {
    if (!value) return {};
    if (isInductionValue(value, canonical)) {
        return {true, false, 1, 0};
    }
    if (auto* constant = dynamic_cast<IR::ConstantInt*>(value)) {
        return {true, false, 0, constant->getValue()};
    }
    auto* instruction = dynamic_cast<IR::Instruction*>(value);
    if (!instruction || !instructionInLoop(instruction, loop)) {
        return isLoopInvariantValue(value, loop)
                   ? AffineIndex{true, true, 0, 0}
                   : AffineIndex{};
    }
    if (!visiting.insert(value).second) return {};

    AffineIndex result;
    if ((instruction->getOpcode() == Opc::ADD ||
         instruction->getOpcode() == Opc::SUB) &&
        instruction->getNumOperands() == 2) {
        auto lhs = analyzeAffineIndexImpl(
            instruction->getOperand(0), loop, canonical, visiting);
        auto rhs = analyzeAffineIndexImpl(
            instruction->getOperand(1), loop, canonical, visiting);
        if (lhs.valid && rhs.valid) {
            if (instruction->getOpcode() == Opc::SUB) {
                if (!multiplyChecked(rhs.coefficient, -1, rhs.coefficient) ||
                    !multiplyChecked(rhs.constant, -1, rhs.constant)) {
                    visiting.erase(value);
                    return {};
                }
            }
            int64_t coefficient = 0;
            int64_t constant = 0;
            if (addChecked(lhs.coefficient, rhs.coefficient, coefficient) &&
                addChecked(lhs.constant, rhs.constant, constant)) {
                result = {true,
                          lhs.hasSymbolicInvariant ||
                              rhs.hasSymbolicInvariant,
                          coefficient, constant};
            }
        }
    } else if (instruction->getOpcode() == Opc::MUL &&
               instruction->getNumOperands() == 2) {
        IR::ConstantInt* scale =
            dynamic_cast<IR::ConstantInt*>(instruction->getOperand(0));
        IR::Value* other = instruction->getOperand(1);
        if (!scale) {
            scale = dynamic_cast<IR::ConstantInt*>(instruction->getOperand(1));
            other = instruction->getOperand(0);
        }
        if (scale) {
            auto base = analyzeAffineIndexImpl(
                other, loop, canonical, visiting);
            int64_t coefficient = 0;
            int64_t constant = 0;
            if (base.valid &&
                multiplyChecked(
                    base.coefficient, scale->getValue(), coefficient) &&
                multiplyChecked(
                    base.constant, scale->getValue(), constant)) {
                result = {true, base.hasSymbolicInvariant,
                          coefficient, constant};
            }
        }
    } else if (isLoopInvariantValue(value, loop)) {
        result = {true, true, 0, 0};
    }
    visiting.erase(value);
    return result;
}

AffineIndex analyzeAffineIndex(
        IR::Value* value, const NaturalLoop& loop,
        const CanonicalLoop& canonical) {
    std::unordered_set<IR::Value*> visiting;
    return analyzeAffineIndexImpl(value, loop, canonical, visiting);
}

bool isSupportedElementType(IR::Type* type) {
    if (!type) return false;
    if (type->isFloat()) return true;
    auto* integer = dynamic_cast<IR::IntegerType*>(type);
    return integer && integer->getBitWidth() > 1;
}

bool isAddressMaterializationLoad(
        IR::Instruction* instruction,
        const ArgumentSpills& argumentSpills) {
    return instruction->getOpcode() == Opc::LOAD &&
           instruction->getNumOperands() == 1 &&
           instruction->getType()->isPointer() &&
           argumentSpills.count(instruction->getOperand(0)) != 0;
}

bool analyzeMemoryInstruction(
        IR::Instruction* instruction, const NaturalLoop& loop,
        const CanonicalLoop& canonical,
        const ArgumentSpills& argumentSpills,
        CheckedAccess& checked, Failure& failure) {
    if (!instruction ||
        (instruction->getOpcode() != Opc::LOAD &&
         instruction->getOpcode() != Opc::STORE)) {
        failure = {VectorRejectReason::UnanalyzableMemoryAccess,
                   "instruction is not a load or store"};
        return false;
    }
    const bool isLoad = instruction->getOpcode() == Opc::LOAD;
    if ((isLoad && instruction->getNumOperands() != 1) ||
        (!isLoad && instruction->getNumOperands() != 2)) {
        failure = {VectorRejectReason::UnanalyzableMemoryAccess,
                   "memory instruction has malformed operands"};
        return false;
    }
    IR::Value* pointer = isLoad ? instruction->getOperand(0)
                                : instruction->getOperand(1);
    DecomposedPointer decomposed;
    if (!decomposePointer(pointer, argumentSpills, decomposed) ||
        !decomposed.root || decomposed.indices.empty()) {
        failure = {VectorRejectReason::UnanalyzableMemoryAccess,
                   "memory address has no auditable base and GEP path"};
        return false;
    }
    for (size_t i = 0; i + 1 < decomposed.indices.size(); ++i) {
        if (!isLoopInvariantValue(decomposed.indices[i], loop)) {
            failure = {VectorRejectReason::NonContiguousMemoryAccess,
                       "a non-final GEP index varies in the loop"};
            return false;
        }
    }
    auto* linearIndex = decomposed.indices.back();
    auto affine = analyzeAffineIndex(linearIndex, loop, canonical);
    if (!affine.valid || affine.coefficient != 1 ||
        affine.hasSymbolicInvariant) {
        failure = {VectorRejectReason::NonContiguousMemoryAccess,
                   "final GEP index is not induction plus a constant"};
        return false;
    }
    auto* elementType = isLoad ? instruction->getType()
                               : instruction->getOperand(0)->getType();
    if (!isSupportedElementType(elementType)) {
        failure = {VectorRejectReason::UnsupportedElementType,
                   "memory element is not a scalar integer or float"};
        return false;
    }
    checked.access.kind = isLoad ? VectorMemoryAccessKind::Load
                                 : VectorMemoryAccessKind::Store;
    checked.access.instruction = instruction;
    checked.access.root = decomposed.root;
    checked.access.elementType = elementType;
    checked.access.linearIndex = linearIndex;
    checked.access.inductionOffset = affine.constant;
    checked.access.invariantIndices.assign(
        decomposed.indices.begin(), decomposed.indices.end() - 1);
    return true;
}

bool rootsDoNotAlias(IR::Value* lhs, IR::Value* rhs) {
    if (lhs == rhs) return false;
    if (dynamic_cast<IR::GlobalVariable*>(lhs) &&
        dynamic_cast<IR::GlobalVariable*>(rhs)) {
        return true;
    }
    auto* lhsAlloca = dynamic_cast<IR::Instruction*>(lhs);
    auto* rhsAlloca = dynamic_cast<IR::Instruction*>(rhs);
    const bool lhsPrivate = lhsAlloca &&
        lhsAlloca->getOpcode() == Opc::ALLOCA &&
        pointerTreeDoesNotEscape(lhsAlloca);
    const bool rhsPrivate = rhsAlloca &&
        rhsAlloca->getOpcode() == Opc::ALLOCA &&
        pointerTreeDoesNotEscape(rhsAlloca);
    return lhsPrivate || rhsPrivate;
}

bool validateMemoryDependences(
        const std::vector<CheckedAccess>& accesses,
        Failure& failure) {
    for (size_t i = 0; i < accesses.size(); ++i) {
        for (size_t j = i + 1; j < accesses.size(); ++j) {
            const auto& lhs = accesses[i].access;
            const auto& rhs = accesses[j].access;
            if (lhs.kind == VectorMemoryAccessKind::Load &&
                rhs.kind == VectorMemoryAccessKind::Load) {
                continue;
            }
            if (lhs.root == rhs.root) {
                // A same-root read/write pair needs an explicit dependence
                // proof and instruction-order model. The initial plan has no
                // such model, so fail closed even when both addresses happen
                // to use the same lane.
                failure = {VectorRejectReason::LoopCarriedDependence,
                           "same memory root has mixed reads and writes"};
                return false;
            }
            if (!rootsDoNotAlias(lhs.root, rhs.root)) {
                failure = {VectorRejectReason::PossibleAlias,
                           "distinct memory roots may alias without a runtime guard"};
                return false;
            }
        }
    }
    return true;
}

bool hasNestedLoop(
        const NaturalLoop& loop,
        const std::vector<NaturalLoop>& loops) {
    for (const auto& other : loops) {
        if (&other == &loop || other.body.size() >= loop.body.size()) continue;
        if (loop.body.count(other.header)) return true;
    }
    return false;
}

bool validateControlFlow(
        IR::Function* function, const NaturalLoop& loop,
        const CanonicalLoop& canonical,
        Failure& failure) {
    if (loop.exitingBlocks.size() != 1 || loop.exitBlocks.size() != 1 ||
        loop.exitingBlocks.front() != loop.header) {
        failure = {VectorRejectReason::MultipleExits,
                   "loop does not have one header exit"};
        return false;
    }
    for (const auto& ownedBlock : function->getBlocks()) {
        auto* block = ownedBlock.get();
        if (!loop.body.count(block)) continue;
        auto* terminator = block->getTerminator();
        if (!terminator) {
            failure = {VectorRejectReason::UnsupportedControlFlow,
                       "loop block has no terminator"};
            return false;
        }
        if (block == loop.header) continue;
        if (terminator->getOpcode() != Opc::BR ||
            terminator->getNumOperands() != 1) {
            failure = {VectorRejectReason::UnsupportedControlFlow,
                       "loop contains internal conditional control flow"};
            return false;
        }
        auto* successor =
            dynamic_cast<IR::BasicBlock*>(terminator->getOperand(0));
        if (!successor || !loop.body.count(successor)) {
            failure = {VectorRejectReason::UnsupportedControlFlow,
                       "non-header loop block branches outside the loop"};
            return false;
        }
    }
    return canonical.exit == loop.exitBlocks.front();
}

bool hasOnlyControlUses(
        IR::Instruction* instruction, const NaturalLoop& loop,
        const CanonicalLoop& canonical) {
    for (const auto& use : instruction->getUses()) {
        auto* user = dynamic_cast<IR::Instruction*>(use.user);
        if (!user || !instructionInLoop(user, loop) ||
            canonical.controlOnly.count(user) == 0) {
            return false;
        }
    }
    return true;
}

bool isSupportedPureOpcode(Opc opcode) {
    switch (opcode) {
        case Opc::ADD:
        case Opc::SUB:
        case Opc::MUL:
        case Opc::FADD:
        case Opc::FSUB:
        case Opc::FMUL:
        case Opc::AND:
        case Opc::OR:
        case Opc::XOR:
        case Opc::ICMP:
        case Opc::FCMP:
        case Opc::ZEXT:
        case Opc::SEXT:
        case Opc::TRUNC:
        case Opc::SELECT:
            return true;
        default:
            return false;
    }
}

std::vector<IR::BasicBlock*> orderedLoopBlocks(
        IR::Function* function, const NaturalLoop& loop) {
    std::vector<IR::BasicBlock*> result;
    for (const auto& block : function->getBlocks()) {
        if (loop.body.count(block.get())) result.push_back(block.get());
    }
    return result;
}

bool validateInstructions(
        IR::Function* function, const NaturalLoop& loop,
        const CanonicalLoop& canonical, VectorPlan& plan,
        Failure& failure) {
    const auto argumentSpills = buildArgumentSpills(function);
    std::vector<CheckedAccess> accesses;
    bool hasStore = false;

    for (auto* block : plan.blocks) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            const auto opcode = instruction->getOpcode();

            if (opcode == Opc::CALL) {
                failure = {VectorRejectReason::CallInLoop,
                           "loop contains a call"};
                return false;
            }
            if (opcode == Opc::ALLOCA || opcode == Opc::RET) {
                failure = {VectorRejectReason::UnknownSideEffect,
                           "loop contains an unsupported side effect"};
                return false;
            }
            if (opcode == Opc::PHI) {
                if (instruction != canonical.induction) {
                    failure = {VectorRejectReason::LoopCarriedDependence,
                               "loop contains a non-induction phi"};
                    return false;
                }
                continue;
            }
            if (opcode == Opc::BR || opcode == Opc::COND_BR) continue;
            if (block == loop.header &&
                canonical.controlOnly.count(instruction) == 0) {
                failure = {VectorRejectReason::UnsupportedControlFlow,
                           "loop header contains non-control work"};
                return false;
            }
            if (canonical.controlOnly.count(instruction)) {
                if (instruction != canonical.induction &&
                    canonical.currentInductionLoads.count(instruction) == 0 &&
                    !hasOnlyControlUses(instruction, loop, canonical)) {
                    failure = {VectorRejectReason::LoopCarriedDependence,
                               "control definition is also used by loop work"};
                    return false;
                }
                continue;
            }
            if (canonical.inductionStorage &&
                ((opcode == Opc::LOAD &&
                  canonical.currentInductionLoads.count(instruction) != 0) ||
                 (opcode == Opc::STORE && instruction->getNumOperands() == 2 &&
                  instruction->getOperand(1) == canonical.inductionStorage &&
                  canonical.controlOnly.count(instruction) != 0))) {
                continue;
            }
            if (isAddressMaterializationLoad(instruction, argumentSpills)) {
                continue;
            }
            if (opcode == Opc::GETELEMENTPTR) continue;
            if (opcode == Opc::LOAD || opcode == Opc::STORE) {
                CheckedAccess checked;
                if (!analyzeMemoryInstruction(
                        instruction, loop, canonical,
                        argumentSpills, checked, failure)) {
                    return false;
                }
                hasStore |= opcode == Opc::STORE;
                accesses.push_back(std::move(checked));
                continue;
            }
            if (!isSupportedPureOpcode(opcode)) {
                failure = {VectorRejectReason::UnsupportedInstruction,
                           "loop contains an unsupported instruction"};
                return false;
            }
            plan.scalarOperations.push_back(instruction);
        }
    }
    if (!hasStore) {
        failure = {VectorRejectReason::NoMemoryWrite,
                   "loop has no contiguous memory write"};
        return false;
    }
    if (!validateMemoryDependences(accesses, failure)) return false;

    for (auto* block : plan.blocks) {
        for (const auto& owned : block->getInstructions()) {
            auto* instruction = owned.get();
            if (instruction->getType()->isVoid()) continue;
            for (const auto& use : instruction->getUses()) {
                auto* user = dynamic_cast<IR::Instruction*>(use.user);
                if (user && instructionInLoop(user, loop)) continue;
                failure = {VectorRejectReason::LiveOutValue,
                           "loop-defined value is used outside the loop"};
                return false;
            }
        }
    }

    for (auto& checked : accesses) {
        plan.memoryAccesses.push_back(std::move(checked.access));
    }
    return true;
}

int64_t computeConstantTripCount(const CanonicalLoop& canonical) {
    auto* start = dynamic_cast<IR::ConstantInt*>(canonical.start);
    auto* bound = dynamic_cast<IR::ConstantInt*>(canonical.bound);
    if (!start || !bound) return -1;
    const int64_t begin = start->getValue();
    const int64_t end = bound->getValue();
    if (end < begin) return 0;
    int64_t count = 0;
    if (begin == std::numeric_limits<int64_t>::min() ||
        !addChecked(end, -begin, count)) {
        return -1;
    }
    if (canonical.inclusiveUpperBound) {
        if (!addChecked(count, 1, count)) return -1;
    }
    return count;
}

void classifyPlan(VectorPlan& plan, const NaturalLoop& loop) {
    IR::Instruction* onlyStore = nullptr;
    unsigned storeCount = 0;
    for (const auto& access : plan.memoryAccesses) {
        if (access.kind == VectorMemoryAccessKind::Store) {
            onlyStore = access.instruction;
            ++storeCount;
        }
    }
    if (storeCount != 1 || !onlyStore) {
        plan.kind = VectorPlanKind::Elementwise;
        return;
    }
    auto* storedValue = onlyStore->getOperand(0);
    if (isLoopInvariantValue(storedValue, loop)) {
        plan.kind = VectorPlanKind::Fill;
        return;
    }
    auto* load = dynamic_cast<IR::Instruction*>(storedValue);
    if (load && load->getOpcode() == Opc::LOAD &&
        std::any_of(
            plan.memoryAccesses.begin(), plan.memoryAccesses.end(),
            [load](const VectorMemoryAccess& access) {
                return access.kind == VectorMemoryAccessKind::Load &&
                       access.instruction == load;
            })) {
        plan.kind = VectorPlanKind::Copy;
        return;
    }
    plan.kind = VectorPlanKind::Elementwise;
}

VectorizationReport reject(
        IR::Function* function, IR::BasicBlock* header,
        const Failure& failure) {
    VectorizationReport report;
    report.function = function;
    report.header = header;
    report.reason = failure.reason;
    report.detail = failure.detail;
    return report;
}

VectorizationReport analyzeLoop(
        IR::Function* function, const NaturalLoop& loop,
        const std::vector<NaturalLoop>& allLoops) {
    if (hasNestedLoop(loop, allLoops)) {
        return reject(function, loop.header,
            {VectorRejectReason::NotInnermost,
             "loop contains a nested natural loop"});
    }
    if (loop.exitingBlocks.size() != 1 || loop.exitBlocks.size() != 1) {
        return reject(function, loop.header,
            {VectorRejectReason::MultipleExits,
             "loop has multiple or unrecognized exits"});
    }

    CanonicalLoop canonical;
    Failure failure;
    if (!analyzeCanonicalLoop(function, loop, canonical, failure)) {
        return reject(function, loop.header, failure);
    }
    if (!validateControlFlow(function, loop, canonical, failure)) {
        return reject(function, loop.header, failure);
    }

    VectorPlan plan;
    plan.function = function;
    plan.header = loop.header;
    plan.latch = canonical.latch;
    plan.exit = canonical.exit;
    plan.induction = canonical.induction;
    plan.inductionStorage = canonical.inductionStorage;
    plan.compare = canonical.compare;
    plan.start = canonical.start;
    plan.bound = canonical.bound;
    plan.inclusiveUpperBound = canonical.inclusiveUpperBound;
    plan.constantTripCount = computeConstantTripCount(canonical);
    plan.blocks = orderedLoopBlocks(function, loop);

    if (!validateInstructions(
            function, loop, canonical, plan, failure)) {
        return reject(function, loop.header, failure);
    }
    classifyPlan(plan, loop);

    VectorizationReport report;
    report.function = function;
    report.header = loop.header;
    report.plan = std::move(plan);
    return report;
}

const char* vectorPlanKindName(VectorPlanKind kind) {
    switch (kind) {
        case VectorPlanKind::Fill: return "fill";
        case VectorPlanKind::Copy: return "copy";
        case VectorPlanKind::Elementwise: return "elementwise";
    }
    return "unknown";
}

} // namespace

std::vector<VectorizationReport>
analyzeVectorizationCandidates(IR::Function* function) {
    std::vector<VectorizationReport> reports;
    if (!function || function->isExternal()) return reports;

    auto loops = findNaturalLoops(function);
    std::unordered_map<IR::BasicBlock*, size_t> blockOrder;
    for (size_t i = 0; i < function->getBlocks().size(); ++i) {
        blockOrder[function->getBlocks()[i].get()] = i;
    }
    std::vector<size_t> order(loops.size());
    for (size_t i = 0; i < loops.size(); ++i) order[i] = i;
    std::stable_sort(
        order.begin(), order.end(),
        [&](size_t lhs, size_t rhs) {
            const auto lhsOrder = blockOrder[loops[lhs].header];
            const auto rhsOrder = blockOrder[loops[rhs].header];
            if (lhsOrder != rhsOrder) return lhsOrder < rhsOrder;
            return loops[lhs].body.size() < loops[rhs].body.size();
        });
    reports.reserve(order.size());
    for (size_t index : order) {
        reports.push_back(analyzeLoop(function, loops[index], loops));
    }
    return reports;
}

std::vector<VectorizationReport>
analyzeVectorizationCandidates(IR::Module* module) {
    std::vector<VectorizationReport> reports;
    if (!module) return reports;
    for (const auto& function : module->getFunctions()) {
        auto functionReports =
            analyzeVectorizationCandidates(function.get());
        reports.insert(
            reports.end(),
            std::make_move_iterator(functionReports.begin()),
            std::make_move_iterator(functionReports.end()));
    }
    return reports;
}

const char* vectorRejectReasonName(VectorRejectReason reason) {
    switch (reason) {
        case VectorRejectReason::None: return "accepted";
        case VectorRejectReason::NotInnermost: return "not-innermost";
        case VectorRejectReason::MultipleExits: return "multiple-exits";
        case VectorRejectReason::UnsupportedControlFlow:
            return "unsupported-control-flow";
        case VectorRejectReason::MissingCanonicalInduction:
            return "missing-canonical-induction";
        case VectorRejectReason::NonUnitStep: return "non-unit-step";
        case VectorRejectReason::NonInvariantBound:
            return "non-invariant-bound";
        case VectorRejectReason::CallInLoop: return "call-in-loop";
        case VectorRejectReason::UnknownSideEffect:
            return "unknown-side-effect";
        case VectorRejectReason::UnsupportedInstruction:
            return "unsupported-instruction";
        case VectorRejectReason::UnanalyzableMemoryAccess:
            return "unanalyzable-memory-access";
        case VectorRejectReason::NonContiguousMemoryAccess:
            return "non-contiguous-memory-access";
        case VectorRejectReason::UnsupportedElementType:
            return "unsupported-element-type";
        case VectorRejectReason::PossibleAlias: return "possible-alias";
        case VectorRejectReason::LoopCarriedDependence:
            return "loop-carried-dependence";
        case VectorRejectReason::PotentialInductionOverflow:
            return "potential-induction-overflow";
        case VectorRejectReason::LiveOutValue: return "live-out-value";
        case VectorRejectReason::NoMemoryWrite: return "no-memory-write";
    }
    return "unknown";
}

std::string formatVectorizationReport(
        const VectorizationReport& report) {
    std::ostringstream stream;
    stream << (report.function && !report.function->getName().empty()
                   ? report.function->getName()
                   : "<anonymous-function>")
           << ':'
           << (report.header && !report.header->getName().empty()
                   ? report.header->getName()
                   : "<unnamed-loop>")
           << ": " << vectorRejectReasonName(report.reason);
    if (report.accepted()) {
        stream << " (" << vectorPlanKindName(report.plan.kind) << ')';
    } else if (!report.detail.empty()) {
        stream << " - " << report.detail;
    }
    return stream.str();
}

} // namespace Opt
