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
    enterScope();
    registerBuiltinFunctions();

    for (auto* child : ctx->children) {
        if (dynamic_cast<SysY2022Parser::DeclContext*>(child)) {
            visitDecl(static_cast<SysY2022Parser::DeclContext*>(child));
        } else if (dynamic_cast<SysY2022Parser::FuncDefContext*>(child)) {
            visitFuncDef(static_cast<SysY2022Parser::FuncDefContext*>(child));
        }
    }

    exitScope();
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

        // Compute array dimensions
        Type* varType = baseType;
        std::vector<int> dimensions;
        for (size_t i = 0; i < defCtx->L_BRACKET().size(); ++i) {
            Value* dimVal = valFrom(visitConstExp(defCtx->constExp(i)));
            int dim = static_cast<int>(static_cast<ConstantInt*>(dimVal)->getValue());
            dimensions.push_back(dim);
        }
        for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
            varType = ArrayType::get(varType, *it);
        }

        if (currentFunc == nullptr) {
            // ===== 全局常量 =====
            Constant* init = nullptr;
            if (defCtx->constInitVal()->constExp()) {
                Value* iv = valFrom(visitConstExp(defCtx->constInitVal()->constExp()));
                init = dynamic_cast<Constant*>(iv);
            }
            auto* gv = module->createGlobalVariable(
                PointerType::get(varType), name, true, init);
            declare(name, gv);
        } else {
            // ===== 局部常量 =====
            auto* alloca = Instruction::createAlloca(varType, name);
            currentBB->pushBack(alloca);

            auto* initCtx = defCtx->constInitVal();
            if (initCtx->constExp()) {
                Value* initVal = valFrom(visitConstExp(initCtx->constExp()));
                if (initVal) {
                    auto* store = Instruction::createStore(initVal, alloca);
                    currentBB->pushBack(store);
                }
            } else if (!initCtx->constInitVal().empty()) {
                // Aggregate init for const array — emit GEP+store recursively
                auto initChildren = initCtx->constInitVal();
                std::vector<SysY2022Parser::ConstInitValContext*> childVec(
                    initChildren.begin(), initChildren.end());
                std::vector<Value*> baseIndices;
                baseIndices.push_back(ConstantInt::get(IntegerType::I32, 0));
                int flatIdx = 0;
                emitInitStoresConst(varType, alloca, baseIndices, childVec, flatIdx);
            }
            declare(name, alloca);
        }
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

        // Compute array dimensions (if any)
        Type* varType = baseType;
        std::vector<int> dimensions;
        for (size_t i = 0; i < defCtx->L_BRACKET().size(); ++i) {
            Value* dimVal = valFrom(visitConstExp(defCtx->constExp(i)));
            int dim = static_cast<int>(static_cast<ConstantInt*>(dimVal)->getValue());
            dimensions.push_back(dim);
        }
        // Wrap from right to left: int a[2][3] → ArrayType(ArrayType(I32, 3), 2)
        for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
            varType = ArrayType::get(varType, *it);
        }

        if (currentFunc == nullptr) {
            // ===== 全局变量 =====
            Constant* init = nullptr;
            if (defCtx->ASSIGN() && defCtx->initVal()) {
                auto* initCtx = defCtx->initVal();
                if (initCtx->exp()) {
                    Value* iv = constEval(initCtx->exp()->addExp());
                    if (!iv) {
                        iv = valFrom(visitInitVal(defCtx->initVal()));
                    }
                    init = dynamic_cast<Constant*>(iv);
                }
            }
            auto* gv = module->createGlobalVariable(
                PointerType::get(varType), name, false, init);
            declare(name, gv);
        } else {
            // ===== 局部变量 =====
            auto* alloca = Instruction::createAlloca(varType, name);
            currentBB->pushBack(alloca);

            if (defCtx->ASSIGN() && defCtx->initVal()) {
                auto* initCtx = defCtx->initVal();
                if (initCtx->exp()) {
                    // Scalar init
                    Value* init = valFrom(visitExp(initCtx->exp()));
                    if (init) {
                        auto* store = Instruction::createStore(init, alloca);
                        currentBB->pushBack(store);
                    }
                } else {
                    // Aggregate init: emit GEP+store recursively
                    auto children = initCtx->initVal();
                    std::vector<SysY2022Parser::InitValContext*> childVec(
                        children.begin(), children.end());
                    std::vector<Value*> baseIndices;
                    baseIndices.push_back(ConstantInt::get(IntegerType::I32, 0));
                    int flatIdx = 0;
                    emitInitStoresVar(varType, alloca, baseIndices, childVec, flatIdx);
                }
            }
            declare(name, alloca);
        }
    }
    return {};
}

