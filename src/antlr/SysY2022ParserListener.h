
// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Parser.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "SysY2022Parser.h"


/**
 * This interface defines an abstract listener for a parse tree produced by SysY2022Parser.
 */
class  SysY2022ParserListener : public antlr4::tree::ParseTreeListener {
public:

  virtual void enterCompilationUnit(SysY2022Parser::CompilationUnitContext *ctx) = 0;
  virtual void exitCompilationUnit(SysY2022Parser::CompilationUnitContext *ctx) = 0;

  virtual void enterDecl(SysY2022Parser::DeclContext *ctx) = 0;
  virtual void exitDecl(SysY2022Parser::DeclContext *ctx) = 0;

  virtual void enterVecDecl(SysY2022Parser::VecDeclContext *ctx) = 0;
  virtual void exitVecDecl(SysY2022Parser::VecDeclContext *ctx) = 0;

  virtual void enterVecInit(SysY2022Parser::VecInitContext *ctx) = 0;
  virtual void exitVecInit(SysY2022Parser::VecInitContext *ctx) = 0;

  virtual void enterConstDecl(SysY2022Parser::ConstDeclContext *ctx) = 0;
  virtual void exitConstDecl(SysY2022Parser::ConstDeclContext *ctx) = 0;

  virtual void enterBType(SysY2022Parser::BTypeContext *ctx) = 0;
  virtual void exitBType(SysY2022Parser::BTypeContext *ctx) = 0;

  virtual void enterConstDef(SysY2022Parser::ConstDefContext *ctx) = 0;
  virtual void exitConstDef(SysY2022Parser::ConstDefContext *ctx) = 0;

  virtual void enterConstInitVal(SysY2022Parser::ConstInitValContext *ctx) = 0;
  virtual void exitConstInitVal(SysY2022Parser::ConstInitValContext *ctx) = 0;

  virtual void enterVarDecl(SysY2022Parser::VarDeclContext *ctx) = 0;
  virtual void exitVarDecl(SysY2022Parser::VarDeclContext *ctx) = 0;

  virtual void enterVarDef(SysY2022Parser::VarDefContext *ctx) = 0;
  virtual void exitVarDef(SysY2022Parser::VarDefContext *ctx) = 0;

  virtual void enterInitVal(SysY2022Parser::InitValContext *ctx) = 0;
  virtual void exitInitVal(SysY2022Parser::InitValContext *ctx) = 0;

  virtual void enterFuncDef(SysY2022Parser::FuncDefContext *ctx) = 0;
  virtual void exitFuncDef(SysY2022Parser::FuncDefContext *ctx) = 0;

  virtual void enterFuncType(SysY2022Parser::FuncTypeContext *ctx) = 0;
  virtual void exitFuncType(SysY2022Parser::FuncTypeContext *ctx) = 0;

  virtual void enterFuncFParams(SysY2022Parser::FuncFParamsContext *ctx) = 0;
  virtual void exitFuncFParams(SysY2022Parser::FuncFParamsContext *ctx) = 0;

  virtual void enterFuncFParam(SysY2022Parser::FuncFParamContext *ctx) = 0;
  virtual void exitFuncFParam(SysY2022Parser::FuncFParamContext *ctx) = 0;

  virtual void enterBlock(SysY2022Parser::BlockContext *ctx) = 0;
  virtual void exitBlock(SysY2022Parser::BlockContext *ctx) = 0;

  virtual void enterBlockItem(SysY2022Parser::BlockItemContext *ctx) = 0;
  virtual void exitBlockItem(SysY2022Parser::BlockItemContext *ctx) = 0;

  virtual void enterStmt(SysY2022Parser::StmtContext *ctx) = 0;
  virtual void exitStmt(SysY2022Parser::StmtContext *ctx) = 0;

  virtual void enterExp(SysY2022Parser::ExpContext *ctx) = 0;
  virtual void exitExp(SysY2022Parser::ExpContext *ctx) = 0;

  virtual void enterCond(SysY2022Parser::CondContext *ctx) = 0;
  virtual void exitCond(SysY2022Parser::CondContext *ctx) = 0;

  virtual void enterLVal(SysY2022Parser::LValContext *ctx) = 0;
  virtual void exitLVal(SysY2022Parser::LValContext *ctx) = 0;

  virtual void enterPrimaryExp(SysY2022Parser::PrimaryExpContext *ctx) = 0;
  virtual void exitPrimaryExp(SysY2022Parser::PrimaryExpContext *ctx) = 0;

  virtual void enterNumber(SysY2022Parser::NumberContext *ctx) = 0;
  virtual void exitNumber(SysY2022Parser::NumberContext *ctx) = 0;

  virtual void enterUnaryExp(SysY2022Parser::UnaryExpContext *ctx) = 0;
  virtual void exitUnaryExp(SysY2022Parser::UnaryExpContext *ctx) = 0;

  virtual void enterUnaryOp(SysY2022Parser::UnaryOpContext *ctx) = 0;
  virtual void exitUnaryOp(SysY2022Parser::UnaryOpContext *ctx) = 0;

  virtual void enterFuncRParams(SysY2022Parser::FuncRParamsContext *ctx) = 0;
  virtual void exitFuncRParams(SysY2022Parser::FuncRParamsContext *ctx) = 0;

  virtual void enterMulExp(SysY2022Parser::MulExpContext *ctx) = 0;
  virtual void exitMulExp(SysY2022Parser::MulExpContext *ctx) = 0;

  virtual void enterAddExp(SysY2022Parser::AddExpContext *ctx) = 0;
  virtual void exitAddExp(SysY2022Parser::AddExpContext *ctx) = 0;

  virtual void enterRelExp(SysY2022Parser::RelExpContext *ctx) = 0;
  virtual void exitRelExp(SysY2022Parser::RelExpContext *ctx) = 0;

  virtual void enterEqExp(SysY2022Parser::EqExpContext *ctx) = 0;
  virtual void exitEqExp(SysY2022Parser::EqExpContext *ctx) = 0;

  virtual void enterLAndExp(SysY2022Parser::LAndExpContext *ctx) = 0;
  virtual void exitLAndExp(SysY2022Parser::LAndExpContext *ctx) = 0;

  virtual void enterLOrExp(SysY2022Parser::LOrExpContext *ctx) = 0;
  virtual void exitLOrExp(SysY2022Parser::LOrExpContext *ctx) = 0;

  virtual void enterConstExp(SysY2022Parser::ConstExpContext *ctx) = 0;
  virtual void exitConstExp(SysY2022Parser::ConstExpContext *ctx) = 0;


};

