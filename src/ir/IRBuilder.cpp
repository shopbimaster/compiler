#include "ir/IRBuilder.h"
#include "SysY2022Lexer.h"
#include "SysY2022Parser.h"
#include "antlr4-runtime.h"
#include <fstream>
#include <sstream>
#include <cassert>
#include <any>

using namespace antlr4;

namespace IR {

// ================================================================
// 工具宏：简化 std::any 操作
// ================================================================
static inline Value* valFrom(const std::any& a) {
    if (!a.has_value()) return nullptr;
    return std::any_cast<Value*>(a);
}

// ================================================================
// 构造
// ================================================================
IRBuilder::IRBuilder()
    : currentFunc(nullptr)
    , currentBB(nullptr)
    , tempCount(0)
    , blockCounter(0) {}

// ================================================================
// 主入口： .sy 文件 → IR Module
// ================================================================
std::unique_ptr<Module> IRBuilder::compile(const std::string& sourcePath) {
    std::ifstream stream(sourcePath);
    if (!stream.is_open()) {
        throw std::runtime_error("Cannot open file: " + sourcePath);
    }

    ANTLRInputStream input(stream);
    SysY2022Lexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    SysY2022Parser parser(&tokens);
    parser.setErrorHandler(std::make_shared<BailErrorStrategy>());

    SysY2022Parser::CompilationUnitContext* tree = parser.compilationUnit();
    visitCompilationUnit(tree);

    return std::move(module);
}

// ================================================================
// compilationUnit: (decl | funcDef)* EOF
// ================================================================
std::any IRBuilder::visitCompilationUnit(SysY2022Parser::CompilationUnitContext* ctx) {
    module = std::make_unique<Module>();

    for (auto* child : ctx->children) {
        if (dynamic_cast<SysY2022Parser::DeclContext*>(child)) {
            visitDecl(static_cast<SysY2022Parser::DeclContext*>(child));
        } else if (dynamic_cast<SysY2022Parser::FuncDefContext*>(child)) {
            visitFuncDef(static_cast<SysY2022Parser::FuncDefContext*>(child));
        }
    }

    return {};
}

// ================================================================
// decl: constDecl | varDecl
// ================================================================
std::any IRBuilder::visitDecl(SysY2022Parser::DeclContext* ctx) {
    if (ctx->constDecl()) {
        return visitConstDecl(ctx->constDecl());
    }
    return visitVarDecl(ctx->varDecl());
}

// ================================================================
// constDecl: CONST bType constDef (COMMA constDef)* SEMICOLON
// ================================================================
std::any IRBuilder::visitConstDecl(SysY2022Parser::ConstDeclContext* ctx) {
    Type* baseType = std::any_cast<Type*>(visitBType(ctx->bType()));
    for (auto* defCtx : ctx->constDef()) {
        std::string name = defCtx->IDENTIFIER()->getText();
        Value* initVal = valFrom(visitConstInitVal(defCtx->constInitVal()));

        auto* alloca = Instruction::createAlloca(baseType, name);
        currentBB->pushBack(alloca);

        if (initVal) {
            auto* store = Instruction::createStore(initVal, alloca);
            currentBB->pushBack(store);
        }
        declare(name, alloca);
    }
    return {};
}

// ================================================================
// bType: INT | FLOAT
// ================================================================
std::any IRBuilder::visitBType(SysY2022Parser::BTypeContext* ctx) {
    return std::any(toIRType(ctx->getText()));
}

// ================================================================
// varDecl: bType varDef (COMMA varDef)* SEMICOLON
// ================================================================
std::any IRBuilder::visitVarDecl(SysY2022Parser::VarDeclContext* ctx) {
    Type* baseType = std::any_cast<Type*>(visitBType(ctx->bType()));
    for (auto* defCtx : ctx->varDef()) {
        std::string name = defCtx->IDENTIFIER()->getText();

        auto* alloca = Instruction::createAlloca(baseType, name);
        currentBB->pushBack(alloca);

        if (defCtx->ASSIGN()) {
            Value* init = valFrom(visitInitVal(defCtx->initVal()));
            if (init) {
                auto* store = Instruction::createStore(init, alloca);
                currentBB->pushBack(store);
            }
        }
        declare(name, alloca);
    }
    return {};
}

// ================================================================
// varDef: IDENTIFIER (L_BRACKET constExp R_BRACKET)* [ASSIGN initVal]
// ================================================================
std::any IRBuilder::visitVarDef(SysY2022Parser::VarDefContext* ctx) {
    return {}; // handled in visitVarDecl for simplicity
}

// ================================================================
// initVal: exp | L_BRACE ...
// ================================================================
std::any IRBuilder::visitInitVal(SysY2022Parser::InitValContext* ctx) {
    if (ctx->exp()) {
        return visitExp(ctx->exp());
    }
    return {}; // aggregate init: TBD
}

// ================================================================
// funcDef: funcType IDENTIFIER LPAREN funcFParams? RPAREN block
// ================================================================
std::any IRBuilder::visitFuncDef(SysY2022Parser::FuncDefContext* ctx) {
    std::string name = ctx->IDENTIFIER()->getText();
    Type* retType = std::any_cast<Type*>(visitFuncType(ctx->funcType()));

    std::vector<Type*> paramTypes;
    if (ctx->funcFParams()) {
        const auto& paramList = ctx->funcFParams()->funcFParam();
        for (auto* param : paramList) {
            paramTypes.push_back(std::any_cast<Type*>(visitBType(param->bType())));
        }
    }

    auto* ft = FunctionType::get(retType, paramTypes);
    Function* func = module->createFunction(ft, name);
    currentFunc = func;

    enterScope();

    // Register arguments in symbol table
    if (ctx->funcFParams()) {
        const auto& paramList = ctx->funcFParams()->funcFParam();
        for (unsigned i = 0; i < paramList.size(); ++i) {
            auto* param = paramList[i];
            std::string pName = param->IDENTIFIER()->getText();
            Type* pType = std::any_cast<Type*>(visitBType(param->bType()));

            auto* alloca = Instruction::createAlloca(pType, pName);
            if (!currentFunc->getEntryBlock()) {
                currentFunc->createBlock("entry");
            }
            auto* savedBB = currentBB;
            currentBB = currentFunc->getEntryBlock();
            currentBB->pushBack(alloca);

            auto* store = Instruction::createStore(func->getArg(i), alloca);
            currentBB->pushBack(store);

            currentBB = savedBB;
            declare(pName, alloca);
        }
    }

    visitBlock(ctx->block());

    // Implicit return void if needed
    if (retType->isVoid() && (!currentBB || !currentBB->getTerminator())) {
        if (!currentBB) {
            currentFunc->createBlock("entry");
            currentBB = currentFunc->getEntryBlock();
        }
        createRetVoidInst();
    }

    exitScope();
    currentFunc = nullptr;
    currentBB = nullptr;
    return {};
}

// ================================================================
// funcType: VOID | INT | FLOAT
// ================================================================
std::any IRBuilder::visitFuncType(SysY2022Parser::FuncTypeContext* ctx) {
    return std::any(toIRType(ctx->getText()));
}

// ================================================================
// funcFParams: funcFParam (COMMA funcFParam)*
// ================================================================
std::any IRBuilder::visitFuncFParams(SysY2022Parser::FuncFParamsContext* ctx) {
    return {}; // param registration done in visitFuncDef
}

// ================================================================
// funcFParam: bType IDENTIFIER ...
// ================================================================
std::any IRBuilder::visitFuncFParam(SysY2022Parser::FuncFParamContext* ctx) {
    return {}; // handled in visitFuncDef
}

// ================================================================
// block: L_BRACE blockItem* R_BRACE
// ================================================================
std::any IRBuilder::visitBlock(SysY2022Parser::BlockContext* ctx) {
    currentBB = createBlock();
    enterScope();

    for (auto* item : ctx->blockItem()) {
        visitBlockItem(item);
    }

    exitScope();
    return {};
}

// ================================================================
// blockItem: decl | stmt
// ================================================================
std::any IRBuilder::visitBlockItem(SysY2022Parser::BlockItemContext* ctx) {
    if (ctx->decl()) return visitDecl(ctx->decl());
    if (ctx->stmt()) return visitStmt(ctx->stmt());
    return {};
}

// ================================================================
// stmt: (7 alternatives)
// ================================================================
std::any IRBuilder::visitStmt(SysY2022Parser::StmtContext* ctx) {
    // RETURN exp? SEMICOLON  (必须放在 exp? SEMICOLON 前面)
    if (ctx->RETURN()) {
        if (ctx->exp()) {
            Value* val = valFrom(visitExp(ctx->exp()));
            createRetInst(val);
        } else {
            createRetVoidInst();
        }
        return {};
    }

    // lVal ASSIGN exp SEMICOLON
    if (ctx->lVal() && ctx->ASSIGN()) {
        Value* lhsPtr = valFrom(visitLVal(ctx->lVal())); // get pointer (alloca)
        Value* rhs = valFrom(visitExp(ctx->exp()));
        if (lhsPtr && rhs) {
            auto* store = Instruction::createStore(rhs, lhsPtr);
            currentBB->pushBack(store);
        }
        return {};
    }

    // exp? SEMICOLON
    if (ctx->exp() && !ctx->lVal()) {
        visitExp(ctx->exp());
        return {};
    }

    // block
    if (ctx->block()) {
        return visitBlock(ctx->block());
    }

    // IF LPAREN cond RPAREN stmt (ELSE stmt)?
    if (ctx->IF()) {
        Value* condVal = valFrom(visitCond(ctx->cond()));
        BasicBlock* thenBB = createBlock("then");
        BasicBlock* elseBB = ctx->ELSE() ? createBlock("else") : createBlock("endif");
        BasicBlock* mergeBB = createBlock("merge");

        auto* condBr = Instruction::createCondBr(condVal, thenBB, elseBB);
        currentBB->pushBack(condBr);

        // then: 若 stmt 是 { block } 则直接展开到 thenBB
        currentBB = thenBB;
        if (ctx->stmt(0)->block()) {
            auto* bCtx = ctx->stmt(0)->block();
            enterScope();
            for (auto* item : bCtx->blockItem()) visitBlockItem(item);
            exitScope();
        } else {
            visitStmt(ctx->stmt(0));
        }
        if (!currentBB->getTerminator()) {
            auto* br = Instruction::createBr(mergeBB);
            currentBB->pushBack(br);
        }

        // else: 同样逻辑
        if (ctx->ELSE()) {
            currentBB = elseBB;
            if (ctx->stmt(1)->block()) {
                auto* bCtx = ctx->stmt(1)->block();
                enterScope();
                for (auto* item : bCtx->blockItem()) visitBlockItem(item);
                exitScope();
            } else {
                visitStmt(ctx->stmt(1));
            }
            if (!currentBB->getTerminator()) {
                auto* br = Instruction::createBr(mergeBB);
                currentBB->pushBack(br);
            }
        } else {
            auto* br = Instruction::createBr(mergeBB);
            elseBB->pushBack(br);
        }

        currentBB = mergeBB;
        return {};
    }

    // WHILE LPAREN cond RPAREN stmt
    if (ctx->WHILE()) {
        BasicBlock* condBB = createBlock("while_cond");
        BasicBlock* bodyBB = createBlock("while_body");
        BasicBlock* endBB  = createBlock("while_end");

        auto* entryBr = Instruction::createBr(condBB);
        currentBB->pushBack(entryBr);

        currentBB = condBB;
        Value* condVal = valFrom(visitCond(ctx->cond()));
        auto* condBr = Instruction::createCondBr(condVal, bodyBB, endBB);
        currentBB->pushBack(condBr);

        currentBB = bodyBB;
        if (ctx->stmt(0)->block()) {
            auto* bCtx = ctx->stmt(0)->block();
            enterScope();
            for (auto* item : bCtx->blockItem()) visitBlockItem(item);
            exitScope();
        } else {
            visitStmt(ctx->stmt(0));
        }
        if (!currentBB->getTerminator()) {
            auto* loopBr = Instruction::createBr(condBB);
            currentBB->pushBack(loopBr);
        }

        currentBB = endBB;
        return {};
    }

    // BREAK SEMICOLON | CONTINUE SEMICOLON
    if (ctx->BREAK() || ctx->CONTINUE()) {
        // TODO: break/continue requires tracking loop exit blocks
        return {};
    }

    // Empty statement (just SEMICOLON)
    return {};
}

// ================================================================
// exp → addExp
// ================================================================
std::any IRBuilder::visitExp(SysY2022Parser::ExpContext* ctx) {
    return visitAddExp(ctx->addExp());
}

// ================================================================
// cond → lOrExp
// ================================================================
std::any IRBuilder::visitCond(SysY2022Parser::CondContext* ctx) {
    return visitLOrExp(ctx->lOrExp());
}

// ================================================================
// lVal: IDENTIFIER (L_BRACKET exp R_BRACKET)*
// ================================================================
std::any IRBuilder::visitLVal(SysY2022Parser::LValContext* ctx) {
    std::string name = ctx->IDENTIFIER()->getText();
    Value* alloca = lookup(name);
    if (!alloca) {
        throw std::runtime_error("Undefined variable: " + name);
    }

    // For now: return pointer directly (store destination) or load (read)
    // When used in expression context, the caller should load
    // We use a flag: return the alloca pointer, loading is caller's job

    if (ctx->L_BRACKET().empty()) {
        // Simple variable: return alloca pointer
        return std::any(alloca);
    }

    // Array access TBD
    return std::any(alloca);
}

// ================================================================
// primaryExp: LPAREN exp RPAREN | lVal | number
// ================================================================
std::any IRBuilder::visitPrimaryExp(SysY2022Parser::PrimaryExpContext* ctx) {
    if (ctx->exp()) {
        return visitExp(ctx->exp());
    }
    if (ctx->lVal()) {
        Value* ptr = valFrom(visitLVal(ctx->lVal()));
        if (ptr) {
            // Load from alloca
            auto* load = Instruction::createLoad(
                static_cast<PointerType*>(ptr->getType())->getPointeeType(),
                ptr,
                newTempName()
            );
            currentBB->pushBack(load);
            return std::any(static_cast<Value*>(load));
        }
    }
    if (ctx->number()) {
        return visitNumber(ctx->number());
    }
    return std::any(static_cast<Value*>(nullptr));
}

// ================================================================
// number: INTCONST | FLOATCONST
// ================================================================
std::any IRBuilder::visitNumber(SysY2022Parser::NumberContext* ctx) {
    if (ctx->INTCONST()) {
        std::string text = ctx->INTCONST()->getText();
        int64_t val;
        if (text.size() > 1 && text[0] == '0') {
            if (text[1] == 'x' || text[1] == 'X') {
                val = std::stoll(text.substr(2), nullptr, 16);
            } else {
                val = std::stoll(text, nullptr, 8);
            }
        } else {
            val = std::stoll(text);
        }
        return std::any(static_cast<Value*>(ConstantInt::get(IntegerType::I32, val)));
    }
    if (ctx->FLOATCONST()) {
        std::string text = ctx->FLOATCONST()->getText();
        double val = std::stod(text);
        return std::any(static_cast<Value*>(ConstantFloat::get(FloatType::get(), val)));
    }
    return std::any(static_cast<Value*>(nullptr));
}

// ================================================================
// unaryExp: primaryExp | IDENTIFIER LPAREN funcRParams? RPAREN | unaryOp unaryExp
// ================================================================
std::any IRBuilder::visitUnaryExp(SysY2022Parser::UnaryExpContext* ctx) {
    // primaryExp
    if (ctx->primaryExp()) {
        return visitPrimaryExp(ctx->primaryExp());
    }

    // Function call: IDENTIFIER LPAREN funcRParams? RPAREN
    if (ctx->IDENTIFIER() && !ctx->unaryOp()) {
        std::string calleeName = ctx->IDENTIFIER()->getText();

        // Collect arguments
        std::vector<Value*> args;
        if (ctx->funcRParams()) {
            const auto& expList = ctx->funcRParams()->exp();
            for (auto* e : expList) {
                Value* arg = valFrom(visitExp(e));
                if (arg) args.push_back(arg);
            }
        }

        // Build param types for FunctionType
        std::vector<Type*> paramTypes;
        for (auto* a : args) paramTypes.push_back(a->getType());

        // Create or find function
        // For now: assume i32 return, later lookup from module
        auto* ft = FunctionType::get(IntegerType::I32, paramTypes);
        Function* callee = module->createFunction(ft, calleeName, true);

        auto* call = Instruction::createCall(ft, callee, args, newTempName());
        currentBB->pushBack(call);
        return std::any(static_cast<Value*>(call));
    }

    // Unary operator
    if (ctx->unaryOp()) {
        std::string op = ctx->unaryOp()->getText();
        Value* operand = valFrom(visitUnaryExp(ctx->unaryExp()));

        if (op == "-") {
            Value* zero = ConstantInt::get(IntegerType::I32, 0);
            auto* inst = Instruction::createBinOp(
                Instruction::Opcode::SUB, IntegerType::I32, newTempName(), zero, operand);
            currentBB->pushBack(inst);
            return std::any(static_cast<Value*>(inst));
        } else if (op == "!") {
            // !x → eq x, 0
            Value* zero = ConstantInt::get(IntegerType::I32, 0);
            auto* inst = Instruction::createCmp(
                Instruction::Opcode::ICMP, operand, zero, newTempName());
            currentBB->pushBack(inst);
            return std::any(static_cast<Value*>(inst));
        }
        // '+' is no-op
        return std::any(operand);
    }

    return std::any(static_cast<Value*>(nullptr));
}

// ================================================================
// mulExp: unaryExp | mulExp (* / %) unaryExp
// ================================================================
std::any IRBuilder::visitMulExp(SysY2022Parser::MulExpContext* ctx) {
    auto result = visit(ctx->children[0]);
    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        auto* opNode = ctx->children[i];
        auto right = visit(ctx->children[i + 1]);
        Value* left = valFrom(result);
        Value* rightVal = valFrom(right);

        Instruction::Opcode op;
        std::string opText = opNode->toString();
        if (dynamic_cast<tree::TerminalNode*>(opNode)) {
            opText = static_cast<tree::TerminalNode*>(opNode)->getSymbol()->getText();
        }
        if (opText == "*")      op = Instruction::Opcode::MUL;
        else if (opText == "/") op = Instruction::Opcode::SDIV;
        else                    op = Instruction::Opcode::SREM;

        auto* inst = Instruction::createBinOp(op, IntegerType::I32, newTempName(), left, rightVal);
        currentBB->pushBack(inst);
        result = std::any(static_cast<Value*>(inst));
        i += 2;
    }
    return result;
}