// ================================================================
// funcDef: funcType IDENTIFIER (L_BRACKET constExp R_BRACKET)* [ASSIGN initVal]
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
            Type* pType = std::any_cast<Type*>(visitBType(param->bType()));
            if (!param->L_BRACKET().empty()) {
                pType = PointerType::get(pType);
            }
            paramTypes.push_back(pType);
        }
    }

    auto* ft = FunctionType::get(retType, paramTypes);
    Function* func = module->createFunction(ft, name);
    funcTypeTable[name] = ft;
    currentFunc = func;

    enterScope();

    // Register arguments in symbol table
    if (ctx->funcFParams()) {
        const auto& paramList = ctx->funcFParams()->funcFParam();
        for (unsigned i = 0; i < paramList.size(); ++i) {
            auto* param = paramList[i];
            std::string pName = param->IDENTIFIER()->getText();
            Type* pType = std::any_cast<Type*>(visitBType(param->bType()));
            if (!param->L_BRACKET().empty()) {
                pType = PointerType::get(pType);
            }

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

        loopStack.push_back({condBB, endBB});

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

        loopStack.pop_back();
        currentBB = endBB;
        return {};
    }

    // BREAK SEMICOLON | CONTINUE SEMICOLON
    if (ctx->BREAK() || ctx->CONTINUE()) {
        if (loopStack.empty()) {
            throw std::runtime_error("break/continue not in loop");
        }
        if (ctx->BREAK()) {
            auto* br = Instruction::createBr(loopStack.back().breakBB);
            currentBB->pushBack(br);
        } else {
            auto* br = Instruction::createBr(loopStack.back().continueBB);
            currentBB->pushBack(br);
        }
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
    Value* ptr = lookup(name);
    if (!ptr) {
        throw std::runtime_error("Undefined variable: " + name);
    }

    if (ctx->L_BRACKET().empty()) {
        return std::any(ptr);
    }

    // Array access: compute GETELEMENTPTR for each index
    PointerType* ptrTy = static_cast<PointerType*>(ptr->getType());
    Type* pointee = ptrTy->getPointeeType();

    // If this is an array parameter (alloca stores a pointer), load it first
    if (pointee->isPointer()) {
        auto* paramLoad = Instruction::createLoad(pointee, ptr, newTempName());
        currentBB->pushBack(paramLoad);
        ptr = paramLoad;
        pointee = static_cast<PointerType*>(ptr->getType())->getPointeeType();
    }

    std::vector<Value*> indices;
    indices.push_back(ConstantInt::get(IntegerType::I32, 0));

    for (size_t i = 0; i < ctx->L_BRACKET().size(); ++i) {
        Value* idx = valFrom(visitExp(ctx->exp(i)));
        indices.push_back(idx);
    }

    auto* gep = Instruction::createGetElementPtr(pointee, ptr, indices, newTempName());
    currentBB->pushBack(gep);
    return std::any(static_cast<Value*>(gep));
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
            PointerType* ptrTy = static_cast<PointerType*>(ptr->getType());
            Type* pointee = ptrTy->getPointeeType();
            if (pointee->isArray()) {
                // Array decay to pointer to first element
                std::vector<Value*> indices;
                indices.push_back(ConstantInt::get(IntegerType::I32, 0));
                indices.push_back(ConstantInt::get(IntegerType::I32, 0));
                auto* gep = Instruction::createGetElementPtr(pointee, ptr, indices, newTempName());
                currentBB->pushBack(gep);
                return std::any(static_cast<Value*>(gep));
            }
            auto* load = Instruction::createLoad(pointee, ptr, newTempName());
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

        std::vector<Value*> args;
        std::vector<Type*> paramTypes;
        if (ctx->funcRParams()) {
            const auto& expList = ctx->funcRParams()->exp();
            for (auto* e : expList) {
                Value* arg = valFrom(visitExp(e));
                if (arg) {
                    args.push_back(arg);
                    paramTypes.push_back(arg->getType());
                }
            }
        }

        FunctionType* ft = nullptr;
        auto it = funcTypeTable.find(calleeName);
        if (it != funcTypeTable.end()) {
            ft = it->second;
        } else {
            ft = FunctionType::get(IntegerType::I32, paramTypes);
            funcTypeTable[calleeName] = ft;
        }

        Function* callee = module->createFunction(ft, calleeName, true);

        if (ft->getReturnType()->isVoid()) {
            auto* call = Instruction::createCall(ft, callee, args, "");
            currentBB->pushBack(call);
            return std::any(static_cast<Value*>(nullptr));
        }

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

        auto* inst = Instruction::createBinOp(op, left->getType(), newTempName(), left, rightVal);
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

        auto* inst = Instruction::createBinOp(op, left->getType(), newTempName(), left, rightVal);
        currentBB->pushBack(inst);
        result = std::any(static_cast<Value*>(inst));
        i += 2;
    }
    return result;
}

