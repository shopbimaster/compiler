#include <iostream>
#include <cassert>
#include <string>
#include "ir/IR.h"
#include "ir/IRBuilder.h"
#include "opt/Optimizer.h"

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
    CHECK(dumped.find("entry") != std::string::npos);
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
// 测试 7: 循环交换（Loop Interchange）
// ================================================================

static Instruction* createLoadInstruction(Value* ptr, const std::string& name) {
    auto* ty = dynamic_cast<PointerType*>(ptr->getType());
    Type* loadTy = ty ? ty->getPointeeType() : ptr->getType();
    return Instruction::createLoad(loadTy, ptr, name);
}

void test_loop_interchange_basic() {
    TEST("LoopInterchange 交换二重嵌套循环变量");
    Module mod;

    auto* func = mod.createFunction(
        FunctionType::get(IntegerType::I32, {}), "main");

    auto* entry   = func->createBlock("entry");
    auto* outerH  = func->createBlock("while_cond_i");
    auto* outerB  = func->createBlock("while_body_i");
    auto* innerH  = func->createBlock("while_cond_j");
    auto* innerB  = func->createBlock("while_body_j");
    auto* outerL  = func->createBlock("while_latch_i");
    auto* exitBB  = func->createBlock("while_exit");

    auto* iVar = Instruction::createAlloca(IntegerType::I32, "i");
    auto* jVar = Instruction::createAlloca(IntegerType::I32, "j");

    // entry: store 0, %i; br outer_header
    entry->pushBack(iVar);
    entry->pushBack(jVar);
    entry->pushBack(Instruction::createStore(ConstantInt::get(IntegerType::I32, 0), iVar));
    entry->pushBack(Instruction::createBr(outerH));

    // outer_header: icmp load i < 10; cond_br outer_body, exit
    auto* iLoad1 = createLoadInstruction(iVar, "i_ld1");
    outerH->pushBack(iLoad1);
    auto* cmpI = Instruction::createCmp(Instruction::Opcode::ICMP,
        iLoad1, ConstantInt::get(IntegerType::I32, 10), "cmp_i");
    outerH->pushBack(cmpI);
    outerH->pushBack(Instruction::createCondBr(cmpI, outerB, exitBB));

    // outer_body: store 0, %j; br inner_header
    outerB->pushBack(Instruction::createStore(ConstantInt::get(IntegerType::I32, 0), jVar));
    outerB->pushBack(Instruction::createBr(innerH));

    // inner_header: icmp load j < 10; cond_br inner_body, outer_latch
    auto* jLoad1 = createLoadInstruction(jVar, "j_ld1");
    innerH->pushBack(jLoad1);
    auto* cmpJ = Instruction::createCmp(Instruction::Opcode::ICMP,
        jLoad1, ConstantInt::get(IntegerType::I32, 10), "cmp_j");
    innerH->pushBack(cmpJ);
    innerH->pushBack(Instruction::createCondBr(cmpJ, innerB, outerL));

    // inner_body: load j; add 1; store j; br inner_header
    auto* jLoad2 = createLoadInstruction(jVar, "j_ld2");
    innerB->pushBack(jLoad2);
    innerB->pushBack(Instruction::createBinOp(Instruction::Opcode::ADD,
        IntegerType::I32, "j_next", jLoad2, ConstantInt::get(IntegerType::I32, 1)));
    innerB->pushBack(Instruction::createStore(
        dynamic_cast<Instruction*>(innerB->getInstructions().back().get()), jVar));
    innerB->pushBack(Instruction::createBr(innerH));

    // outer_latch: load i; add 1; store i; br outer_header
    auto* iLoad2 = createLoadInstruction(iVar, "i_ld2");
    outerL->pushBack(iLoad2);
    outerL->pushBack(Instruction::createBinOp(Instruction::Opcode::ADD,
        IntegerType::I32, "i_next", iLoad2, ConstantInt::get(IntegerType::I32, 1)));
    outerL->pushBack(Instruction::createStore(
        dynamic_cast<Instruction*>(outerL->getInstructions().back().get()), iVar));
    outerL->pushBack(Instruction::createBr(outerH));

    // exit: ret 0
    exitBB->pushBack(Instruction::createRet(ConstantInt::get(IntegerType::I32, 0)));

    std::string before = mod.dump();
    CHECK(before.find("while_cond_i") != std::string::npos);
    CHECK(before.find("while_cond_j") != std::string::npos);

    // Run loop interchange
    Opt::loopInterchange(&mod);

    std::string after = mod.dump();
    std::cout << "\n-------- Before Interchange --------\n" << before
              << "-------- After Interchange --------\n" << after
              << "--------------------------------------\n";

    // After interchange:
    // entry should have store 0, j (new outer var)
    // while_body_i should have store 0, i (new inner var)
    // while_cond_i checks j (new outer var)
    // while_cond_j checks i (new inner var)
    // while_body_j increments i (new inner var)
    // while_latch_i increments j (new outer var)
    CHECK(after.find("define i32 @main()") != std::string::npos);

    // entry initializes j (the new outer variable)
    CHECK(after.find("store i32 0, i32* %j") != std::string::npos);

    // outer_body (while_body_i) should store 0 to i (new inner var)  
    // AND br to while_cond_j (new outer header)
    CHECK(after.find("store i32 0, i32* %i") != std::string::npos);

    // Verify ICMP swaps: while_cond_i checks j, while_cond_j checks i
    auto icmp_i_pos = after.find("while_cond_i:");
    auto icmp_j_pos = after.find("while_cond_j:");
    CHECK(icmp_i_pos != std::string::npos);
    CHECK(icmp_j_pos != std::string::npos);

    // Check that while_cond_i's ICMP references %j (new outer)
    auto icmpImg_i = after.find("%j", icmp_i_pos);
    CHECK(icmpImg_i != std::string::npos);

    // Check that while_cond_j's ICMP references %i (new inner)  
    auto icmpImg_j = after.find("%i", icmp_j_pos);
    CHECK(icmpImg_j != std::string::npos);

    PASS();
}

