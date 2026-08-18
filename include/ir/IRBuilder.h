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
    std::any visitConstExp(SysY2022Parser::ConstExpContext* ctx) override;

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
    Value*          implConvert(Value* val, Type* targetTy);
    Constant*       constantToType(Constant* val, Type* targetTy);
    Value*          conditionToBool(Value* val);
    void            registerBuiltinFunctions();
    void            emitInitStoresVar(Type* targetType, Value* basePtr,
                                  std::vector<Value*>& indices,
                                  const std::vector<SysY2022Parser::InitValContext*>& children,
                                  int& flatIdx);
    void            emitInitStoresConst(Type* targetType, Value* basePtr,
                                  std::vector<Value*>& indices,
                                  const std::vector<SysY2022Parser::ConstInitValContext*>& children,
                                  int& flatIdx);
    void            collectInitData(Type* targetType,
                                  const std::vector<SysY2022Parser::InitValContext*>& children,
                                  std::vector<uint32_t>& outData);
    void            collectInitDataConst(Type* targetType,
                                  const std::vector<SysY2022Parser::ConstInitValContext*>& children,
                                  std::vector<uint32_t>& outData);
    Value*          zeroForType(Type* ty);

    // ===== SysY2026 张量辅助 =====
    // 记录张量变量名（由 'tensor' 关键字声明），区分普通数组。
    std::unordered_map<std::string, bool> tensorVars;   // name → isFloat
    // 判断 Value 是否为张量操作数（指向数组的 alloca 指针，且变量名在 tensorVars 中）
    // 注：表达式中的张量通过 lVal 的裸名进入，先由 visitPrimaryExp 保留 alloca 指针。
    bool   isTensorOperand(Value* v);
    // 张量逐元素标量运算：lhs/rhs 均为同形张量 alloca，生成结果张量 alloca。
    Value* emitTensorElementWise(Instruction::Opcode intOp, Instruction::Opcode floatOp,
                                 Value* lhs, Value* rhs);
    // 张量与标量：标量提升到同型张量（每个分量都等于该标量），再逐元素运算。
    Value* emitTensorScalarOp(Instruction::Opcode intOp, Instruction::Opcode floatOp,
                              Value* tensorVal, Value* scalarVal, bool scalarOnLeft);
    // 单目取负：-tensor → 逐元素取负（0 - elem 或 0.0 - elem）。
    Value* emitTensorNeg(Value* tensorVal);
    // 张量拷贝赋值：dst = src（同形张量逐元素拷贝）。
    void   emitTensorCopy(Value* dst, Value* src);
    // 矩阵乘法 @：lhs[M x N] @ rhs[N x L] → result[M x L]。
    Value* emitTensorMatMul(Value* lhs, Value* rhs);

    // ===== 常数表达式编译期求值 =====
    Value* constEval(SysY2022Parser::AddExpContext* ctx);
    Value* constEvalMul(SysY2022Parser::MulExpContext* ctx);
    Value* constEvalUnary(SysY2022Parser::UnaryExpContext* ctx);
    Value* constEvalPrimary(SysY2022Parser::PrimaryExpContext* ctx);
    Value* constFoldBinOp(Instruction::Opcode op, Value* left, Value* right);

    // ===== 左递归表达式通用处理 =====
    Value* visitLeftRecursiveBinary(
        antlr4::tree::ParseTree* firstChild,
        antlr4::tree::ParseTree* opChild,
        antlr4::tree::ParseTree* rightChild,
        Instruction::Opcode opcode);

    // ===== 循环上下文（用于 break/continue）=====
    struct LoopContext {
        BasicBlock* continueBB;
        BasicBlock* breakBB;
    };

    // ===== 成员状态 =====
    std::unique_ptr<Module>  module;
    Function*    currentFunc;
    BasicBlock*  currentBB;
    int          tempCount;
    int          blockCounter;

    // 符号表: 作用域栈, 变量名 → 栈上地址 (alloca)
    std::vector<std::unordered_map<std::string, Value*>> scopeStack;

    // 循环栈: 用于 break/continue 跳转目标
    std::vector<LoopContext> loopStack;

    // 函数表: 用于函数调用时查找已声明函数的类型
    std::unordered_map<std::string, FunctionType*> funcTypeTable;
};

} // namespace IR