// ================================================================
// constExp: exp (in constant context) → addExp
// ================================================================
std::any IRBuilder::visitConstExp(SysY2022Parser::ConstExpContext* ctx) {
    Value* folded = constEval(ctx->addExp());
    if (folded) return std::any(folded);
    return visitAddExp(ctx->addExp());
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

        auto* inst = Instruction::createBinOp(op, IntegerType::I1, newTempName(), left, rightVal);
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
            Instruction::Opcode::ICMP, IntegerType::I1, newTempName(), left, rightVal);
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

// ================================================================
// 内置运行时函数注册（SysY I/O 函数）
// ================================================================
void IRBuilder::registerBuiltinFunctions() {
    auto* i32 = IntegerType::I32;
    auto* flt = FloatType::get();
    auto* vd  = VoidType::get();

    // int getint()
    funcTypeTable["getint"] = FunctionType::get(i32, {});
    module->createFunction(FunctionType::get(i32, {}), "getint", true);

    // int getch()
    funcTypeTable["getch"] = FunctionType::get(i32, {});
    module->createFunction(FunctionType::get(i32, {}), "getch", true);

    // int getarray(int a[])
    funcTypeTable["getarray"] = FunctionType::get(i32, {PointerType::get(i32)});
    module->createFunction(FunctionType::get(i32, {PointerType::get(i32)}), "getarray", true);

    // float getfloat()
    funcTypeTable["getfloat"] = FunctionType::get(flt, {});
    module->createFunction(FunctionType::get(flt, {}), "getfloat", true);

    // int getfarray(float a[])
    funcTypeTable["getfarray"] = FunctionType::get(i32, {PointerType::get(flt)});
    module->createFunction(FunctionType::get(i32, {PointerType::get(flt)}), "getfarray", true);

    // void putint(int x)
    funcTypeTable["putint"] = FunctionType::get(vd, {i32});
    module->createFunction(FunctionType::get(vd, {i32}), "putint", true);

    // void putch(int x)
    funcTypeTable["putch"] = FunctionType::get(vd, {i32});
    module->createFunction(FunctionType::get(vd, {i32}), "putch", true);

    // void putarray(int n, int a[])
    funcTypeTable["putarray"] = FunctionType::get(vd, {i32, PointerType::get(i32)});
    module->createFunction(FunctionType::get(vd, {i32, PointerType::get(i32)}), "putarray", true);

    // void putfloat(float x)
    funcTypeTable["putfloat"] = FunctionType::get(vd, {flt});
    module->createFunction(FunctionType::get(vd, {flt}), "putfloat", true);

    // void putfarray(int n, float a[])
    funcTypeTable["putfarray"] = FunctionType::get(vd, {i32, PointerType::get(flt)});
    module->createFunction(FunctionType::get(vd, {i32, PointerType::get(flt)}), "putfarray", true);

    // void starttime()
    funcTypeTable["starttime"] = FunctionType::get(vd, {});
    module->createFunction(FunctionType::get(vd, {}), "starttime", true);

    // void stoptime()
    funcTypeTable["stoptime"] = FunctionType::get(vd, {});
    module->createFunction(FunctionType::get(vd, {}), "stoptime", true);
}

