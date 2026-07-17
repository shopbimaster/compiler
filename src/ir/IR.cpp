#include "ir/IR.h"
#include <sstream>
#include <cassert>

namespace IR {

    // ================================================================
    // Type 系统实现 —— 全局单例 + 缓存
    // ================================================================

    // 初始化静态成员
    IntegerType* IntegerType::I1  = nullptr;
    IntegerType* IntegerType::I8  = nullptr;
    IntegerType* IntegerType::I32 = nullptr;

    namespace {
        std::unordered_map<unsigned, std::unique_ptr<IntegerType>>  intTypes;
        std::unique_ptr<VoidType>                                   voidType;
        std::unique_ptr<LabelType>                                  labelType;
        std::unique_ptr<FloatType>                                  floatType;
        std::unordered_map<Type*, std::unique_ptr<PointerType>>     ptrTypes;
        struct ArrayKey { Type* elem; unsigned n; };

        struct TypeInitializer {
            TypeInitializer() {
                IntegerType::get(1);
                IntegerType::get(8);
                IntegerType::get(32);
            }
        };
        static TypeInitializer __type_init;
    }

    VoidType* VoidType::get() {
        if (!voidType) voidType = std::make_unique<VoidType>();
        return voidType.get();
    }

    LabelType* LabelType::get() {
        if (!labelType) labelType = std::make_unique<LabelType>();
        return labelType.get();
    }

    IntegerType* IntegerType::get(unsigned bitWidth) {
        auto& p = intTypes[bitWidth];
        if (!p) {
            p = std::make_unique<IntegerType>(bitWidth);
            if (bitWidth == 1)  I1  = p.get();
            if (bitWidth == 8)  I8  = p.get();
            if (bitWidth == 32) I32 = p.get();
        }
        return p.get();
    }

    std::string IntegerType::toString() const {
        return "i" + std::to_string(bitWidth);
    }

    FloatType* FloatType::get() {
        if (!floatType) floatType = std::make_unique<FloatType>();
        return floatType.get();
    }

    PointerType* PointerType::get(Type* pointee) {
        auto& p = ptrTypes[pointee];
        if (!p) p = std::make_unique<PointerType>(pointee);
        return p.get();
    }

    std::string PointerType::toString() const {
        return pointee->toString() + "*";
    }

    ArrayType* ArrayType::get(Type* elem, unsigned numElem) {
        static std::vector<std::unique_ptr<ArrayType>> cache;
        for (auto& a : cache) {
            if (a->getElementType() == elem && a->getNumElements() == numElem)
                return a.get();
        }
        cache.push_back(std::make_unique<ArrayType>(elem, numElem));
        return cache.back().get();
    }

    std::string ArrayType::toString() const {
        return "[" + std::to_string(numElements) + " x " + elemType->toString() + "]";
    }

    FunctionType* FunctionType::get(Type* ret, const std::vector<Type*>& params) {
        static std::vector<std::unique_ptr<FunctionType>> cache;
        for (auto& f : cache) {
            if (f->getReturnType() == ret && f->getParamTypes() == params)
                return f.get();
        }
        cache.push_back(std::make_unique<FunctionType>(ret, params));
        return cache.back().get();
    }

