
// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Parser.g4 by ANTLR 4.10.1

#pragma once


#include "antlr4-runtime.h"
#include "SysY2022ParserListener.h"


/**
 * This class provides an empty implementation of SysY2022ParserListener,
 * which can be extended to create a listener which only needs to handle a subset
 * of the available methods.
 */
class  SysY2022ParserBaseListener : public SysY2022ParserListener {
public:

  virtual void enterCompilationUnit(SysY2022Parser::CompilationUnitContext * /*ctx*/) override { }
  virtual void exitCompilationUnit(SysY2022Parser::CompilationUnitContext * /*ctx*/) override { }

  virtual void enterDecl(SysY2022Parser::DeclContext * /*ctx*/) override { }
  virtual void exitDecl(SysY2022Parser::DeclContext * /*ctx*/) override { }

  virtual void enterVectorDecl(SysY2022Parser::VectorDeclContext * /*ctx*/) override { }
  virtual void exitVectorDecl(SysY2022Parser::VectorDeclContext * /*ctx*/) override { }

  virtual void enterConstDecl(SysY2022Parser::ConstDeclContext * /*ctx*/) override { }
  virtual void exitConstDecl(SysY2022Parser::ConstDeclContext * /*ctx*/) override { }

  virtual void enterBType(SysY2022Parser::BTypeContext * /*ctx*/) override { }
  virtual void exitBType(SysY2022Parser::BTypeContext * /*ctx*/) override { }

  virtual void enterConstDef(SysY2022Parser::ConstDefContext * /*ctx*/) override { }
  virtual void exitConstDef(SysY2022Parser::ConstDefContext * /*ctx*/) override { }

  virtual void enterConstInitVal(SysY2022Parser::ConstInitValContext * /*ctx*/) override { }
  virtual void exitConstInitVal(SysY2022Parser::ConstInitValContext * /*ctx*/) override { }

  virtual void enterVarDecl(SysY2022Parser::VarDeclContext * /*ctx*/) override { }
  virtual void exitVarDecl(SysY2022Parser::VarDeclContext * /*ctx*/) override { }

  virtual void enterVarDef(SysY2022Parser::VarDefContext * /*ctx*/) override { }
  virtual void exitVarDef(SysY2022Parser::VarDefContext * /*ctx*/) override { }

  virtual void enterInitVal(SysY2022Parser::InitValContext * /*ctx*/) override { }
  virtual void exitInitVal(SysY2022Parser::InitValContext * /*ctx*/) override { }

  virtual void enterFuncDef(SysY2022Parser::FuncDefContext * /*ctx*/) override { }
  virtual void exitFuncDef(SysY2022Parser::FuncDefContext * /*ctx*/) override { }

  virtual void enterFuncType(SysY2022Parser::FuncTypeContext * /*ctx*/) override { }
  virtual void exitFuncType(SysY2022Parser::FuncTypeContext * /*ctx*/) override { }

  virtual void enterFuncFParams(SysY2022Parser::FuncFParamsContext * /*ctx*/) override { }
  virtual void exitFuncFParams(SysY2022Parser::FuncFParamsContext * /*ctx*/) override { }

  virtual void enterFuncFParam(SysY2022Parser::FuncFParamContext * /*ctx*/) override { }
  virtual void exitFuncFParam(SysY2022Parser::FuncFParamContext * /*ctx*/) override { }

  virtual void enterBlock(SysY2022Parser::BlockContext * /*ctx*/) override { }
  virtual void exitBlock(SysY2022Parser::BlockContext * /*ctx*/) override { }

  virtual void enterBlockItem(SysY2022Parser::BlockItemContext * /*ctx*/) override { }
  virtual void exitBlockItem(SysY2022Parser::BlockItemContext * /*ctx*/) override { }

  virtual void enterStmt(SysY2022Parser::StmtContext * /*ctx*/) override { }
  virtual void exitStmt(SysY2022Parser::StmtContext * /*ctx*/) override { }

  virtual void enterExp(SysY2022Parser::ExpContext * /*ctx*/) override { }
  virtual void exitExp(SysY2022Parser::ExpContext * /*ctx*/) override { }

  virtual void enterCond(SysY2022Parser::CondContext * /*ctx*/) override { }
  virtual void exitCond(SysY2022Parser::CondContext * /*ctx*/) override { }

  virtual void enterLVal(SysY2022Parser::LValContext * /*ctx*/) override { }
  virtual void exitLVal(SysY2022Parser::LValContext * /*ctx*/) override { }

  virtual void enterPrimaryExp(SysY2022Parser::PrimaryExpContext * /*ctx*/) override { }
  virtual void exitPrimaryExp(SysY2022Parser::PrimaryExpContext * /*ctx*/) override { }

  virtual void enterNumber(SysY2022Parser::NumberContext * /*ctx*/) override { }
  virtual void exitNumber(SysY2022Parser::NumberContext * /*ctx*/) override { }

  virtual void enterUnaryExp(SysY2022Parser::UnaryExpContext * /*ctx*/) override { }
  virtual void exitUnaryExp(SysY2022Parser::UnaryExpContext * /*ctx*/) override { }

  virtual void enterUnaryOp(SysY2022Parser::UnaryOpContext * /*ctx*/) override { }
  virtual void exitUnaryOp(SysY2022Parser::UnaryOpContext * /*ctx*/) override { }

  virtual void enterFuncRParams(SysY2022Parser::FuncRParamsContext * /*ctx*/) override { }
  virtual void exitFuncRParams(SysY2022Parser::FuncRParamsContext * /*ctx*/) override { }

  virtual void enterMulExp(SysY2022Parser::MulExpContext * /*ctx*/) override { }
  virtual void exitMulExp(SysY2022Parser::MulExpContext * /*ctx*/) override { }

  virtual void enterAddExp(SysY2022Parser::AddExpContext * /*ctx*/) override { }
  virtual void exitAddExp(SysY2022Parser::AddExpContext * /*ctx*/) override { }

  virtual void enterRelExp(SysY2022Parser::RelExpContext * /*ctx*/) override { }
  virtual void exitRelExp(SysY2022Parser::RelExpContext * /*ctx*/) override { }

  virtual void enterEqExp(SysY2022Parser::EqExpContext * /*ctx*/) override { }
  virtual void exitEqExp(SysY2022Parser::EqExpContext * /*ctx*/) override { }

  virtual void enterLAndExp(SysY2022Parser::LAndExpContext * /*ctx*/) override { }
  virtual void exitLAndExp(SysY2022Parser::LAndExpContext * /*ctx*/) override { }

  virtual void enterLOrExp(SysY2022Parser::LOrExpContext * /*ctx*/) override { }
  virtual void exitLOrExp(SysY2022Parser::LOrExpContext * /*ctx*/) override { }

  virtual void enterConstExp(SysY2022Parser::ConstExpContext * /*ctx*/) override { }
  virtual void exitConstExp(SysY2022Parser::ConstExpContext * /*ctx*/) override { }


  virtual void enterEveryRule(antlr4::ParserRuleContext * /*ctx*/) override { }
  virtual void exitEveryRule(antlr4::ParserRuleContext * /*ctx*/) override { }
  virtual void visitTerminal(antlr4::tree::TerminalNode * /*node*/) override { }
  virtual void visitErrorNode(antlr4::tree::ErrorNode * /*node*/) override { }

};

