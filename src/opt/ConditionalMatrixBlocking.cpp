#include "opt/LoopPatternAnalysis.h"
#include "opt/MemoryAccessAnalysis.h"
#include "opt/Optimizer.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

constexpr unsigned kBlockWidth = 4;

struct StackCountedLoop {
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::Value* pointer = nullptr;
    IR::Value* bound = nullptr;
    std::unordered_set<IR::BasicBlock*> body;
};

struct ConditionalMatrixPlan {
    IR::Function* function = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::GlobalVariable* matrixA = nullptr;
    IR::GlobalVariable* matrixB = nullptr;
    IR::GlobalVariable* output = nullptr;
    IR::ArrayType* rowType = nullptr;
    IR::Type* outerMatrixType = nullptr;
    IR::Value* size = nullptr;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
}

bool sameBound(IR::Value* left, IR::Value* right) {
    if (left == right) return true;
    auto* leftConstant = dynamic_cast<IR::ConstantInt*>(left);
    auto* rightConstant = dynamic_cast<IR::ConstantInt*>(right);
    return leftConstant && rightConstant &&
           leftConstant->getValue() == rightConstant->getValue();
}

IR::Instruction* makePhi(
    IR::Type* type, const std::string& name,
    IR::Value* firstValue, IR::BasicBlock* firstBlock,
    IR::Value* secondValue, IR::BasicBlock* secondBlock) {
    auto* phi = IR::Instruction::createPhi(type, name, 4);
    phi->addOperand(firstValue);
    phi->addOperand(firstBlock);
    phi->addOperand(secondValue);
    phi->addOperand(secondBlock);
    return phi;
}

bool collectCanonicalLoop(
    IR::Function* function, IR::Value* induction,
    IR::BasicBlock* containedBlock,
    CanonicalCountedLoop& loop) {
    auto* phi = dynamic_cast<IR::Instruction*>(induction);
    auto* header = phi ? phi->getParent() : nullptr;
    auto* terminator = header ? header->getTerminator() : nullptr;
    auto* compare = terminator && terminator->getOpcode() == Opc::COND_BR
        ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
        : nullptr;
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getNumOperands() != 2) {
        return false;
    }

    IR::Value* bound = nullptr;
    if ((compare->getName() == "slt" || compare->getName() == "sle") &&
        compare->getOperand(0) == induction) {
        bound = compare->getOperand(1);
    } else if ((compare->getName() == "sgt" ||
                compare->getName() == "sge") &&
               compare->getOperand(1) == induction) {
        bound = compare->getOperand(0);
    }
    if (!bound || !analyzeCanonicalCountedLoop(
                      function, induction, bound,
                      containedBlock, loop)) {
        return false;
    }
    return isConstant(loop.start, 0) && loop.step == 1 &&
           !loop.inclusiveUpperBound;
}

bool isNestedInside(
    const std::unordered_set<IR::BasicBlock*>& inner,
    const std::unordered_set<IR::BasicBlock*>& outer) {
    return std::all_of(
        inner.begin(), inner.end(), [&](IR::BasicBlock* block) {
            return outer.count(block) != 0;
        });
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

IR::Value* stackIndexPointer(IR::Value* value) {
    auto* load = dynamic_cast<IR::Instruction*>(value);
    return load && load->getOpcode() == Opc::LOAD &&
                   load->getNumOperands() == 1
        ? load->getOperand(0)
        : nullptr;
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

bool collectStackCountedLoop(
    IR::Function* function, IR::Value* pointer,
    IR::BasicBlock* containedBlock, StackCountedLoop& result) {
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
        if (compare->getName() == "slt" &&
            isLoadFrom(compare->getOperand(0), pointer)) {
            bound = compare->getOperand(1);
        } else if (compare->getName() == "sgt" &&
                   isLoadFrom(compare->getOperand(1), pointer)) {
            bound = compare->getOperand(0);
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

        unsigned incrementStores = 0;
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
                ++incrementStores;
            }
            if (invalidStore) break;
        }
        if (invalidStore || incrementStores != 1) continue;

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
        result.pointer = pointer;
        result.bound = bound;
        result.body = loop.body;
        return true;
    }
    return false;
}

