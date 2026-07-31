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

struct MatrixBlockingPlan {
    IR::Function* function = nullptr;
    IR::BasicBlock* preheader = nullptr;
    IR::BasicBlock* exit = nullptr;
    IR::GlobalVariable* coefficient = nullptr;
    IR::GlobalVariable* matrix = nullptr;
    IR::ArrayType* rowType = nullptr;
    IR::Type* outerMatrixType = nullptr;
    IR::Value* size = nullptr;
};

bool isConstant(IR::Value* value, int64_t expected) {
    auto* constant = dynamic_cast<IR::ConstantInt*>(value);
    return constant && constant->getValue() == expected;
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
    auto* compare = terminator &&
                            terminator->getOpcode() == Opc::COND_BR
        ? dynamic_cast<IR::Instruction*>(terminator->getOperand(0))
        : nullptr;
    if (!compare || compare->getOpcode() != Opc::ICMP ||
        compare->getNumOperands() != 2) {
        return false;
    }

    IR::Value* bound = nullptr;
    if ((compare->getName() == "slt" ||
         compare->getName() == "sle") &&
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
    const CanonicalCountedLoop& inner,
    const CanonicalCountedLoop& outer) {
    return std::all_of(
        inner.body.begin(), inner.body.end(),
        [&](IR::BasicBlock* block) {
            return outer.body.count(block) != 0;
        });
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

bool findUniquePreheader(
    IR::Function* function, const CanonicalCountedLoop& loop,
    IR::BasicBlock*& preheader) {
    auto predecessors = buildPredecessors(function);
    preheader = nullptr;
    for (auto* predecessor : predecessors[loop.header]) {
        if (loop.body.count(predecessor)) continue;
        if (preheader) return false;
        preheader = predecessor;
    }
    auto* terminator = preheader ? preheader->getTerminator() : nullptr;
    return terminator && terminator->getOpcode() == Opc::BR &&
           terminator->getNumOperands() == 1 &&
           terminator->getOperand(0) == loop.header;
}

bool exitAcceptsBypass(IR::BasicBlock* exit) {
    if (!exit) return false;
    for (const auto& instruction : exit->getInstructions()) {
        if (instruction->getOpcode() == Opc::PHI) return false;
    }
    return true;
}

bool matchReductionStore(
    IR::Function* function, IR::Instruction* store,
    MatrixBlockingPlan& plan) {
    if (!store || store->getOpcode() != Opc::STORE ||
        store->getNumOperands() != 2) {
        return false;
    }

    PointerAccess destination;
    if (!collectPointerAccess(store->getOperand(1), nullptr, destination) ||
        destination.indices.size() != 2) {
        return false;
    }
    auto* matrix = dynamic_cast<IR::GlobalVariable*>(destination.root);
    auto* reduction =
        dynamic_cast<IR::Instruction*>(store->getOperand(0));
    if (!matrix || !reduction || reduction->getOpcode() != Opc::PHI ||
        reduction->getType() != IR::IntegerType::I32 ||
        reduction->getNumOperands() != 4) {
        return false;
    }

    IR::Instruction* update = nullptr;
    IR::BasicBlock* updateBlock = nullptr;
    IR::BasicBlock* initialBlock = nullptr;
    for (unsigned index = 0; index < 4; index += 2) {
        auto* block =
            dynamic_cast<IR::BasicBlock*>(reduction->getOperand(index + 1));
        auto* candidate =
            dynamic_cast<IR::Instruction*>(reduction->getOperand(index));
        if (isConstant(reduction->getOperand(index), 0)) {
            if (initialBlock) return false;
            initialBlock = block;
        } else if (candidate && candidate->getOpcode() == Opc::ADD) {
            if (update) return false;
            update = candidate;
            updateBlock = block;
        } else {
            return false;
        }
    }
    if (!update || !updateBlock || !initialBlock ||
        update->getNumOperands() != 2) {
        return false;
    }

    IR::Instruction* product = nullptr;
    if (update->getOperand(0) == reduction) {
        product = dynamic_cast<IR::Instruction*>(update->getOperand(1));
    } else if (update->getOperand(1) == reduction) {
        product = dynamic_cast<IR::Instruction*>(update->getOperand(0));
    }
    if (!product || product->getOpcode() != Opc::MUL ||
        product->getType() != IR::IntegerType::I32 ||
        product->getNumOperands() != 2) {
        return false;
    }

    CanonicalCountedLoop loopK;
    IR::Value* indexK = nullptr;
    for (const auto& owned : reduction->getParent()->getInstructions()) {
        auto* candidate = owned.get();
        if (candidate == reduction || candidate->getOpcode() != Opc::PHI) {
            continue;
        }
        CanonicalCountedLoop candidateLoop;
        if (!collectCanonicalLoop(
                function, candidate, product->getParent(), candidateLoop)) {
            continue;
        }
        if (indexK) return false;
        indexK = candidate;
        loopK = std::move(candidateLoop);
    }
    if (!indexK || loopK.header != reduction->getParent() ||
        !loopK.body.count(updateBlock) ||
        loopK.body.count(initialBlock)) {
        return false;
    }

    auto* firstLoad =
        dynamic_cast<IR::Instruction*>(product->getOperand(0));
    auto* secondLoad =
        dynamic_cast<IR::Instruction*>(product->getOperand(1));
    if (!firstLoad || !secondLoad ||
        firstLoad->getOpcode() != Opc::LOAD ||
        secondLoad->getOpcode() != Opc::LOAD ||
        firstLoad->getType() != IR::IntegerType::I32 ||
        secondLoad->getType() != IR::IntegerType::I32) {
        return false;
    }

    PointerAccess firstAccess;
    PointerAccess secondAccess;
    if (!collectPointerAccess(firstLoad->getOperand(0), nullptr, firstAccess) ||
        !collectPointerAccess(secondLoad->getOperand(0), nullptr, secondAccess) ||
        firstAccess.indices.size() != 2 ||
        secondAccess.indices.size() != 2) {
        return false;
    }

    IR::Value* indexI = destination.indices[0];
    IR::Value* indexJ = destination.indices[1];
    if (indexI == indexJ || indexI == indexK || indexJ == indexK) {
        return false;
    }

    IR::Instruction* matrixLoad = nullptr;
    IR::Instruction* coefficientLoad = nullptr;
    PointerAccess coefficientAccess;
    auto classifyLoad = [&](IR::Instruction* load,
                            const PointerAccess& access) {
        if (access.root == matrix &&
            access.indices[0] == indexK &&
            access.indices[1] == indexJ) {
            if (matrixLoad) return false;
            matrixLoad = load;
            return true;
        }
        if (access.indices[0] == indexI &&
            access.indices[1] == indexK) {
            if (coefficientLoad) return false;
            coefficientLoad = load;
            coefficientAccess = access;
            return true;
        }
        return false;
    };
    if (!classifyLoad(firstLoad, firstAccess) ||
        !classifyLoad(secondLoad, secondAccess) ||
        !matrixLoad || !coefficientLoad) {
        return false;
    }
    auto* coefficient =
        dynamic_cast<IR::GlobalVariable*>(coefficientAccess.root);
    if (!coefficient || coefficient == matrix) return false;

    CanonicalCountedLoop loopI;
    CanonicalCountedLoop loopJ;
    if (!collectCanonicalLoop(
            function, indexI, product->getParent(), loopI) ||
        !collectCanonicalLoop(
            function, indexJ, product->getParent(), loopJ) ||
        loopI.bound != loopK.bound || loopJ.bound != loopK.bound ||
        !isNestedInside(loopK, loopJ) ||
        !isNestedInside(loopJ, loopI) ||
        loopK.body.count(store->getParent()) ||
        !loopJ.body.count(store->getParent())) {
        return false;
    }

    unsigned loadCount = 0;
    unsigned storeCount = 0;
    for (auto* block : loopI.body) {
        for (const auto& instruction : block->getInstructions()) {
            if (instruction->getOpcode() == Opc::CALL) return false;
            if (instruction->getOpcode() == Opc::LOAD) {
                ++loadCount;
                if (instruction.get() != matrixLoad &&
                    instruction.get() != coefficientLoad) {
                    return false;
                }
            }
            if (instruction->getOpcode() == Opc::STORE) {
                ++storeCount;
                if (instruction.get() != store) return false;
            }
        }
    }
    if (loadCount != 2 || storeCount != 1) return false;

    IR::Type* matrixOuterType = nullptr;
    IR::Type* coefficientOuterType = nullptr;
    auto* rowType = getRowType(matrix, matrixOuterType);
    auto* coefficientRow = getRowType(
        coefficient, coefficientOuterType);
    if (!rowType || coefficientRow != rowType ||
        coefficientOuterType != matrixOuterType) {
        return false;
    }

    IR::BasicBlock* preheader = nullptr;
    if (!findUniquePreheader(function, loopI, preheader)) return false;
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
    plan.coefficient = coefficient;
    plan.matrix = matrix;
    plan.rowType = rowType;
    plan.outerMatrixType = matrixOuterType;
    plan.size = loopI.bound;
    return true;
}

bool analyzePlan(IR::Module* module, MatrixBlockingPlan& plan) {
    std::vector<MatrixBlockingPlan> matches;
    for (const auto& function : module->getFunctions()) {
        if (function->isExternal()) continue;
        for (const auto& block : function->getBlocks()) {
            for (const auto& instruction : block->getInstructions()) {
                if (instruction->getOpcode() != Opc::STORE) continue;
                MatrixBlockingPlan candidate;
                if (matchReductionStore(
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

IR::Function* createBlockedKernel(
    IR::Module* module, IR::ArrayType* rowType) {
    auto* i32 = IR::IntegerType::I32;
    auto* rowPointer = IR::PointerType::get(rowType);
    auto* functionType = IR::FunctionType::get(
        IR::VoidType::get(), {i32, rowPointer, rowPointer});
    auto* function = module->createFunction(
        functionType, "__opt_inplace_matrix_block4", false);
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

    auto* coefficientStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1), {indexI, zero},
        "block.coefficient.start");
    iBody->pushBack(coefficientStart);
    iBody->pushBack(IR::Instruction::createBr(fullHeader));

    auto* fullJNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.j.full.next", nullptr, blockWidth);
    auto* fullJ = makePhi(
        i32, "block.j.full", zero, iBody,
        fullJNext, fullStore);
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

    auto* inputStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {zero, fullJ},
        "block.input.start");
    auto* outputStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {indexI, fullJ},
        "block.output.start");
    fullPreheader->pushBack(inputStart);
    fullPreheader->pushBack(outputStart);
    fullPreheader->pushBack(IR::Instruction::createBr(kHeader));

    auto* kNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.k.next", nullptr, one);
    auto* coefficientNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "block.coefficient.next");
    auto* inputNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {rowElements}, "block.input.next");
    auto* indexK = makePhi(
        i32, "block.k", zero, fullPreheader, kNext, kBody);
    auto* coefficientPointer = makePhi(
        coefficientStart->getType(), "block.coefficient.ptr",
        coefficientStart, fullPreheader, coefficientNext, kBody);
    auto* inputPointer = makePhi(
        inputStart->getType(), "block.input.ptr",
        inputStart, fullPreheader, inputNext, kBody);
    kNext->setOperand(0, indexK);
    coefficientNext->setOperand(0, coefficientPointer);
    inputNext->setOperand(0, inputPointer);

    std::vector<IR::Instruction*> accumulators;
    for (unsigned lane = 0; lane < kBlockWidth; ++lane) {
        auto* accumulator = makePhi(
            i32, "block.acc." + std::to_string(lane),
            zero, fullPreheader, nullptr, kBody);
        accumulators.push_back(accumulator);
    }
    kHeader->pushBack(indexK);
    kHeader->pushBack(coefficientPointer);
    kHeader->pushBack(inputPointer);
    for (auto* accumulator : accumulators) {
        kHeader->pushBack(accumulator);
    }
    auto* kCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexK, function->getArg(0), "slt");
    kHeader->pushBack(kCompare);
    kHeader->pushBack(IR::Instruction::createCondBr(
        kCompare, kBody, fullStore));

    auto* coefficient = IR::Instruction::createLoad(
        i32, coefficientPointer, "block.coefficient");
    kBody->pushBack(coefficient);
    for (unsigned lane = 0; lane < kBlockWidth; ++lane) {
        IR::Value* address = inputPointer;
        if (lane != 0) {
            auto* laneAddress = IR::Instruction::createGetElementPtr(
                i32, inputPointer,
                {IR::ConstantInt::get(i32, lane)},
                "block.input.addr." + std::to_string(lane));
            kBody->pushBack(laneAddress);
            address = laneAddress;
        }
        auto* input = IR::Instruction::createLoad(
            i32, address, "block.input." + std::to_string(lane));
        auto* product = IR::Instruction::createBinOp(
            Opc::MUL, i32, "block.product." + std::to_string(lane),
            coefficient, input);
        auto* updated = IR::Instruction::createBinOp(
            Opc::ADD, i32, "block.acc.next." + std::to_string(lane),
            accumulators[lane], product);
        kBody->pushBack(input);
        kBody->pushBack(product);
        kBody->pushBack(updated);
        accumulators[lane]->setOperand(2, updated);
    }
    kBody->pushBack(coefficientNext);
    kBody->pushBack(inputNext);
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
    auto* tailJCompare = IR::Instruction::createCmp(
        Opc::ICMP, tailJ, function->getArg(0), "slt");
    tailHeader->pushBack(tailJCompare);
    tailHeader->pushBack(IR::Instruction::createCondBr(
        tailJCompare, tailPreheader, iLatch));

    auto* tailInputStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {zero, tailJ},
        "block.tail.input.start");
    auto* tailOutput = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {indexI, tailJ},
        "block.tail.output");
    tailPreheader->pushBack(tailInputStart);
    tailPreheader->pushBack(tailOutput);
    tailPreheader->pushBack(IR::Instruction::createBr(tailKHeader));

    auto* tailKNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.tail.k.next", nullptr, one);
    auto* tailCoefficientNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "block.tail.coefficient.next");
    auto* tailInputNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {rowElements}, "block.tail.input.next");
    auto* tailK = makePhi(
        i32, "block.tail.k", zero, tailPreheader,
        tailKNext, tailKBody);
    auto* tailCoefficientPointer = makePhi(
        coefficientStart->getType(), "block.tail.coefficient.ptr",
        coefficientStart, tailPreheader,
        tailCoefficientNext, tailKBody);
    auto* tailInputPointer = makePhi(
        tailInputStart->getType(), "block.tail.input.ptr",
        tailInputStart, tailPreheader,
        tailInputNext, tailKBody);
    auto* tailAccumulator = makePhi(
        i32, "block.tail.acc", zero, tailPreheader,
        nullptr, tailKBody);
    tailKNext->setOperand(0, tailK);
    tailCoefficientNext->setOperand(0, tailCoefficientPointer);
    tailInputNext->setOperand(0, tailInputPointer);
    tailKHeader->pushBack(tailK);
    tailKHeader->pushBack(tailCoefficientPointer);
    tailKHeader->pushBack(tailInputPointer);
    tailKHeader->pushBack(tailAccumulator);
    auto* tailKCompare = IR::Instruction::createCmp(
        Opc::ICMP, tailK, function->getArg(0), "slt");
    tailKHeader->pushBack(tailKCompare);
    tailKHeader->pushBack(IR::Instruction::createCondBr(
        tailKCompare, tailKBody, tailStore));

    auto* tailCoefficient = IR::Instruction::createLoad(
        i32, tailCoefficientPointer, "block.tail.coefficient");
    auto* tailInput = IR::Instruction::createLoad(
        i32, tailInputPointer, "block.tail.input");
    auto* tailProduct = IR::Instruction::createBinOp(
        Opc::MUL, i32, "block.tail.product",
        tailCoefficient, tailInput);
    auto* tailUpdated = IR::Instruction::createBinOp(
        Opc::ADD, i32, "block.tail.acc.next",
        tailAccumulator, tailProduct);
    tailAccumulator->setOperand(2, tailUpdated);
    tailKBody->pushBack(tailCoefficient);
    tailKBody->pushBack(tailInput);
    tailKBody->pushBack(tailProduct);
    tailKBody->pushBack(tailUpdated);
    tailKBody->pushBack(tailCoefficientNext);
    tailKBody->pushBack(tailInputNext);
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

