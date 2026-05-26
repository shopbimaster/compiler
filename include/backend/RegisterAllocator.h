#pragma once

#include "ir/IR.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace Backend {

struct LiveInterval {
    IR::Value* value;
    int start;
    int end;
    bool isFloat;
    std::string reg;
    int spillSlot;
};

class RegisterAllocator {
public:
    RegisterAllocator();

    void allocate(IR::Function& func);

    bool hasReg(IR::Value* val) const;
    std::string getReg(IR::Value* val) const;
    int getSpillSlot(IR::Value* val) const;
    bool isFloatValue(IR::Value* val) const;
    const std::vector<std::string>& getUsedCalleeSaved() const;
    int getTotalSpillSize() const;

private:
    int assignInstructionIds(IR::Function& func);
    void buildIntervals(IR::Function& func);
    void linearScan();
    void expireOldIntervals(int pos, std::vector<LiveInterval*>& active);
    void spillAtInterval(LiveInterval& current, std::vector<LiveInterval*>& active);

    std::vector<LiveInterval> intervals;
    std::unordered_map<IR::Instruction*, int> instId;
    int maxInstId;

    std::unordered_map<IR::Value*, std::string> regMap;
    std::unordered_map<IR::Value*, int> spillMap;
    std::unordered_set<IR::Value*> floatValues;
    int nextSpillSlot;
    int spillSlotSize;

    std::vector<std::string> usedCalleeSaved;

    static const std::vector<std::string> INT_REGS;
    static const std::vector<std::string> FLOAT_REGS;
};

} // namespace Backend