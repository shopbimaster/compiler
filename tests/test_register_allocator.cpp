#include <iostream>

#include "backend/RegisterAllocator.h"
#include "ir/IR.h"

using namespace IR;

// A loop edge assigns B' = B + 1 and C' = B in parallel. Coalescing B' into
// B's register would overwrite old B before the C' edge copy can read it.
static bool testPhiCoalescingPreservesSiblingIncoming() {
    Module mod;
    auto* func = mod.createFunction(
        FunctionType::get(IntegerType::I32, {}), "phi_parallel_copy");
    auto* entry = func->createBlock("entry");
    auto* header = func->createBlock("loop_header");
    auto* latch = func->createBlock("loop_latch");
    auto* exitBB = func->createBlock("exit");

    entry->pushBack(Instruction::createBr(header));

    auto* bPhi = Instruction::createPhi(IntegerType::I32, "b.phi", 4);
    bPhi->addOperand(ConstantInt::get(IntegerType::I32, 1));
    bPhi->addOperand(entry);
    bPhi->addOperand(nullptr);
    bPhi->addOperand(latch);
    header->pushBack(bPhi);

    auto* cPhi = Instruction::createPhi(IntegerType::I32, "c.phi", 4);
    cPhi->addOperand(ConstantInt::get(IntegerType::I32, 2));
    cPhi->addOperand(entry);
    cPhi->addOperand(bPhi);
    cPhi->addOperand(latch);
    header->pushBack(cPhi);

    header->pushBack(Instruction::createCondBr(
        ConstantInt::get(IntegerType::I1, 1), latch, exitBB));

    auto* bNext = Instruction::createBinOp(
        Instruction::Opcode::ADD, IntegerType::I32, "b.next",
        bPhi, ConstantInt::get(IntegerType::I32, 1));
    latch->pushBack(bNext);
    latch->pushBack(Instruction::createBr(header));
    bPhi->setOperand(2, bNext);

    exitBB->pushBack(Instruction::createRet(cPhi));

    Backend::RegisterAllocator allocator;
    allocator.allocate(*func);

    return allocator.hasReg(bPhi) && allocator.hasReg(bNext) &&
           allocator.getReg(bPhi) != allocator.getReg(bNext);
}

int main() {
    if (!testPhiCoalescingPreservesSiblingIncoming()) {
        std::cerr << "FAILED: sibling PHI lost its old incoming value\n";
        return 1;
    }
    std::cout << "PASSED: PHI parallel edge-copy coalescing\n";
    return 0;
}
