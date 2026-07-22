#ifdef LOCAL_REGISTER_ALLOCATOR_TEST

#include <iostream>

#include "backend/RegisterAllocator.h"
#include "backend/TargetCodeGen.h"
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

// Stack slots for i32 SSA values are accessed with lw/sw and need only four
// bytes. A long dependency chain keeps register pressure low while still
// exercising the code generator's slot reservation for every virtual value.
// With eight-byte slots this crosses the 12-bit stack-immediate boundary and
// requires a materialized frame adjustment; packed slots stay below it.
static bool testIntegerVirtualSlotsUseFourBytes() {
    Module mod;
    auto* func = mod.createFunction(
        FunctionType::get(IntegerType::I32, {}), "packed_i32_slots");
    auto* entry = func->createBlock("entry");

    Value* value = ConstantInt::get(IntegerType::I32, 0);
    for (int i = 0; i < 260; ++i) {
        auto* next = Instruction::createBinOp(
            Instruction::Opcode::ADD, IntegerType::I32,
            "slot." + std::to_string(i), value,
            ConstantInt::get(IntegerType::I32, i + 1));
        entry->pushBack(next);
        value = next;
    }
    entry->pushBack(Instruction::createRet(value));

    Backend::TargetCodeGen codegen;
    std::string assembly = codegen.generate(mod);
    return assembly.find("sub     sp, sp, t0") == std::string::npos &&
           assembly.find("addi    sp, sp, -") != std::string::npos;
}

int main() {
    if (!testPhiCoalescingPreservesSiblingIncoming()) {
        std::cerr << "FAILED: sibling PHI lost its old incoming value\n";
        return 1;
    }
    std::cout << "PASSED: PHI parallel edge-copy coalescing\n";

    if (!testIntegerVirtualSlotsUseFourBytes()) {
        std::cerr << "FAILED: i32 virtual slots exceeded the compact frame\n";
        return 1;
    }
    std::cout << "PASSED: compact i32 virtual stack slots\n";
    return 0;
}

#endif // LOCAL_REGISTER_ALLOCATOR_TEST
