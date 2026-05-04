#pragma once

#include "IR.h"
#include "frontend/AST.h"
#include "utils/Error.h"
#include <unordered_map>
#include <memory>

namespace IR {

class IRBuilder : public AST::Visitor {
private:
    std::unique_ptr<Module> module;
    Function* currentFunction;
    BasicBlock* currentBlock;
    ErrorReporter& errorReporter;
    std::unordered_map<std::string, Value*> symbolTable;
    std::vector<BasicBlock*> breakTargets;
    std::vector<BasicBlock*> continueTargets;
    int tempCount;

public:
    explicit IRBuilder(ErrorReporter& reporter);

    std::unique_ptr<Module> build(AST::CompilationUnit& cu);

    void visit(AST::Number&) override;
    void visit(AST::Identifier&) override;
    void visit(AST::BinaryExpr&) override;
    void visit(AST::UnaryExpr&) override;
    void visit(AST::CallExpr&) override;
    void visit(AST::ArrayAccess&) override;
    void visit(AST::Block&) override;
    void visit(AST::AssignStmt&) override;
    void visit(AST::IfStmt&) override;
    void visit(AST::WhileStmt&) override;
    void visit(AST::BreakStmt&) override;
    void visit(AST::ContinueStmt&) override;
    void visit(AST::ReturnStmt&) override;
    void visit(AST::ExprStmt&) override;
    void visit(AST::VarDecl&) override;
    void visit(AST::FuncDef&) override;
    void visit(AST::CompilationUnit&) override;

private:
    std::string newTemp();
    Value* currentValue;
    Instruction* createBinOp(IR::Opcode op, Value* lhs, Value* rhs, const std::string& name = "");
    Instruction* createAlloca(Type type, const std::string& name = "");
    Instruction* createLoad(Value* ptr, const std::string& name = "");
    Instruction* createStore(Value* val, Value* ptr);
    Instruction* createBr(BasicBlock* target);
    Instruction* createCondBr(Value* cond, BasicBlock* trueBB, BasicBlock* falseBB);
    Instruction* createRet(Value* val = nullptr);
    Instruction* createCall(Function* func, const std::vector<Value*>& args, const std::string& name = "");
};

}