IR::ArrayType* getRowType(
    IR::GlobalVariable* global, IR::Type*& outerType) {
    auto* pointer = global
        ? dynamic_cast<IR::PointerType*>(global->getType())
        : nullptr;
    auto* outer = pointer
        ? dynamic_cast<IR::ArrayType*>(pointer->getPointeeType())
        : nullptr;
    auto* row = outer
        ? dynamic_cast<IR::ArrayType*>(outer->getElementType())
        : nullptr;
    if (!row || row->getElementType() != IR::IntegerType::I32 ||
        row->getNumElements() < kBlockWidth) {
        return nullptr;
    }
    outerType = outer;
    return row;
}

bool exitAcceptsBypass(IR::BasicBlock* exit) {
    if (!exit) return false;
    for (const auto& instruction : exit->getInstructions()) {
        if (instruction->getOpcode() == Opc::PHI) return false;
    }
    return true;
}

bool classifyLoad(
    IR::Instruction* load, IR::Value* firstIndex,
    IR::Value* secondIndex, IR::Value* stackIndex,
    IR::GlobalVariable*& root) {
    if (!load || load->getOpcode() != Opc::LOAD ||
        load->getType() != IR::IntegerType::I32 ||
        load->getNumOperands() != 1) {
        return false;
    }
    PointerAccess access;
    if (!collectPointerAccess(load->getOperand(0), nullptr, access) ||
        access.indices.size() != 2 ||
        access.indices[0] != firstIndex) {
        return false;
    }
    if (stackIndex) {
        if (!isLoadFrom(access.indices[1], stackIndex)) return false;
    } else if (access.indices[1] != secondIndex) {
        return false;
    }
    root = dynamic_cast<IR::GlobalVariable*>(access.root);
    return root != nullptr;
}

bool matchEvenCondition(
    IR::Value* condition, IR::Instruction*& firstLoad,
    IR::Instruction*& secondLoad) {
    auto* compare = dynamic_cast<IR::Instruction*>(condition);
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getName() != "eq" || compare->getNumOperands() != 2) {
        return false;
    }
    IR::Instruction* remainder = nullptr;
    if (isConstant(compare->getOperand(0), 0)) {
        remainder = dynamic_cast<IR::Instruction*>(compare->getOperand(1));
    } else if (isConstant(compare->getOperand(1), 0)) {
        remainder = dynamic_cast<IR::Instruction*>(compare->getOperand(0));
    }
    if (!remainder || remainder->getOpcode() != Opc::SREM ||
        remainder->getNumOperands() != 2 ||
        !isConstant(remainder->getOperand(1), 2)) {
        return false;
    }
    auto* product = dynamic_cast<IR::Instruction*>(
        remainder->getOperand(0));
    if (!product || product->getOpcode() != Opc::MUL ||
        product->getType() != IR::IntegerType::I32 ||
        product->getNumOperands() != 2) {
        return false;
    }
    firstLoad = dynamic_cast<IR::Instruction*>(product->getOperand(0));
    secondLoad = dynamic_cast<IR::Instruction*>(product->getOperand(1));
    return firstLoad && secondLoad;
}

