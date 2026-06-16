#include "backend/RegisterAllocator.h"
#include <algorithm>
#include <cassert>
#include <set>

namespace Backend {

const std::vector<std::string> RegisterAllocator::INT_REGS = {
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11"
};

const std::vector<std::string> RegisterAllocator::FLOAT_REGS = {
    "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11"
};

RegisterAllocator::RegisterAllocator()
    : maxInstId(0), nextSpillSlot(0), spillSlotSize(0) {}

void RegisterAllocator::allocate(IR::Function& func) {
    regMap.clear();
    spillMap.clear();
    floatValues.clear();
    intervals.clear();
    usedCalleeSaved.clear();
    nextSpillSlot = 0;
    spillSlotSize = 0;

    maxInstId = assignInstructionIds(func);
    buildIntervals(func);
    linearScan();
}

int RegisterAllocator::assignInstructionIds(IR::Function& func) {
    int id = 0;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            instId[inst.get()] = id++;
        }
    }
    return id;
}

void RegisterAllocator::buildIntervals(IR::Function& func) {
    std::unordered_map<IR::Value*, int> firstSeen;
    std::unordered_map<IR::Value*, int> lastSeen;

    for (unsigned i = 0; i < func.getNumArgs(); ++i) {
        auto* arg = func.getArg(i);
        firstSeen[arg] = 0;
        lastSeen[arg] = 0;
    }

    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            int curId = instId[inst.get()];

            auto* ty = inst->getType();
            bool instIsFloat = ty && ty->isFloat();

            if (inst->getOpcode() != IR::Instruction::Opcode::RET
                && inst->getOpcode() != IR::Instruction::Opcode::BR
                && inst->getOpcode() != IR::Instruction::Opcode::COND_BR
                && inst->getOpcode() != IR::Instruction::Opcode::STORE
                && inst->getOpcode() != IR::Instruction::Opcode::ALLOCA
                && ty && !ty->isVoid()) {
                IR::Value* vr = inst.get();
                if (!dynamic_cast<IR::Constant*>(vr)) {
                    if (firstSeen.find(vr) == firstSeen.end()) {
                        firstSeen[vr] = curId;
                    }
                    lastSeen[vr] = curId;
                    if (instIsFloat) {
                        floatValues.insert(vr);
                    }
                }
            }

            unsigned opCount = inst->getNumOperands();
            for (unsigned i = 0; i < opCount; ++i) {
                IR::Value* opVal = inst->getOperand(i);
                if (!opVal) continue;
                if (dynamic_cast<IR::Constant*>(opVal)) continue;
                if (dynamic_cast<IR::BasicBlock*>(opVal)) continue;
                if (dynamic_cast<IR::Function*>(opVal)) continue;
                if (dynamic_cast<IR::GlobalVariable*>(opVal)) continue;

                if (firstSeen.find(opVal) == firstSeen.end()) {
                    firstSeen[opVal] = curId;
                }
                lastSeen[opVal] = curId;

                auto* opTy = opVal->getType();
                if (opTy && opTy->isFloat()) {
                    floatValues.insert(opVal);
                }
            }
        }
    }

    for (auto it = firstSeen.begin(); it != firstSeen.end(); ++it) {
        LiveInterval interval;
        interval.value = it->first;
        interval.start = it->second;
        interval.end = lastSeen[it->first];
        interval.isFloat = floatValues.count(it->first) > 0;
        interval.reg = "";
        interval.spillSlot = -1;
        intervals.push_back(interval);
    }

    std::sort(intervals.begin(), intervals.end(),
        [](const LiveInterval& a, const LiveInterval& b) {
            return a.start < b.start;
        });
}

void RegisterAllocator::linearScan() {
    std::vector<LiveInterval*> active;

    for (auto& current : intervals) {
        expireOldIntervals(current.start, active);

        const auto& regPool = current.isFloat ? FLOAT_REGS : INT_REGS;
        std::set<std::string> freeRegs(regPool.begin(), regPool.end());

        for (auto* a : active) {
            if (!a->reg.empty()) {
                freeRegs.erase(a->reg);
            }
        }

        if (!freeRegs.empty()) {
            current.reg = *freeRegs.begin();
            regMap[current.value] = current.reg;
            active.push_back(&current);
            if (std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(), current.reg)
                == usedCalleeSaved.end()) {
                usedCalleeSaved.push_back(current.reg);
            }
        } else {
            spillAtInterval(current, active);
        }
    }
}

void RegisterAllocator::expireOldIntervals(int pos, std::vector<LiveInterval*>& active) {
    auto it = active.begin();
    while (it != active.end()) {
        if ((*it)->end < pos) {
            it = active.erase(it);
        } else {
            ++it;
        }
    }
}

void RegisterAllocator::spillAtInterval(LiveInterval& current, std::vector<LiveInterval*>& active) {
    LiveInterval* toSpill = nullptr;
    int farthestEnd = -1;

    // Only spill intervals of the same type (float/int) to keep register types correct
    for (auto* a : active) {
        if (a->spillSlot < 0 && a->isFloat == current.isFloat && a->end > farthestEnd) {
            farthestEnd = a->end;
            toSpill = a;
        }
    }

    if (toSpill && farthestEnd > current.end) {
        std::string freedReg = toSpill->reg;
        toSpill->reg = "";
        regMap.erase(toSpill->value);
        toSpill->spillSlot = nextSpillSlot;
        spillMap[toSpill->value] = nextSpillSlot;
        nextSpillSlot += 8;
        spillSlotSize = std::max(spillSlotSize, nextSpillSlot);

        current.reg = freedReg;
        regMap[current.value] = current.reg;
        if (std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(), current.reg)
            == usedCalleeSaved.end()) {
            usedCalleeSaved.push_back(current.reg);
        }
    } else {
        current.spillSlot = nextSpillSlot;
        spillMap[current.value] = nextSpillSlot;
        nextSpillSlot += 8;
        spillSlotSize = std::max(spillSlotSize, nextSpillSlot);
    }

    active.push_back(&current);
}

bool RegisterAllocator::hasReg(IR::Value* val) const {
    return regMap.find(val) != regMap.end();
}

std::string RegisterAllocator::getReg(IR::Value* val) const {
    auto it = regMap.find(val);
    return it != regMap.end() ? it->second : "";
}

int RegisterAllocator::getSpillSlot(IR::Value* val) const {
    auto it = spillMap.find(val);
    return it != spillMap.end() ? it->second : -1;
}

bool RegisterAllocator::isFloatValue(IR::Value* val) const {
    return floatValues.count(val) > 0;
}

const std::vector<std::string>& RegisterAllocator::getUsedCalleeSaved() const {
    return usedCalleeSaved;
}

int RegisterAllocator::getTotalSpillSize() const {
    return spillSlotSize;
}

} // namespace Backend