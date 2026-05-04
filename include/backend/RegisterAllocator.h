#pragma once

#include "ir/IR.h"
#include "TargetCodeGen.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>

namespace Backend {

struct LiveInterval {
    IR::Value* value;
    int start;
    int end;
    Register reg;
    bool spilled;
};

class RegisterAllocator {
private:
    std::vector<Register> availableRegs;
    std::unordered_map<IR::Value*, Register> regMap;
    std::unordered_map<IR::Value*, int> spillSlots;
    int nextSpillSlot;

public:
    RegisterAllocator();

    void allocate(IR::Function& func);
    Register getRegister(IR::Value* val);
    int getSpillSlot(IR::Value* val);

private:
    void computeLiveIntervals(IR::Function& func, std::vector<LiveInterval>& intervals);
    void linearScan(std::vector<LiveInterval>& intervals);
    Register selectRegister(LiveInterval& current, std::vector<LiveInterval>& active);
    void spill(LiveInterval& toSpill, std::vector<LiveInterval>& active, LiveInterval& current);
};

}