bool matchConditionalStore(
    IR::Function* function, IR::Instruction* store,
    ConditionalMatrixPlan& plan) {
    if (!store || store->getOpcode() != Opc::STORE ||
        store->getNumOperands() != 2) {
        return false;
    }
    PointerAccess destination;
    if (!collectPointerAccess(store->getOperand(1), nullptr, destination) ||
        destination.indices.size() != 2) {
        return false;
    }
    auto* output = dynamic_cast<IR::GlobalVariable*>(destination.root);
    auto* reduction = dynamic_cast<IR::Instruction*>(store->getOperand(0));
    IR::Value* indexI = destination.indices[0];
    IR::Value* indexJPointer = stackIndexPointer(destination.indices[1]);
    if (!output || !indexJPointer || !reduction ||
        reduction->getOpcode() != Opc::PHI ||
        reduction->getType() != IR::IntegerType::I32 ||
        reduction->getNumOperands() != 4) {
        return false;
    }

    IR::Instruction* mergePhi = nullptr;
    IR::BasicBlock* initialBlock = nullptr;
    for (unsigned index = 0; index < 4; index += 2) {
        auto* block = dynamic_cast<IR::BasicBlock*>(
            reduction->getOperand(index + 1));
        if (isConstant(reduction->getOperand(index), 0)) {
            if (initialBlock) return false;
            initialBlock = block;
        } else {
            auto* candidate = dynamic_cast<IR::Instruction*>(
                reduction->getOperand(index));
            if (mergePhi || !candidate ||
                candidate->getOpcode() != Opc::PHI) {
                return false;
            }
            mergePhi = candidate;
        }
    }
    if (!initialBlock || !mergePhi || mergePhi->getNumOperands() != 4) {
        return false;
    }

    IR::Instruction* update = nullptr;
    IR::BasicBlock* updateBlock = nullptr;
    IR::BasicBlock* skipBlock = nullptr;
    for (unsigned index = 0; index < 4; index += 2) {
        auto* block = dynamic_cast<IR::BasicBlock*>(
            mergePhi->getOperand(index + 1));
        auto* value = mergePhi->getOperand(index);
        if (value == reduction) {
            if (skipBlock) return false;
            skipBlock = block;
            continue;
        }
        auto* candidate = dynamic_cast<IR::Instruction*>(value);
        if (update || !candidate || candidate->getOpcode() != Opc::ADD ||
            candidate->getNumOperands() != 2) {
            return false;
        }
        update = candidate;
        updateBlock = block;
    }
    if (!update || !updateBlock || !skipBlock) return false;

    IR::Instruction* updateProduct = nullptr;
    if (update->getOperand(0) == reduction) {
        updateProduct = dynamic_cast<IR::Instruction*>(update->getOperand(1));
    } else if (update->getOperand(1) == reduction) {
        updateProduct = dynamic_cast<IR::Instruction*>(update->getOperand(0));
    }
    if (!updateProduct || updateProduct->getOpcode() != Opc::MUL ||
        updateProduct->getType() != IR::IntegerType::I32 ||
        updateProduct->getNumOperands() != 2) {
        return false;
    }
    auto* updateFirst = dynamic_cast<IR::Instruction*>(
        updateProduct->getOperand(0));
    auto* updateSecond = dynamic_cast<IR::Instruction*>(
        updateProduct->getOperand(1));
    if (!updateFirst || !updateSecond) return false;

    CanonicalCountedLoop loopK;
    IR::Value* indexK = nullptr;
    for (const auto& owned : reduction->getParent()->getInstructions()) {
        auto* candidate = owned.get();
        if (candidate == reduction || candidate->getOpcode() != Opc::PHI) {
            continue;
        }
        CanonicalCountedLoop candidateLoop;
        if (!collectCanonicalLoop(
                function, candidate, updateProduct->getParent(),
                candidateLoop)) {
            continue;
        }
        if (indexK) return false;
        indexK = candidate;
        loopK = std::move(candidateLoop);
    }
    if (!indexK || loopK.header != reduction->getParent() ||
        !loopK.body.count(mergePhi->getParent())) {
        return false;
    }

    IR::Instruction* parityFirst = nullptr;
    IR::Instruction* paritySecond = nullptr;
    bool foundCondition = false;
    for (auto* block : loopK.body) {
        auto* terminator = block->getTerminator();
        if (!terminator || terminator->getOpcode() != Opc::COND_BR ||
            terminator->getNumOperands() != 3 ||
            terminator->getOperand(1) != updateBlock ||
            terminator->getOperand(2) != skipBlock) {
            continue;
        }
        if (foundCondition || !matchEvenCondition(
                                  terminator->getOperand(0),
                                  parityFirst, paritySecond)) {
            return false;
        }
        foundCondition = true;
    }
    if (!foundCondition) return false;

    IR::GlobalVariable* matrixB = nullptr;
    IR::GlobalVariable* matrixA = nullptr;
    IR::GlobalVariable* updateFirstRoot = nullptr;
    IR::GlobalVariable* updateSecondRoot = nullptr;
    if (classifyLoad(
            updateFirst, indexI, indexK, nullptr, updateFirstRoot) &&
        classifyLoad(
            updateSecond, indexK, nullptr, indexJPointer,
            updateSecondRoot)) {
        matrixB = updateFirstRoot;
        matrixA = updateSecondRoot;
    } else if (classifyLoad(
                   updateSecond, indexI, indexK, nullptr,
                   updateSecondRoot) &&
               classifyLoad(
                   updateFirst, indexK, nullptr, indexJPointer,
                   updateFirstRoot)) {
        matrixB = updateSecondRoot;
        matrixA = updateFirstRoot;
    } else {
        return false;
    }
    if (!matrixA || !matrixB || matrixA == matrixB ||
        output == matrixA || output == matrixB) {
        return false;
    }

    IR::GlobalVariable* parityRowRoot = nullptr;
    IR::GlobalVariable* parityColumnRoot = nullptr;
    auto matchesParity = [&](IR::Instruction* rowLoad,
                             IR::Instruction* columnLoad) {
        return classifyLoad(
                   rowLoad, indexI, indexK, nullptr, parityRowRoot) &&
               classifyLoad(
                   columnLoad, indexK, nullptr, indexJPointer,
                   parityColumnRoot) &&
               parityRowRoot == matrixA && parityColumnRoot == matrixB;
    };
    if (!matchesParity(parityFirst, paritySecond)) {
        parityRowRoot = nullptr;
        parityColumnRoot = nullptr;
        if (!matchesParity(paritySecond, parityFirst)) return false;
    }

    CanonicalCountedLoop loopI;
    StackCountedLoop loopJ;
    if (!collectCanonicalLoop(
            function, indexI, updateProduct->getParent(), loopI) ||
        !collectStackCountedLoop(
            function, indexJPointer,
            updateProduct->getParent(), loopJ) ||
        !sameBound(loopI.bound, loopJ.bound) ||
        !sameBound(loopI.bound, loopK.bound) ||
        !isNestedInside(loopK.body, loopJ.body) ||
        !isNestedInside(loopJ.body, loopI.body) ||
        loopK.body.count(store->getParent()) ||
        !loopJ.body.count(store->getParent())) {
        return false;
    }

    unsigned globalLoads = 0;
    unsigned globalStores = 0;
    const std::unordered_set<IR::Instruction*> expectedLoads = {
        updateFirst, updateSecond, parityFirst, paritySecond};
    for (auto* block : loopI.body) {
        for (const auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL) return false;
            if (instruction->getOpcode() != Opc::LOAD &&
                instruction->getOpcode() != Opc::STORE) {
                continue;
            }
            const unsigned pointerOperand =
                instruction->getOpcode() == Opc::LOAD ? 0 : 1;
            PointerAccess access;
            if (!collectPointerAccess(
                    instruction->getOperand(pointerOperand), nullptr,
                    access)) {
                continue;
            }
            if (!dynamic_cast<IR::GlobalVariable*>(access.root)) continue;
            if (instruction->getOpcode() == Opc::LOAD) {
                ++globalLoads;
                if (!expectedLoads.count(instruction.get())) return false;
            } else {
                ++globalStores;
                if (instruction.get() != store) return false;
            }
        }
    }
    if (globalLoads != 4 || globalStores != 1) return false;

    IR::Type* outerA = nullptr;
    IR::Type* outerB = nullptr;
    IR::Type* outerOutput = nullptr;
    auto* rowA = getRowType(matrixA, outerA);
    auto* rowB = getRowType(matrixB, outerB);
    auto* rowOutput = getRowType(output, outerOutput);
    auto* constantSize = dynamic_cast<IR::ConstantInt*>(loopI.bound);
    if (!rowA || rowB != rowA || rowOutput != rowA ||
        outerB != outerA || outerOutput != outerA || !constantSize ||
        constantSize->getValue() != rowA->getNumElements()) {
        return false;
    }

    IR::BasicBlock* preheader = nullptr;
    if (!findUniquePreheader(
            function, loopI.body, loopI.header, preheader)) {
        return false;
    }
    auto* outerTerminator = loopI.header->getTerminator();
    auto* bodyTarget = outerTerminator
        ? dynamic_cast<IR::BasicBlock*>(outerTerminator->getOperand(1))
        : nullptr;
    auto* exit = outerTerminator
        ? dynamic_cast<IR::BasicBlock*>(outerTerminator->getOperand(2))
        : nullptr;
    if (!bodyTarget || !exit || !loopI.body.count(bodyTarget) ||
        loopI.body.count(exit) || !exitAcceptsBypass(exit)) {
        return false;
    }

    plan.function = function;
    plan.preheader = preheader;
    plan.exit = exit;
    plan.matrixA = matrixA;
    plan.matrixB = matrixB;
    plan.output = output;
    plan.rowType = rowA;
    plan.outerMatrixType = outerA;
    plan.size = loopI.bound;
    return true;
}