// ================================================================
// 数组初始化辅助: 递归 emit GEP+store
// ================================================================
void IRBuilder::emitInitStoresVar(Type* targetType, Value* basePtr,
                                  std::vector<Value*>& indices,
                                  const std::vector<SysY2022Parser::InitValContext*>& children,
                                  int& flatIdx) {
    if (auto* arrTy = dynamic_cast<ArrayType*>(targetType)) {
        Type* elemType = arrTy->getElementType();
        unsigned total = arrTy->getNumElements();
        unsigned pos = 0;

        for (auto* child : children) {
            if (pos >= total) break;

            if (child->exp()) {
                Value* val = valFrom(visitExp(child->exp()));
                if (!val) val = zeroForType(elemType);
                std::vector<Value*> gepIndices = indices;
                gepIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(flatIdx)));
                auto* gep = Instruction::createGetElementPtr(targetType, basePtr, gepIndices, newTempName());
                currentBB->pushBack(gep);
                auto* store = Instruction::createStore(val, gep);
                currentBB->pushBack(store);
                flatIdx++;
                pos++;
            } else {
                auto subChildren = child->initVal();
                if (elemType->isArray()) {
                    std::vector<Value*> subIndices = indices;
                    subIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(pos)));
                    std::vector<SysY2022Parser::InitValContext*> subVec(
                        subChildren.begin(), subChildren.end());
                    int subIdx = 0;
                    if (!subVec.empty()) {
                        emitInitStoresVar(elemType, basePtr, subIndices, subVec, subIdx);
                    }
                    auto* subArrTy = static_cast<ArrayType*>(elemType);
                    unsigned subTotal = subArrTy->getNumElements();
                    // Zero-fill remaining sub-array elements
                    while (subIdx < static_cast<int>(subTotal)) {
                        Value* zv = zeroForType(subArrTy->getElementType());
                        std::vector<Value*> zpIndices = subIndices;
                        zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(subIdx)));
                        auto* zgep = Instruction::createGetElementPtr(elemType, basePtr, zpIndices, newTempName());
                        currentBB->pushBack(zgep);
                        auto* zstore = Instruction::createStore(zv, zgep);
                        currentBB->pushBack(zstore);
                        subIdx++;
                    }
                    pos++;
                    flatIdx = 0;
                } else {
                    // Scalar element type with brace init: {expr} takes first value
                    if (!subChildren.empty() && subChildren[0]->exp()) {
                        Value* val = valFrom(visitExp(subChildren[0]->exp()));
                        if (!val) val = zeroForType(elemType);
                        std::vector<Value*> gepIndices = indices;
                        gepIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(flatIdx)));
                        auto* gep = Instruction::createGetElementPtr(targetType, basePtr, gepIndices, newTempName());
                        currentBB->pushBack(gep);
                        auto* store = Instruction::createStore(val, gep);
                        currentBB->pushBack(store);
                    }
                    flatIdx++;
                    pos++;
                }
            }
        }
        // Zero-fill remaining top-level elements
        while (pos < total) {
            if (elemType->isArray()) {
                auto* subArrTy = static_cast<ArrayType*>(elemType);
                for (unsigned j = 0; j < static_cast<unsigned>(subArrTy->getNumElements()); ++j) {
                    Value* zv = zeroForType(subArrTy->getElementType());
                    std::vector<Value*> zpIndices = indices;
                    zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(pos)));
                    zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(j)));
                    auto* zgep = Instruction::createGetElementPtr(targetType, basePtr, zpIndices, newTempName());
                    currentBB->pushBack(zgep);
                    auto* zstore = Instruction::createStore(zv, zgep);
                    currentBB->pushBack(zstore);
                }
            } else {
                Value* zv = zeroForType(elemType);
                std::vector<Value*> zpIndices = indices;
                zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(flatIdx)));
                auto* zgep = Instruction::createGetElementPtr(targetType, basePtr, zpIndices, newTempName());
                currentBB->pushBack(zgep);
                auto* zstore = Instruction::createStore(zv, zgep);
                currentBB->pushBack(zstore);
                flatIdx++;
            }
            pos++;
        }
    }
}