// ================================================================
// addExp: mulExp | addExp (+ -) mulExp
// ================================================================
std::any IRBuilder::visitAddExp(SysY2022Parser::AddExpContext* ctx) {
    auto result = visit(ctx->children[0]);
    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        auto* opNode = ctx->children[i];
        auto right = visit(ctx->children[i + 1]);
        Value* left = valFrom(result);
        Value* rightVal = valFrom(right);

        std::string opText;
        if (dynamic_cast<tree::TerminalNode*>(opNode)) {
            opText = static_cast<tree::TerminalNode*>(opNode)->getSymbol()->getText();
        }
        auto op = (opText == "+") ? Instruction::Opcode::ADD : Instruction::Opcode::SUB;

        auto* inst = Instruction::createBinOp(op, IntegerType::I32, newTempName(), left, rightVal);
        currentBB->pushBack(inst);
        result = std::any(static_cast<Value*>(inst));
        i += 2;
    }
    return result;
}

// ================================================================
// relExp: addExp | relExp (< > <= >=) addExp
// ================================================================
std::any IRBuilder::visitRelExp(SysY2022Parser::RelExpContext* ctx) {
    auto result = visit(ctx->children[0]);
    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        auto* opNode = ctx->children[i];
        auto right = visit(ctx->children[i + 1]);
        Value* left = valFrom(result);
        Value* rightVal = valFrom(right);

        std::string opText;
        if (dynamic_cast<tree::TerminalNode*>(opNode)) {
            opText = static_cast<tree::TerminalNode*>(opNode)->getSymbol()->getText();
        }
        Instruction::Opcode op;
        if (opText == "<")        op = Instruction::Opcode::ICMP;
        else if (opText == ">")   op = Instruction::Opcode::ICMP;
        else if (opText == "<=")  op = Instruction::Opcode::ICMP;
        else                      op = Instruction::Opcode::ICMP;
        // Note: condition code differentiation for < > <= >= handled in codegen

        auto* inst = Instruction::createBinOp(op, IntegerType::I32, newTempName(), left, rightVal);
        currentBB->pushBack(inst);
        result = std::any(static_cast<Value*>(inst));
        i += 2;
    }
    return result;
}