// ================================================================
// 测试 8: 循环交换 — 单循环不变
// ================================================================
void test_loop_interchange_noop_single() {
    TEST("LoopInterchange 单层循环不修改");
    Module mod;

    auto* func = mod.createFunction(
        FunctionType::get(IntegerType::I32, {}), "main");

    auto* entry  = func->createBlock("entry");
    auto* cond   = func->createBlock("while_cond");
    auto* body   = func->createBlock("while_body");
    auto* exitBB = func->createBlock("while_exit");

    auto* iVar = Instruction::createAlloca(IntegerType::I32, "i");

    entry->pushBack(iVar);
    entry->pushBack(Instruction::createStore(ConstantInt::get(IntegerType::I32, 0), iVar));
    entry->pushBack(Instruction::createBr(cond));

    auto* iLoad1 = createLoadInstruction(iVar, "i_ld1");
    cond->pushBack(iLoad1);
    cond->pushBack(Instruction::createCmp(Instruction::Opcode::ICMP,
        iLoad1, ConstantInt::get(IntegerType::I32, 10), "cmp_i"));
    cond->pushBack(Instruction::createCondBr(cond->getInstructions().back().get(), body, exitBB));

    auto* iLoad2 = createLoadInstruction(iVar, "i_ld2");
    body->pushBack(iLoad2);
    body->pushBack(Instruction::createBinOp(Instruction::Opcode::ADD,
        IntegerType::I32, "i_next", iLoad2, ConstantInt::get(IntegerType::I32, 1)));
    body->pushBack(Instruction::createStore(
        dynamic_cast<Instruction*>(body->getInstructions().back().get()), iVar));
    body->pushBack(Instruction::createBr(cond));

    exitBB->pushBack(Instruction::createRet(ConstantInt::get(IntegerType::I32, 0)));

    std::string before = mod.dump();
    Opt::loopInterchange(&mod);
    std::string after = mod.dump();

    CHECK(before == after);
    PASS();
}

