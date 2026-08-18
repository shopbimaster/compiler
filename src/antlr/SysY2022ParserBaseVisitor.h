
// Generated from grammar/SysY2022Parser.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "SysY2022ParserVisitor.h"


/**
 * This class provides an empty implementation of SysY2022ParserVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  SysY2022ParserBaseVisitor : public SysY2022ParserVisitor {
public:

  virtual std::any visitCompilationUnit(SysY2022Parser::CompilationUnitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDecl(SysY2022Parser::DeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDecl(SysY2022Parser::ConstDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBType(SysY2022Parser::BTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTensorType(SysY2022Parser::TensorTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDef(SysY2022Parser::ConstDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstInitVal(SysY2022Parser::ConstInitValContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVarDecl(SysY2022Parser::VarDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVarDef(SysY2022Parser::VarDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInitVal(SysY2022Parser::InitValContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncDef(SysY2022Parser::FuncDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncType(SysY2022Parser::FuncTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncFParams(SysY2022Parser::FuncFParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncFParam(SysY2022Parser::FuncFParamContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlock(SysY2022Parser::BlockContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlockItem(SysY2022Parser::BlockItemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStmt(SysY2022Parser::StmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExp(SysY2022Parser::ExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCond(SysY2022Parser::CondContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLVal(SysY2022Parser::LValContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryExp(SysY2022Parser::PrimaryExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNumber(SysY2022Parser::NumberContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryExp(SysY2022Parser::UnaryExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryOp(SysY2022Parser::UnaryOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncRParams(SysY2022Parser::FuncRParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulExp(SysY2022Parser::MulExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddExp(SysY2022Parser::AddExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelExp(SysY2022Parser::RelExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEqExp(SysY2022Parser::EqExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLAndExp(SysY2022Parser::LAndExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLOrExp(SysY2022Parser::LOrExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstExp(SysY2022Parser::ConstExpContext *ctx) override {
    return visitChildren(ctx);
  }


};