// ================================================================
// eqExp: relExp | eqExp (== !=) relExp
// ================================================================
std::any IRBuilder::visitEqExp(SysY2022Parser::EqExpContext* ctx) {
    auto result = visit(ctx->children[0]);
    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        auto* opNode = ctx->children[i];
        auto right = visit(ctx->children[i + 1]);
        Value* left = valFrom(result);
        Value* rightVal = valFrom(right);

        auto* inst = Instruction::createBinOp(
            Instruction::Opcode::ICMP, IntegerType::I32, newTempName(), left, rightVal);
        currentBB->pushBack(inst);
        result = std::any(static_cast<Value*>(inst));
        i += 2;
    }
    return result;
}

// ================================================================
// lAndExp: eqExp | lAndExp && eqExp  (short-circuit via branch)
// ================================================================
std::any IRBuilder::visitLAndExp(SysY2022Parser::LAndExpContext* ctx) {
    auto result = visit(ctx->children[0]);
    if (ctx->children.size() == 1) return result;

    // && with short-circuit: convert to phi at merge point
    size_t i = 1;
    while (i < ctx->children.size()) {
        Value* left = valFrom(result);
        BasicBlock* rhsBB = createBlock("and_rhs");
        BasicBlock* mergeBB = createBlock("and_merge");

        auto* condBr = Instruction::createCondBr(left, rhsBB, mergeBB);
        currentBB->pushBack(condBr);

        currentBB = rhsBB;
        auto right = visit(ctx->children[i + 1]);
        Value* rightVal = valFrom(right);
        auto* rhsBr = Instruction::createBr(mergeBB);
        currentBB->pushBack(rhsBr);

        currentBB = mergeBB;
        auto* phi = Instruction::createPhi(IntegerType::I32, "and_phi", 2);
        currentBB->pushBack(phi);
        phi->addOperand(ConstantInt::get(IntegerType::I32, 0));
        phi->addOperand(rightVal);
        result = std::any(static_cast<Value*>(phi));
        i += 2;
    }
    return result;
}