IR::Function* createRowPrivateKernel(
    IR::Module* module, IR::ArrayType* rowType) {
    // Privatize one output row before interchanging j and k.  The source
    // matrix is not modified until every value in row i has been reduced, so
    // rows k >= i retain their original values and rows k < i retain their
    // already-updated values exactly as in the matched in-place loop nest.
    auto* i32 = IR::IntegerType::I32;
    auto* rowPointer = IR::PointerType::get(rowType);
    auto* elementPointer = IR::PointerType::get(i32);
    auto* functionType = IR::FunctionType::get(
        IR::VoidType::get(),
        {i32, rowPointer, rowPointer, elementPointer});
    auto* function = module->createFunction(
        functionType, "__opt_inplace_matrix_row_private", false);
    function->setPreferExpandedLeafRegisters();

    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);

    auto* entry = function->createBlock("rowprivate.entry");
    auto* iHeader = function->createBlock("rowprivate.i.header");
    auto* iBody = function->createBlock("rowprivate.i.body");
    auto* clearHeader = function->createBlock("rowprivate.clear.header");
    auto* clearBody = function->createBlock("rowprivate.clear.body");
    auto* kHeader = function->createBlock("rowprivate.k.header");
    auto* kBody = function->createBlock("rowprivate.k.body");
    auto* jFullHeader = function->createBlock("rowprivate.j.full.header");
    auto* jFullBody = function->createBlock("rowprivate.j.full.body");
    auto* jTailHeader = function->createBlock("rowprivate.j.tail.header");
    auto* jTailBody = function->createBlock("rowprivate.j.tail.body");
    auto* kLatch = function->createBlock("rowprivate.k.latch");
    auto* copyHeader = function->createBlock("rowprivate.copy.header");
    auto* copyBody = function->createBlock("rowprivate.copy.body");
    auto* iLatch = function->createBlock("rowprivate.i.latch");
    auto* exit = function->createBlock("rowprivate.exit");

    auto* iNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.i.next", nullptr, one);
    auto* indexI = makePhi(
        i32, "rowprivate.i", zero, entry, iNext, iLatch);
    iNext->setOperand(0, indexI);
    entry->pushBack(IR::Instruction::createBr(iHeader));
    iHeader->pushBack(indexI);
    auto* iCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexI, function->getArg(0), "slt");
    iHeader->pushBack(iCompare);
    iHeader->pushBack(IR::Instruction::createCondBr(
        iCompare, iBody, exit));

    auto* coefficientStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1), {indexI, zero},
        "rowprivate.coefficient.start");
    auto* outputStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {indexI, zero},
        "rowprivate.output.start");
    iBody->pushBack(coefficientStart);
    iBody->pushBack(outputStart);
    iBody->pushBack(IR::Instruction::createBr(clearHeader));

    auto* clearNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.clear.next", nullptr, one);
    auto* clearPointerNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "rowprivate.clear.pointer.next");
    auto* clearIndex = makePhi(
        i32, "rowprivate.clear.index", zero, iBody,
        clearNext, clearBody);
    auto* clearPointer = makePhi(
        elementPointer, "rowprivate.clear.pointer", function->getArg(3),
        iBody, clearPointerNext, clearBody);
    clearNext->setOperand(0, clearIndex);
    clearPointerNext->setOperand(0, clearPointer);
    clearHeader->pushBack(clearIndex);
    clearHeader->pushBack(clearPointer);
    auto* clearCompare = IR::Instruction::createCmp(
        Opc::ICMP, clearIndex, function->getArg(0), "slt");
    clearHeader->pushBack(clearCompare);
    clearHeader->pushBack(IR::Instruction::createCondBr(
        clearCompare, clearBody, kHeader));
    clearBody->pushBack(IR::Instruction::createStore(zero, clearPointer));
    clearBody->pushBack(clearPointerNext);
    clearBody->pushBack(clearNext);
    clearBody->pushBack(IR::Instruction::createBr(clearHeader));

    auto* kNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.k.next", nullptr, one);
    auto* coefficientPointerNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "rowprivate.coefficient.next");
    auto* indexK = makePhi(
        i32, "rowprivate.k", zero, clearHeader, kNext, kLatch);
    auto* coefficientPointer = makePhi(
        coefficientStart->getType(), "rowprivate.coefficient.pointer",
        coefficientStart, clearHeader, coefficientPointerNext, kLatch);
    kNext->setOperand(0, indexK);
    coefficientPointerNext->setOperand(0, coefficientPointer);
    kHeader->pushBack(indexK);
    kHeader->pushBack(coefficientPointer);
    auto* kCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexK, function->getArg(0), "slt");
    kHeader->pushBack(kCompare);
    kHeader->pushBack(IR::Instruction::createCondBr(
        kCompare, kBody, copyHeader));

    auto* inputStart = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(2), {indexK, zero},
        "rowprivate.input.start");
    auto* coefficient = IR::Instruction::createLoad(
        i32, coefficientPointer, "rowprivate.coefficient");
    kBody->pushBack(inputStart);
    kBody->pushBack(coefficient);
    kBody->pushBack(IR::Instruction::createBr(jFullHeader));

    auto* four = IR::ConstantInt::get(i32, 4);
    auto* fullIndexNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.j.full.next", nullptr, four);
    auto* fullInputNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {four}, "rowprivate.input.full.next");
    auto* fullScratchNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {four}, "rowprivate.scratch.full.next");
    auto* fullIndex = makePhi(
        i32, "rowprivate.j.full", zero, kBody,
        fullIndexNext, jFullBody);
    auto* fullInput = makePhi(
        inputStart->getType(), "rowprivate.input.full", inputStart,
        kBody, fullInputNext, jFullBody);
    auto* fullScratch = makePhi(
        elementPointer, "rowprivate.scratch.full", function->getArg(3),
        kBody, fullScratchNext, jFullBody);
    fullIndexNext->setOperand(0, fullIndex);
    fullInputNext->setOperand(0, fullInput);
    fullScratchNext->setOperand(0, fullScratch);
    jFullHeader->pushBack(fullIndex);
    jFullHeader->pushBack(fullInput);
    jFullHeader->pushBack(fullScratch);
    auto* fullEnd = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.j.full.end", fullIndex, four);
    auto* hasFullBlock = IR::Instruction::createCmp(
        Opc::ICMP, fullEnd, function->getArg(0), "sle");
    jFullHeader->pushBack(fullEnd);
    jFullHeader->pushBack(hasFullBlock);
    jFullHeader->pushBack(IR::Instruction::createCondBr(
        hasFullBlock, jFullBody, jTailHeader));

    for (unsigned lane = 0; lane < 4; ++lane) {
        IR::Value* inputAddress = fullInput;
        IR::Value* scratchAddress = fullScratch;
        if (lane != 0) {
            auto* offset = IR::ConstantInt::get(i32, lane);
            auto* laneInput = IR::Instruction::createGetElementPtr(
                i32, fullInput, {offset},
                "rowprivate.input.lane." + std::to_string(lane));
            auto* laneScratch = IR::Instruction::createGetElementPtr(
                i32, fullScratch, {offset},
                "rowprivate.scratch.lane." + std::to_string(lane));
            jFullBody->pushBack(laneInput);
            jFullBody->pushBack(laneScratch);
            inputAddress = laneInput;
            scratchAddress = laneScratch;
        }
        auto* input = IR::Instruction::createLoad(
            i32, inputAddress,
            "rowprivate.input." + std::to_string(lane));
        auto* accumulated = IR::Instruction::createLoad(
            i32, scratchAddress,
            "rowprivate.accumulated." + std::to_string(lane));
        auto* product = IR::Instruction::createBinOp(
            Opc::MUL, i32,
            "rowprivate.product." + std::to_string(lane),
            coefficient, input);
        auto* updated = IR::Instruction::createBinOp(
            Opc::ADD, i32,
            "rowprivate.updated." + std::to_string(lane),
            accumulated, product);
        jFullBody->pushBack(input);
        jFullBody->pushBack(accumulated);
        jFullBody->pushBack(product);
        jFullBody->pushBack(updated);
        jFullBody->pushBack(IR::Instruction::createStore(
            updated, scratchAddress));
    }
    jFullBody->pushBack(fullInputNext);
    jFullBody->pushBack(fullScratchNext);
    jFullBody->pushBack(fullIndexNext);
    jFullBody->pushBack(IR::Instruction::createBr(jFullHeader));

    auto* tailIndexNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.j.tail.next", nullptr, one);
    auto* tailInputNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "rowprivate.input.tail.next");
    auto* tailScratchNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "rowprivate.scratch.tail.next");
    auto* tailIndex = makePhi(
        i32, "rowprivate.j.tail", fullIndex, jFullHeader,
        tailIndexNext, jTailBody);
    auto* tailInput = makePhi(
        fullInput->getType(), "rowprivate.input.tail", fullInput,
        jFullHeader, tailInputNext, jTailBody);
    auto* tailScratch = makePhi(
        elementPointer, "rowprivate.scratch.tail", fullScratch,
        jFullHeader, tailScratchNext, jTailBody);
    tailIndexNext->setOperand(0, tailIndex);
    tailInputNext->setOperand(0, tailInput);
    tailScratchNext->setOperand(0, tailScratch);
    jTailHeader->pushBack(tailIndex);
    jTailHeader->pushBack(tailInput);
    jTailHeader->pushBack(tailScratch);
    auto* hasTail = IR::Instruction::createCmp(
        Opc::ICMP, tailIndex, function->getArg(0), "slt");
    jTailHeader->pushBack(hasTail);
    jTailHeader->pushBack(IR::Instruction::createCondBr(
        hasTail, jTailBody, kLatch));

    auto* tailInputValue = IR::Instruction::createLoad(
        i32, tailInput, "rowprivate.input.tail.value");
    auto* tailAccumulated = IR::Instruction::createLoad(
        i32, tailScratch, "rowprivate.accumulated.tail");
    auto* tailProduct = IR::Instruction::createBinOp(
        Opc::MUL, i32, "rowprivate.product.tail",
        coefficient, tailInputValue);
    auto* tailUpdated = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.updated.tail",
        tailAccumulated, tailProduct);
    jTailBody->pushBack(tailInputValue);
    jTailBody->pushBack(tailAccumulated);
    jTailBody->pushBack(tailProduct);
    jTailBody->pushBack(tailUpdated);
    jTailBody->pushBack(IR::Instruction::createStore(
        tailUpdated, tailScratch));
    jTailBody->pushBack(tailInputNext);
    jTailBody->pushBack(tailScratchNext);
    jTailBody->pushBack(tailIndexNext);
    jTailBody->pushBack(IR::Instruction::createBr(jTailHeader));

    kLatch->pushBack(coefficientPointerNext);
    kLatch->pushBack(kNext);
    kLatch->pushBack(IR::Instruction::createBr(kHeader));

    auto* copyNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rowprivate.copy.next", nullptr, one);
    auto* copySourceNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "rowprivate.copy.source.next");
    auto* copyDestinationNext = IR::Instruction::createGetElementPtr(
        i32, nullptr, {one}, "rowprivate.copy.destination.next");
    auto* copyIndex = makePhi(
        i32, "rowprivate.copy.index", zero, kHeader,
        copyNext, copyBody);
    auto* copySource = makePhi(
        elementPointer, "rowprivate.copy.source", function->getArg(3),
        kHeader, copySourceNext, copyBody);
    auto* copyDestination = makePhi(
        outputStart->getType(), "rowprivate.copy.destination", outputStart,
        kHeader, copyDestinationNext, copyBody);
    copyNext->setOperand(0, copyIndex);
    copySourceNext->setOperand(0, copySource);
    copyDestinationNext->setOperand(0, copyDestination);
    copyHeader->pushBack(copyIndex);
    copyHeader->pushBack(copySource);
    copyHeader->pushBack(copyDestination);
    auto* copyCompare = IR::Instruction::createCmp(
        Opc::ICMP, copyIndex, function->getArg(0), "slt");
    copyHeader->pushBack(copyCompare);
    copyHeader->pushBack(IR::Instruction::createCondBr(
        copyCompare, copyBody, iLatch));

    auto* copyValue = IR::Instruction::createLoad(
        i32, copySource, "rowprivate.copy.value");
    copyBody->pushBack(copyValue);
    copyBody->pushBack(IR::Instruction::createStore(
        copyValue, copyDestination));
    copyBody->pushBack(copySourceNext);
    copyBody->pushBack(copyDestinationNext);
    copyBody->pushBack(copyNext);
    copyBody->pushBack(IR::Instruction::createBr(copyHeader));

    iLatch->pushBack(iNext);
    iLatch->pushBack(IR::Instruction::createBr(iHeader));
    exit->pushBack(IR::Instruction::createRet(nullptr));
    return function;
}

