#include <iostream>
#include <cassert>
#include "ir/IR.h"
#include "ir/IRBuilder.h"

using namespace IR;

static int passed = 0;
static int failed = 0;

#define TEST(name) \
    do { std::cout << "  TEST: " << name << " ... "; } while (0)
#define PASS() \
    do { std::cout << "PASSED\n"; passed++; } while (0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << msg << "\n"; failed++; } while (0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); return; } } while (0)

// ================================================================
// 测试 1: Type 系统 —— 全局唯一性
// ================================================================
void test_type_system() {
    TEST("IntegerType 唯一性");
    IntegerType* i32a = IntegerType::get(32);
    IntegerType* i32b = IntegerType::get(32);
    IntegerType* i8  = IntegerType::get(8);
    CHECK(i32a == i32b);
    CHECK(i32a != i8);
    CHECK(i32a->getBitWidth() == 32);
    CHECK(i8->getBitWidth() == 8);
    PASS();

    TEST("IntegerType 静态指针");
    CHECK(IntegerType::I32 == IntegerType::get(32));
    CHECK(IntegerType::I1  == IntegerType::get(1));
    CHECK(IntegerType::I8  == IntegerType::get(8));
    PASS();

    TEST("VoidType 唯一性");
    CHECK(VoidType::get() == VoidType::get());
    CHECK(VoidType::get()->isVoid());
    PASS();

    TEST("LabelType 唯一性");
    CHECK(LabelType::get() == LabelType::get());
    PASS();

    TEST("FloatType 唯一性");
    CHECK(FloatType::get() == FloatType::get());
    PASS();

    TEST("PointerType 缓存");
    PointerType* p1 = PointerType::get(IntegerType::I32);
    PointerType* p2 = PointerType::get(IntegerType::I32);
    PointerType* p3 = PointerType::get(IntegerType::I8);
    CHECK(p1 == p2);
    CHECK(p1 != p3);
    CHECK(p1->getPointeeType() == IntegerType::I32);
    PASS();

    TEST("ArrayType 缓存");
    ArrayType* a1 = ArrayType::get(IntegerType::I32, 10);
    ArrayType* a2 = ArrayType::get(IntegerType::I32, 10);
    ArrayType* a3 = ArrayType::get(IntegerType::I32, 5);
    CHECK(a1 == a2);
    CHECK(a1 != a3);
    CHECK(a1->getNumElements() == 10);
    PASS();

    TEST("FunctionType 缓存");
    std::vector<Type*> params = {IntegerType::I32, IntegerType::I32};
    FunctionType* ft1 = FunctionType::get(IntegerType::I32, params);
    FunctionType* ft2 = FunctionType::get(IntegerType::I32, params);
    std::vector<Type*> params2 = {IntegerType::I32};
    FunctionType* ft3 = FunctionType::get(IntegerType::I32, params2);
    CHECK(ft1 == ft2);
    CHECK(ft1 != ft3);
    CHECK(ft1->getReturnType() == IntegerType::I32);
    CHECK(ft1->getParamTypes().size() == 2);
    PASS();

    TEST("Type toString");
    CHECK(IntegerType::I32->toString() == "i32");
    CHECK(VoidType::get()->toString() == "void");
    CHECK(PointerType::get(IntegerType::I32)->toString() == "i32*");
    CHECK(ArrayType::get(IntegerType::I8, 4)->toString() == "[4 x i8]");
    PASS();
}

// ================================================================
// 测试 2: Value / User / Def-Use 链
// ================================================================
void test_def_use_chain() {
    TEST("Value addUse / removeUse");
    auto* ty = IntegerType::I32;
    auto* v1 = VReg::create(ty, "v1");
    CHECK(v1->getNumUses() == 0);

    // 构造一个 Instruction (User) 并使用 v1
    auto* add = Instruction::createBinOp(Instruction::Opcode::ADD, ty, "add", v1, v1);
    CHECK(v1->getNumUses() == 2);

    // dropAllUses
    add->dropAllUses();
    CHECK(v1->getNumUses() == 0);
    delete v1;
    delete add;
    PASS();

    TEST("User addOperand / setOperand");
    auto* v2 = VReg::create(ty, "v2");
    auto* v3 = VReg::create(ty, "v3");
    auto* sub = Instruction::createBinOp(Instruction::Opcode::SUB, ty, "sub", v2, v3);
    CHECK(sub->getNumOperands() == 2);
    CHECK(sub->getOperand(0) == v2);
    CHECK(sub->getOperand(1) == v3);
    CHECK(v2->getNumUses() == 1);
    CHECK(v3->getNumUses() == 1);

    sub->setOperand(0, v3);
    CHECK(sub->getOperand(0) == v3);
    CHECK(v2->getNumUses() == 0);
    CHECK(v3->getNumUses() == 2);
    delete v2;
    delete v3;
    delete sub;
    PASS();
}