// ================================================================
// lOrExp: lAndExp | lOrExp || lAndExp  (short-circuit)
// ================================================================
std::any IRBuilder::visitLOrExp(SysY2022Parser::LOrExpContext* ctx) {
    auto result = visit(ctx->children[0]);
    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        Value* left = valFrom(result);
        BasicBlock* rhsBB = createBlock("or_rhs");
        BasicBlock* mergeBB = createBlock("or_merge");

        auto* condBr = Instruction::createCondBr(left, mergeBB, rhsBB);
        currentBB->pushBack(condBr);

        currentBB = rhsBB;
        auto right = visit(ctx->children[i + 1]);
        Value* rightVal = valFrom(right);
        auto* rhsBr = Instruction::createBr(mergeBB);
        currentBB->pushBack(rhsBr);

        currentBB = mergeBB;
        auto* phi = Instruction::createPhi(IntegerType::I32, "or_phi", 2);
        currentBB->pushBack(phi);
        phi->addOperand(ConstantInt::get(IntegerType::I32, 1));
        phi->addOperand(rightVal);
        result = std::any(static_cast<Value*>(phi));
        i += 2;
    }
    return result;
}

// ================================================================
// 测试辅助：int main() { return <returnValue>; }
// ================================================================
std::unique_ptr<Module> IRBuilder::buildSimpleMain(int64_t returnValue) {
    module = std::make_unique<Module>();

    Function* mainFunc = module->createFunction(
        FunctionType::get(IntegerType::I32, {}), "main");
    currentFunc = mainFunc;
    currentBB = createBlock("entry");

    Value* constVal = ConstantInt::get(IntegerType::I32, returnValue);
    Instruction* ret = Instruction::createRet(constVal);
    currentBB->pushBack(ret);

    currentFunc = nullptr;
    currentBB = nullptr;
    return std::move(module);
}

