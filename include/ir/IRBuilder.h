#pragma once

#include "ir/IR.h"
#include "SysY2022ParserBaseVisitor.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>

namespace IR {

class IRBuilder : public SysY2022ParserBaseVisitor {
public:
    IRBuilder();

    // ===== 主入口 =====
    // 从 SysY2022 源文件编译为 IR Module
    std::unique_ptr<Module> compile(const std::string& sourcePath);

    // ===== Visitor 接口实现 =====
    std::any visitCompilationUnit(SysY2022Parser::CompilationUnitContext* ctx) override;
    std::any visitDecl(SysY2022Parser::DeclContext* ctx) override;
    std::any visitConstDecl(SysY2022Parser::ConstDeclContext* ctx) override;
    std::any visitBType(SysY2022Parser::BTypeContext* ctx) override;
    std::any visitVarDecl(SysY2022Parser::VarDeclContext* ctx) override;
    std::any visitVarDef(SysY2022Parser::VarDefContext* ctx) override;
    std::any visitInitVal(SysY2022Parser::InitValContext* ctx) override;
    std::any visitFuncDef(SysY2022Parser::FuncDefContext* ctx) override;
    std::any visitFuncType(SysY2022Parser::FuncTypeContext* ctx) override;
    std::any visitFuncFParams(SysY2022Parser::FuncFParamsContext* ctx) override;
    std::any visitFuncFParam(SysY2022Parser::FuncFParamContext* ctx) override;
    std::any visitBlock(SysY2022Parser::BlockContext* ctx) override;
    std::any visitBlockItem(SysY2022Parser::BlockItemContext* ctx) override;
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

    // ===== 测试辅助 =====
    std::unique_ptr<Module> buildSimpleMain(int64_t returnValue);

private:
    // ===== IR 构建辅助 =====
    Function*    createFunction(const std::string& name, Type* retTy,
                                const std::vector<Type*>& paramTys);
    BasicBlock*  createBlock(const std::string& name = "");
    Instruction* createRetInst(Value* val);
    Instruction* createRetVoidInst();

    // ===== 符号表 =====
    void   enterScope();
    void   exitScope();
    void   declare(const std::string& name, Value* val);
    Value* lookup(const std::string& name);

    // ===== 工具 =====
    std::string     newTempName();
    Type*           toIRType(const std::string& sysyType);
    Instruction*    emitBinOp(Instruction::Opcode op, Value* lhs, Value* rhs);

    // ===== 左递归表达式通用处理 =====
    Value* visitLeftRecursiveBinary(
        antlr4::tree::ParseTree* firstChild,
        antlr4::tree::ParseTree* opChild,
        antlr4::tree::ParseTree* rightChild,
        Instruction::Opcode opcode);

    // ===== 成员状态 =====
    std::unique_ptr<Module>  module;
    Function*    currentFunc;
    BasicBlock*  currentBB;
    int          tempCount;
    int          blockCounter;

    // 符号表: 作用域栈, 变量名 → 栈上地址 (alloca)
    std::vector<std::unordered_map<std::string, Value*>> scopeStack;
};

} // namespace IR