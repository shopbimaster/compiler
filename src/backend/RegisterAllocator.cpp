#include "backend/RegisterAllocator.h"
#include "opt/Optimizer.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <set>
#include <unordered_set>

namespace Backend {

const std::vector<std::string> RegisterAllocator::INT_REGS = {
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11",
    "t3", "t4", "t5", "t6"    // caller-saved registers for short-lived values
    // a0-a7 excluded: using them for allocation causes massive save/restore
    // overhead at call sites, especially for recursive float-heavy code.
    // They are reserved for parameter passing and return values.
};

const std::vector<std::string> RegisterAllocator::FLOAT_REGS = {
    "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11",
    "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "ft8", "ft9", "ft10", "ft11"
    // fa0-fa7 excluded: same reason as a0-a7.
    // ft0/ft1 are reserved as scratch registers in codegen.
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
    coalescePhis(func);  // 修复安全检查后重新启用
}

// ★ K1+K2 修复：重建 usedCalleeSaved，移除两种情况下残留的无用寄存器：
//   K1: coalescePhis 将 incoming 的寄存器重新指派为 phiReg 后，
//       incoming 原来的寄存器仍残留在 usedCalleeSaved 中。
//   K2: 代码生成器将 GEP+LOAD/STORE 融合、ICMP+COND_BR 内联，
//       这些指令的寄存器从未被写入，但仍残留在 usedCalleeSaved 中。
//   deadInsts: 代码生成器收集的"折叠"指令集（GEP/ICMP），其寄存器不写入
void RegisterAllocator::pruneUnusedCalleeSaved(
    const std::set<IR::Instruction*>& deadInsts) {
    std::unordered_set<std::string> usedRegs;
    for (auto& kv : regMap) {
        if (kv.second.empty()) continue;
        // 跳过被折叠的指令（GEP+LOAD/STORE 融合、ICMP+COND_BR 内联）
        auto* inst = dynamic_cast<IR::Instruction*>(kv.first);
        if (inst && deadInsts.count(inst)) continue;
        usedRegs.insert(kv.second);
    }
    auto oldList = usedCalleeSaved;
    usedCalleeSaved.clear();
    for (const auto& reg : oldList) {
        // 保留：预留寄存器（全局地址缓存/promoted ALLOCA）或仍被使用的寄存器
        if (reservedRegs.count(reg) || usedRegs.count(reg)) {
            usedCalleeSaved.push_back(reg);
        }
    }
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
    // Track all blocks where a value is used (for loop-aware liveness extension)
    std::unordered_map<IR::Value*, std::unordered_set<IR::BasicBlock*>> useBlocks;

    // Build instId → block mapping and block → min/max instId mapping
    std::unordered_map<int, IR::BasicBlock*> idToBlock;
    std::unordered_map<IR::BasicBlock*, int> blockMaxId;
    std::unordered_map<IR::BasicBlock*, int> blockMinId;
    for (auto& bb : func.getBlocks()) {
        int maxId = -1;
        int minId = INT_MAX;
        for (auto& inst : bb->getInstructions()) {
            int curId = instId[inst.get()];
            idToBlock[curId] = bb.get();
            maxId = std::max(maxId, curId);
            minId = std::min(minId, curId);
        }
        blockMaxId[bb.get()] = maxId;
        blockMinId[bb.get()] = minId;
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
                    // ★ PHI 的定义点应视为 BB 入口（blockMinId），而非 PHI 指令本身的 ID。
                    // 原因：PHI 在 BB 入口处"并行"执行，语义上在所有非 PHI 指令之前。
                    // 如果 PHI 在 IR 中不在 BB 开头（可能在 mul/add 之后），
                    // 基于指令 ID 的活跃区间计算会错误地认为 PHI 和前面的指令
                    // （如 mul）不重叠，导致它们被分配到同一寄存器，mul 覆盖 PHI 的值。
                    // 修复：PHI 的 firstSeen 设为 blockMinId，使活跃区间从 BB 入口开始。
                    // （62_percolation 无限循环根因：s5 同时被用作 i.i.phi 和 t3.i）
                    if (inst->getOpcode() == IR::Instruction::Opcode::PHI) {
                        auto minIt = blockMinId.find(bb.get());
                        int bbMinId = (minIt != blockMinId.end()) ? minIt->second : curId;
                        if (firstSeen.find(vr) == firstSeen.end()) {
                            firstSeen[vr] = bbMinId;
                        }
                        lastSeen[vr] = std::max(lastSeen[vr], curId);
                    } else {
                        if (firstSeen.find(vr) == firstSeen.end()) {
                            firstSeen[vr] = curId;
                        }
                        lastSeen[vr] = curId;
                    }
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
                useBlocks[opVal].insert(bb.get());

                auto* opTy = opVal->getType();
                if (opTy && opTy->isFloat()) {
                    floatValues.insert(opVal);
                }
            }
        }
    }

    // ================================================================
    // PHI elimination liveness extension:
    // PHI 的 incoming 值需要在前驱块末尾（terminator 之前）存活，
    // 因为 emitPhiMovesForEdge 在那里发射寄存器拷贝。
    // 对于回边（back-edge），incoming 值定义在 latch（高 ID），
    // 而 PHI 在 loop header（低 ID），lastSeen 默认会被设为 PHI 的 ID，
    // 导致 firstSeen > lastSeen（无效区间）。
    // 修复：将 incoming 的 lastSeen 扩展到前驱块的末尾。
    // ================================================================
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                if (!predBB) continue;
                IR::Value* incoming = inst->getOperand(i);
                if (!incoming) continue;
                if (dynamic_cast<IR::Constant*>(incoming)) continue;
                if (dynamic_cast<IR::BasicBlock*>(incoming)) continue;
                if (dynamic_cast<IR::Function*>(incoming)) continue;
                if (dynamic_cast<IR::GlobalVariable*>(incoming)) continue;
                auto maxIt = blockMaxId.find(predBB);
                if (maxIt != blockMaxId.end()) {
                    auto lsIt = lastSeen.find(incoming);
                    if (lsIt != lastSeen.end()) {
                        lsIt->second = std::max(lsIt->second, maxIt->second);
                    } else {
                        lastSeen[incoming] = maxIt->second;
                    }
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

    // Find all loops (back-edges) and compute loop body sets.
    // IMPORTANT: Merge loops with the same header before computing
    // minLoopId/maxLoopId. Without merging, a multi-back-edge loop
    // (e.g., while with continue) produces separate NaturalLoops whose
    // bodies are incomplete. A value used only in one body but needing
    // to survive across the other body's blocks (e.g., then_12 with
    // continue) would not have its live interval extended to cover
    // those blocks, causing the register allocator to reuse its
    // register and clobber it (01_mm1 SEGFAULT root cause).
    struct LoopInfo {
        Opt::BBSet body;
        IR::BasicBlock* header;
        int minLoopId;
        int maxLoopId;
    };
    std::vector<LoopInfo> loops;
    std::unordered_map<IR::BasicBlock*, size_t> headerToLoop;

    for (auto& bb : func.getBlocks()) {
        for (auto* succ : succs[bb.get()]) {
            if (Opt::strictlyDominates(succ, bb.get(), dom)) {
                // Back-edge: bb → succ, where succ is the loop header
                auto it = headerToLoop.find(succ);
                if (it == headerToLoop.end()) {
                    LoopInfo loop;
                    loop.header = succ;
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

                    loop.minLoopId = INT_MAX;
                    loop.maxLoopId = -1;
                    for (auto* loopBB : loop.body) {
                        auto maxIt = blockMaxId.find(loopBB);
                        if (maxIt != blockMaxId.end()) {
                            loop.maxLoopId = std::max(loop.maxLoopId, maxIt->second);
                        }
                        auto minIt = blockMinId.find(loopBB);
                        if (minIt != blockMinId.end()) {
                            loop.minLoopId = std::min(loop.minLoopId, minIt->second);
                        }
                    }
                    headerToLoop[succ] = loops.size();
                    loops.push_back(std::move(loop));
                } else {
                    // Merge: same header, add this back-edge's body to existing loop
                    auto& existing = loops[it->second];
                    std::vector<IR::BasicBlock*> worklist;
                    std::unordered_set<IR::BasicBlock*> visited;
                    worklist.push_back(bb.get());
                    visited.insert(bb.get());

                    while (!worklist.empty()) {
                        auto* cur = worklist.back();
                        worklist.pop_back();
                        if (!existing.body.count(cur)) {
                            existing.body.insert(cur);
                            for (auto* p : preds[cur]) {
                                if (!visited.count(p) && !existing.body.count(p)) {
                                    visited.insert(p);
                                    worklist.push_back(p);
                                }
                            }
                        }
                    }

                    // Recompute minLoopId/maxLoopId with merged body
                    existing.minLoopId = INT_MAX;
                    existing.maxLoopId = -1;
                    for (auto* loopBB : existing.body) {
                        auto maxIt = blockMaxId.find(loopBB);
                        if (maxIt != blockMaxId.end()) {
                            existing.maxLoopId = std::max(existing.maxLoopId, maxIt->second);
                        }
                        auto minIt = blockMinId.find(loopBB);
                        if (minIt != blockMinId.end()) {
                            existing.minLoopId = std::min(existing.minLoopId, minIt->second);
                        }
                    }
                }
            }
        }
    }

    // Extend intervals for values used inside a loop to cover the entire
    // loop body. This is necessary because the linear instruction-ID ordering
    // does not match the execution order for loops with back-edges. A value
    // defined in the loop header (e.g., a PHI) and used in a block that
    // appears earlier in the linear order (but is executed later in the
    // control flow) must be live across the entire loop body, including
    // blocks that come after its last use in the linear order.
    //
    // Example (56_sort_test2):
    //   Block layout: while_cond_0 → while_body_1 → ... → while_end_5 → and_rhs_6
    //   Control flow: while_cond_3 → and_rhs_6 → and_merge_7 → ... → while_end_5
    //   %i.phi is defined in while_cond_0 (ID 5), last used in while_end_5 (ID 30).
    //   Without extension, %slt (defined in and_rhs_6, ID 33) would reuse %i.phi's
    //   register, corrupting %i.phi before it's used in while_end_5.
    for (auto& loop : loops) {
        for (auto it = firstSeen.begin(); it != firstSeen.end(); ++it) {
            auto* val = it->first;
            int startId = it->second;

            // Verify the definition block exists
            auto defBlockIt = idToBlock.find(startId);
            if (defBlockIt == idToBlock.end()) continue;

            // Count how many distinct blocks within the loop body use this value.
            // Only extend if the value is used in more than one block within the
            // loop, OR if it's defined outside the loop but used inside (the
            // original case). Values used in only one block within the loop
            // don't need extension because their live range is local to that
            // block and won't conflict with blocks at different linear positions.
            int useBlockCountInLoop = 0;
            auto ubIt = useBlocks.find(val);
            if (ubIt != useBlocks.end()) {
                for (auto* useBB : ubIt->second) {
                    if (loop.body.count(useBB)) {
                        useBlockCountInLoop++;
                    }
                }
            }

            bool definedOutsideLoop = !loop.body.count(defBlockIt->second);
            bool usedInsideLoop = useBlockCountInLoop > 0;

            // Extend if:
            // 1. Defined outside, used inside (original case: hoisted values)
            // 2. Used in multiple blocks within the loop (cross-block liveness)
            // 3. Defined outside, live range spans into loop linear range but
            //    doesn't cover the entire loop body. This happens when a value
            //    is defined before the loop and used in the loop exit block,
            //    but the loop exit block comes before some loop body blocks
            //    in linear order (due to BB layout not matching control flow).
            //    Example: GEPStrengthReduce creates lsr.init in the preheader
            //    (before the loop) used in the loop exit block (while_end_5).
            //    If and_rhs_6 (loop body) comes after while_end_5 in linear
            //    order, lsr.init's live range [pre, while_end_5] doesn't
            //    cover and_rhs_6, causing register reuse → SEGFAULT.
            bool spansLoopPartial = false;
            if (definedOutsideLoop && !usedInsideLoop) {
                int lastId = lastSeen[val];
                if (lastId >= loop.minLoopId && lastId < loop.maxLoopId) {
                    spansLoopPartial = true;
                }
            }

            bool needsExtension = (usedInsideLoop && definedOutsideLoop)
                               || (useBlockCountInLoop > 1)
                               || spansLoopPartial;

            if (needsExtension) {
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

    // ================================================================
    // Compute loop depth for each block
    // Depth = number of loops that contain this block
    // ================================================================
    std::unordered_map<IR::BasicBlock*, int> blockDepth;
    for (auto& bb : func.getBlocks()) {
        int depth = 0;
        for (auto& loop : loops) {
            if (loop.body.count(bb.get())) {
                depth++;
            }
        }
        blockDepth[bb.get()] = depth;
    }

    // ================================================================
    // Compute use count for each value
    // ================================================================
    std::unordered_map<IR::Value*, int> useCount;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                IR::Value* opVal = inst->getOperand(i);
                if (!opVal) continue;
                if (dynamic_cast<IR::Constant*>(opVal)) continue;
                if (dynamic_cast<IR::BasicBlock*>(opVal)) continue;
                if (dynamic_cast<IR::Function*>(opVal)) continue;
                if (dynamic_cast<IR::GlobalVariable*>(opVal)) continue;
                useCount[opVal]++;
            }
        }
    }

    // ================================================================
    // Compute max loop depth for each value based on its use blocks
    // ================================================================
    std::unordered_map<IR::Value*, int> valLoopDepth;
    for (auto it = firstSeen.begin(); it != firstSeen.end(); ++it) {
        auto* val = it->first;
        int maxDepth = 0;
        auto ubIt = useBlocks.find(val);
        if (ubIt != useBlocks.end()) {
            for (auto* useBB : ubIt->second) {
                auto depthIt = blockDepth.find(useBB);
                if (depthIt != blockDepth.end()) {
                    maxDepth = std::max(maxDepth, depthIt->second);
                }
            }
        }
        valLoopDepth[val] = maxDepth;
    }

    for (auto it = firstSeen.begin(); it != firstSeen.end(); ++it) {
        LiveInterval interval;
        interval.value = it->first;
        interval.start = it->second;
        interval.end = lastSeen[it->first];
        interval.isFloat = floatValues.count(it->first) > 0;
        interval.reg = "";
        interval.spillSlot = -1;
        interval.useCount = useCount[it->first];
        interval.loopDepth = valLoopDepth[it->first];
        intervals.push_back(interval);
    }

    std::sort(intervals.begin(), intervals.end(),
        [](const LiveInterval& a, const LiveInterval& b) {
            if (a.start != b.start) return a.start < b.start;
            // tiebreaker: 确保相同 start 时顺序确定（消除 unordered_map 迭代顺序的非确定性）
            if (a.end != b.end) return a.end < b.end;
            return a.value->getName() < b.value->getName();
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
    // Compute spill cost: higher cost = more important to keep in register.
    // loopDepth * 10000: values in deeper loops are much more important
    // useCount * 100:    values used more often are more important
    // (end - start):     values with longer live ranges are more important
    // (this is the classic heuristic: longer ranges = harder to spill)
    auto spillCost = [](const LiveInterval& iv) -> int {
        return iv.loopDepth * 10000 + iv.useCount * 100 + (iv.end - iv.start);
    };

    int currentCost = spillCost(current);

    // Find the active interval with the lowest spill cost (easiest to spill)
    LiveInterval* toSpill = nullptr;
    int lowestCost = INT_MAX;

    for (auto* a : active) {
        if (a->spillSlot < 0 && a->isFloat == current.isFloat) {
            int cost = spillCost(*a);
            if (cost < lowestCost) {
                lowestCost = cost;
                toSpill = a;
            }
        }
    }

    // Spill the one with lower spill cost, but only if current is more important
    if (toSpill && lowestCost < currentCost) {
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

// ================================================================
// PHI copy coalescing:
// When a PHI's incoming value is defined by a single instruction
// whose result is only used by this PHI, we can assign the same
// register to both, eliminating the "mv" copy instruction.
//
// Example (h-5-01 inner loop):
//   subw s11, s7, s1    # t27 = w - mul
//   mv  s7, s11         # w.phi = t27
// After coalescing:
//   subw s7, s7, s1     # w = w - mul  (no mv needed!)
// ================================================================
void RegisterAllocator::coalescePhis(IR::Function& func) {
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::PHI)
                continue;

            auto* phi = inst.get();
            std::string phiReg = getReg(phi);
            if (phiReg.empty()) continue; // PHI spilled, can't coalesce

            for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
                auto* incoming = phi->getOperand(i);
                if (!incoming) continue;
                if (dynamic_cast<IR::Constant*>(incoming)) continue;

                // Check: incoming is defined by a single instruction
                // and only used by this PHI
                if (!incoming->hasOneUse()) continue;
                auto& uses = incoming->getUses();
                if (uses.size() != 1) continue;
                if (uses[0].user != phi) continue;

                // Check: incoming has a register
                std::string incomingReg = getReg(incoming);
                if (incomingReg.empty()) continue; // spilled

                // Already same register? No need to coalesce
                if (incomingReg == phiReg) continue;

                // Check safety: the PHI's register must not be used
                // by any other active interval at the point of the
                // incoming value's definition.
                // The incoming value is only used by the PHI, so
                // the only use of the PHI's register at the point
                // of the incoming value's definition is the PHI's
                // old value (which is being read by the instruction
                // that defines the incoming value).
                //
                // This is safe for read-modify-write operations
                // (like subw s7, s7, s1 → subw s7, s7, s1).
                // The instruction reads the old value and writes
                // the new value to the same register.

                // Find the definition instruction of the incoming value
                auto* defInst = dynamic_cast<IR::Instruction*>(incoming);
                if (!defInst) continue;

                // Verify: the instruction uses the PHI as a source
                // (read-modify-write pattern), which is always safe
                // to coalesce.
                bool usesPhi = false;
                for (unsigned j = 0; j < defInst->getNumOperands(); ++j) {
                    if (defInst->getOperand(j) == phi) {
                        usesPhi = true;
                        break;
                    }
                }

                // Only coalesce if the instruction uses the PHI as a source.
                // This ensures the instruction is a read-modify-write
                // (e.g., subw s7, s7, s1) and the PHI's register is not
                // being used by a different live interval.
                if (!usesPhi) continue;

                // ★ 安全检查：PHI 在 incoming 定义所在 BB 中不能有其他使用。
                //   coalescing 后，incoming 定义写入 phiReg。如果 PHI 在同一 BB
                //   的 incoming 定义之后还有使用，会读到 incoming 的值而非 PHI
                //   的值，导致语义错误（SEGFAULT/无限循环）。
                //   使用在其他 BB（body/header）是安全的，因为它们在 latch 之前执行。
                auto* defBB = defInst->getParent();
                bool hasOtherUseInDefBB = false;
                for (auto& phiUse : phi->getUses()) {
                    auto* phiUser = dynamic_cast<IR::Instruction*>(phiUse.user);
                    if (!phiUser) continue;
                    if (phiUser == defInst) continue; // read-modify-write 本身
                    if (phiUser->getParent() == defBB) {
                        hasOtherUseInDefBB = true;
                        break;
                    }
                }
                if (hasOtherUseInDefBB) continue;

                // Coalesce: assign the PHI's register to the incoming value
                regMap[incoming] = phiReg;
            }
        }
    }
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

std::vector<std::string> RegisterAllocator::getRegsLiveAtCall(IR::Instruction* callInst) const {
    auto it = instId.find(callInst);
    if (it == instId.end()) return {};
    int callId = it->second;

    // A value is live across the call if it was defined before the call
    // (start < callId) and is used after the call (end > callId).
    // Values defined AT the call (return value) or whose last use IS the call
    // don't need to survive the call.
    std::set<std::string> liveRegs;
    for (const auto& interval : intervals) {
        if (interval.reg.empty()) continue;  // spilled, not in a register
        if (interval.start < callId && interval.end > callId) {
            liveRegs.insert(interval.reg);
        }
    }
    return std::vector<std::string>(liveRegs.begin(), liveRegs.end());
}

} // namespace Backend