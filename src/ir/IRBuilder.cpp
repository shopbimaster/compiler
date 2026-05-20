#include "ir/IRBuilder.h"
#include <cassert>

namespace IR {

IRBuilder::IRBuilder()
    : module(nullptr)
    , currentFunc(nullptr)
    , currentBB(nullptr)
    , tempCount(0) {}

std::unique_ptr<Module> IRBuilder::buildModule() {
    module = new Module();
    return std::unique_ptr<Module>(module);
}

// ================================================================
// 测试：构造 int main() { return <returnValue>; }
// ================================================================
std::unique_ptr<Module> IRBuilder::buildSimpleMain(int64_t returnValue) {
    auto mod = buildModule();

    // 创建 main 函数: define i32 @main()
    Function* mainFunc = module->createFunction(
        FunctionType::get(IntegerType::I32, {}),
        "main"
    );

    // 创建 entry 基本块
    BasicBlock* entryBB = mainFunc->createBlock("entry");
    currentFunc = mainFunc;
    currentBB = entryBB;

    // 创建 ret 指令
    Value* constVal = createConstantInt(returnValue, 32);
    Instruction* ret = Instruction::createRet(constVal);
    currentBB->pushBack(ret);

    return mod;
}

// ================================================================
// IR 构建辅助方法
// ================================================================

Function* IRBuilder::createFunction(const std::string& name, Type* retTy,
                                     const std::vector<Type*>& paramTys) {
    auto* ft = FunctionType::get(retTy, paramTys);
    Function* func = module->createFunction(ft, name);
    return func;
}

BasicBlock* IRBuilder::createBlock(const std::string& name) {
    assert(currentFunc && "no current function");
    return currentFunc->createBlock(name);
}

Instruction* IRBuilder::createRet(Value* val) {
    assert(currentBB && "no current block");
    auto* inst = Instruction::createRet(val);
    currentBB->pushBack(inst);
    return inst;
}

Instruction* IRBuilder::createRetVoid() {
    assert(currentBB && "no current block");
    auto* inst = Instruction::createRet(nullptr);
    currentBB->pushBack(inst);
    return inst;
}

Value* IRBuilder::createConstantInt(int64_t value, unsigned bitWidth) {
    return ConstantInt::get(IntegerType::get(bitWidth), value);
}

// ================================================================
// 符号表
// ================================================================

void IRBuilder::enterScope() {
    scopeStack.emplace_back();
}

void IRBuilder::exitScope() {
    assert(!scopeStack.empty() && "scope stack underflow");
    scopeStack.pop_back();
}

void IRBuilder::declare(const std::string& name, Value* val) {
    assert(!scopeStack.empty() && "no active scope");
    scopeStack.back()[name] = val;
}

Value* IRBuilder::lookup(const std::string& name) {
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return nullptr;
}

// ================================================================
// 工具
// ================================================================

std::string IRBuilder::newTempName() {
    return "t" + std::to_string(tempCount++);
}

} // namespace IR