bool applyPlan(IR::Module* module, const MatrixBlockingPlan& plan) {
    auto* kernel = createRowPrivateKernel(module, plan.rowType);
    auto* zero = IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto* scratch = module->createGlobalVariable(
        IR::PointerType::get(plan.rowType),
        "__opt_inplace_matrix_row_scratch", false);
    auto* coefficientBase = IR::Instruction::createGetElementPtr(
        plan.outerMatrixType, plan.coefficient, {zero, zero},
        "block.coefficient.base");
    auto* matrixBase = IR::Instruction::createGetElementPtr(
        plan.outerMatrixType, plan.matrix, {zero, zero},
        "block.matrix.base");
    auto* scratchBase = IR::Instruction::createGetElementPtr(
        plan.rowType, scratch, {zero, zero},
        "rowprivate.scratch.base");
    auto* call = IR::Instruction::createCall(
        kernel->getFunctionType(), kernel,
        {plan.size, coefficientBase, matrixBase, scratchBase}, "");

    auto* terminator = plan.preheader->getTerminator();
    if (!terminator || terminator->getOpcode() != Opc::BR) return false;
    for (auto iterator = plan.preheader->begin();
         iterator != plan.preheader->end(); ++iterator) {
        if (iterator->get() != terminator) continue;
        iterator = plan.preheader->insert(iterator, coefficientBase);
        ++iterator;
        iterator = plan.preheader->insert(iterator, matrixBase);
        ++iterator;
        iterator = plan.preheader->insert(iterator, scratchBase);
        ++iterator;
        plan.preheader->insert(iterator, call);
        terminator->setOperand(0, plan.exit);
        return true;
    }
    return false;
}

} // namespace

bool inplaceMatrixBlocking(IR::Module* module) {
    MatrixBlockingPlan plan;
    if (!analyzePlan(module, plan)) return false;
    return applyPlan(module, plan);
}

} // namespace Opt
