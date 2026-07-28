#include "opt/MatrixReductionPlan.h"

#include <string>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

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

IR::Function* createAffineRowSummaryFunction(
    IR::Module* module, const AffineKernelSummary& summary) {
    auto* function = module->createFunction(
        summary.sourceFunction->getFunctionType(),
        "__opt_affine_row_summary", false);
    auto* i32 = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* indexStart = IR::ConstantInt::get(
        i32, summary.indexStart);

    auto* entry = function->createBlock("entry");
    auto* iHeader = function->createBlock("summary.i.cond");
    auto* iBody = function->createBlock("summary.i.body");
    auto* kHeader = function->createBlock("summary.k.cond");
    auto* kBody = function->createBlock("summary.k.body");
    auto* iLatch = function->createBlock("summary.i.latch");
    auto* exit = function->createBlock("summary.exit");

    auto* iNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "summary.i.next", nullptr, one);
    auto* kNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "summary.k.next", nullptr, one);
    auto* indexI = makePhi(
        i32, "summary.i", indexStart, entry, iNext, iLatch);
    auto* indexK = makePhi(
        i32, "summary.k", indexStart, iBody, kNext, kBody);
    auto* accumulation =
        IR::Instruction::createPhi(i32, "summary.acc", 4);
    accumulation->addOperand(zero);
    accumulation->addOperand(iBody);
    accumulation->addOperand(nullptr);
    accumulation->addOperand(kBody);
    iNext->setOperand(0, indexI);
    kNext->setOperand(0, indexK);

    entry->pushBack(IR::Instruction::createBr(iHeader));
    iHeader->pushBack(indexI);
    auto* iCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexI, function->getArg(0), "slt");
    iHeader->pushBack(iCompare);
    iHeader->pushBack(
        IR::Instruction::createCondBr(iCompare, iBody, exit));

    auto* rowA = IR::Instruction::createGetElementPtr(
        summary.rowType, function->getArg(1),
        {indexI}, "summary.A.row");
    auto* outputRow = IR::Instruction::createGetElementPtr(
        summary.rowType, function->getArg(3),
        {indexI}, "summary.C.row");
    iBody->pushBack(rowA);
    iBody->pushBack(outputRow);
    iBody->pushBack(IR::Instruction::createBr(kHeader));

    kHeader->pushBack(indexK);
    kHeader->pushBack(accumulation);
    auto* kCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexK, function->getArg(0), "slt");
    kHeader->pushBack(kCompare);
    kHeader->pushBack(
        IR::Instruction::createCondBr(
            kCompare, kBody, iLatch));

    auto* coefficientAddress =
        IR::Instruction::createGetElementPtr(
            i32, rowA, {zero, indexK},
            "summary.A.element");
    auto* coefficient = IR::Instruction::createLoad(
        i32, coefficientAddress, "summary.coefficient");
    auto* inputRow = IR::Instruction::createGetElementPtr(
        summary.rowType, function->getArg(2),
        {indexK}, "summary.B.row");
    auto* inputAddress =
        IR::Instruction::createGetElementPtr(
            i32, inputRow, {zero, indexStart},
            "summary.B.sum.addr");
    auto* inputSum = IR::Instruction::createLoad(
        i32, inputAddress, "summary.B.sum");
    auto* product = IR::Instruction::createBinOp(
        Opc::MUL, i32, "summary.product",
        accumulation, coefficient);
    auto* updated = IR::Instruction::createBinOp(
        Opc::ADD, i32, "summary.updated",
        product, inputSum);
    IR::Instruction* skip = nullptr;
    IR::Instruction* nextAccumulation = updated;
    if (summary.skippedScale) {
        auto* skippedCoefficient = IR::ConstantInt::get(
            i32, summary.skippedScale->getValue());
        skip = IR::Instruction::createCmp(
            Opc::ICMP, coefficient, skippedCoefficient, "eq");
        nextAccumulation = IR::Instruction::createSelect(
            skip, accumulation, updated, "summary.acc.next");
    }
    accumulation->setOperand(2, nextAccumulation);

    kBody->pushBack(coefficientAddress);
    kBody->pushBack(coefficient);
    kBody->pushBack(inputRow);
    kBody->pushBack(inputAddress);
    kBody->pushBack(inputSum);
    kBody->pushBack(product);
    kBody->pushBack(updated);
    if (skip) {
        kBody->pushBack(skip);
        kBody->pushBack(nextAccumulation);
    }
    kBody->pushBack(kNext);
    kBody->pushBack(IR::Instruction::createBr(kHeader));

    auto* outputAddress =
        IR::Instruction::createGetElementPtr(
            i32, outputRow, {zero, indexStart},
            "summary.C.sum.addr");
    iLatch->pushBack(outputAddress);
    iLatch->pushBack(
        IR::Instruction::createStore(
            accumulation, outputAddress));
    iLatch->pushBack(iNext);
    iLatch->pushBack(IR::Instruction::createBr(iHeader));
    exit->pushBack(IR::Instruction::createRet(nullptr));
    return function;
}

