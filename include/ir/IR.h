#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <stdexcept>

namespace IR {

    // ================================================================
    // Type 系统 —— 类型对象全局唯一，用指针恒等比较
    // ================================================================

    class Type {
    public:
        enum class Kind { Void, Label, Integer, Float, Pointer, Array, Function };

        virtual ~Type() = default;
        Kind getKind() const { return kind; }
        virtual std::string toString() const = 0;

        bool isVoid()    const { return kind == Kind::Void; }
        bool isLabel()   const { return kind == Kind::Label; }
        bool isInteger() const { return kind == Kind::Integer; }
        bool isFloat()   const { return kind == Kind::Float; }
        bool isPointer() const { return kind == Kind::Pointer; }
        bool isArray()   const { return kind == Kind::Array; }

    protected:
        explicit Type(Kind k) : kind(k) {}
        Kind kind;
    };

    class VoidType : public Type {
    public:
        static VoidType* get();
        std::string toString() const override { return "void"; }
        VoidType() : Type(Kind::Void) {}
    };

    class LabelType : public Type {
    public:
        static LabelType* get();
        std::string toString() const override { return "label"; }
        LabelType() : Type(Kind::Label) {}
    };

    class IntegerType : public Type {
    public:
        static IntegerType* get(unsigned bitWidth);
        unsigned getBitWidth() const { return bitWidth; }
        std::string toString() const override;

        static IntegerType* I1;
        static IntegerType* I8;
        static IntegerType* I32;

        explicit IntegerType(unsigned w) : Type(Kind::Integer), bitWidth(w) {}
    private:
        unsigned bitWidth;
    };

    class FloatType : public Type {
    public:
        static FloatType* get();
        std::string toString() const override { return "float"; }
        FloatType() : Type(Kind::Float) {}
    };

    class PointerType : public Type {
    public:
        static PointerType* get(Type* pointee);
        Type* getPointeeType() const { return pointee; }
        std::string toString() const override;
        explicit PointerType(Type* p) : Type(Kind::Pointer), pointee(p) {}
    private:
        Type* pointee;
    };

    class ArrayType : public Type {
    public:
        static ArrayType* get(Type* elem, unsigned numElem);
        Type* getElementType() const { return elemType; }
        unsigned getNumElements() const { return numElements; }
        std::string toString() const override;
        ArrayType(Type* e, unsigned n) : Type(Kind::Array), elemType(e), numElements(n) {}
    private:
        Type* elemType;
        unsigned numElements;
    };

    class FunctionType : public Type {
    public:
        static FunctionType* get(Type* ret, const std::vector<Type*>& params);
        Type* getReturnType() const { return returnType; }
        const std::vector<Type*>& getParamTypes() const { return paramTypes; }
        std::string toString() const override;
        FunctionType(Type* r, const std::vector<Type*>& p)
            : Type(Kind::Function), returnType(r), paramTypes(p) {}
    private:
        Type* returnType;
        std::vector<Type*> paramTypes;
    };

    // ================================================================
    // Use —— Def-Use 链的一个边
    // ================================================================

    class User;

    struct Use {
        User*    user;
        unsigned operandNo;

        Use(User* u, unsigned op) : user(u), operandNo(op) {}
    };

    // ================================================================
// Value —— 所有 IR 实体的基类，持有 uses 列表
// ================================================================

    class Value {
    public:
        virtual ~Value() = default;

        void             setName(const std::string& n) { name = n; }
        const std::string& getName() const              { return name; }
        Type*            getType() const                { return type; }

        void addUse(User* user, unsigned operandNo);
        void removeUse(User* user, unsigned operandNo);
        const std::vector<Use>& getUses() const { return uses; }
        unsigned getNumUses() const             { return static_cast<unsigned>(uses.size()); }
        bool     hasOneUse() const              { return uses.size() == 1; }
        void     replaceAllUsesWith(Value* newVal);

    protected:
        explicit Value(Type* t, const std::string& n = "") : type(t), name(n) {}