    std::string FunctionType::toString() const {
        std::ostringstream oss;
        oss << returnType->toString() << " (";
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << paramTypes[i]->toString();
        }
        oss << ")";
        return oss.str();
    }

    // ================================================================
    // Value
    // ================================================================

    void Value::addUse(User* user, unsigned operandNo) {
        uses.emplace_back(user, operandNo);
    }

    void Value::removeUse(User* user, unsigned operandNo) {
        for (auto it = uses.begin(); it != uses.end(); ++it) {
            if (it->user == user && it->operandNo == operandNo) {
                uses.erase(it);
                return;
            }
        }
    }

    void Value::replaceAllUsesWith(Value* newVal) {
        auto usesCopy = uses;
        for (auto& u : usesCopy) {
            u.user->setOperand(u.operandNo, newVal);
        }
    }

    // ================================================================
    // User
    // ================================================================

    Value* User::getOperand(unsigned i) const {
        assert(i < operands.size() && "operand index out of range");
        return operands[i];
    }

    void User::setOperand(unsigned i, Value* v) {
        assert(i < operands.size() && "operand index out of range");
        if (operands[i]) {
            operands[i]->removeUse(this, i);
        }
        operands[i] = v;
        if (v) {
            v->addUse(this, i);
        }
    }

    void User::addOperand(Value* v) {
        unsigned idx = static_cast<unsigned>(operands.size());
        operands.push_back(v);
        if (v) {
            v->addUse(this, idx);
        }
    }

    void User::dropAllUses() {
        for (unsigned i = 0; i < operands.size(); ++i) {
            if (operands[i]) {
                operands[i]->removeUse(this, i);
            }
        }
    }

    // ================================================================
    // VReg
    // ================================================================

    VReg* VReg::create(Type* t, const std::string& name) {
        return new VReg(t, name);
    }

    // ================================================================
    // Constant
    // ================================================================

    ConstantInt* ConstantInt::get(IntegerType* ty, int64_t val) {
        static std::unordered_map<int64_t, std::unique_ptr<ConstantInt>> cache;
        auto& p = cache[val];
        if (!p) p = std::make_unique<ConstantInt>(ty, val);
        return p.get();
    }

    ConstantFloat* ConstantFloat::get(FloatType* ty, double val) {
        // SysY only has 32-bit floats; truncate to float32 precision
        // so that constant folding produces results matching runtime semantics.
        float fval = static_cast<float>(val);
        double dval = static_cast<double>(fval);
        static std::vector<std::unique_ptr<ConstantFloat>> cache;
        for (auto& c : cache) {
            if (c->getValue() == dval) return c.get();
        }
        cache.push_back(std::make_unique<ConstantFloat>(ty, dval));
        return cache.back().get();
    }

    // ================================================================
    // Instruction 创建方法
    // ================================================================

    Instruction* Instruction::createRet(Value* val) {
        // ret void or ret <value>
        Type* retType = val ? val->getType() : VoidType::get();
        unsigned res = val ? 1 : 0;
        auto* inst = new Instruction(Opcode::RET, retType, "", res);
        if (val) inst->addOperand(val);
        return inst;
    }

    Instruction* Instruction::createBr(BasicBlock* target) {
        auto* inst = new Instruction(Opcode::BR, VoidType::get(), "", 1);
        inst->addOperand(target);
        return inst;
    }

    Instruction* Instruction::createCondBr(Value* cond, BasicBlock* thenBB, BasicBlock* elseBB) {
        auto* inst = new Instruction(Opcode::COND_BR, VoidType::get(), "", 3);
        inst->addOperand(cond);
        inst->addOperand(thenBB);
        inst->addOperand(elseBB);
        return inst;
    }

    Instruction* Instruction::createBinOp(Opcode op, Type* ty, const std::string& name,
                                        Value* lhs, Value* rhs) {
        auto* inst = new Instruction(op, ty, name, 2);
        inst->addOperand(lhs);
        inst->addOperand(rhs);
        return inst;
    }

    Instruction* Instruction::createAlloca(Type* ty, const std::string& name) {
        auto* inst = new Instruction(Opcode::ALLOCA, PointerType::get(ty), name, 1);
        inst->addOperand(nullptr); // 占位: 对齐/大小信息可在后续处理
        return inst;
    }

    Instruction* Instruction::createLoad(Type* ty, Value* ptr, const std::string& name) {
        auto* inst = new Instruction(Opcode::LOAD, ty, name, 1);
        inst->addOperand(ptr);
        return inst;
    }

    Instruction* Instruction::createStore(Value* val, Value* ptr) {
        auto* inst = new Instruction(Opcode::STORE, VoidType::get(), "", 2);
        inst->addOperand(val);
        inst->addOperand(ptr);
        return inst;
    }

    Instruction* Instruction::createCall(FunctionType* ft, Value* callee,
                                        const std::vector<Value*>& args, const std::string& name) {
        auto* inst = new Instruction(Opcode::CALL, ft->getReturnType(), name, 1 + args.size());
        inst->addOperand(callee);
        for (auto* a : args) inst->addOperand(a);
        return inst;
    }

    Instruction* Instruction::createGetElementPtr(Type* pointee, Value* ptr,
                                                const std::vector<Value*>& indices,
                                                const std::string& name) {
        Type* resultTy = pointee;
        for (size_t i = 1; i < indices.size(); ++i) {
            if (auto* at = dynamic_cast<ArrayType*>(resultTy)) {
                resultTy = at->getElementType();
            }
        }
        auto* inst = new Instruction(Opcode::GETELEMENTPTR, PointerType::get(resultTy), name,
                                    1 + indices.size());
        inst->addOperand(ptr);
        for (auto* idx : indices) inst->addOperand(idx);
        return inst;
    }

    Instruction* Instruction::createCmp(Opcode op, Value* lhs, Value* rhs, const std::string& name) {
        auto* inst = new Instruction(op, IntegerType::I1, name, 2);
        inst->addOperand(lhs);
        inst->addOperand(rhs);
        return inst;
    }

    Instruction* Instruction::createCast(Opcode op, Type* toTy, Value* src, const std::string& name) {
        auto* inst = new Instruction(op, toTy, name, 1);
        inst->addOperand(src);
        return inst;
    }

    Instruction* Instruction::createPhi(Type* ty, const std::string& name, unsigned reserve) {
        return new Instruction(Opcode::PHI, ty, name, reserve);
    }

    Instruction* Instruction::createSelect(Value* cond, Value* trueVal, Value* falseVal, const std::string& name) {
        auto* inst = new Instruction(Opcode::SELECT, trueVal->getType(), name, 3);
        inst->addOperand(cond);
        inst->addOperand(trueVal);
        inst->addOperand(falseVal);
        return inst;
    }

    // ================================================================
    // BasicBlock
    // ================================================================

    BasicBlock::BasicBlock(const std::string& n)
        : Value(LabelType::get(), n), parent(nullptr) {}

    Instruction* BasicBlock::getTerminator() const {
        if (insts.empty()) return nullptr;
        auto* last = insts.back().get();
        Instruction::Opcode op = last->getOpcode();
        if (op == Instruction::Opcode::RET ||
            op == Instruction::Opcode::BR   ||
            op == Instruction::Opcode::COND_BR) {
            return last;
        }
        return nullptr;
    }

    void BasicBlock::addInstruction(std::unique_ptr<Instruction> inst) {
        inst->setParent(this);
        insts.push_back(std::move(inst));
    }

    // ================================================================
    // Function
    // ================================================================

    Function::Function(FunctionType* ft, const std::string& n, bool ext)
        : Value(ft, n), funcType(ft), parent(nullptr), external(ext) {
        auto& paramTypes = ft->getParamTypes();
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            auto arg = std::make_unique<Argument>(paramTypes[i], static_cast<unsigned>(i), "arg" + std::to_string(i));
            args.push_back(std::move(arg));
        }
    }

    Argument* Function::getArg(unsigned i) const {
        assert(i < args.size() && "argument index out of range");
        return args[i].get();
    }

    BasicBlock* Function::createBlock(const std::string& name) {
        static int blockCounter = 0;
        std::string blockName = name;
        if (blockName.empty()) {
            blockName = "bb" + std::to_string(blockCounter++);
        }
        auto bb = std::make_unique<BasicBlock>(blockName);
        bb->setParent(this);
        BasicBlock* ptr = bb.get();
        blocks.push_back(std::move(bb));
        return ptr;
    }

    BasicBlock* Function::insertBlock(const std::string& name, BasicBlock* before) {
        auto bb = std::make_unique<BasicBlock>(name);
        bb->setParent(this);
        BasicBlock* ptr = bb.get();
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == before) {
                blocks.insert(it, std::move(bb));
                return ptr;
            }
        }
        // If 'before' not found, append to end
        blocks.push_back(std::move(bb));
        return ptr;
    }

    // ================================================================
    // GlobalVariable
    // ================================================================

    GlobalVariable::GlobalVariable(PointerType* ty, const std::string& n,
                                    bool isConst, Constant* init)
        : Constant(ty, n), constant(isConst), initVal(init) {}

    // ================================================================
    // Module
    // ================================================================

    Module::~Module() {
        // ★ 在 functions 向量销毁之前，清空所有函数中所有指令的操作数。
        //
        // 跨函数引用问题：CALL 指令引用 Function 对象（callee）。
        // 当 functions 向量按逆序销毁时，先释放的 Function 的 use-list 被访问 →
        // heap-use-after-free。
        //
        // 函数内引用问题：BR/COND_BR/PHI 引用 BasicBlock 对象。
        // 当 blocks 向量按逆序销毁时，先释放的 BB 的 use-list 被访问 →
        // heap-use-after-free。
        //
        // 通过预先清空所有操作数（setOperand(i, nullptr) 会从被引用者的 use-list
        // 中移除条目），后续析构时 dropAllUses() 只会看到 null 操作数，
        // 不会访问任何已释放的内存。
        for (auto& func : functions) {
            for (auto& bb : func->getBlocks()) {
                for (auto& inst : bb->getInstructions()) {
                    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                        if (inst->getOperand(i)) {
                            inst->setOperand(i, nullptr);
                        }
                    }
                }
            }
        }
    }

    Function* Module::createFunction(FunctionType* ft, const std::string& name, bool external) {
        for (auto& f : functions) {
            if (f->getName() == name) return f.get();
        }
        auto func = std::make_unique<Function>(ft, name, external);
        func->setParent(this);
        Function* ptr = func.get();
        functions.push_back(std::move(func));
        return ptr;
    }

    GlobalVariable* Module::createGlobalVariable(PointerType* ty, const std::string& name,
                                                bool isConst, Constant* init) {
        auto gv = std::make_unique<GlobalVariable>(ty, name, isConst, init);
        GlobalVariable* ptr = gv.get();
        globals.push_back(std::move(gv));
        return ptr;
    }

    std::string Module::dump() const {
        std::ostringstream oss;
        for (auto& gv : globals) {
            PointerType* gvTy = static_cast<PointerType*>(gv->getType());
            oss << "@" << gv->getName() << " = global "
                << gvTy->getPointeeType()->toString() << "\n";
        }
        for (auto& func : functions) {
            auto* ft = func->getFunctionType();
            if (func->isExternal()) {
                oss << "declare " << ft->getReturnType()->toString()
                    << " @" << func->getName() << "(";
            } else {
                oss << "define " << ft->getReturnType()->toString()
                    << " @" << func->getName() << "(";
            }
            auto& params = ft->getParamTypes();
            for (size_t i = 0; i < params.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << params[i]->toString() << " %" << func->getArg(i)->getName();
            }
            if (func->isExternal()) {
                oss << ")\n\n";
                continue;
            }
            oss << ") {\n";
            for (auto& bb : func->getBlocks()) {
                oss << bb->getName() << ":\n";
                for (auto& inst : bb->getInstructions()) {
                    oss << "  ";
                    auto op = inst->getOpcode();
                    if (op == Instruction::Opcode::RET) {
                        oss << "ret ";
                        if (inst->getNumOperands() == 0 || inst->getOperand(0) == nullptr) {
                            oss << "void";
                        } else {
                            auto* val = inst->getOperand(0);
                            oss << val->getType()->toString() << " ";
                            if (auto* ci = dynamic_cast<ConstantInt*>(val)) {
                                oss << ci->getValue();
                            } else if (auto* cf = dynamic_cast<ConstantFloat*>(val)) {
                                oss << cf->getValue();
                            } else {
                                oss << "%" << val->getName();
                            }
                        }
                    } else if (op == Instruction::Opcode::BR) {
                        oss << "br label %" << inst->getOperand(0)->getName();
                    } else if (op == Instruction::Opcode::COND_BR) {
                        auto* condVal = inst->getOperand(0);
                        oss << "br i1 ";
                        if (auto* ci = dynamic_cast<ConstantInt*>(condVal))
                            oss << ci->getValue();
                        else if (auto* cf = dynamic_cast<ConstantFloat*>(condVal))
                            oss << cf->getValue();
                        else
                            oss << "%" << condVal->getName();
                        oss << ", label %" << inst->getOperand(1)->getName()
                            << ", label %" << inst->getOperand(2)->getName();
                    } else if (op == Instruction::Opcode::PHI) {
                        oss << "%" << inst->getName() << " = phi " << inst->getType()->toString() << " ";
                        for (unsigned i = 0; i < inst->getNumOperands(); i += 2) {
                            if (i > 0) oss << ", ";
                            auto* val = inst->getOperand(i);
                            auto* label = inst->getOperand(i + 1);
                            oss << "[ ";
                            if (auto* ci = dynamic_cast<ConstantInt*>(val))
                                oss << ci->getValue();
                            else if (auto* cf = dynamic_cast<ConstantFloat*>(val))
                                oss << cf->getValue();
                            else
                                oss << "%" << (val ? val->getName() : "null");
                            oss << ", %" << (label ? label->getName() : "null") << " ]";
                        }
                        oss << "\n";
                        continue;
                    } else {
                        if (!inst->getName().empty())
                            oss << "%" << inst->getName() << " = ";
                        switch (op) {
                        case Instruction::Opcode::ALLOCA: {
                            auto* ptrTy = static_cast<PointerType*>(inst->getType());
                            oss << "alloca " << ptrTy->getPointeeType()->toString() << "\n";
                            continue;
                        }
                        case Instruction::Opcode::STORE: {
                            auto* val = inst->getOperand(0);
                            auto* ptr = inst->getOperand(1);
                            oss << "store " << val->getType()->toString() << " ";
                            if (auto* ci = dynamic_cast<ConstantInt*>(val))
                                oss << ci->getValue();
                            else if (auto* cf = dynamic_cast<ConstantFloat*>(val))
                                oss << cf->getValue();
                            else
                                oss << "%" << val->getName();
                            oss << ", " << ptr->getType()->toString() << " %" << ptr->getName() << "\n";
                            continue;
                        }
                        case Instruction::Opcode::LOAD: {
                            auto* ptr = inst->getOperand(0);
                            oss << "load " << inst->getType()->toString() << ", "
                                << ptr->getType()->toString() << " %" << ptr->getName() << "\n";
                            continue;
                        }
                        case Instruction::Opcode::CALL: {
                            auto* callee = inst->getOperand(0);
                            auto* ft = static_cast<FunctionType*>(callee->getType());
                            oss << "call " << ft->getReturnType()->toString() << " %" << callee->getName() << "(";
                            for (unsigned i = 1; i < inst->getNumOperands(); ++i) {
                                if (i > 1) oss << ", ";
                                auto* v = inst->getOperand(i);
                                oss << v->getType()->toString() << " ";
                                if (auto* ci = dynamic_cast<ConstantInt*>(v))
                                    oss << ci->getValue();
                                else if (auto* cf = dynamic_cast<ConstantFloat*>(v))
                                    oss << cf->getValue();
                                else
                                    oss << "%" << v->getName();
                            }
                            oss << ")\n";
                            continue;
                        }
                        case Instruction::Opcode::ADD:  oss << "add ";  break;
                        case Instruction::Opcode::SUB:  oss << "sub ";  break;
                        case Instruction::Opcode::MUL:  oss << "mul ";  break;
                        case Instruction::Opcode::SDIV: oss << "sdiv "; break;
                        case Instruction::Opcode::SREM: oss << "srem "; break;
                        case Instruction::Opcode::FADD: oss << "fadd "; break;
                        case Instruction::Opcode::FSUB: oss << "fsub "; break;
                        case Instruction::Opcode::FMUL: oss << "fmul "; break;
                        case Instruction::Opcode::FDIV: oss << "fdiv "; break;
                        case Instruction::Opcode::AND:  oss << "and ";  break;
                        case Instruction::Opcode::OR:   oss << "or ";   break;
                        case Instruction::Opcode::XOR:  oss << "xor ";  break;
                        case Instruction::Opcode::SHL:  oss << "shl ";  break;
                        case Instruction::Opcode::ASHR: oss << "ashr "; break;
                        case Instruction::Opcode::SMULH: oss << "smulh "; break;
                        case Instruction::Opcode::ICMP: oss << "icmp "; break;
                        case Instruction::Opcode::FCMP: oss << "fcmp "; break;
                        case Instruction::Opcode::SITOFP: oss << "sitofp "; break;
                        case Instruction::Opcode::FPTOSI: oss << "fptosi "; break;
                        case Instruction::Opcode::ZEXT: oss << "zext "; break;
                        case Instruction::Opcode::SEXT: oss << "sext "; break;
                        case Instruction::Opcode::GETELEMENTPTR: oss << "getelementptr "; break;
                        case Instruction::Opcode::SELECT: oss << "select "; break;
                        default: oss << "unknown "; break;
                        }
                        oss << inst->getType()->toString();
                        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                            oss << (i == 0 ? " " : ", ");
                            auto* v = inst->getOperand(i);
                            if (!v) {
                                oss << "null";
                            } else if (auto* ci = dynamic_cast<ConstantInt*>(v)) {
                                oss << ci->getValue();
                            } else if (auto* cf = dynamic_cast<ConstantFloat*>(v)) {
                                oss << cf->getValue();
                            } else {
                                oss << "%" << v->getName();
                            }
                        }
                    }
                    oss << "\n";
                }
            }
            oss << "}\n\n";
        }
        return oss.str();
    }

} // namespace IR