IR::Function* createRowSummaryFunction(
    IR::Module* module, IR::ArrayType* rowType,
    int64_t start) {
    auto* i32 = IR::IntegerType::I32;
    auto* rowPointer = IR::PointerType::get(rowType);
    auto* type = IR::FunctionType::get(
        IR::VoidType::get(), {i32, rowPointer});
    auto* function = module->createFunction(
        type, "__opt_contract_row_sum", false);

    auto* zero = IR::ConstantInt::get(i32, 0);
    auto* one = IR::ConstantInt::get(i32, 1);
    auto* indexStart = IR::ConstantInt::get(i32, start);
    auto* entry = function->createBlock("entry");
    auto* iHeader = function->createBlock("rows.i.cond");
    auto* iBody = function->createBlock("rows.i.body");
    auto* jHeader = function->createBlock("rows.j.cond");
    auto* jBody = function->createBlock("rows.j.body");
    auto* iLatch = function->createBlock("rows.i.latch");
    auto* exit = function->createBlock("rows.exit");

    auto* iNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rows.i.next", nullptr, one);
    auto* jNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rows.j.next", nullptr, one);
    auto* sumNext = IR::Instruction::createBinOp(
        Opc::ADD, i32, "rows.sum.next", nullptr, nullptr);
    auto* indexI = makePhi(
        i32, "rows.i", indexStart, entry, iNext, iLatch);
    auto* indexJ = makePhi(
        i32, "rows.j", indexStart, iBody, jNext, jBody);
    auto* sum = makePhi(
        i32, "rows.sum", zero, iBody, sumNext, jBody);
    iNext->setOperand(0, indexI);
    jNext->setOperand(0, indexJ);

    entry->pushBack(IR::Instruction::createBr(iHeader));
    iHeader->pushBack(indexI);
    auto* iCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexI, function->getArg(0), "slt");
    iHeader->pushBack(iCompare);
    iHeader->pushBack(
        IR::Instruction::createCondBr(iCompare, iBody, exit));

    auto* row = IR::Instruction::createGetElementPtr(
        rowType, function->getArg(1),
        {indexI}, "rows.row");
    iBody->pushBack(row);
    iBody->pushBack(IR::Instruction::createBr(jHeader));

    jHeader->pushBack(indexJ);
    jHeader->pushBack(sum);
    auto* jCompare = IR::Instruction::createCmp(
        Opc::ICMP, indexJ, function->getArg(0), "slt");
    jHeader->pushBack(jCompare);
    jHeader->pushBack(
        IR::Instruction::createCondBr(
            jCompare, jBody, iLatch));

    auto* elementAddress =
        IR::Instruction::createGetElementPtr(
            i32, row, {zero, indexJ},
            "rows.element.addr");
    auto* element = IR::Instruction::createLoad(
        i32, elementAddress, "rows.element");
    sumNext->setOperand(0, sum);
    sumNext->setOperand(1, element);
    jBody->pushBack(elementAddress);
    jBody->pushBack(element);
    jBody->pushBack(sumNext);
    jBody->pushBack(jNext);
    jBody->pushBack(IR::Instruction::createBr(jHeader));

    auto* outputAddress =
        IR::Instruction::createGetElementPtr(
            i32, row, {zero, indexStart},
            "rows.output.addr");
    iLatch->pushBack(outputAddress);
    iLatch->pushBack(
        IR::Instruction::createStore(sum, outputAddress));
    iLatch->pushBack(iNext);
    iLatch->pushBack(IR::Instruction::createBr(iHeader));
    exit->pushBack(IR::Instruction::createRet(nullptr));
    return function;
}

} // namespace

bool applyMatrixReductionPlan(
    IR::Module* module, const MatrixReductionPlan& plan) {
    auto* summaryFunction =
        createRowSummaryFunction(
            module, plan.kernel.rowType,
            plan.kernel.indexStart);
    auto* summaryKernel =
        createAffineRowSummaryFunction(module, plan.kernel);
    auto* zero =
        IR::ConstantInt::get(IR::IntegerType::I32, 0);
    auto* matrixType = dynamic_cast<IR::PointerType*>(
        plan.seedMatrix->getType());
    if (!matrixType) return false;
    auto* matrixBase =
        IR::Instruction::createGetElementPtr(
            matrixType->getPointeeType(), plan.seedMatrix,
            {zero, zero}, "contract.B.base");
    auto* summaryCall = IR::Instruction::createCall(
        summaryFunction->getFunctionType(), summaryFunction,
        {plan.size, matrixBase}, "");

    auto* terminator = plan.loopPreheader->getTerminator();
    if (!terminator) return false;
    for (auto iterator = plan.loopPreheader->begin();
         iterator != plan.loopPreheader->end(); ++iterator) {
        if (iterator->get() != terminator) continue;
        iterator =
            plan.loopPreheader->insert(iterator, matrixBase);
        ++iterator;
        plan.loopPreheader->insert(iterator, summaryCall);
        for (auto* call : plan.calls) {
            call->setOperand(0, summaryKernel);
        }
        plan.finalInnerCompare->setOperand(
            plan.finalInnerBoundOperand,
            IR::ConstantInt::get(
                IR::IntegerType::I32,
                plan.kernel.indexStart + 1));
        return true;
    }
    return false;
}

} // namespace Opt
