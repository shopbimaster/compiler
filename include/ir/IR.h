#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>

namespace IR {

enum class Opcode {
    ADD, SUB, MUL, SDIV, SREM,
    AND, OR, XOR, SHL, ASHR,
    SLT, SLTU,
    ALLOCA, LOAD, STORE,
    BR, COND_BR, RET,
    CALL,
    GETELEMENTPTR,
    ZEXT, SEXT, TRUNC,
    PHI
};

enum class Type {
    VOID,
    I1,
    I8,
    I16,
    I32,
    I64,
    PTR
};

class Value {
public:
    virtual ~Value() = default;
    virtual std::string getName() const = 0;
    virtual Type getType() const = 0;
};

class Constant : public Value {
public:
    int64_t value;
    Type type;

    Constant(int64_t v, Type t) : value(v), type(t) {}
    std::string getName() const override { return std::to_string(value); }
    Type getType() const override { return type; }
};

class Instruction : public Value {
public:
    Opcode opcode;
    std::vector<Value*> operands;
    Type type;
    std::string name;

    Instruction(Opcode op, Type t, const std::string& n = "")
        : opcode(op), type(t), name(n) {}

    void addOperand(Value* v) { operands.push_back(v); }
    std::string getName() const override { return name; }
    Type getType() const override { return type; }
};

class BasicBlock : public Value {
public:
    std::string name;
    std::vector<std::unique_ptr<Instruction>> instructions;
    Instruction* terminator;

    explicit BasicBlock(const std::string& n) : name(n), terminator(nullptr) {}

    void addInstruction(std::unique_ptr<Instruction> inst) {
        instructions.push_back(std::move(inst));
    }

    std::string getName() const override { return name; }
    Type getType() const override { return Type::PTR; }
};

class Function : public Value {
public:
    std::string name;
    Type returnType;
    std::vector<Type> paramTypes;
    std::vector<std::unique_ptr<BasicBlock>> blocks;
    std::vector<std::unique_ptr<Value>> params;
    bool isExternal;

    Function(const std::string& n, Type ret, const std::vector<Type>& params, bool ext = false)
        : name(n), returnType(ret), paramTypes(params), isExternal(ext) {}

    BasicBlock* createBlock(const std::string& name) {
        auto block = std::make_unique<BasicBlock>(name);
        BasicBlock* ptr = block.get();
        blocks.push_back(std::move(block));
        return ptr;
    }

    std::string getName() const override { return name; }
    Type getType() const override { return Type::PTR; }
};

class GlobalVariable : public Value {
public:
    std::string name;
    Type type;
    bool isConst;
    std::unique_ptr<Constant> initVal;
    std::vector<int> dims;

    GlobalVariable(const std::string& n, Type t, bool c)
        : name(n), type(t), isConst(c) {}

    std::string getName() const override { return name; }
    Type getType() const override { return type; }
};

class Module {
public:
    std::vector<std::unique_ptr<Function>> functions;
    std::vector<std::unique_ptr<GlobalVariable>> globals;

    Function* createFunction(const std::string& name, Type retType,
                              const std::vector<Type>& params, bool isExt = false) {
        auto func = std::make_unique<Function>(name, retType, params, isExt);
        Function* ptr = func.get();
        functions.push_back(std::move(func));
        return ptr;
    }

    GlobalVariable* createGlobal(const std::string& name, Type type, bool isConst) {
        auto gv = std::make_unique<GlobalVariable>(name, type, isConst);
        GlobalVariable* ptr = gv.get();
        globals.push_back(std::move(gv));
        return ptr;
    }
};

}