void IRBuilder::emitInitStoresConst(Type* targetType, Value* basePtr,
                                    std::vector<Value*>& indices,
                                    const std::vector<SysY2022Parser::ConstInitValContext*>& children,
                                    int& flatIdx) {
    if (auto* arrTy = dynamic_cast<ArrayType*>(targetType)) {
        Type* elemType = arrTy->getElementType();
        unsigned total = arrTy->getNumElements();
        unsigned pos = 0;

        for (auto* child : children) {
            if (pos >= total) break;

            if (child->constExp()) {
                Value* val = valFrom(visitConstExp(child->constExp()));
                if (!val) val = zeroForType(elemType);
                std::vector<Value*> gepIndices = indices;
                gepIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(flatIdx)));
                auto* gep = Instruction::createGetElementPtr(targetType, basePtr, gepIndices, newTempName());
                currentBB->pushBack(gep);
                auto* store = Instruction::createStore(val, gep);
                currentBB->pushBack(store);
                flatIdx++;
                pos++;
            } else {
                auto subChildren = child->constInitVal();
                if (elemType->isArray()) {
                    std::vector<Value*> subIndices = indices;
                    subIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(pos)));
                    std::vector<SysY2022Parser::ConstInitValContext*> subVec(
                        subChildren.begin(), subChildren.end());
                    int subIdx = 0;
                    if (!subVec.empty()) {
                        emitInitStoresConst(elemType, basePtr, subIndices, subVec, subIdx);
                    }
                    auto* subArrTy = static_cast<ArrayType*>(elemType);
                    unsigned subTotal = subArrTy->getNumElements();
                    while (subIdx < static_cast<int>(subTotal)) {
                        Value* zv = zeroForType(subArrTy->getElementType());
                        std::vector<Value*> zpIndices = subIndices;
                        zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(subIdx)));
                        auto* zgep = Instruction::createGetElementPtr(elemType, basePtr, zpIndices, newTempName());
                        currentBB->pushBack(zgep);
                        auto* zstore = Instruction::createStore(zv, zgep);
                        currentBB->pushBack(zstore);
                        subIdx++;
                    }
                    pos++;
                    flatIdx = 0;
                } else {
                    if (!subChildren.empty() && subChildren[0]->constExp()) {
                        Value* val = valFrom(visitConstExp(subChildren[0]->constExp()));
                        if (!val) val = zeroForType(elemType);
                        std::vector<Value*> gepIndices = indices;
                        gepIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(flatIdx)));
                        auto* gep = Instruction::createGetElementPtr(targetType, basePtr, gepIndices, newTempName());
                        currentBB->pushBack(gep);
                        auto* store = Instruction::createStore(val, gep);
                        currentBB->pushBack(store);
                    }
                    flatIdx++;
                    pos++;
                }
            }
        }
        while (pos < total) {
            if (elemType->isArray()) {
                auto* subArrTy = static_cast<ArrayType*>(elemType);
                for (unsigned j = 0; j < static_cast<unsigned>(subArrTy->getNumElements()); ++j) {
                    Value* zv = zeroForType(subArrTy->getElementType());
                    std::vector<Value*> zpIndices = indices;
                    zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(pos)));
                    zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(j)));
                    auto* zgep = Instruction::createGetElementPtr(targetType, basePtr, zpIndices, newTempName());
                    currentBB->pushBack(zgep);
                    auto* zstore = Instruction::createStore(zv, zgep);
                    currentBB->pushBack(zstore);
                }
            } else {
                Value* zv = zeroForType(elemType);
                std::vector<Value*> zpIndices = indices;
                zpIndices.push_back(ConstantInt::get(IntegerType::I32, static_cast<int64_t>(flatIdx)));
                auto* zgep = Instruction::createGetElementPtr(targetType, basePtr, zpIndices, newTempName());
                currentBB->pushBack(zgep);
                auto* zstore = Instruction::createStore(zv, zgep);
                currentBB->pushBack(zstore);
                flatIdx++;
            }
            pos++;
        }
    }
}

