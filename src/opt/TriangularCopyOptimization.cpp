#include "opt/LoopPatternAnalysis.h"
#include "opt/MemoryAccessAnalysis.h"
#include "opt/Optimizer.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

struct StackLoop {
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Instruction* compare = nullptr;
    IR::Value* pointer = nullptr;
    IR::Value* bound = nullptr;
    unsigned boundOperand = 1;
    std::unordered_set<IR::BasicBlock*> body;
};

struct TriangularCopyPlan {
    IR::Function* function = nullptr;
    IR::Instruction* outerIndex = nullptr;
    IR::Value* rowSize = nullptr;
    IR::Value* columnSize = nullptr;
    IR::BasicBlock* innerPreheader = nullptr;
    IR::Instruction* innerCompare = nullptr;
    unsigned innerBoundOperand = 1;
    IR::Instruction* innerBranch = nullptr;
    IR::BasicBlock* copyBlock = nullptr;
    IR::Instruction* selfStore = nullptr;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool isLoadFrom(IR::Value* value, IR::Value* pointer) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
           load->getNumOperands() == 1 &&
           load->getOperand(0) == pointer;
}

bool isUnitIncrement(IR::Value* value, IR::Value* pointer) {
    auto* add = dynamic_cast<IR::Instruction*>(value);
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2) {
        return false;
    }
    return (isLoadFrom(add->getOperand(0), pointer) &&
            isConstant(add->getOperand(1), 1)) ||
           (isLoadFrom(add->getOperand(1), pointer) &&
            isConstant(add->getOperand(0), 1));
}

bool findUniquePreheader(
    IR::Function* function,
    const std::unordered_set<IR::BasicBlock*>& body,
    IR::BasicBlock* header, IR::BasicBlock*& preheader) {
    auto predecessors = buildPredecessors(function);
    preheader = nullptr;
    for (auto* predecessor : predecessors[header]) {
        if (body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator = preheader ? preheader->getTerminator() : nullptr;
    return terminator && terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == header;
}

bool collectCanonicalLoop(
    IR::Function* function, IR::Instruction* induction,
    IR::BasicBlock* containedBlock, CanonicalCountedLoop& result) {
    auto* header = induction ? induction->getParent() : nullptr;
    auto* terminator = header ? header->getTerminator() : nullptr;
    auto* compare = terminator && terminator->getOpcode() == Opc::COND_BR
        ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
        : nullptr;
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getNumOperands() != 2) {
        return false;
    }

    IR::Value* bound = nullptr;
    if (compare->getName() == "slt" &&
        compare->getOperand(0) == induction) {
        bound = compare->getOperand(1);
    } else if (compare->getName() == "sgt" &&
               compare->getOperand(1) == induction) {
        bound = compare->getOperand(0);
    }
    return bound && analyzeCanonicalCountedLoop(
                        function, induction, bound,
                        containedBlock, result) &&
           isConstant(result.start, 0) && result.step == 1 &&
           !result.inclusiveUpperBound;
}

bool collectStackLoop(
    IR::Function* function, IR::Value* pointer,
    IR::BasicBlock* containedBlock, StackLoop& result) {
    for (const auto& loop : findNaturalLoops(function)) {
        if (!loop.body.count(containedBlock)) continue;
        auto* terminator = loop.header ? loop.header->getTerminator() : nullptr;
        auto* compare = terminator && terminator->getOpcode() == Opc::COND_BR
            ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
            : nullptr;
        if (!compare || compare->getOpcode() != Opc::ICMP ||
            compare->getNumOperands() != 2) {
            continue;
        }

        IR::Value* bound = nullptr;
        unsigned boundOperand = 1;
        if (compare->getName() == "slt" &&
            isLoadFrom(compare->getOperand(0), pointer)) {
            bound = compare->getOperand(1);
        } else if (compare->getName() == "sgt" &&
                   isLoadFrom(compare->getOperand(1), pointer)) {
            bound = compare->getOperand(0);
            boundOperand = 0;
        }
        if (!bound) continue;

        IR::BasicBlock* preheader = nullptr;
        if (!findUniquePreheader(
                function, loop.body, loop.header, preheader)) {
            continue;
        }
        bool initialized = false;
        for (const auto& instruction : preheader->getInstructions()) {
            if (instruction->getOpcode() == Opc::STORE &&
                instruction->getNumOperands() == 2 &&
                instruction->getOperand(1) == pointer &&
                isConstant(instruction->getOperand(0), 0)) {
                initialized = true;
            }
        }
        if (!initialized) continue;

        unsigned increments = 0;
        bool invalidStore = false;
        for (auto* block : loop.body) {
            for (const auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() != Opc::STORE ||
                    instruction->getNumOperands() != 2 ||
                    instruction->getOperand(1) != pointer) {
                    continue;
                }
                if (!isUnitIncrement(instruction->getOperand(0), pointer)) {
                    invalidStore = true;
                    break;
                }
                ++increments;
            }
            if (invalidStore) break;
        }
        if (invalidStore || increments != 2) continue;

        auto* bodyTarget = dynamic_cast<IR::BasicBlock*>(
            terminator->getOperand(1));
        auto* exit = dynamic_cast<IR::BasicBlock*>(
            terminator->getOperand(2));
        if (!bodyTarget || !exit || !loop.body.count(bodyTarget) ||
            loop.body.count(exit)) {
            continue;
        }

        result.header = loop.header;
        result.preheader = preheader;
        result.exit = exit;
        result.compare = compare;
        result.pointer = pointer;
        result.bound = bound;
        result.boundOperand = boundOperand;
        result.body = loop.body;
        return true;
    }
    return false;
}

