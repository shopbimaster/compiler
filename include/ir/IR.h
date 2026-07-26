#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

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
        virtual ~User() { dropAllUses(); }

        unsigned getNumOperands() const { return static_cast<unsigned>(operands.size()); }
        Value*   getOperand(unsigned i) const;

        void setOperand(unsigned i, Value* v);
        void addOperand(Value* v);
        void dropAllUses();

        // 移除 PHI 节点中 null 的 (value, block) 操作数对
        // nullifyPhiEntriesForBlock/Predecessor 会将已删除块的 PHI 条目置 null，
        // 这些 null 条目必须被移除，否则会导致代码生成阶段崩溃。
        // ★ 必须重建 use-list：compact 后非 null 操作数的索引会变化，
        //   旧 use-list 条目中的 operandNo 会失效，导致后续 removeUse 找不到条目。
        void removeNullPhiPairs() {
            // 先移除所有非 null 操作数的 use-list 条目
            // （null 操作数的 use-list 条目已在 setOperand(i, nullptr) 时移除）
            for (size_t i = 0; i < operands.size(); ++i) {
                if (operands[i]) {
                    operands[i]->removeUse(this, static_cast<unsigned>(i));
                }
            }
            // 构建新的操作数向量，仅保留非 null 对
            std::vector<Value*> kept;
            kept.reserve(operands.size());
            for (size_t i = 0; i + 1 < operands.size(); i += 2) {
                if (operands[i] != nullptr && operands[i + 1] != nullptr) {
                    kept.push_back(operands[i]);
                    kept.push_back(operands[i + 1]);
                }
            }
            // 重新赋值并按新索引重建 use-list
            operands = std::move(kept);
            for (size_t i = 0; i < operands.size(); ++i) {
                operands[i]->addUse(this, static_cast<unsigned>(i));
            }
        }

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
            SMULH, WIDE_SMOD_MUL,
            ICMP, FCMP,
            ALLOCA, LOAD, STORE,
            CALL,
            GETELEMENTPTR,
            ZEXT, SEXT, TRUNC, SITOFP, FPTOSI,
            PHI, SELECT
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
        static Instruction* createTernaryOp(Opcode op, Type* ty, const std::string& name,
                                            Value* first, Value* second, Value* third);
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
        static Instruction* createSelect(Value* cond, Value* trueVal, Value* falseVal, const std::string& name);

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
            inst->setParent(this);
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
        std::vector<std::unique_ptr<BasicBlock>>& getBlocks() { return blocks; }
        BasicBlock* getEntryBlock() const { return blocks.empty() ? nullptr : blocks.front().get(); }

        BasicBlock* createBlock(const std::string& name = "");
        BasicBlock* insertBlock(const std::string& name, BasicBlock* before);

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
        // ★ 必须显式定义析构函数：在 functions 向量销毁之前清空所有指令的操作数。
        // CALL 指令引用 Function 对象（callee），BR/COND_BR/PHI 引用 BasicBlock 对象。
        // 当 functions 向量按逆序销毁时，先释放的 Function 的 use-list 被访问 →
        // heap-use-after-free（82_long_func 根因）。
        // 通过预先清空所有操作数，后续析构时 dropAllUses() 只会看到 null 操作数。
        ~Module();

        Function* createFunction(FunctionType* ft, const std::string& name, bool external = false);

        GlobalVariable* createGlobalVariable(PointerType* ty, const std::string& name,
                                            bool isConst, Constant* init = nullptr);

        const std::vector<std::unique_ptr<Function>>&       getFunctions() const { return functions; }
        std::vector<std::unique_ptr<Function>>&             getFunctions()       { return functions; }
        const std::vector<std::unique_ptr<GlobalVariable>>& getGlobals()   const { return globals; }
        std::vector<std::unique_ptr<GlobalVariable>>&       getGlobals()         { return globals; }

        // 移除满足谓词的函数（用于 TreeShaking 等死代码消除）
        template<typename Pred>
        void removeFunctionsIf(Pred pred) {
            functions.erase(
                std::remove_if(functions.begin(), functions.end(),
                    [&](const std::unique_ptr<Function>& f) { return pred(f.get()); }),
                functions.end());
        }

        // 移除满足谓词的全局变量
        template<typename Pred>
        void removeGlobalsIf(Pred pred) {
            globals.erase(
                std::remove_if(globals.begin(), globals.end(),
                    [&](const std::unique_ptr<GlobalVariable>& g) { return pred(g.get()); }),
                globals.end());
        }

        std::string dump() const;

    private:
        // ★ 声明顺序至关重要：globals 必须在 functions 之前声明
        // C++ 成员按声明逆序析构，因此 functions 会先析构（Instructions 的
        // dropAllUses 能安全访问 globals 的 use list），然后 globals 才析构。
        // 若顺序相反，globals 先析构后 functions 中的 Instruction 析构时
        // 访问已释放的 GlobalVariable → heap-use-after-free
        std::vector<std::unique_ptr<GlobalVariable>> globals;
        std::vector<std::unique_ptr<Function>>       functions;
    };

} // namespace IR