Value* IRBuilder::zeroForType(Type* ty) {
    if (ty->isInteger()) return ConstantInt::get(static_cast<IntegerType*>(ty), 0);
    if (ty->isFloat())   return ConstantFloat::get(static_cast<FloatType*>(ty), 0.0);
    return ConstantInt::get(IntegerType::I32, 0);
}

// ================================================================
// 常数表达式编译期求值 (constant folding)
// ================================================================
Value* IRBuilder::constFoldBinOp(Instruction::Opcode op, Value* left, Value* right) {
    auto* lci = dynamic_cast<ConstantInt*>(left);
    auto* rci = dynamic_cast<ConstantInt*>(right);
    if (lci && rci) {
        IntegerType* ty = static_cast<IntegerType*>(lci->getType());
        int64_t lv = lci->getValue(), rv = rci->getValue();
        switch (op) {
        case Instruction::Opcode::ADD:  return ConstantInt::get(ty, lv + rv);
        case Instruction::Opcode::SUB:  return ConstantInt::get(ty, lv - rv);
        case Instruction::Opcode::MUL:  return ConstantInt::get(ty, lv * rv);
        case Instruction::Opcode::SDIV:
            return ConstantInt::get(ty, rv != 0 ? lv / rv : 0);
        case Instruction::Opcode::SREM:
            return ConstantInt::get(ty, rv != 0 ? lv % rv : 0);
        default: return nullptr;
        }
    }

    auto* lcf = dynamic_cast<ConstantFloat*>(left);
    auto* rcf = dynamic_cast<ConstantFloat*>(right);
    if (lcf && rcf) {
        double lv = lcf->getValue(), rv = rcf->getValue();
        switch (op) {
        case Instruction::Opcode::ADD:  return ConstantFloat::get(FloatType::get(), lv + rv);
        case Instruction::Opcode::SUB:  return ConstantFloat::get(FloatType::get(), lv - rv);
        case Instruction::Opcode::MUL:  return ConstantFloat::get(FloatType::get(), lv * rv);
        case Instruction::Opcode::SDIV:
            return ConstantFloat::get(FloatType::get(), rv != 0.0 ? lv / rv : 0.0);
        case Instruction::Opcode::SREM: return nullptr;
        default: return nullptr;
        }
    }

    if (lcf && rci) {
        double lv = lcf->getValue(), rv = static_cast<double>(rci->getValue());
        switch (op) {
        case Instruction::Opcode::ADD:  return ConstantFloat::get(FloatType::get(), lv + rv);
        case Instruction::Opcode::SUB:  return ConstantFloat::get(FloatType::get(), lv - rv);
        case Instruction::Opcode::MUL:  return ConstantFloat::get(FloatType::get(), lv * rv);
        case Instruction::Opcode::SDIV:
            return ConstantFloat::get(FloatType::get(), rv != 0.0 ? lv / rv : 0.0);
        default: return nullptr;
        }
    }

    if (lci && rcf) {
        double lv = static_cast<double>(lci->getValue()), rv = rcf->getValue();
        switch (op) {
        case Instruction::Opcode::ADD:  return ConstantFloat::get(FloatType::get(), lv + rv);
        case Instruction::Opcode::SUB:  return ConstantFloat::get(FloatType::get(), lv - rv);
        case Instruction::Opcode::MUL:  return ConstantFloat::get(FloatType::get(), lv * rv);
        case Instruction::Opcode::SDIV:
            return ConstantFloat::get(FloatType::get(), rv != 0.0 ? lv / rv : 0.0);
        default: return nullptr;
        }
    }

    return nullptr;
}

