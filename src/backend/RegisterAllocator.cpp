#include "backend/RegisterAllocator.h"
#include "opt/Optimizer.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <iostream>
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

    if (useAutoSelection()) {
        struct AllocationSnapshot {
            std::unordered_map<IR::Value*, std::string> regs;
            std::unordered_map<IR::Value*, int> spills;
            std::vector<std::string> intervalRegs;
            std::vector<int> intervalSpills;
            std::vector<std::string> usedRegs;
            int nextSlot;
            int spillSize;
            long long cost;
        };

        const auto reservedUsedRegs = usedCalleeSaved;
        auto resetCandidate = [&]() {
            regMap.clear();
            spillMap.clear();
            usedCalleeSaved = reservedUsedRegs;
            nextSpillSlot = 0;
            spillSlotSize = 0;
            for (auto& interval : intervals) {
                interval.reg.clear();
                interval.spillSlot = -1;
            }
        };
        auto captureCandidate = [&](long long cost) {
            AllocationSnapshot snapshot;
            snapshot.regs = regMap;
            snapshot.spills = spillMap;
            snapshot.usedRegs = usedCalleeSaved;
            snapshot.nextSlot = nextSpillSlot;
            snapshot.spillSize = spillSlotSize;
            snapshot.cost = cost;
            snapshot.intervalRegs.reserve(intervals.size());
            snapshot.intervalSpills.reserve(intervals.size());
            for (const auto& interval : intervals) {
                snapshot.intervalRegs.push_back(interval.reg);
                snapshot.intervalSpills.push_back(interval.spillSlot);
            }
            return snapshot;
        };
        auto restoreCandidate = [&](const AllocationSnapshot& snapshot) {
            regMap = snapshot.regs;
            spillMap = snapshot.spills;
            usedCalleeSaved = snapshot.usedRegs;
            nextSpillSlot = snapshot.nextSlot;
            spillSlotSize = snapshot.spillSize;
            for (size_t i = 0; i < intervals.size(); ++i) {
                intervals[i].reg = snapshot.intervalRegs[i];
                intervals[i].spillSlot = snapshot.intervalSpills[i];
            }
        };

        resetCandidate();
        linearScan();
        coalescePhis(func);
        auto linear = captureCandidate(estimateAllocationCost(func));

        resetCandidate();
        colorAllocate();
        coalescePhis(func);
        auto graph = captureCandidate(estimateAllocationCost(func));

        bool chooseGraph = graph.cost < linear.cost;
        restoreCandidate(chooseGraph ? graph : linear);
        const char* debug = std::getenv("RA_DEBUG_SELECTION");
        if (debug && std::string(debug) == "1") {
            std::cerr << "[RA] " << func.getName()
                      << " linear=" << linear.cost
                      << " graph=" << graph.cost
                      << " selected=" << (chooseGraph ? "graph" : "linear")
                      << '\n';
        }
    } else if (useGraphColoring()) {
        colorAllocate();
        coalescePhis(func);
    } else {
        linearScan();
        coalescePhis(func);
    }
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
    auto registerRank = [](const std::string& reg) {
        auto intIt = std::find(INT_REGS.begin(), INT_REGS.end(), reg);
        if (intIt != INT_REGS.end()) {
            return static_cast<int>(intIt - INT_REGS.begin());
        }
        auto floatIt = std::find(FLOAT_REGS.begin(), FLOAT_REGS.end(), reg);
        if (floatIt != FLOAT_REGS.end()) {
            return static_cast<int>(INT_REGS.size() +
                                    (floatIt - FLOAT_REGS.begin()));
        }
        return INT_MAX;
    };
    std::sort(usedCalleeSaved.begin(), usedCalleeSaved.end(),
        [&](const std::string& lhs, const std::string& rhs) {
            int lhsRank = registerRank(lhs);
            int rhsRank = registerRank(rhs);
            return lhsRank != rhsRank ? lhsRank < rhsRank : lhs < rhs;
        });
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
    // ================================================================
    // Collect call instruction IDs (needed for crossesCall analysis)
    // ================================================================
    std::vector<int> callIds;
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                auto it = instId.find(inst.get());
                if (it != instId.end()) {
                    callIds.push_back(it->second);
                }
            }
        }
    }
    // Sort for efficient interval checking
    std::sort(callIds.begin(), callIds.end());

    std::unordered_map<IR::Value*, int> firstSeen;
    std::unordered_map<IR::Value*, int> lastSeen;
    std::unordered_map<IR::Value*, int> stableOrder;
    int nextStableOrder = 0;
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
        stableOrder[arg] = nextStableOrder++;
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
                    stableOrder[vr] = nextStableOrder++;
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

    // ================================================================
    // Compute crossesCall for each value once (before interval creation)
    // ================================================================
    std::unordered_map<IR::Value*, bool> valCrossesCall;
    for (auto it = firstSeen.begin(); it != firstSeen.end(); ++it) {
        auto* val = it->first;
        int start = it->second;
        int end = lastSeen[val];
        bool crosses = false;
        // Check if any call falls strictly within (start, end).
        // Defined AT call (start == callId): the return value does NOT cross its
        // own call. Last used AT call (end == callId): the value is dead at the
        // call and does not need to survive it.
        for (int cid : callIds) {
            if (start < cid && cid < end) {
                crosses = true;
                break;
            }
        }
        valCrossesCall[val] = crosses;
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
        interval.crossesCall = valCrossesCall[it->first];
        intervals.push_back(interval);
    }

    std::sort(intervals.begin(), intervals.end(),
        [&stableOrder](const LiveInterval& a, const LiveInterval& b) {
            if (a.start != b.start) return a.start < b.start;
            if (a.end != b.end) return a.end < b.end;
            if (a.value->getName() != b.value->getName()) {
                return a.value->getName() < b.value->getName();
            }
            return stableOrder.at(a.value) < stableOrder.at(b.value);
        });
}