        Type*            type;
        std::string      name;
        std::vector<Use> uses;
    };

    // ================================================================
    // User —— 使用其他 Value 的基类，持有 operands
    // ================================================================

    class User : public Value {
    public:
        unsigned getNumOperands() const { return static_cast<unsigned>(operands.size()); }
        Value*   getOperand(unsigned i) const;

        void setOperand(unsigned i, Value* v);
        void addOperand(Value* v);
        void dropAllUses();

        using op_iterator = std::vector<Value*>::iterator;
        op_iterator op_begin() { return operands.begin(); }
        op_iterator op_end()   { return operands.end(); }

    protected:
        explicit User(Type* t, const std::string& n = "", unsigned reserve = 2)
            : Value(t, n) { operands.reserve(reserve); }

        std::vector<Value*> operands;
    };

    // ================================================================
    // VReg —— SSA 虚拟寄存器（不持有 operands，纯符号）
    // ================================================================

    class VReg : public Value {
    public:
        VReg(Type* t, const std::string& n = "") : Value(t, n) {}

        static VReg* create(Type* t, const std::string& name = "");
    };

    // ================================================================
    // Constant 体系
    // ================================================================

    class Constant : public User {
    protected:
        explicit Constant(Type* t, const std::string& n = "") : User(t, n, 0) {}
    };

    class ConstantInt : public Constant {
    public:
        static ConstantInt* get(IntegerType* ty, int64_t val);

        int64_t getValue() const { return value; }
        ConstantInt(IntegerType* ty, int64_t v) : Constant(ty), value(v) {}
    private:
        int64_t value;
    };

    class ConstantFloat : public Constant {
    public:
        static ConstantFloat* get(FloatType* ty, double val);

        double getValue() const { return value; }
        ConstantFloat(FloatType* ty, double v) : Constant(ty), value(v) {}
    private:
        double value;
    };

    // ================================================================
    // Instruction 基类
    // ================================================================

    class BasicBlock;

    class Instruction : public User {
    public:
        enum class Opcode {
            RET, BR, COND_BR,
            ADD, SUB, MUL, SDIV, SREM,
            FADD, FSUB, FMUL, FDIV,
            AND, OR, XOR, SHL, ASHR,
            ICMP, FCMP,
            ALLOCA, LOAD, STORE,
            CALL,
            GETELEMENTPTR,
            ZEXT, SEXT, TRUNC, SITOFP, FPTOSI,
            PHI
        };

        Opcode      getOpcode()    const { return opcode; }
        BasicBlock* getParent()    const { return parent; }
        void        setParent(BasicBlock* bb) { parent = bb; }

        // LLVM 风格便捷创建方法
        static Instruction* createRet(Value* val);
        static Instruction* createBr(BasicBlock* target);
        static Instruction* createCondBr(Value* cond, BasicBlock* thenBB, BasicBlock* elseBB);
        static Instruction* createBinOp(Opcode op, Type* ty, const std::string& name,
                                        Value* lhs, Value* rhs);
        static Instruction* createAlloca(Type* ty, const std::string& name);
        static Instruction* createLoad(Type* ty, Value* ptr, const std::string& name);
        static Instruction* createStore(Value* val, Value* ptr);
        static Instruction* createCall(FunctionType* ft, Value* callee,
                                        const std::vector<Value*>& args, const std::string& name);
        static Instruction* createGetElementPtr(Type* pointee, Value* ptr,
                                                const std::vector<Value*>& indices,
                                                const std::string& name);
        static Instruction* createCmp(Opcode op, Value* lhs, Value* rhs, const std::string& name);
        static Instruction* createCast(Opcode op, Type* toTy, Value* src, const std::string& name);
        static Instruction* createPhi(Type* ty, const std::string& name, unsigned reserve);

    protected:
        Instruction(Opcode op, Type* ty, const std::string& name, unsigned reserve)
            : User(ty, name, reserve), opcode(op), parent(nullptr) {}