Value* IRBuilder::constEval(SysY2022Parser::AddExpContext* ctx) {
    Value* result;
    if (auto* subAdd = dynamic_cast<SysY2022Parser::AddExpContext*>(ctx->children[0]))
        result = constEval(subAdd);
    else
        result = constEvalMul(dynamic_cast<SysY2022Parser::MulExpContext*>(ctx->children[0]));

    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        auto* opNode = ctx->children[i];
        auto* rightCtx = dynamic_cast<SysY2022Parser::MulExpContext*>(ctx->children[i + 1]);
        Value* rightVal = constEvalMul(rightCtx);
        if (!result || !rightVal) return nullptr;

        std::string opText;
        if (auto* tn = dynamic_cast<antlr4::tree::TerminalNode*>(opNode))
            opText = tn->getSymbol()->getText();

        Instruction::Opcode op;
        if (opText == "+")      op = Instruction::Opcode::ADD;
        else                    op = Instruction::Opcode::SUB;

        result = constFoldBinOp(op, result, rightVal);
        if (!result) return nullptr;
        i += 2;
    }
    return result;
}

Value* IRBuilder::constEvalMul(SysY2022Parser::MulExpContext* ctx) {
    Value* result;
    if (auto* subMul = dynamic_cast<SysY2022Parser::MulExpContext*>(ctx->children[0]))
        result = constEvalMul(subMul);
    else
        result = constEvalUnary(dynamic_cast<SysY2022Parser::UnaryExpContext*>(ctx->children[0]));

    if (ctx->children.size() == 1) return result;

    size_t i = 1;
    while (i < ctx->children.size()) {
        auto* opNode = ctx->children[i];
        auto* rightCtx = dynamic_cast<SysY2022Parser::UnaryExpContext*>(ctx->children[i + 1]);
        Value* rightVal = constEvalUnary(rightCtx);
        if (!result || !rightVal) return nullptr;

        std::string opText;
        if (auto* tn = dynamic_cast<antlr4::tree::TerminalNode*>(opNode))
            opText = tn->getSymbol()->getText();

        Instruction::Opcode op;
        if (opText == "*")      op = Instruction::Opcode::MUL;
        else if (opText == "/") op = Instruction::Opcode::SDIV;
        else                    op = Instruction::Opcode::SREM;

        result = constFoldBinOp(op, result, rightVal);
        if (!result) return nullptr;
        i += 2;
    }
    return result;
}

Value* IRBuilder::constEvalUnary(SysY2022Parser::UnaryExpContext* ctx) {
    if (ctx->primaryExp()) {
        return constEvalPrimary(ctx->primaryExp());
    }
    if (ctx->unaryExp()) {
        auto* opNode = ctx->children[0];
        std::string opText;
        if (auto* tn = dynamic_cast<antlr4::tree::TerminalNode*>(opNode))
            opText = tn->getSymbol()->getText();

        Value* val = constEvalUnary(ctx->unaryExp());
        if (!val) return nullptr;

        if (opText == "-") {
            if (auto* ci = dynamic_cast<ConstantInt*>(val))
                return ConstantInt::get(static_cast<IntegerType*>(ci->getType()), -ci->getValue());
            if (auto* cf = dynamic_cast<ConstantFloat*>(val))
                return ConstantFloat::get(FloatType::get(), -cf->getValue());
            return nullptr;
        }
        if (opText == "!") {
            if (auto* ci = dynamic_cast<ConstantInt*>(val))
                return ConstantInt::get(static_cast<IntegerType*>(ci->getType()), ci->getValue() ? 0 : 1);
            if (auto* cf = dynamic_cast<ConstantFloat*>(val))
                return ConstantInt::get(IntegerType::I32, cf->getValue() != 0.0 ? 0 : 1);
            return nullptr;
        }
        return val;
    }
    if (ctx->IDENTIFIER()) return nullptr; // function call
    return nullptr;
}

Value* IRBuilder::constEvalPrimary(SysY2022Parser::PrimaryExpContext* ctx) {
    if (ctx->number()) return valFrom(visitNumber(ctx->number()));
    if (ctx->exp())
        return constEval(dynamic_cast<SysY2022Parser::AddExpContext*>(ctx->exp()->children[0]));
    if (ctx->lVal()) {
        std::string name = ctx->lVal()->IDENTIFIER()->getText();
        Value* sym = lookup(name);
        if (!sym) return nullptr;
        if (auto* gv = dynamic_cast<GlobalVariable*>(sym)) {
            if (gv->isConstant() && gv->getInitializer())
                return gv->getInitializer();
        }
    }
    return nullptr;
}

} // namespace IR