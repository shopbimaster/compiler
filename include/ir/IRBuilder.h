#pragma once

#include "IR.h"
#include "utils/Error.h"

// 注意：ANTLR4 生成的头文件不在 include/ 目录中
// 它们会在编译时从生成目录引用，或者在 IRBuilder.cpp 中引用

// 前向声明（避免包含不存在的头文件）
namespace antlr4 {
    class ANTLRInputStream;
}

// 这些类会在 IRBuilder.cpp 中完整定义
class SysY2022ParserBaseVisitor;
namespace SysY2022Parser {
    class CompilationUnitContext;
    class ConstDeclContext;
    class VarDeclContext;
    class FuncDefContext;
    class BlockContext;
    class StmtContext;
    class ExpContext;
    class CondContext;
    class LValContext;
    class PrimaryExpContext;
    class NumberContext;
    class UnaryExpContext;
    class MulExpContext;
    class AddExpContext;
    class RelExpContext;
    class EqExpContext;
    class LAndExpContext;
    class LOrExpContext;
    class ConstExpContext;
    class BTypeContext;
    class FuncTypeContext;
}

namespace IR {

class IRBuilder {
private:
    std::unique_ptr<Module> module;
    ErrorReporter& errorReporter;
    int tempCount;

    // 符号表
    std::unordered_map<std::string, Value*> symbolTable;
    std::vector<std::unordered_map<std::string, Value*>> scopeStack;

    // 循环上下文
    struct LoopContext {
        BasicBlock* continueBlock;
        BasicBlock* breakBlock;
    };
    std::vector<LoopContext> loopStack;

public:
    explicit IRBuilder(ErrorReporter& reporter);

    // ===== 主要构建方法 =====
    std::unique_ptr<Module> buildFromFile(const std::string& filename);

    // ===== 工具方法（声明）=====
    std::string newTemp();

private:
    // 作用域管理
    void enterScope();
    void exitScope();
    void declareVariable(const std::string& name, Value* value);
    Value* lookupVariable(const std::string& name);

    // 运行时库函数声明
    void declareRuntimeFunctions();
};

}
