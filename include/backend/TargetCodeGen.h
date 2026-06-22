#pragma once

#include "ir/IR.h"
#include "backend/RegisterAllocator.h"
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

std::string regToString(Register reg);

class CodeEmitter {
public:
    void emitText(const std::string& asmLine) { text << asmLine << std::endl; }
    void emitData(const std::string& asmLine) { data << asmLine << std::endl; }
    std::string getTextSection() const { return text.str(); }
    std::string getDataSection() const { return data.str(); }

private:
    std::ostringstream text;
    std::ostringstream data;
};

class TargetCodeGen {
public:
    TargetCodeGen();
    std::string generate(IR::Module& module);

private:
    CodeEmitter emitter;
    IR::Function* currentFunc = nullptr;
    int stackSize;
    int labelCounter;

    std::unordered_map<IR::Value*, int> vregStackOffset;
    std::unordered_map<IR::Value*, int> allocaOffset;
    std::unordered_map<IR::Argument*, int> paramOffsets;
    RegisterAllocator regAlloc;

    // ALLOCA 寄存器提升：将符合条件的 ALLOCA 映射到物理寄存器
    std::unordered_map<IR::Value*, std::string> promotedAllocas;
    void promoteAllocasInFunction(IR::Function& func);
    bool isAllocaPromotable(IR::Instruction* alloca) const;

    // 全局变量地址缓存：避免循环内重复 la 指令
    std::unordered_map<IR::GlobalVariable*, std::string> globalAddrCache;
    void collectGlobalAddresses(IR::Function& func);

    void emitGlobal(IR::GlobalVariable* gv);
    void emitGlobalInitData(IR::Constant* init, IR::Type* type, const std::string& indent);

    void emitFunction(IR::Function& func);
    void emitBasicBlock(IR::BasicBlock& bb);
    void emitInstruction(IR::Instruction& inst);

    void computeStackLayout(IR::Function& func);
    int getTypeSize(IR::Type* t);
    int getStackOffset(IR::Value* val);
    int allocSlot(IR::Value* val);

    void emitPrologue(IR::Function& func);
    void emitEpilogue(IR::Function& func);

    void emitRet(IR::Instruction& inst);
    void emitBr(IR::Instruction& inst);
    void emitCondBr(IR::Instruction& inst);
    void emitBinOp(IR::Instruction& inst);
    void emitFBinOp(IR::Instruction& inst);
    void emitIcmp(IR::Instruction& inst);
    void emitFcmp(IR::Instruction& inst);
    void emitLoad(IR::Instruction& inst);
    void emitStore(IR::Instruction& inst);
    void emitCall(IR::Instruction& inst);
    void emitGetElementPtr(IR::Instruction& inst);
    void emitSitofp(IR::Instruction& inst);
    void emitFptosi(IR::Instruction& inst);

    std::string loadToReg(IR::Value* val, const std::string& destReg);
    std::string storeFromReg(IR::Value* val, const std::string& srcReg, bool addNew = false);
    std::string getValueReg(IR::Value* val);
    std::string emitGlobalAddr(IR::GlobalVariable* gv, const std::string& destReg);
    std::string emitValueToReg(IR::Value* val, const std::string& destReg);

    // Helpers for large stack frames exceeding RISC-V 12-bit immediate
    static bool fitsImm12(int val) { return val >= -2048 && val <= 2047; }
    std::string emitStackLoad(const std::string& reg, int offset, const std::string& insn);
    std::string emitStackStore(const std::string& reg, int offset, const std::string& insn);
    std::string emitSPAddImm(int delta);
};

}