bool analyzePlan(IR::Module* module, ConditionalMatrixPlan& plan) {
    std::vector<ConditionalMatrixPlan> matches;
    for (const auto& function : module->getFunctions()) {
        if (function->isExternal()) continue;
        for (const auto& block : function->getBlocks()) {
            for (const auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() != Opc::STORE) continue;
                ConditionalMatrixPlan candidate;
                if (matchConditionalStore(
                        function.get(), instruction.get(), candidate)) {
                    matches.push_back(candidate);
                }
            }
        }
    }
    if (matches.size() != 1) return false;
    plan = matches.front();
    return true;
}

IR::Instruction* appendLaneUpdate(
    IR::BasicBlock* block, IR::IntegerType* i32,
    IR::Value* rowAValue, IR::Value* rowBValue,
    IR::Value* columnA, IR::Value* columnB,
    IR::Instruction* accumulator, unsigned lane,
    const std::string& prefix) {
    IR::Value* addressA = columnA;
    IR::Value* addressB = columnB;
    if (lane != 0) {
        auto* offset = IR::ConstantInt::get(i32, lane);
        auto* laneA = IR::Instruction::createGetElementPtr(
            i32, columnA, {offset}, prefix + ".A.addr." +
                                      std::to_string(lane));
        auto* laneB = IR::Instruction::createGetElementPtr(
            i32, columnB, {offset}, prefix + ".B.addr." +
                                      std::to_string(lane));
        block->pushBack(laneA);
        block->pushBack(laneB);
        addressA = laneA;
        addressB = laneB;
    }
    auto* valueA = IR::Instruction::createLoad(
        i32, addressA, prefix + ".A." + std::to_string(lane));
    auto* valueB = IR::Instruction::createLoad(
        i32, addressB, prefix + ".B." + std::to_string(lane));
    auto* parity = IR::Instruction::createBinOp(
        Opc::AND, i32, prefix + ".parity." + std::to_string(lane),
        rowAValue, valueB);
    auto* odd = IR::Instruction::createBinOp(
        Opc::AND, i32, prefix + ".odd." + std::to_string(lane),
        parity, IR::ConstantInt::get(i32, 1));
    auto* enabled = IR::Instruction::createBinOp(
        Opc::XOR, i32, prefix + ".enabled." + std::to_string(lane),
        odd, IR::ConstantInt::get(i32, 1));
    auto* product = IR::Instruction::createBinOp(
        Opc::MUL, i32, prefix + ".product." + std::to_string(lane),
        rowBValue, valueA);
    auto* contribution = IR::Instruction::createBinOp(
        Opc::MUL, i32, prefix + ".contribution." + std::to_string(lane),
        product, enabled);
    auto* updated = IR::Instruction::createBinOp(
        Opc::ADD, i32, prefix + ".updated." + std::to_string(lane),
        accumulator, contribution);
    block->pushBack(valueA);
    block->pushBack(valueB);
    block->pushBack(parity);
    block->pushBack(odd);
    block->pushBack(enabled);
    block->pushBack(product);
    block->pushBack(contribution);
    block->pushBack(updated);
    return updated;
}

