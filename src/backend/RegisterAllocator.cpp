#include "backend/RegisterAllocator.h"
#include "opt/Optimizer.h"
#include <algorithm>
#include <cassert>
#include <set>

namespace Backend {

const std::vector<std::string> RegisterAllocator::INT_REGS = {
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11",
    "t3", "t4", "t5", "t6"   // caller-saved registers for short-lived values
};

const std::vector<std::string> RegisterAllocator::FLOAT_REGS = {
    "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11",
    "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "ft8", "ft9", "ft10", "ft11"
    // ft0/ft1 are reserved as scratch registers in codegen
};

RegisterAllocator::RegisterAllocator()
    : maxInstId(0), nextSpillSlot(0), spillSlotSize(0) {}

void RegisterAllocator::allocate(IR::Function& func) {
    regMap.clear();
    spillMap.clear();
    floatValues.clear();
    intervals.clear();
    // usedCalleeSaved 不清空 — reserveReg() 在 allocate() 之前调用会预填充
    nextSpillSlot = 0;
    spillSlotSize = 0;

    maxInstId = assignInstructionIds(func);
    buildIntervals(func);
    linearScan();
}

void RegisterAllocator::reserveReg(const std::string& reg) {
    reservedRegs.insert(reg);
    if (std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(), reg)
        == usedCalleeSaved.end()) {
        usedCalleeSaved.push_back(reg);
    }
}

bool RegisterAllocator::isRegReserved(const std::string& reg) const {
    return reservedRegs.count(reg) > 0;
}

void RegisterAllocator::clearReservedRegs() {
    reservedRegs.clear();
    usedCalleeSaved.clear();
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

    // Build instId → block mapping and block → max instId mapping
    std::unordered_map<int, IR::BasicBlock*> idToBlock;
    std::unordered_map<IR::BasicBlock*, int> blockMaxId;
    for (auto& bb : func.getBlocks()) {
        int maxId = -1;
        for (auto& inst : bb->getInstructions()) {
            int curId = instId[inst.get()];
            idToBlock[curId] = bb.get();
            maxId = std::max(maxId, curId);
        }
        blockMaxId[bb.get()] = maxId;
    }

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
                    // 不在此处设置 firstSeen。
                    // firstSeen 应当始终是定义点（产生该值的指令），
                    // 而非第一个使用点。如果基本块重排导致使用
                    // 先于定义被遍历到，此处错误设置 firstSeen
                    // 会破坏活跃区间，导致线性扫描分配器错误分配寄存器。
                    // 仅由 ALLOCA 产生且未被提升的指针值不会出现在
                    // firstSeen 中，这是正确的——它们不需要寄存器。
                }
                lastSeen[opVal] = curId;

                auto* opTy = opVal->getType();
                if (opTy && opTy->isFloat()) {
                    floatValues.insert(opVal);
                }
            }
        }
    }

    // ================================================================
    // Loop-aware liveness extension:
    // Values defined outside a loop body but used inside must have
    // their live intervals extended to cover the entire loop body.
    // Without this, the linear scan allocator may assign the same
    // register to a hoisted value and a loop-local value, causing
    // the hoisted value to be clobbered across loop iterations.
    // ================================================================
    auto dom = Opt::computeDominators(&func);
    auto preds = Opt::buildPredecessors(&func);
    auto succs = Opt::buildSuccessors(&func);

    // Find all loops (back-edges) and compute loop body sets
    struct LoopInfo {
        Opt::BBSet body;
        int maxLoopId;
    };
    std::vector<LoopInfo> loops;

    for (auto& bb : func.getBlocks()) {
        for (auto* succ : succs[bb.get()]) {
            if (Opt::strictlyDominates(succ, bb.get(), dom)) {
                // Back-edge: bb → succ, where succ is the loop header
                LoopInfo loop;
                loop.body.insert(succ); // header is part of loop body

                std::vector<IR::BasicBlock*> worklist;
                std::unordered_set<IR::BasicBlock*> visited;
                worklist.push_back(bb.get());
                visited.insert(bb.get());

                while (!worklist.empty()) {
                    auto* cur = worklist.back();
                    worklist.pop_back();
                    loop.body.insert(cur);
                    for (auto* p : preds[cur]) {
                        if (!visited.count(p) && !loop.body.count(p)) {
                            visited.insert(p);
                            worklist.push_back(p);
                        }
                    }
                }

                loop.maxLoopId = -1;
                for (auto* loopBB : loop.body) {
                    auto it = blockMaxId.find(loopBB);
                    if (it != blockMaxId.end()) {
                        loop.maxLoopId = std::max(loop.maxLoopId, it->second);
                    }
                }
                loops.push_back(std::move(loop));
            }
        }
    }

    // Extend intervals for values defined outside a loop but used inside
    for (auto& loop : loops) {
        for (auto it = firstSeen.begin(); it != firstSeen.end(); ++it) {
            auto* val = it->first;
            int startId = it->second;
            int endId = lastSeen[val];

            // Check if definition is outside the loop body
            auto defBlockIt = idToBlock.find(startId);
            if (defBlockIt == idToBlock.end()) continue;
            if (loop.body.count(defBlockIt->second)) continue; // defined inside loop, skip

            // Check if any use is inside the loop body
            bool usedInLoop = false;
            auto endBlockIt = idToBlock.find(endId);
            if (endBlockIt != idToBlock.end() && loop.body.count(endBlockIt->second)) {
                usedInLoop = true;
            }

            if (usedInLoop) {
                // Extend the interval to cover the entire loop body
                lastSeen[val] = std::max(lastSeen[val], loop.maxLoopId);
            }
        }
    }

    // ================================================================
    // FoldMemoryAccess liveness extension:
    // When a GEP has only one use (a LOAD/STORE), the GEP's operands
    // must be live until the LOAD/STORE instruction, not just until
    // the GEP instruction. Otherwise the register allocator may reuse
    // the GEP operands' registers for the LOAD/STORE's value.
    // ================================================================
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::GETELEMENTPTR)
                continue;
            if (!inst->hasOneUse())
                continue;
            auto& uses = inst->getUses();
            auto* user = dynamic_cast<IR::Instruction*>(uses[0].user);
            if (!user) continue;
            auto userOp = user->getOpcode();
            if (userOp != IR::Instruction::Opcode::LOAD &&
                userOp != IR::Instruction::Opcode::STORE)
                continue;

            // Folded GEP: extend the live ranges of all GEP operands
            // to cover the LOAD/STORE instruction
            auto userIdIt = instId.find(user);
            if (userIdIt == instId.end()) continue;
            int userId = userIdIt->second;

            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                auto* opVal = inst->getOperand(i);
                if (!opVal) continue;
                if (dynamic_cast<IR::Constant*>(opVal)) continue;
                if (dynamic_cast<IR::BasicBlock*>(opVal)) continue;
                if (dynamic_cast<IR::Function*>(opVal)) continue;
                if (dynamic_cast<IR::GlobalVariable*>(opVal)) continue;
                auto it = lastSeen.find(opVal);
                if (it != lastSeen.end()) {
                    it->second = std::max(it->second, userId);
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

        // 移除已被预留的寄存器
        for (const auto& r : reservedRegs) {
            freeRegs.erase(r);
        }

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

void RegisterAllocator::setReg(IR::Value* val, const std::string& reg) {
    regMap[val] = reg;
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