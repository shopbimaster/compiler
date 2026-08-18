
// Generated from grammar/SysY2022Parser.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "SysY2022Parser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by SysY2022Parser.
 */
class  SysY2022ParserVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by SysY2022Parser.
   */
    virtual std::any visitCompilationUnit(SysY2022Parser::CompilationUnitContext *context) = 0;

    virtual std::any visitDecl(SysY2022Parser::DeclContext *context) = 0;

    virtual std::any visitConstDecl(SysY2022Parser::ConstDeclContext *context) = 0;

    virtual std::any visitBType(SysY2022Parser::BTypeContext *context) = 0;

    virtual std::any visitTensorType(SysY2022Parser::TensorTypeContext *context) = 0;

    virtual std::any visitConstDef(SysY2022Parser::ConstDefContext *context) = 0;

    virtual std::any visitConstInitVal(SysY2022Parser::ConstInitValContext *context) = 0;

    virtual std::any visitVarDecl(SysY2022Parser::VarDeclContext *context) = 0;

    virtual std::any visitVarDef(SysY2022Parser::VarDefContext *context) = 0;

    virtual std::any visitInitVal(SysY2022Parser::InitValContext *context) = 0;

    virtual std::any visitFuncDef(SysY2022Parser::FuncDefContext *context) = 0;

    virtual std::any visitFuncType(SysY2022Parser::FuncTypeContext *context) = 0;

    virtual std::any visitFuncFParams(SysY2022Parser::FuncFParamsContext *context) = 0;

    virtual std::any visitFuncFParam(SysY2022Parser::FuncFParamContext *context) = 0;

    virtual std::any visitBlock(SysY2022Parser::BlockContext *context) = 0;

    virtual std::any visitBlockItem(SysY2022Parser::BlockItemContext *context) = 0;

    virtual std::any visitStmt(SysY2022Parser::StmtContext *context) = 0;

    virtual std::any visitExp(SysY2022Parser::ExpContext *context) = 0;

    virtual std::any visitCond(SysY2022Parser::CondContext *context) = 0;

    virtual std::any visitLVal(SysY2022Parser::LValContext *context) = 0;

    virtual std::any visitPrimaryExp(SysY2022Parser::PrimaryExpContext *context) = 0;

    virtual std::any visitNumber(SysY2022Parser::NumberContext *context) = 0;

    virtual std::any visitUnaryExp(SysY2022Parser::UnaryExpContext *context) = 0;

    virtual std::any visitUnaryOp(SysY2022Parser::UnaryOpContext *context) = 0;

    virtual std::any visitFuncRParams(SysY2022Parser::FuncRParamsContext *context) = 0;

    virtual std::any visitMulExp(SysY2022Parser::MulExpContext *context) = 0;

    virtual std::any visitAddExp(SysY2022Parser::AddExpContext *context) = 0;

    virtual std::any visitRelExp(SysY2022Parser::RelExpContext *context) = 0;

    virtual std::any visitEqExp(SysY2022Parser::EqExpContext *context) = 0;

    virtual std::any visitLAndExp(SysY2022Parser::LAndExpContext *context) = 0;

    virtual std::any visitLOrExp(SysY2022Parser::LOrExpContext *context) = 0;

    virtual std::any visitConstExp(SysY2022Parser::ConstExpContext *context) = 0;


};

