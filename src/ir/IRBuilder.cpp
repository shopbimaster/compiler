#include "ir/IRBuilder.h"

// 注意：ANTLR4 生成的头文件会在实际编译时包含进来
// 这里是骨架实现 - 待完善

namespace IR {

IRBuilder::IRBuilder(ErrorReporter& reporter)
    : errorReporter(reporter), tempCount(0) {
}

std::unique_ptr<Module> IRBuilder::buildFromFile(const std::string& filename) {
    // TODO: 实现完整的构建过程
    // 1. 读取文件
    // 2. 使用 ANTLR4 解析
    // 3. Visitor 遍历 ParseTree 生成 IR

    // 临时返回空 Module
    return std::make_unique<Module>();
}

std::string IRBuilder::newTemp() {
    return "%t" + std::to_string(tempCount++);
}

void IRBuilder::enterScope() {
    scopeStack.push_back(std::unordered_map<std::string, Value*>());
}

void IRBuilder::exitScope() {
    if (!scopeStack.empty()) {
        scopeStack.pop_back();
    }
}

void IRBuilder::declareVariable(const std::string& name, Value* value) {
    if (!scopeStack.empty()) {
        scopeStack.back()[name] = value;
    } else {
        symbolTable[name] = value;
    }
}

Value* IRBuilder::lookupVariable(const std::string& name) {
    // 先从当前作用域查找
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    // 再从全局符号表查找
    auto found = symbolTable.find(name);
    if (found != symbolTable.end()) {
        return found->second;
    }
    return nullptr;
}

void IRBuilder::declareRuntimeFunctions() {
    // TODO: 声明 SysY2022 运行时库函数
}

}