bool matchesIndex(IR::Value* value, IR::Value* exact,
                  IR::Value* stackPointer) {
    return stackPointer ? isLoadFrom(value, stackPointer) : value == exact;
}

bool matchesProduct(IR::Value* value, IR::Value* first,
                    IR::Value* second, IR::Value* stackFirst = nullptr) {
    auto* product = dynamic_cast<IR::Instruction*>(value);
    if (!product || product->getOpcode() != Opc::MUL ||
        product->getNumOperands() != 2) {
        return false;
    }
    auto matchesFirst = [&](IR::Value* candidate) {
        return matchesIndex(candidate, first, stackFirst);
    };
    return (matchesFirst(product->getOperand(0)) &&
            product->getOperand(1) == second) ||
           (matchesFirst(product->getOperand(1)) &&
            product->getOperand(0) == second);
}

bool matchesLinearIndex(
    IR::Value* value, IR::Value* multipliedIndex,
    IR::Value* scale, IR::Value* addedIndex,
    IR::Value* multipliedStack = nullptr,
    IR::Value* addedStack = nullptr) {
    auto* add = dynamic_cast<IR::Instruction*>(value);
    if (!add || add->getOpcode() != Opc::ADD ||
        add->getNumOperands() != 2) {
        return false;
    }
    auto matches = [&](IR::Value* product, IR::Value* added) {
        return matchesProduct(
                   product, multipliedIndex, scale,
                   multipliedStack) &&
               matchesIndex(added, addedIndex, addedStack);
    };
    return matches(add->getOperand(0), add->getOperand(1)) ||
           matches(add->getOperand(1), add->getOperand(0));
}

bool getFlatGlobalAccess(
    IR::Value* pointer, IR::GlobalVariable*& root,
    IR::Value*& index) {
    PointerAccess access;
    if (!collectPointerAccess(pointer, nullptr, access) ||
        access.indices.empty()) {
        return false;
    }
    root = dynamic_cast<IR::GlobalVariable*>(access.root);
    index = access.indices.back();
    return root != nullptr;
}

bool blockHasOnlyIncrementAndBranch(
    IR::BasicBlock* block, IR::Value* pointer,
    IR::BasicBlock* target) {
    if (!block) return false;
    unsigned increments = 0;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opc::BR) {
            if (instruction->getNumOperands() != 1 ||
                instruction->getOperand(0) != target) {
                return false;
            }
            continue;
        }
        if (instruction->getOpcode() == Opc::LOAD &&
            instruction->getNumOperands() == 1 &&
            instruction->getOperand(0) == pointer) {
            continue;
        }
        if (instruction->getOpcode() == Opc::ADD &&
            isUnitIncrement(instruction.get(), pointer)) {
            continue;
        }
        if (instruction->getOpcode() == Opc::STORE &&
            instruction->getNumOperands() == 2 &&
            instruction->getOperand(1) == pointer &&
            isUnitIncrement(instruction->getOperand(0), pointer)) {
            ++increments;
            continue;
        }
        return false;
    }
    return increments == 1;
}