// ================================================================
// 测试 3: Constant
// ================================================================
void test_constants() {
    TEST("ConstantInt 缓存");
    ConstantInt* c1 = ConstantInt::get(IntegerType::I32, 42);
    ConstantInt* c2 = ConstantInt::get(IntegerType::I32, 42);
    ConstantInt* c3 = ConstantInt::get(IntegerType::I32, 0);
    CHECK(c1 == c2);
    CHECK(c1 != c3);
    CHECK(c1->getValue() == 42);
    PASS();
}

// ================================================================
// 测试 4: Instruction 创建
// ================================================================
void test_instructions() {
    TEST("createRet");
    auto* c42 = ConstantInt::get(IntegerType::I32, 42);
    auto* ret = Instruction::createRet(c42);
    CHECK(ret->getOpcode() == Instruction::Opcode::RET);
    CHECK(ret->getNumOperands() == 1);
    CHECK(ret->getOperand(0) == c42);
    delete ret;
    PASS();

    TEST("createRetVoid");
    auto* retV = Instruction::createRet(nullptr);
    CHECK(retV->getOpcode() == Instruction::Opcode::RET);
    CHECK(retV->getNumOperands() == 0);
    delete retV;
    PASS();

    TEST("createBinOp");
    auto* c1 = ConstantInt::get(IntegerType::I32, 1);
    auto* c2 = ConstantInt::get(IntegerType::I32, 2);
    auto* add = Instruction::createBinOp(
        Instruction::Opcode::ADD, IntegerType::I32, "add", c1, c2);
    CHECK(add->getOpcode() == Instruction::Opcode::ADD);
    CHECK(add->getNumOperands() == 2);
    CHECK(add->getType() == IntegerType::I32);
    delete add;
    PASS();
}

// ================================================================
// 测试 5: BasicBlock / Function / Module
// ================================================================
void test_module() {
    TEST("Module → Function → BasicBlock → Return");
    Module mod;

    Function* mainFunc = mod.createFunction(
        FunctionType::get(IntegerType::I32, {}),
        "main"
    );
    CHECK(mainFunc->getName() == "main");
    CHECK(mainFunc->getEntryBlock() == nullptr);

    BasicBlock* entry = mainFunc->createBlock("entry");
    CHECK(mainFunc->getEntryBlock() == entry);
    CHECK(entry->getParent() == mainFunc);

    auto* c0 = ConstantInt::get(IntegerType::I32, 0);
    Instruction* ret = Instruction::createRet(c0);
    entry->pushBack(ret);
    CHECK(entry->getTerminator() == ret);

    std::string dumped = mod.dump();
    CHECK(dumped.find("define i32 @main()") != std::string::npos);
    CHECK(dumped.find("ret i32 0") != std::string::npos);
    PASS();
}

// ================================================================
// 测试 6: IRBuilder → 端到端 simple main
// ================================================================
void test_builder_simple_main() {
    TEST("IRBuilder::buildSimpleMain(0)");
    IRBuilder builder;
    auto mod = builder.buildSimpleMain(0);
    std::string ir = mod->dump();

    std::cout << "\n-------- Generated IR --------\n";
    std::cout << ir;
    std::cout << "-------------------------------\n";

    CHECK(ir.find("define i32 @main()") != std::string::npos);
    CHECK(ir.find("entry:") != std::string::npos);
    CHECK(ir.find("ret i32 0") != std::string::npos);
    PASS();

    TEST("IRBuilder::buildSimpleMain(42)");
    IRBuilder builder2;
    auto mod2 = builder2.buildSimpleMain(42);
    std::string ir2 = mod2->dump();
    CHECK(ir2.find("ret i32 42") != std::string::npos);
    PASS();
}

// ================================================================
// main
// ================================================================
int main() {
    std::cout << "=== IR 框架单元测试 ===\n\n";

    std::cout << "[Type System]\n";
    test_type_system();

    std::cout << "\n[Def-Use Chain]\n";
    test_def_use_chain();

    std::cout << "\n[Constants]\n";
    test_constants();

    std::cout << "\n[Instructions]\n";
    test_instructions();

    std::cout << "\n[Module / Function / BasicBlock]\n";
    test_module();

    std::cout << "\n[IRBuilder - End to End]\n";
    test_builder_simple_main();

    std::cout << "\n=== 结果: " << passed << " passed, "
              << failed << " failed ===\n";

    return failed > 0 ? 1 : 0;
}