void RegisterAllocator::linearScan() {
    // ================================================================
    // RA-CALL-1: Call-aware register preference.
    //
    // Pool layout (both INT and FLOAT): callee-saved first (12 regs),
    // caller-saved last.  INT_REGS:  s0-s11 | t3-t6   (12+4)
    //                     FLOAT_REGS: fs0-fs11 | ft2-ft11 (12+10)
    //
    // - crossesCall=true  → prefer callee-saved (s*/fs*), avoid
    //   caller-save traffic at every call site
    // - crossesCall=false → prefer caller-saved (t*/ft*), keeping
    //   callee-saved registers free and reducing prologue traffic
    //
    // When the preferred class is exhausted we fall back to the other
    // class.  A caller-saved register assigned to a call-crossing
    // value is still correct: getRegsLiveAtCall will save/restore it
    // around each call.
    // ================================================================
    static const size_t CALLEE_COUNT = 12;  // s0-s11 or fs0-fs11

    std::vector<LiveInterval*> active;

    for (auto& current : intervals) {
        expireOldIntervals(current.start, active);

        const auto& regPool = current.isFloat ? FLOAT_REGS : INT_REGS;
        const size_t poolSize = regPool.size();

        // Build active-register set for O(1) lookup
        std::unordered_set<std::string> activeRegs;
        for (auto* a : active) {
            if (!a->reg.empty()) activeRegs.insert(a->reg);
        }

        auto tryAllocate = [&](const std::string& r) -> bool {
            if (reservedRegs.count(r)) return false;
            if (activeRegs.count(r)) return false;
            current.reg = r;
            regMap[current.value] = r;
            if (std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(), r)
                == usedCalleeSaved.end()) {
                usedCalleeSaved.push_back(r);
            }
            return true;
        };

        bool allocated = false;

        if (current.crossesCall) {
            // Prefer callee-saved (indices 0..CALLEE_COUNT-1),
            // fall back to caller-saved (CALLEE_COUNT..poolSize-1).
            for (size_t i = 0; i < poolSize; ++i) {
                if (tryAllocate(regPool[i])) { allocated = true; break; }
            }
        } else {
            // Prefer caller-saved first (later pool indices),
            // fall back to callee-saved (earlier pool indices).
            for (size_t i = CALLEE_COUNT; i < poolSize; ++i) {
                if (tryAllocate(regPool[i])) { allocated = true; break; }
            }
            if (!allocated) {
                for (size_t i = 0; i < CALLEE_COUNT; ++i) {
                    if (tryAllocate(regPool[i])) { allocated = true; break; }
                }
            }
        }

        if (allocated) {
            active.push_back(&current);
        } else {
            spillAtInterval(current, active);
        }
    }
}