bool matchGuard(
    IR::Instruction* branch, IR::Instruction*& outerIndex,
    IR::Value*& innerPointer, IR::BasicBlock*& skipBlock,
    IR::BasicBlock*& copyBlock) {
    if (!branch || branch->getOpcode() != Opc::COND_BR ||
        branch->getNumOperands() != 3) {
        return false;
    }
    auto* compare = dynamic_cast<IR::Instruction*>(branch->getOperand(0));
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getNumOperands() != 2) {
        return false;
    }

    IR::Value* innerValue = nullptr;
    if (compare->getName() == "slt") {
        outerIndex = dynamic_cast<IR::Instruction*>(compare->getOperand(0));
        innerValue = compare->getOperand(1);
    } else if (compare->getName() == "sgt") {
        outerIndex = dynamic_cast<IR::Instruction*>(compare->getOperand(1));
        innerValue = compare->getOperand(0);
    } else {
        return false;
    }
    auto* innerLoad = dynamic_cast<IR::Instruction*>(innerValue);
    if (!outerIndex || outerIndex->getOpcode() != Opc::PHI ||
        !innerLoad || innerLoad->getOpcode() != Opc::LOAD ||
        innerLoad->getNumOperands() != 1) {
        return false;
    }
    innerPointer = innerLoad->getOperand(0);
    skipBlock = dynamic_cast<IR::BasicBlock*>(branch->getOperand(1));
    copyBlock = dynamic_cast<IR::BasicBlock*>(branch->getOperand(2));
    return skipBlock && copyBlock;
}

bool matchCopyBlock(
    IR::BasicBlock* block, IR::Value* outerIndex,
    IR::Value* innerPointer, IR::Value* rowSize,
    IR::Value* columnSize, IR::Instruction*& selfStore) {
    std::vector<IR::Instruction*> globalLoads;
    std::vector<IR::Instruction*> globalStores;
    for (const auto& instruction : block->getInstructions()) {
        if (instruction->getOpcode() == Opc::CALL) return false;
        if (instruction->getOpcode() == Opc::LOAD) {
            IR::GlobalVariable* root = nullptr;
            IR::Value* index = nullptr;
            if (getFlatGlobalAccess(
                    instruction->getOperand(0), root, index)) {
                globalLoads.push_back(instruction.get());
            }
        } else if (instruction->getOpcode() == Opc::STORE) {
            IR::GlobalVariable* root = nullptr;
            IR::Value* index = nullptr;
            if (getFlatGlobalAccess(
                    instruction->getOperand(1), root, index)) {
                globalStores.push_back(instruction.get());
            }
        }
    }
    if (globalLoads.size() != 2 || globalStores.size() != 2) return false;

    IR::GlobalVariable* matrix = nullptr;
    for (auto* load : globalLoads) {
        IR::GlobalVariable* root = nullptr;
        IR::Value* index = nullptr;
        if (!getFlatGlobalAccess(load->getOperand(0), root, index) ||
            !matchesLinearIndex(
                index, outerIndex, rowSize, nullptr,
                nullptr, innerPointer)) {
            return false;
        }
        if (!matrix) matrix = root;
        if (root != matrix) return false;
    }

    IR::Instruction* destinationStore = nullptr;
    selfStore = nullptr;
    for (auto* store : globalStores) {
        auto* storedLoad = dynamic_cast<IR::Instruction*>(
            store->getOperand(0));
        if (!storedLoad ||
            std::find(globalLoads.begin(), globalLoads.end(), storedLoad) ==
                globalLoads.end()) {
            return false;
        }
        IR::GlobalVariable* root = nullptr;
        IR::Value* index = nullptr;
        if (!getFlatGlobalAccess(store->getOperand(1), root, index) ||
            root != matrix) {
            return false;
        }
        if (matchesLinearIndex(
                index, outerIndex, rowSize, nullptr,
                nullptr, innerPointer)) {
            if (selfStore) return false;
            selfStore = store;
        } else if (matchesLinearIndex(
                       index, nullptr, columnSize, outerIndex,
                       innerPointer, nullptr)) {
            if (destinationStore) return false;
            destinationStore = store;
        } else {
            return false;
        }
    }
    return matrix && destinationStore && selfStore;
}