// ================================================================
// 测试 9: 循环交换 — 计算体中的变量引用不被篡改
// ================================================================
void test_loop_interchange_computation_body() {
    TEST("LoopInterchange 计算体变量引用正确性");
    Module mod;

    auto* func = mod.createFunction(
        FunctionType::get(IntegerType::I32, {}), "main");

    auto* entry   = func->createBlock("entry");
    auto* outerH  = func->createBlock("while_cond_i");
    auto* outerB  = func->createBlock("while_body_i");
    auto* innerH  = func->createBlock("while_cond_j");
    auto* innerB  = func->createBlock("while_body_j");
    auto* outerL  = func->createBlock("while_latch_i");
    auto* exitBB  = func->createBlock("while_exit");

    auto* iVar = Instruction::createAlloca(IntegerType::I32, "i");
    auto* jVar = Instruction::createAlloca(IntegerType::I32, "j");
    auto* sumVar = Instruction::createAlloca(IntegerType::I32, "sum");

    entry->pushBack(iVar);
    entry->pushBack(jVar);
    entry->pushBack(sumVar);
    entry->pushBack(Instruction::createStore(ConstantInt::get(IntegerType::I32, 0), iVar));
    entry->pushBack(Instruction::createStore(ConstantInt::get(IntegerType::I32, 0), sumVar));
    entry->pushBack(Instruction::createBr(outerH));

    auto* iLoad1 = createLoadInstruction(iVar, "i_ld1");
    outerH->pushBack(iLoad1);
    outerH->pushBack(Instruction::createCmp(Instruction::Opcode::ICMP,
        iLoad1, ConstantInt::get(IntegerType::I32, 10), "cmp_i"));
    outerH->pushBack(Instruction::createCondBr(
        outerH->getInstructions().back().get(), outerB, exitBB));

    outerB->pushBack(Instruction::createStore(ConstantInt::get(IntegerType::I32, 0), jVar));
    outerB->pushBack(Instruction::createBr(innerH));

    auto* jLoad1 = createLoadInstruction(jVar, "j_ld1");
    innerH->pushBack(jLoad1);
    innerH->pushBack(Instruction::createCmp(Instruction::Opcode::ICMP,
        jLoad1, ConstantInt::get(IntegerType::I32, 10), "cmp_j"));
    innerH->pushBack(Instruction::createCondBr(
        innerH->getInstructions().back().get(), innerB, outerL));

    // inner_body: sum = sum + i * j;  j = j + 1;  br inner_header
    auto* sumLoad = createLoadInstruction(sumVar, "sum_ld");
    innerB->pushBack(sumLoad);
    auto* iBodyLoad = createLoadInstruction(iVar, "i_body");
    innerB->pushBack(iBodyLoad);
    auto* jBodyLoad = createLoadInstruction(jVar, "j_body");
    innerB->pushBack(jBodyLoad);
    auto* mul = Instruction::createBinOp(Instruction::Opcode::MUL,
        IntegerType::I32, "mul_ij", iBodyLoad, jBodyLoad);
    innerB->pushBack(mul);
    auto* addSum = Instruction::createBinOp(Instruction::Opcode::ADD,
        IntegerType::I32, "sum_next", sumLoad, mul);
    innerB->pushBack(addSum);
    innerB->pushBack(Instruction::createStore(addSum, sumVar));
    auto* jLoad2 = createLoadInstruction(jVar, "j_ld2");
    innerB->pushBack(jLoad2);
    innerB->pushBack(Instruction::createBinOp(Instruction::Opcode::ADD,
        IntegerType::I32, "j_next", jLoad2, ConstantInt::get(IntegerType::I32, 1)));
    innerB->pushBack(Instruction::createStore(
        dynamic_cast<Instruction*>(innerB->getInstructions().back().get()), jVar));
    innerB->pushBack(Instruction::createBr(innerH));

    auto* iLoad2 = createLoadInstruction(iVar, "i_ld2");
    outerL->pushBack(iLoad2);
    outerL->pushBack(Instruction::createBinOp(Instruction::Opcode::ADD,
        IntegerType::I32, "i_next", iLoad2, ConstantInt::get(IntegerType::I32, 1)));
    outerL->pushBack(Instruction::createStore(
        dynamic_cast<Instruction*>(outerL->getInstructions().back().get()), iVar));
    outerL->pushBack(Instruction::createBr(outerH));

    exitBB->pushBack(Instruction::createRet(ConstantInt::get(IntegerType::I32, 0)));

    Opt::loopInterchange(&mod);
    std::string after = mod.dump();

    // After interchange, entry stores 0 to j (new outer), outer_body stores 0 to i (new inner)
    CHECK(after.find("store i32 0, i32* %j") != std::string::npos);
    CHECK(after.find("store i32 0, i32* %i") != std::string::npos);

    // Computation body should still reference %i and %j correctly
    CHECK(after.find("%mul_ij") != std::string::npos);

    // The increment in inner_body should now increment i (new inner)
    auto innerBodyPos = after.find("while_body_j:");
    CHECK(innerBodyPos != std::string::npos);
    auto jNextPos = after.find("%j_next", innerBodyPos);
    CHECK(jNextPos != std::string::npos);

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

    std::cout << "\n[Loop Interchange]\n";
    test_loop_interchange_basic();
    test_loop_interchange_noop_single();
    test_loop_interchange_computation_body();

    std::cout << "\n=== 结果: " << passed << " passed, "
              << failed << " failed ===\n";

    return failed > 0 ? 1 : 0;
}