bool RegisterAllocator::useGraphColoring() {
    static const bool enabled = [] {
        const char* allocator = std::getenv("RA_ALLOCATOR");
        return allocator && std::string(allocator) == "graph";
    }();
    return enabled;
}

bool RegisterAllocator::useAutoSelection() {
    static const bool enabled = [] {
        const char* allocator = std::getenv("RA_ALLOCATOR");
        return !allocator || std::string(allocator) == "auto";
    }();
    return enabled;
}

long long RegisterAllocator::estimateAllocationCost(IR::Function& func) const {
    auto isCalleeSaved = [](const std::string& reg) {
        return reg[0] == 's' ||
               (reg.size() >= 2 && reg[0] == 'f' && reg[1] == 's');
    };
    auto isCallerSaved = [](const std::string& reg) {
        return reg[0] == 't' ||
               (reg.size() >= 2 && reg[0] == 'f' && reg[1] == 't');
    };

    long long memoryOps = 0;
    long long moves = 0;

    for (const auto& interval : intervals) {
        if (regMap.find(interval.value) == regMap.end()) {
            memoryOps += 1 + interval.useCount;
        }
    }

    for (const auto& reg : usedCalleeSaved) {
        if (isCalleeSaved(reg)) {
            memoryOps += 2;
        }
    }

    for (const auto& entry : instId) {
        auto* inst = entry.first;
        if (inst->getOpcode() != IR::Instruction::Opcode::CALL) continue;
        int callId = entry.second;
        std::unordered_set<std::string> savedAtCall;
        for (const auto& interval : intervals) {
            if (!interval.reg.empty() &&
                interval.start < callId && callId < interval.end &&
                isCallerSaved(interval.reg)) {
                savedAtCall.insert(interval.reg);
            }
        }
        memoryOps += 2 * static_cast<long long>(savedAtCall.size());
    }

    for (const auto& block : func.getBlocks()) {
        for (const auto& inst : block->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
            auto dest = regMap.find(inst.get());
            if (dest == regMap.end()) continue;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto src = regMap.find(inst->getOperand(i));
                if (src != regMap.end() && src->second != dest->second) {
                    moves++;
                }
            }
        }
    }

    return memoryOps * 16 + moves;
}

void RegisterAllocator::colorAllocate() {
    colorRegClass(false);
    colorRegClass(true);
}