bool matchPlan(
    IR::Function* function, IR::Instruction* branch,
    TriangularCopyPlan& plan) {
    IR::Instruction* outerIndex = nullptr;
    IR::Value* innerPointer = nullptr;
    IR::BasicBlock* skipBlock = nullptr;
    IR::BasicBlock* copyBlock = nullptr;
    if (!matchGuard(
            branch, outerIndex, innerPointer,
            skipBlock, copyBlock)) {
        return false;
    }

    CanonicalCountedLoop outerLoop;
    StackLoop innerLoop;
    if (!collectCanonicalLoop(
            function, outerIndex, copyBlock, outerLoop) ||
        !collectStackLoop(
            function, innerPointer, copyBlock, innerLoop) ||
        !std::all_of(
            innerLoop.body.begin(), innerLoop.body.end(),
            [&](IR::BasicBlock* block) {
                return outerLoop.body.count(block) != 0;
            }) ||
        !blockHasOnlyIncrementAndBranch(
            skipBlock, innerPointer, innerLoop.header)) {
        return false;
    }
    auto* innerBranch = innerLoop.header->getTerminator();
    if (!innerBranch || innerBranch->getOpcode() != Opc::COND_BR ||
        innerBranch->getOperand(1) != branch->getParent() ||
        !innerLoop.body.count(copyBlock) ||
        !innerLoop.body.count(skipBlock)) {
        return false;
    }

    IR::Instruction* selfStore = nullptr;
    if (!matchCopyBlock(
            copyBlock, outerIndex, innerPointer,
            innerLoop.bound, outerLoop.bound, selfStore)) {
        return false;
    }

    unsigned globalLoads = 0;
    unsigned globalStores = 0;
    for (auto* block : outerLoop.body) {
        for (const auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL) return false;
            const bool isLoad = instruction->getOpcode() == Opc::LOAD;
            const bool isStore = instruction->getOpcode() == Opc::STORE;
            if (!isLoad && !isStore) continue;
            const unsigned pointerOperand = isLoad ? 0 : 1;
            if (!rootGlobal(instruction->getOperand(pointerOperand))) continue;
            if (isLoad) ++globalLoads;
            if (isStore) ++globalStores;
        }
    }
    if (globalLoads != 2 || globalStores != 2) return false;

    plan.function = function;
    plan.outerIndex = outerIndex;
    plan.rowSize = innerLoop.bound;
    plan.columnSize = outerLoop.bound;
    plan.innerPreheader = innerLoop.preheader;
    plan.innerCompare = innerLoop.compare;
    plan.innerBoundOperand = innerLoop.boundOperand;
    plan.innerBranch = innerBranch;
    plan.copyBlock = copyBlock;
    plan.selfStore = selfStore;
    return true;
}

bool analyzePlan(IR::Module* module, TriangularCopyPlan& plan) {
    std::vector<TriangularCopyPlan> matches;
    for (const auto& function : module->getFunctions()) {
        if (function->isExternal()) continue;
        for (const auto& block : function->getBlocks()) {
            auto* terminator = block->getTerminator();
            if (!terminator || terminator->getOpcode() != Opc::COND_BR) {
                continue;
            }
            TriangularCopyPlan candidate;
            if (matchPlan(function.get(), terminator, candidate)) {
                matches.push_back(candidate);
            }
        }
    }
    if (matches.size() != 1) return false;
    plan = matches.front();
    return true;
}

bool eraseInstruction(
    IR::BasicBlock* block, IR::Instruction* instruction) {
    for (auto iterator = block->begin(); iterator != block->end();
         ++iterator) {
        if (iterator->get() != instruction) continue;
        block->erase(iterator);
        return true;
    }
    return false;
}

bool applyPlan(const TriangularCopyPlan& plan) {
    auto* one = IR::ConstantInt::get(IR::IntegerType::I32, 1);
    auto* next = IR::Instruction::createBinOp(
        Opc::ADD, IR::IntegerType::I32,
        "triangular.next", plan.outerIndex, one);
    auto* rowIsSmaller = IR::Instruction::createCmp(
        Opc::ICMP, plan.rowSize, next, "slt");
    auto* limit = IR::Instruction::createSelect(
        rowIsSmaller, plan.rowSize, next, "triangular.limit");

    auto* terminator = plan.innerPreheader->getTerminator();
    if (!terminator || terminator->getOpcode() != Opc::BR ||
        terminator->getOperand(0) != plan.innerBranch->getParent()) {
        return false;
    }
    for (auto iterator = plan.innerPreheader->begin();
         iterator != plan.innerPreheader->end(); ++iterator) {
        if (iterator->get() != terminator) continue;
        iterator = plan.innerPreheader->insert(iterator, next);
        ++iterator;
        iterator = plan.innerPreheader->insert(iterator, rowIsSmaller);
        ++iterator;
        plan.innerPreheader->insert(iterator, limit);
        break;
    }

    plan.innerCompare->setOperand(plan.innerBoundOperand, limit);
    plan.innerBranch->setOperand(1, plan.copyBlock);
    return eraseInstruction(plan.copyBlock, plan.selfStore);
}

} // namespace

bool triangularCopyOptimization(IR::Module* module) {
    TriangularCopyPlan plan;
    if (!analyzePlan(module, plan)) return false;
    return applyPlan(plan);
}

} // namespace Opt
