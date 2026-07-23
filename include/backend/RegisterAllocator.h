#pragma once

#include "ir/IR.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace Backend {

struct LiveInterval {
    IR::Value* value;
    int start;
    int end;
    bool isFloat;
    std::string reg;
    int spillSlot;
    int useCount;     // number of times this value is used as an operand
    int loopDepth;    // maximum loop nesting depth (0 = outside all loops)
    bool crossesCall; // true if this interval spans any call instruction
};

class RegisterAllocator {
public:
    RegisterAllocator();

    void allocate(IR::Function& func);
    // K1+K2: 重建 usedCalleeSaved，移除 coalescePhis 释放的寄存器 (K1)
    //        和被代码生成器折叠的 GEP/ICMP 指令的寄存器 (K2)
    void pruneUnusedCalleeSaved(const std::set<IR::Instruction*>& deadInsts);

    void reserveReg(const std::string& reg);
    void clearReservedRegs();
    bool isRegReserved(const std::string& reg) const;

    bool hasReg(IR::Value* val) const;
    std::string getReg(IR::Value* val) const;
    void setReg(IR::Value* val, const std::string& reg);
    int getSpillSlot(IR::Value* val) const;
    bool isFloatValue(IR::Value* val) const;
    const std::vector<std::string>& getUsedCalleeSaved() const;
    int getTotalSpillSize() const;
    // Returns registers holding values live across the given call instruction.
    // Only values whose live interval spans the call (defined before, used after)
    // are included. This avoids saving/restoring caller-saved registers that
    // aren't actually live at this call site.
    std::vector<std::string> getRegsLiveAtCall(IR::Instruction* callInst) const;

private:
    int assignInstructionIds(IR::Function& func);
    void buildIntervals(IR::Function& func);
    void linearScan();
    void colorAllocate();
    bool colorRegClass(bool isFloat);
    static bool useGraphColoring();
    static bool useAutoSelection();
    long long estimateAllocationCost(IR::Function& func) const;
    void expireOldIntervals(int pos, std::vector<LiveInterval*>& active);
    void spillAtInterval(LiveInterval& current, std::vector<LiveInterval*>& active);
    void coalescePhis(IR::Function& func);

    std::vector<LiveInterval> intervals;
    std::unordered_map<IR::Instruction*, int> instId;
    int maxInstId;

    std::unordered_map<IR::Value*, std::string> regMap;
    std::unordered_map<IR::Value*, int> spillMap;
    std::unordered_set<IR::Value*> floatValues;
    int nextSpillSlot;
    int spillSlotSize;

    std::vector<std::string> usedCalleeSaved;
    std::unordered_set<std::string> reservedRegs;

    static const std::vector<std::string> INT_REGS;
    static const std::vector<std::string> FLOAT_REGS;
};

} // namespace Backend