// ================================================================
// 辅助方法
// ================================================================
Function* IRBuilder::createFunction(const std::string& name, Type* retTy,
                                     const std::vector<Type*>& paramTys) {
    auto* ft = FunctionType::get(retTy, paramTys);
    return module->createFunction(ft, name);
}

BasicBlock* IRBuilder::createBlock(const std::string& name) {
    assert(currentFunc && "no current function");
    std::string blockName = name.empty()
        ? "bb" + std::to_string(blockCounter++)
        : name + "_" + std::to_string(blockCounter++);
    return currentFunc->createBlock(blockName);
}

Instruction* IRBuilder::createRetInst(Value* val) {
    assert(currentBB && "no current block");
    auto* inst = Instruction::createRet(val);
    currentBB->pushBack(inst);
    return inst;
}

Instruction* IRBuilder::createRetVoidInst() {
    assert(currentBB && "no current block");
    auto* inst = Instruction::createRet(nullptr);
    currentBB->pushBack(inst);
    return inst;
}

void IRBuilder::enterScope() {
    scopeStack.emplace_back();
}

void IRBuilder::exitScope() {
    assert(!scopeStack.empty());
    scopeStack.pop_back();
}

void IRBuilder::declare(const std::string& name, Value* val) {
    assert(!scopeStack.empty());
    scopeStack.back()[name] = val;
}

Value* IRBuilder::lookup(const std::string& name) {
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return nullptr;
}

std::string IRBuilder::newTempName() {
    return "t" + std::to_string(tempCount++);
}

Type* IRBuilder::toIRType(const std::string& sysyType) {
    if (sysyType == "int")   return IntegerType::I32;
    if (sysyType == "float") return FloatType::get();
    if (sysyType == "void")  return VoidType::get();
    return IntegerType::I32;
}

} // namespace IR