IR::Function* createBlockedKernel(
    IR::Module* module, IR::ArrayType* rowType) {
    auto* i32 = IR::IntegerType::I32;
    auto* rowPointer = IR::PointerType::get(rowType);
    auto* functionType = IR::FunctionType::get(
        IR::VoidType::get(),
        {i32, rowPointer, rowPointer, rowPointer});
    auto* function = module->createFunction(
        functionType, "__opt_conditional_matrix_mask4", false);
    function->setPreferExpandedLeafRegisters();

    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* blockWidth = IR::ConstantInt::get(i32, kBlockWidth);
    auto* rowElements = IR::ConstantInt::get(
        i32, rowType->getNumElements());

    auto* entry = function->createBlock("block.entry");
    auto* iHeader = function->createBlock("block.i.header");
    auto* iBody = function->createBlock("block.i.body");
    auto* fullHeader = function->createBlock("block.j.full.header");
    auto* fullPreheader = function->createBlock("block.j.full.preheader");
    auto* kHeader = function->createBlock("block.k.header");
    auto* kBody = function->createBlock("block.k.body");
    auto* fullStore = function->createBlock("block.j.full.store");
    auto* tailHeader = function->createBlock("block.j.tail.header");
    auto* tailPreheader = function->createBlock("block.j.tail.preheader");
    auto* tailKHeader = function->createBlock("block.tail.k.header");
    auto* tailKBody = function->createBlock("block.tail.k.body");
    auto* tailStore = function->createBlock("block.j.tail.store");
    auto* iLatch = function->createBlock("block.i.latch");
    auto* exit = function->createBlock("block.exit");

    auto* iNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.i.next", nullptr, one);
    auto* indexI = makePhi(
        i32, "block.i", zero, entry, iNext, iLatch);
    iNext->setOperand(0, indexI);
    entry->pushBack(IR::Instruction::createBr(iHeader));
    iHeader->pushBack(indexI);
    auto* iCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexI, function->getArg(0), "slt");
    iHeader->pushBack(iCompare);
    iHeader->pushBack(IR::Instruction::createCondBr(
        iCompare, iBody, exit));

    auto* rowAStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1), {indexI, zero}, "block.A.row");
    auto* rowBStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {indexI, zero}, "block.B.row");
    iBody->pushBack(rowAStart);
    iBody->pushBack(rowBStart);
    iBody->pushBack(IR::Instruction::createBr(fullHeader));

    auto* fullJNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.j.full.next", nullptr, blockWidth);
    auto* fullJ = makePhi(
        i32, "block.j.full", zero, iBody, fullJNext, fullStore);
    fullJNext->setOperand(0, fullJ);
    fullHeader->pushBack(fullJ);
    auto* fullEnd = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.j.full.end", fullJ, blockWidth);
    auto* hasFullBlock = IR::Instruction::createCmp(
        Opc::ICMP, fullEnd, function->getArg(0), "sle");
    fullHeader->pushBack(fullEnd);
    fullHeader->pushBack(hasFullBlock);
    fullHeader->pushBack(IR::Instruction::createCondBr(
        hasFullBlock, fullPreheader, tailHeader));

    auto* columnAStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1), {zero, fullJ}, "block.A.column");
    auto* columnBStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {zero, fullJ}, "block.B.column");
    auto* outputStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(3), {indexI, fullJ}, "block.output");
    fullPreheader->pushBack(columnAStart);
    fullPreheader->pushBack(columnBStart);
    fullPreheader->pushBack(outputStart);
    fullPreheader->pushBack(IR::Instruction::createBr(kHeader));

    auto* kNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.k.next", nullptr, one);
    auto* rowANext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "block.A.row.next");
    auto* rowBNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "block.B.row.next");
    auto* columnANext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {rowElements}, "block.A.column.next");
    auto* columnBNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {rowElements}, "block.B.column.next");
    auto* indexK = makePhi(
        i32, "block.k", zero, fullPreheader, kNext, kBody);
    auto* rowAPointer = makePhi(
        rowAStart->getType(), "block.A.row.ptr",
        rowAStart, fullPreheader, rowANext, kBody);
    auto* rowBPointer = makePhi(
        rowBStart->getType(), "block.B.row.ptr",
        rowBStart, fullPreheader, rowBNext, kBody);
    auto* columnAPointer = makePhi(
        columnAStart->getType(), "block.A.column.ptr",
        columnAStart, fullPreheader, columnANext, kBody);
    auto* columnBPointer = makePhi(
        columnBStart->getType(), "block.B.column.ptr",
        columnBStart, fullPreheader, columnBNext, kBody);
    kNext->setOperand(0, indexK);
    rowANext->setOperand(0, rowAPointer);
    rowBNext->setOperand(0, rowBPointer);
    columnANext->setOperand(0, columnAPointer);
    columnBNext->setOperand(0, columnBPointer);

    std::vector<IR::Instruction*> accumulators;
    for (unsigned lane = 0; lane < kBlockWidth; ++lane) {
        accumulators.push_back(makePhi(
            i32, "block.acc." + std::to_string(lane),
            zero, fullPreheader, nullptr, kBody));
    }
    kHeader->pushBack(indexK);
    kHeader->pushBack(rowAPointer);
    kHeader->pushBack(rowBPointer);
    kHeader->pushBack(columnAPointer);
    kHeader->pushBack(columnBPointer);
    for (auto* accumulator : accumulators) kHeader->pushBack(accumulator);
    auto* kCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexK, function->getArg(0), "slt");
    kHeader->pushBack(kCompare);
    kHeader->pushBack(IR::Instruction::createCondBr(
        kCompare, kBody, fullStore));

    auto* rowAValue = IR::Instruction::createLoad(
        i32, rowAPointer, "block.A.row.value");
    auto* rowBValue = IR::Instruction::createLoad(
        i32, rowBPointer, "block.B.row.value");
    kBody->pushBack(rowAValue);
    kBody->pushBack(rowBValue);
    for (unsigned lane = 0; lane < kBlockWidth; ++lane) {
        auto* next = appendLaneUpdate(
            kBody, i32, rowAValue, rowBValue,
            columnAPointer, columnBPointer,
            accumulators[lane], lane, "block");
        accumulators[lane]->setOperand(2, next);
    }
    kBody->pushBack(rowANext);
    kBody->pushBack(rowBNext);
    kBody->pushBack(columnANext);
    kBody->pushBack(columnBNext);
    kBody->pushBack(kNext);
    kBody->pushBack(IR::Instruction::createBr(kHeader));

    for (unsigned lane = 0; lane < kBlockWidth; ++lane) {
        IR::Value* address = outputStart;
        if (lane != 0) {
            auto* laneAddress = IR::Instruction::createGetElementPtr(
                i32, outputStart,
                {IR::ConstantInt::get(i32, lane)},
                "block.output.addr." + std::to_string(lane));
            fullStore->pushBack(laneAddress);
            address = laneAddress;
        }
        fullStore->pushBack(IR::Instruction::createStore(
            accumulators[lane], address));
    }
    fullStore->pushBack(fullJNext);
    fullStore->pushBack(IR::Instruction::createBr(fullHeader));

    auto* tailJNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.j.tail.next", nullptr, one);
    auto* tailJ = makePhi(
        i32, "block.j.tail", fullJ, fullHeader,
        tailJNext, tailStore);
    tailJNext->setOperand(0, tailJ);
    tailHeader->pushBack(tailJ);
    auto* tailCompare = IR::Instruction::createCmp(
        Opc::ICMP, tailJ, function->getArg(0), "slt");
    tailHeader->pushBack(tailCompare);
    tailHeader->pushBack(IR::Instruction::createCondBr(
        tailCompare, tailPreheader, iLatch));

    auto* tailColumnA = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1), {zero, tailJ}, "tail.A.column");
    auto* tailColumnB = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {zero, tailJ}, "tail.B.column");
    auto* tailOutput = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(3), {indexI, tailJ}, "tail.output");
    tailPreheader->pushBack(tailColumnA);
    tailPreheader->pushBack(tailColumnB);
    tailPreheader->pushBack(tailOutput);
    tailPreheader->pushBack(IR::Instruction::createBr(tailKHeader));

    auto* tailKNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "tail.k.next", nullptr, one);
    auto* tailRowANext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "tail.A.row.next");
    auto* tailRowBNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "tail.B.row.next");
    auto* tailColumnANext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {rowElements}, "tail.A.column.next");
    auto* tailColumnBNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {rowElements}, "tail.B.column.next");
    auto* tailK = makePhi(
        i32, "tail.k", zero, tailPreheader, tailKNext, tailKBody);
    auto* tailRowAPointer = makePhi(
        rowAStart->getType(), "tail.A.row.ptr",
        rowAStart, tailPreheader, tailRowANext, tailKBody);
    auto* tailRowBPointer = makePhi(
        rowBStart->getType(), "tail.B.row.ptr",
        rowBStart, tailPreheader, tailRowBNext, tailKBody);
    auto* tailColumnAPointer = makePhi(
        tailColumnA->getType(), "tail.A.column.ptr",
        tailColumnA, tailPreheader, tailColumnANext, tailKBody);
    auto* tailColumnBPointer = makePhi(
        tailColumnB->getType(), "tail.B.column.ptr",
        tailColumnB, tailPreheader, tailColumnBNext, tailKBody);
    auto* tailAccumulator = makePhi(
        i32, "tail.acc", zero, tailPreheader, nullptr, tailKBody);
    tailKNext->setOperand(0, tailK);
    tailRowANext->setOperand(0, tailRowAPointer);
    tailRowBNext->setOperand(0, tailRowBPointer);
    tailColumnANext->setOperand(0, tailColumnAPointer);
    tailColumnBNext->setOperand(0, tailColumnBPointer);
    tailKHeader->pushBack(tailK);
    tailKHeader->pushBack(tailRowAPointer);
    tailKHeader->pushBack(tailRowBPointer);
    tailKHeader->pushBack(tailColumnAPointer);
    tailKHeader->pushBack(tailColumnBPointer);
    tailKHeader->pushBack(tailAccumulator);
    auto* tailKCompare = IR::Instruction::createCmp(
        Opc::ICMP, tailK, function->getArg(0), "slt");
    tailKHeader->pushBack(tailKCompare);
    tailKHeader->pushBack(IR::Instruction::createCondBr(
        tailKCompare, tailKBody, tailStore));

    auto* tailRowAValue = IR::Instruction::createLoad(
        i32, tailRowAPointer, "tail.A.row.value");
    auto* tailRowBValue = IR::Instruction::createLoad(
        i32, tailRowBPointer, "tail.B.row.value");
    tailKBody->pushBack(tailRowAValue);
    tailKBody->pushBack(tailRowBValue);
    auto* tailNext = appendLaneUpdate(
        tailKBody, i32, tailRowAValue, tailRowBValue,
        tailColumnAPointer, tailColumnBPointer,
        tailAccumulator, 0, "tail");
    tailAccumulator->setOperand(2, tailNext);
    tailKBody->pushBack(tailRowANext);
    tailKBody->pushBack(tailRowBNext);
    tailKBody->pushBack(tailColumnANext);
    tailKBody->pushBack(tailColumnBNext);
    tailKBody->pushBack(tailKNext);
    tailKBody->pushBack(IR::Instruction::createBr(tailKHeader));

    tailStore->pushBack(IR::Instruction::createStore(
        tailAccumulator, tailOutput));
    tailStore->pushBack(tailJNext);
    tailStore->pushBack(IR::Instruction::createBr(tailHeader));

    iLatch->pushBack(iNext);
    iLatch->pushBack(IR::Instruction::createBr(iHeader));
    exit->pushBack(IR::Instruction::createRet(nullptr));
    return function;
}

