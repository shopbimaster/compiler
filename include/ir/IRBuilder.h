#pragma once

#include "IR.h"
#include "SysY2022Parser.h"
#include "SysY2022Lexer.h"
#include "utils/Error.h"

namespace IR {

class IRBuilder : public SysY2022ParserBaseVisitor {
private:
    std::unique_ptr<Module> module;
    Function* currentFunction;
    BasicBlock* currentBlock;
    ErrorReporter& errorReporter;
    int tempCount;

    // 符号表 - 存储变量分配
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

    std::unique_ptr<Module> build(SysY2022Parser::CompilationUnitContext* ctx);

    // ===== Visitor 方法 =====
    std::any visitCompilationUnit(SysY2022Parser::CompilationUnitContext* ctx) override;
    std::any visitConstDecl(SysY2022Parser::ConstDeclContext* ctx) override;
    std::any visitVarDecl(SysY2022Parser::VarDeclContext* ctx) override;
    std::any visitFuncDef(SysY2022Parser::FuncDefContext* ctx) override;
    std::any visitBlock(SysY2022Parser::BlockContext* ctx) override;
    std::any visitStmt(SysY2022Parser::StmtContext* ctx) override;
    std::any visitExp(SysY2022Parser::ExpContext* ctx) override;
    std::any visitCond(SysY2022Parser::CondContext* ctx) override;
    std::any visitLVal(SysY2022Parser::LValContext* ctx) override;
    std::any visitPrimaryExp(SysY2022Parser::PrimaryExpContext* ctx) override;
    std::any visitNumber(SysY2022Parser::NumberContext* ctx) override;
    std::any visitUnaryExp(SysY2022Parser::UnaryExpContext* ctx) override;
    std::any visitMulExp(SysY2022Parser::MulExpContext* ctx) override;
    std::any visitAddExp(SysY2022Parser::AddExpContext* ctx) override;
    std::any visitRelExp(SysY2022Parser::RelExpContext* ctx) override;
    std::any visitEqExp(SysY2022Parser::EqExpContext* ctx) override;
    std::any visitLAndExp(SysY2022Parser::LAndExpContext* ctx) override;
    std::any visitLOrExp(SysY2022Parser::LOrExpContext* ctx) override;
    std::any visitConstExp(SysY2022Parser::ConstExpContext* ctx) override;

private:
    // ===== 辅助方法 =====
    std::string newTemp();

    // 类型转换
    Type typeFromToken(SysY2022Parser::BTypeContext* ctx);
    Type typeFromFuncType(SysY2022Parser::FuncTypeContext* ctx);

    // IR 指令构建
    Instruction* createBinOp(Opcode op, Value* lhs, Value* rhs, const std::string& name = "");
    Instruction* createAlloca(Type type, const std::string& name = "");
    Instruction* createLoad(Value* ptr, const std::string& name = "");
    Instruction* createStore(Value* val, Value* ptr);
    Instruction* createBr(BasicBlock* target);
    Instruction* createCondBr(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB);
    Instruction* createRet(Value* val = nullptr);
    Instruction* createCall(Function* func, const std::vector<Value*>& args, const std::string& name = "");

    // 作用域管理
    void enterScope();
    void exitScope();
    void declareVariable(const std::string& name, Value* value);
    Value* lookupVariable(const std::string& name);

    // 运行时库函数声明
    void declareRuntimeFunctions();
};

}