bool RegisterAllocator::colorRegClass(bool isFloat) {
    const auto& regPool = isFloat ? FLOAT_REGS : INT_REGS;
    std::vector<std::string> availableRegs;
    for (const auto& reg : regPool) {
        if (!reservedRegs.count(reg)) {
            availableRegs.push_back(reg);
        }
    }
    const int colorCount = static_cast<int>(availableRegs.size());

    std::vector<std::string> calleeFirst;
    std::vector<std::string> callerFirst;
    auto isCalleeSaved = [](const std::string& reg) {
        return reg[0] == 's' ||
               (reg.size() >= 2 && reg[0] == 'f' && reg[1] == 's');
    };
    for (const auto& reg : availableRegs) {
        if (isCalleeSaved(reg)) calleeFirst.push_back(reg);
    }
    for (const auto& reg : availableRegs) {
        if (!isCalleeSaved(reg)) calleeFirst.push_back(reg);
    }
    for (const auto& reg : availableRegs) {
        if (!isCalleeSaved(reg)) callerFirst.push_back(reg);
    }
    for (const auto& reg : availableRegs) {
        if (isCalleeSaved(reg)) callerFirst.push_back(reg);
    }

    std::vector<LiveInterval*> nodes;
    for (auto& interval : intervals) {
        if (interval.isFloat == isFloat && interval.reg.empty()) {
            nodes.push_back(&interval);
        }
    }
    if (nodes.empty()) return true;

    const int nodeCount = static_cast<int>(nodes.size());
    std::vector<std::unordered_set<int>> interference(nodeCount);
    for (int i = 0; i < nodeCount; ++i) {
        for (int j = i + 1; j < nodeCount; ++j) {
            if (nodes[i]->start <= nodes[j]->end &&
                nodes[j]->start <= nodes[i]->end) {
                interference[i].insert(j);
                interference[j].insert(i);
            }
        }
    }

    auto spillCost = [](const LiveInterval* interval) -> long long {
        return static_cast<long long>(interval->loopDepth) * 10000 +
               static_cast<long long>(interval->useCount) * 100 +
               (interval->end - interval->start);
    };

    std::vector<int> degree(nodeCount);
    std::vector<char> removed(nodeCount, 0);
    for (int i = 0; i < nodeCount; ++i) {
        degree[i] = static_cast<int>(interference[i].size());
    }

    std::vector<int> selectStack;
    selectStack.reserve(nodeCount);
    int remaining = nodeCount;
    while (remaining > 0) {
        int pick = -1;
        for (int i = 0; i < nodeCount; ++i) {
            if (!removed[i] && degree[i] < colorCount) {
                pick = i;
                break;
            }
        }

        if (pick == -1) {
            long long lowestCost = LLONG_MAX;
            for (int i = 0; i < nodeCount; ++i) {
                if (removed[i]) continue;
                long long cost = spillCost(nodes[i]);
                if (cost < lowestCost ||
                    (cost == lowestCost && pick != -1 &&
                     nodes[i]->value->getName() <
                         nodes[pick]->value->getName())) {
                    lowestCost = cost;
                    pick = i;
                }
            }
        }

        removed[pick] = 1;
        selectStack.push_back(pick);
        for (int neighbor : interference[pick]) {
            if (!removed[neighbor]) degree[neighbor]--;
        }
        remaining--;
    }

    bool allColored = true;
    for (auto it = selectStack.rbegin(); it != selectStack.rend(); ++it) {
        int node = *it;
        LiveInterval* interval = nodes[node];
        std::unordered_set<std::string> unavailable;
        for (int neighbor : interference[node]) {
            if (!nodes[neighbor]->reg.empty()) {
                unavailable.insert(nodes[neighbor]->reg);
            }
        }

        const auto& preference =
            interval->crossesCall ? calleeFirst : callerFirst;
        std::string chosen;
        for (const auto& reg : preference) {
            if (!unavailable.count(reg)) {
                chosen = reg;
                break;
            }
        }

        if (!chosen.empty()) {
            interval->reg = chosen;
            regMap[interval->value] = chosen;
            if (std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(),
                          chosen) == usedCalleeSaved.end()) {
                usedCalleeSaved.push_back(chosen);
            }
        } else {
            interval->spillSlot = nextSpillSlot;
            spillMap[interval->value] = nextSpillSlot;
            nextSpillSlot += 8;
            spillSlotSize = std::max(spillSlotSize, nextSpillSlot);
            allColored = false;
        }
    }
    return allColored;
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

                // PHI assignments on an edge are parallel.  Coalescing a
                // read-modify-write incoming value with its destination PHI
                // overwrites the PHI's old value before the edge copies run.
                // That is unsafe when a sibling PHI still needs the old value
                // from this same predecessor, for example:
                //
                //   b.next = b + x
                //   b = phi [b.next, latch]
                //   c = phi [b,      latch]
                //
                // Keep b.next in a distinct register so c receives old b.
                bool feedsSiblingPhiOnEdge = false;
                auto* phiBB = phi->getParent();
                if (phiBB && defBB) {
                    for (auto& siblingPtr : phiBB->getInstructions()) {
                        auto* sibling = siblingPtr.get();
                        if (sibling == phi ||
                            sibling->getOpcode() != IR::Instruction::Opcode::PHI) {
                            continue;
                        }
                        for (unsigned op = 0; op + 1 < sibling->getNumOperands(); op += 2) {
                            if (sibling->getOperand(op) == phi &&
                                sibling->getOperand(op + 1) == defBB) {
                                feedsSiblingPhiOnEdge = true;
                                break;
                            }
                        }
                        if (feedsSiblingPhiOnEdge) break;
                    }
                }
                if (feedsSiblingPhiOnEdge) continue;

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
