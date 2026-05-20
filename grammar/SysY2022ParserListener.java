// Generated from /mnt/d/VSCodeProjects/compiler/grammar/SysY2022Parser.g4 by ANTLR 4.13.1
import org.antlr.v4.runtime.tree.ParseTreeListener;

/**
 * This interface defines a complete listener for a parse tree produced by
 * {@link SysY2022Parser}.
 */
public interface SysY2022ParserListener extends ParseTreeListener {
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#compilationUnit}.
	 * @param ctx the parse tree
	 */
	void enterCompilationUnit(SysY2022Parser.CompilationUnitContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#compilationUnit}.
	 * @param ctx the parse tree
	 */
	void exitCompilationUnit(SysY2022Parser.CompilationUnitContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#decl}.
	 * @param ctx the parse tree
	 */
	void enterDecl(SysY2022Parser.DeclContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#decl}.
	 * @param ctx the parse tree
	 */
	void exitDecl(SysY2022Parser.DeclContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#constDecl}.
	 * @param ctx the parse tree
	 */
	void enterConstDecl(SysY2022Parser.ConstDeclContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#constDecl}.
	 * @param ctx the parse tree
	 */
	void exitConstDecl(SysY2022Parser.ConstDeclContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#bType}.
	 * @param ctx the parse tree
	 */
	void enterBType(SysY2022Parser.BTypeContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#bType}.
	 * @param ctx the parse tree
	 */
	void exitBType(SysY2022Parser.BTypeContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#constDef}.
	 * @param ctx the parse tree
	 */
	void enterConstDef(SysY2022Parser.ConstDefContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#constDef}.
	 * @param ctx the parse tree
	 */
	void exitConstDef(SysY2022Parser.ConstDefContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#constInitVal}.
	 * @param ctx the parse tree
	 */
	void enterConstInitVal(SysY2022Parser.ConstInitValContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#constInitVal}.
	 * @param ctx the parse tree
	 */
	void exitConstInitVal(SysY2022Parser.ConstInitValContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#varDecl}.
	 * @param ctx the parse tree
	 */
	void enterVarDecl(SysY2022Parser.VarDeclContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#varDecl}.
	 * @param ctx the parse tree
	 */
	void exitVarDecl(SysY2022Parser.VarDeclContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#varDef}.
	 * @param ctx the parse tree
	 */
	void enterVarDef(SysY2022Parser.VarDefContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#varDef}.
	 * @param ctx the parse tree
	 */
	void exitVarDef(SysY2022Parser.VarDefContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#initVal}.
	 * @param ctx the parse tree
	 */
	void enterInitVal(SysY2022Parser.InitValContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#initVal}.
	 * @param ctx the parse tree
	 */
	void exitInitVal(SysY2022Parser.InitValContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#funcDef}.
	 * @param ctx the parse tree
	 */
	void enterFuncDef(SysY2022Parser.FuncDefContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#funcDef}.
	 * @param ctx the parse tree
	 */
	void exitFuncDef(SysY2022Parser.FuncDefContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#funcType}.
	 * @param ctx the parse tree
	 */
	void enterFuncType(SysY2022Parser.FuncTypeContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#funcType}.
	 * @param ctx the parse tree
	 */
	void exitFuncType(SysY2022Parser.FuncTypeContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#funcFParams}.
	 * @param ctx the parse tree
	 */
	void enterFuncFParams(SysY2022Parser.FuncFParamsContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#funcFParams}.
	 * @param ctx the parse tree
	 */
	void exitFuncFParams(SysY2022Parser.FuncFParamsContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#funcFParam}.
	 * @param ctx the parse tree
	 */
	void enterFuncFParam(SysY2022Parser.FuncFParamContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#funcFParam}.
	 * @param ctx the parse tree
	 */
	void exitFuncFParam(SysY2022Parser.FuncFParamContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#block}.
	 * @param ctx the parse tree
	 */
	void enterBlock(SysY2022Parser.BlockContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#block}.
	 * @param ctx the parse tree
	 */
	void exitBlock(SysY2022Parser.BlockContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#blockItem}.
	 * @param ctx the parse tree
	 */
	void enterBlockItem(SysY2022Parser.BlockItemContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#blockItem}.
	 * @param ctx the parse tree
	 */
	void exitBlockItem(SysY2022Parser.BlockItemContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#stmt}.
	 * @param ctx the parse tree
	 */
	void enterStmt(SysY2022Parser.StmtContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#stmt}.
	 * @param ctx the parse tree
	 */
	void exitStmt(SysY2022Parser.StmtContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#exp}.
	 * @param ctx the parse tree
	 */
	void enterExp(SysY2022Parser.ExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#exp}.
	 * @param ctx the parse tree
	 */
	void exitExp(SysY2022Parser.ExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#cond}.
	 * @param ctx the parse tree
	 */
	void enterCond(SysY2022Parser.CondContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#cond}.
	 * @param ctx the parse tree
	 */
	void exitCond(SysY2022Parser.CondContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#lVal}.
	 * @param ctx the parse tree
	 */
	void enterLVal(SysY2022Parser.LValContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#lVal}.
	 * @param ctx the parse tree
	 */
	void exitLVal(SysY2022Parser.LValContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#primaryExp}.
	 * @param ctx the parse tree
	 */
	void enterPrimaryExp(SysY2022Parser.PrimaryExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#primaryExp}.
	 * @param ctx the parse tree
	 */
	void exitPrimaryExp(SysY2022Parser.PrimaryExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#number}.
	 * @param ctx the parse tree
	 */
	void enterNumber(SysY2022Parser.NumberContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#number}.
	 * @param ctx the parse tree
	 */
	void exitNumber(SysY2022Parser.NumberContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#unaryExp}.
	 * @param ctx the parse tree
	 */
	void enterUnaryExp(SysY2022Parser.UnaryExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#unaryExp}.
	 * @param ctx the parse tree
	 */
	void exitUnaryExp(SysY2022Parser.UnaryExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#unaryOp}.
	 * @param ctx the parse tree
	 */
	void enterUnaryOp(SysY2022Parser.UnaryOpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#unaryOp}.
	 * @param ctx the parse tree
	 */
	void exitUnaryOp(SysY2022Parser.UnaryOpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#funcRParams}.
	 * @param ctx the parse tree
	 */
	void enterFuncRParams(SysY2022Parser.FuncRParamsContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#funcRParams}.
	 * @param ctx the parse tree
	 */
	void exitFuncRParams(SysY2022Parser.FuncRParamsContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#mulExp}.
	 * @param ctx the parse tree
	 */
	void enterMulExp(SysY2022Parser.MulExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#mulExp}.
	 * @param ctx the parse tree
	 */
	void exitMulExp(SysY2022Parser.MulExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#addExp}.
	 * @param ctx the parse tree
	 */
	void enterAddExp(SysY2022Parser.AddExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#addExp}.
	 * @param ctx the parse tree
	 */
	void exitAddExp(SysY2022Parser.AddExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#relExp}.
	 * @param ctx the parse tree
	 */
	void enterRelExp(SysY2022Parser.RelExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#relExp}.
	 * @param ctx the parse tree
	 */
	void exitRelExp(SysY2022Parser.RelExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#eqExp}.
	 * @param ctx the parse tree
	 */
	void enterEqExp(SysY2022Parser.EqExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#eqExp}.
	 * @param ctx the parse tree
	 */
	void exitEqExp(SysY2022Parser.EqExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#lAndExp}.
	 * @param ctx the parse tree
	 */
	void enterLAndExp(SysY2022Parser.LAndExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#lAndExp}.
	 * @param ctx the parse tree
	 */
	void exitLAndExp(SysY2022Parser.LAndExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#lOrExp}.
	 * @param ctx the parse tree
	 */
	void enterLOrExp(SysY2022Parser.LOrExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#lOrExp}.
	 * @param ctx the parse tree
	 */
	void exitLOrExp(SysY2022Parser.LOrExpContext ctx);
	/**
	 * Enter a parse tree produced by {@link SysY2022Parser#constExp}.
	 * @param ctx the parse tree
	 */
	void enterConstExp(SysY2022Parser.ConstExpContext ctx);
	/**
	 * Exit a parse tree produced by {@link SysY2022Parser#constExp}.
	 * @param ctx the parse tree
	 */
	void exitConstExp(SysY2022Parser.ConstExpContext ctx);
}