bool applyPlan(IR::Module* module, const ConditionalMatrixPlan& plan) {
    auto* kernel = createBlockedKernel(module, plan.rowType);
    auto* zero = IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto makeBase = [&](IR::GlobalVariable* global,
                        const std::string& name) {
        return IR::Instruction::createGetElementPtr(
            plan.outerMatrixType, global, {zero, zero}, name);
    };
    auto* baseA = makeBase(plan.matrixA, "block.A.base");
    auto* baseB = makeBase(plan.matrixB, "block.B.base");
    auto* baseOutput = makeBase(plan.output, "block.output.base");
    auto* call = IR::Instruction::createCall(
        kernel->getFunctionType(), kernel,
        {plan.size, baseA, baseB, baseOutput}, "");

    auto* terminator = plan.preheader->getTerminator();
    if (!terminator || terminator->getOpcode() != Opc::BR) return false;
    for (auto iterator = plan.preheader->begin();
         iterator != plan.preheader->end(); ++iterator) {
        if (iterator->get() != terminator) continue;
        iterator = plan.preheader->insert(iterator, baseA);
        ++iterator;
        iterator = plan.preheader->insert(iterator, baseB);
        ++iterator;
        iterator = plan.preheader->insert(iterator, baseOutput);
        ++iterator;
        plan.preheader->insert(iterator, call);
        terminator->setOperand(0, plan.exit);
        return true;
    }
    return false;
}

} // namespace

bool conditionalMatrixBlocking(IR::Module* module) {
    ConditionalMatrixPlan plan;
    if (!analyzePlan(module, plan)) return false;
    return applyPlan(module, plan);
}

} // namespace Opt
