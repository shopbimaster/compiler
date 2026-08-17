#pragma once

#include "ir/IR.h"
#include "SysY2022ParserBaseVisitor.h"
#include <unordered_map>
#include <unordered_set>
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
    // 《前端+变长》 变长向量声明访问器（运行时长度，仅前端扩展）
    std::any visitVecDecl(SysY2022Parser::VecDeclContext* ctx) override;
    std::any visitVecInit(SysY2022Parser::VecInitContext* ctx) override;
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

    // ===== 《前端+变长》 变长向量运算辅助（仅前端 IR 生成扩展，运行时长度） =====
    // vec 表示为 alloca [VEC_MAX+1 x i32]：[0]=运行时长度，[1..len]=数据
    // 运算用运行时循环（非编译期展开），与定长版的核心区别
    static constexpr unsigned VEC_MAX = 1024;   // 单个 vec 最大元素数
    // 判断 Value 是否为变长向量 alloca（通过 vecAllocas 侧集合识别）
    bool   isVecValue(Value* v);
    // 获取 vec 指针的第 i+1 个元素地址（跳过 [0] 长度）
    Value* emitVecElemPtr(Value* vecPtr, Value* idx);
    // 分配 vec alloca [VEC_MAX+1 x i32] 并设运行时长度 n
    Value* emitVecAlloca(unsigned n);
    // 运行时长度读取：load vecPtr[0]
    Value* emitVecLen(Value* vecPtr);
    // 向量二元运算（运行时循环）：left op right，返回结果 vec alloca
    Value* emitVecBinOp(Instruction::Opcode op, Value* left, Value* right);
    // 标量广播运算（运行时循环）：scalar op vec，返回结果 vec alloca
    Value* emitVecScalarOp(Instruction::Opcode op, Value* scalar,
                           Value* vec, bool scalarOnLeft);
    // 向量逐元素拷贝（运行时循环）：dst = src
    void   emitVecCopy(Value* dst, Value* src);

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

    // 《前端+变长》 变长向量 alloca 侧集合：记录哪些 alloca 是 vec
    // （vec 类型与普通 [N x i32] 数组类型相同，靠此集合区分）
    std::unordered_set<Value*> vecAllocas;
};

} // namespace IR