        Opcode      opcode;
        BasicBlock* parent;
    };

    // ================================================================
    // Argument —— 函数形参
    // ================================================================

    class Argument : public Value {
    public:
        Argument(Type* t, unsigned idx, const std::string& n = "")
            : Value(t, n), index(idx) {}

        unsigned getIndex() const { return index; }
    private:
        unsigned index;
    };

    // ================================================================
    // BasicBlock
    // ================================================================

    class Function;

    class BasicBlock : public Value {
    public:
        explicit BasicBlock(const std::string& n = "");

        Function* getParent() const { return parent; }
        void      setParent(Function* f) { parent = f; }

        const std::vector<std::unique_ptr<Instruction>>& getInstructions() const { return insts; }
        Instruction* getTerminator() const;

        void addInstruction(std::unique_ptr<Instruction> inst);
        void pushBack(Instruction* inst) { addInstruction(std::unique_ptr<Instruction>(inst)); }

        bool empty() const { return insts.empty(); }
        size_t size() const { return insts.size(); }

        using iterator = std::vector<std::unique_ptr<Instruction>>::iterator;
        iterator begin() { return insts.begin(); }
        iterator end()   { return insts.end(); }
        iterator erase(iterator it) { return insts.erase(it); }
        iterator insert(iterator pos, Instruction* inst) {
            return insts.insert(pos, std::unique_ptr<Instruction>(inst));
        }

    private:
        Function*                                  parent;
        std::vector<std::unique_ptr<Instruction>>  insts;
    };

    // ================================================================
    // Function
    // ================================================================

    class Module;

    class Function : public Value {
    public:
        Function(FunctionType* ft, const std::string& n, bool external = false);

        Module*     getParent() const { return parent; }
        void        setParent(Module* m) { parent = m; }
        FunctionType* getFunctionType() const { return funcType; }

        bool isExternal() const { return external; }

        unsigned getNumArgs() const { return static_cast<unsigned>(args.size()); }
        Argument* getArg(unsigned i) const;

        const std::vector<std::unique_ptr<BasicBlock>>& getBlocks() const { return blocks; }
        BasicBlock* getEntryBlock() const { return blocks.empty() ? nullptr : blocks.front().get(); }

        BasicBlock* createBlock(const std::string& name = "");

    private:
        FunctionType*                             funcType;
        std::vector<std::unique_ptr<Argument>>    args;
        std::vector<std::unique_ptr<BasicBlock>>  blocks;
        Module*                                   parent;
        bool                                      external;
    };

    // ================================================================
    // GlobalVariable
    // ================================================================

    class GlobalVariable : public Constant {
    public:
        GlobalVariable(PointerType* ty, const std::string& n, bool isConst,
                    Constant* init = nullptr);

        bool     isConstant() const { return constant; }
        Constant* getInitializer() const { return initVal; }
        void     setInitializer(Constant* c) { initVal = c; }

        // Flat initializer data for arrays (each element is a 32-bit word)
        const std::vector<uint32_t>& getInitData() const { return initData; }
        void setInitData(const std::vector<uint32_t>& data) { initData = data; }

    private:
        bool      constant;
        Constant* initVal;
        std::vector<uint32_t> initData;
    };

    // ================================================================
    // Module —— 顶层容器
    // ================================================================

    class Module {
    public:
        Module() = default;

        Function* createFunction(FunctionType* ft, const std::string& name, bool external = false);

        GlobalVariable* createGlobalVariable(PointerType* ty, const std::string& name,
                                            bool isConst, Constant* init = nullptr);

        const std::vector<std::unique_ptr<Function>>&       getFunctions() const { return functions; }
        const std::vector<std::unique_ptr<GlobalVariable>>& getGlobals()   const { return globals; }

        std::string dump() const;

    private:
        std::vector<std::unique_ptr<Function>>       functions;
        std::vector<std::unique_ptr<GlobalVariable>> globals;
    };

} // namespace IR