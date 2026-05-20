#pragma once

#include "IR.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace IR {

class IRBuilder {
public:
    IRBuilder();

    // ===== 主构建入口 =====
    std::unique_ptr<Module> buildModule();

    // ===== 测试用：构造 simple main =====
    // int main() { return <returnValue>; }
    std::unique_ptr<Module> buildSimpleMain(int64_t returnValue);

    // ===== IR 构建辅助方法 =====
    Function*    createFunction(const std::string& name, Type* retTy,
                                const std::vector<Type*>& paramTys);
    BasicBlock*  createBlock(const std::string& name = "");
    Instruction* createRet(Value* val);
    Instruction* createRetVoid();
    Value*       createConstantInt(int64_t value, unsigned bitWidth = 32);

    // ===== 符号表 (用于 Syntax-Directed Translation) =====
    void   enterScope();
    void   exitScope();
    void   declare(const std::string& name, Value* val);
    Value* lookup(const std::string& name);

    // ===== 当前上下文 =====
    void         setCurrentFunction(Function* f)   { currentFunc = f; }
    Function*    getCurrentFunction() const         { return currentFunc; }
    void         setCurrentBlock(BasicBlock* bb)    { currentBB = bb; }
    BasicBlock*  getCurrentBlock() const            { return currentBB; }
    void         setCurrentModule(Module* m)        { module = m; }
    Module*      getCurrentModule() const           { return module; }

    std::string  newTempName();

private:
    Module*      module;
    Function*    currentFunc;
    BasicBlock*  currentBB;
    int          tempCount;

    // 符号表: 作用域栈
    std::vector<std::unordered_map<std::string, Value*>> scopeStack;
};

} // namespace IR