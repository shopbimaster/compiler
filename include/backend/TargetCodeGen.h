#pragma once

#include "ir/IR.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

namespace Backend {

enum class Register {
    x0, ra, sp, gp, tp, t0, t1, t2,
    s0, s1, a0, a1, a2, a3, a4, a5,
    a6, a7, s2, s3, s4, s5, s6, s7,
    s8, s9, s10, s11, t3, t4, t5, t6
};

class CodeEmitter {
private:
    std::ostringstream text;
    std::ostringstream data;

public:
    void emitText(const std::string& asmLine) { text << asmLine << std::endl; }
    void emitData(const std::string& asmLine) { data << asmLine << std::endl; }
    std::string getTextSection() const { return text.str(); }
    std::string getDataSection() const { return data.str(); }
};

class TargetCodeGen {
private:
    CodeEmitter emitter;
    std::unordered_map<IR::Value*, Register> regMap;
    int stackOffset;

public:
    TargetCodeGen();

    std::string generate(IR::Module& module);

private:
    void emitFunction(IR::Function& func);
    void emitBasicBlock(IR::BasicBlock& bb);
    void emitInstruction(IR::Instruction& inst);
    void emitPrologue(IR::Function& func);
    void emitEpilogue(IR::Function& func);
    Register getReg(IR::Value* val);
    Register allocReg();
    void freeReg(Register reg);
    std::string regToString(Register reg);
};

}
