#include "backend/RegisterAllocator.h"
#include "opt/Optimizer.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>   // [EXP-SCAFFOLD] std::getenv for A/B switch
#include <set>
#include <string>
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
    valToInterval.clear();
    // usedCalleeSaved 不清空 — reserveReg() 在 allocate() 之前调用会预填充
    nextSpillSlot = 0;
    spillSlotSize = 0;

    maxInstId = assignInstructionIds(func);
    buildIntervals(func);
    if (useGraphColoring()) {
        colorAllocate();   // 图着色分配器（默认；RA_ALLOCATOR=linear 可切回线扫）
    } else {
        linearScan();
    }
    if (!std::getenv("DEBUG_DISABLE_PHI_COALESCE")) {
        coalescePhis(func);  // 修复安全检查后重新启用
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
    // CFG liveness extension
    //
    // The allocator represents liveness as one conservative numeric interval,
    // but block layout order is not execution order. Loop-only heuristics miss
    // values that cross branches or PHI edges, allowing two simultaneously live
    // values to receive the same physical register. Compute standard block-level
    // live-in/live-out sets and widen the numeric intervals to cover every block
    // in which a value is live. PHI incoming values are uses on predecessor
    // edges, not uses in the PHI's block.
    // ================================================================
    if (!std::getenv("DEBUG_DISABLE_CFG_LIVENESS")) {
    using ValueSet = std::unordered_set<IR::Value*>;
    std::unordered_map<IR::BasicBlock*, ValueSet> blockUses;
    std::unordered_map<IR::BasicBlock*, ValueSet> blockDefs;
    std::unordered_map<IR::BasicBlock*, ValueSet> phiEdgeUses;
    std::unordered_map<IR::BasicBlock*, ValueSet> liveIn;
    std::unordered_map<IR::BasicBlock*, ValueSet> liveOut;

    auto isTracked = [&](IR::Value* value) {
        return value && firstSeen.find(value) != firstSeen.end();
    };

    for (auto& bb : func.getBlocks()) {
        auto* block = bb.get();
        blockUses[block];
        blockDefs[block];
        phiEdgeUses[block];
        liveIn[block];
        liveOut[block];

        // PHIs define their results at block entry even if earlier passes have
        // moved a non-PHI instruction before them in the instruction list.
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::PHI &&
                isTracked(inst.get())) {
                blockDefs[block].insert(inst.get());
            }
        }
    }

    if (auto* entry = func.getEntryBlock()) {
        for (unsigned i = 0; i < func.getNumArgs(); ++i) {
            auto* arg = func.getArg(i);
            if (isTracked(arg)) blockDefs[entry].insert(arg);
        }
    }

    for (auto& bb : func.getBlocks()) {
        auto* block = bb.get();
        auto& defs = blockDefs[block];
        auto& uses = blockUses[block];

        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::PHI) {
                for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                    auto* incoming = inst->getOperand(i);
                    auto* pred = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                    if (pred && isTracked(incoming)) {
                        phiEdgeUses[pred].insert(incoming);
                    }
                }
                continue;
            }

            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                auto* operand = inst->getOperand(i);
                if (isTracked(operand) && !defs.count(operand)) {
                    uses.insert(operand);
                }
            }
            if (isTracked(inst.get())) defs.insert(inst.get());
        }
    }

    bool livenessChanged = true;
    while (livenessChanged) {
        livenessChanged = false;
        for (auto it = func.getBlocks().rbegin(); it != func.getBlocks().rend(); ++it) {
            auto* block = it->get();
            ValueSet newOut = phiEdgeUses[block];
            for (auto* succ : succs[block]) {
                newOut.insert(liveIn[succ].begin(), liveIn[succ].end());
            }

            ValueSet newIn = blockUses[block];
            for (auto* value : newOut) {
                if (!blockDefs[block].count(value)) newIn.insert(value);
            }

            if (newOut != liveOut[block] || newIn != liveIn[block]) {
                liveOut[block] = std::move(newOut);
                liveIn[block] = std::move(newIn);
                livenessChanged = true;
            }
        }
    }

    for (auto& bb : func.getBlocks()) {
        auto* block = bb.get();
        auto minIt = blockMinId.find(block);
        auto maxIt = blockMaxId.find(block);
        if (minIt == blockMinId.end() || maxIt == blockMaxId.end() ||
            minIt->second == INT_MAX || maxIt->second < 0) {
            continue;
        }

        auto widenForBlock = [&](IR::Value* value) {
            auto firstIt = firstSeen.find(value);
            if (firstIt == firstSeen.end()) return;
            firstIt->second = std::min(firstIt->second, minIt->second);
            lastSeen[value] = std::max(lastSeen[value], maxIt->second);
        };
        for (auto* value : liveIn[block]) widenForBlock(value);
        for (auto* value : liveOut[block]) widenForBlock(value);
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

    // ★ 填充 valToInterval：必须在 sort 之后，且此后 intervals 不得再增删。
    //   coalesceMoves 会原地修改元素的 reg 字段，但不会 push_back，指针保持有效。
    valToInterval.clear();
    for (auto& iv : intervals) {
        valToInterval[iv.value] = &iv;
    }
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

// ================================================================
// 图着色寄存器分配（Chaitin-Briggs），实验性。
// 复用 buildIntervals 产出的 intervals 作为唯一输入，与 linearScan 同源。
// 干涉判定：同寄存器类（isFloat 相同）且活跃区间重叠 ⇒ 相互干涉。
// 与 linearScan 的对齐约定（必须一致，否则 codegen 契约破裂）：
//   - 寄存器池：INT_REGS(16) / FLOAT_REGS(22)，各扣除 reservedRegs；
//   - 着色成功：写 regMap[val] / iv.reg / usedCalleeSaved；
//   - 溢出：写 spillMap + nextSpillSlot（8 字节/槽），不写 regMap，
//     codegen 通过 hasReg()==false 自动走 vregStackOffset 落栈——与 spillAtInterval 一致。
// 溢出选择用与 spillAtInterval 相同的代价函数，但这里是“全局”决策（在完整
// 干涉图上选代价最低者溢出），正是线性扫描所缺的全局视野。
// ================================================================
// 图着色现为默认分配器（native WSL 60 perf 实测：相较线性扫描净降 1.5%，
// 零正确性回退）。逃生开关：RA_ALLOCATOR=linear 切回线性扫描。
// GVN 暂仍关闭——朴素图着色尚缺 move coalescing / live-range splitting，
// 吃不下 GVN 拉长的活跃区间；补齐后再评估启用 GVN。
bool RegisterAllocator::useGraphColoring() {
    static const bool on = [] {
        const char* v = std::getenv("RA_ALLOCATOR");
        return !(v && std::string(v) == "linear");   // 默认 true，仅 =linear 时回退
    }();
    return on;
}

void RegisterAllocator::colorAllocate() {
    // 两个寄存器类独立着色（int / float 互不干涉，寄存器不重叠）。
    colorRegClass(false);  // int
    colorRegClass(true);   // float
}

bool RegisterAllocator::colorRegClass(bool isFloat) {
    const auto& regPool = isFloat ? FLOAT_REGS : INT_REGS;
    // 可用物理寄存器 = 池 - 预留寄存器
    std::vector<std::string> kRegs;
    for (const auto& r : regPool) {
        if (!reservedRegs.count(r)) kRegs.push_back(r);
    }
    const int K = static_cast<int>(kRegs.size());

    // ── call-aware 偏好支持 ──
    // 将可用寄存器按 callee-saved / caller-saved 分成两组，保持池内原有相对次序。
    // 跨调用值（活跃区间包含某 CALL）优先 callee-saved（s*/fs*）：prologue 存一次即可，
    //   调用间无需在每个 call site 反复保存。
    // 不跨调用值优先 caller-saved（t*/ft*）：不进 prologue（零 save/restore 成本），
    //   且因不跨调用，call site 也无需保护它。
    // 这复刻线性扫描的 RA-CALL 策略——朴素图着色无差别抢 s* 破坏了它，
    // 导致 knapsack 等深递归/调用密集程序 prologue 膨胀（平台实测 +22.8%）。
    auto isCalleeSaved = [](const std::string& r) {
        // s0-s11 / fs0-fs11 为 callee-saved；t*/ft* 为 caller-saved
        if (r.size() >= 2 && r[0] == 'f' && r[1] == 's') return true;   // fs*
        if (r[0] == 's') return true;                                    // s*
        return false;
    };
    std::vector<std::string> calleeFirst, callerFirst;
    for (const auto& r : kRegs) {
        if (isCalleeSaved(r)) calleeFirst.push_back(r);
    }
    for (const auto& r : kRegs) {
        if (!isCalleeSaved(r)) calleeFirst.push_back(r);
    }
    for (const auto& r : kRegs) {
        if (!isCalleeSaved(r)) callerFirst.push_back(r);
    }
    for (const auto& r : kRegs) {
        if (isCalleeSaved(r)) callerFirst.push_back(r);
    }

    // 收集所有 CALL 指令的位置（instId），用于判断区间是否跨调用。
    std::vector<int> callIds;
    for (const auto& kv : instId) {
        if (kv.first->getOpcode() == IR::Instruction::Opcode::CALL) {
            callIds.push_back(kv.second);
        }
    }
    std::sort(callIds.begin(), callIds.end());
    // 区间 [start,end] 是否严格跨越某 CALL（定义在调用前、使用在调用后）。
    // 用与 getRegsLiveAtCall 一致的判据：start < callId < end。
    // [EXP] RA_COLOR_CALLAWARE=0 关闭 call-aware 偏好（退回无差别抢 callee-saved，
    //   即初版图着色行为），用于 A/B 量化本修复。默认开启。
    static const bool callAware = [] {
        const char* v = std::getenv("RA_COLOR_CALLAWARE");
        return !(v && std::string(v) == "0");
    }();
    auto spansCall = [&](const LiveInterval* iv) -> bool {
        if (!callAware) return true;  // 关闭时一律视为跨调用 → 全走 calleeFirst（旧行为）
        // 二分：第一个 > start 的 callId，检查它是否 < end
        auto it = std::upper_bound(callIds.begin(), callIds.end(), iv->start);
        return it != callIds.end() && *it < iv->end;
    };

    // 收集本寄存器类的待分配节点（该类且尚未染色/溢出的区间）
    std::vector<LiveInterval*> nodes;
    for (auto& iv : intervals) {
        if (iv.isFloat != isFloat) continue;
        if (!iv.reg.empty()) continue;      // 已预着色（一般不会，保守跳过）
        nodes.push_back(&iv);
    }
    if (nodes.empty()) return true;

    const int N = static_cast<int>(nodes.size());
    // 节点 → 下标
    std::unordered_map<LiveInterval*, int> idx;
    idx.reserve(N * 2);
    for (int i = 0; i < N; ++i) idx[nodes[i]] = i;

    // 构建干涉图（邻接集合）。区间重叠即干涉。
    // O(N^2) 朴素构图；N 为单函数单寄存器类的活跃值数，规模可接受。
    std::vector<std::unordered_set<int>> adj(N);
    for (int i = 0; i < N; ++i) {
        const LiveInterval* a = nodes[i];
        for (int j = i + 1; j < N; ++j) {
            const LiveInterval* b = nodes[j];
            // 区间重叠：a.start <= b.end && b.start <= a.end
            if (a->start <= b->end && b->start <= a->end) {
                adj[i].insert(j);
                adj[j].insert(i);
            }
        }
    }

    // Chaitin-Briggs 简化：反复移除度 < K 的节点压栈；无低度节点时，
    // 按溢出代价选最低者作为“潜在溢出”压栈（乐观着色，select 阶段再定夺）。
    auto spillCost = [](const LiveInterval* iv) -> long long {
        return (long long)iv->loopDepth * 10000 + (long long)iv->useCount * 100
             + (iv->end - iv->start);
    };
    std::vector<int> degree(N);
    std::vector<char> removed(N, 0);
    for (int i = 0; i < N; ++i) degree[i] = static_cast<int>(adj[i].size());

    std::vector<int> selectStack;
    selectStack.reserve(N);
    int remaining = N;
    while (remaining > 0) {
        // 优先移除低度节点（度 < K）
        int pick = -1;
        for (int i = 0; i < N; ++i) {
            if (!removed[i] && degree[i] < K) { pick = i; break; }
        }
        if (pick == -1) {
            // 无低度节点：选溢出代价最低的节点作为潜在溢出。
            // 决定性：代价相同用节点名兜底，消除迭代顺序非确定性。
            long long best = LLONG_MAX;
            for (int i = 0; i < N; ++i) {
                if (removed[i]) continue;
                long long c = spillCost(nodes[i]);
                if (c < best || (c == best && pick != -1
                        && nodes[i]->value->getName() < nodes[pick]->value->getName())) {
                    best = c; pick = i;
                }
            }
        }
        // 压栈并从图中移除
        removed[pick] = 1;
        selectStack.push_back(pick);
        for (int nb : adj[pick]) {
            if (!removed[nb]) degree[nb]--;
        }
        remaining--;
    }

    // Select：逆序出栈染色。为节点选一个邻居未占用的寄存器；
    // 若无可用寄存器则真正溢出。
    bool allColored = true;
    for (int si = static_cast<int>(selectStack.size()) - 1; si >= 0; --si) {
        int n = selectStack[si];
        LiveInterval* iv = nodes[n];
        std::set<std::string> used;
        for (int nb : adj[n]) {
            if (!nodes[nb]->reg.empty()) used.insert(nodes[nb]->reg);
        }
        std::string chosen;
        // call-aware 偏好：跨调用值先试 callee-saved，否则先试 caller-saved。
        // 两个顺序都覆盖全部 K 个寄存器，只是优先级不同——不影响可着色性，
        // 只影响“选哪个”，从而最小化 usedCalleeSaved 与 call-site 保护开销。
        const std::vector<std::string>& tryOrder =
            spansCall(iv) ? calleeFirst : callerFirst;
        for (const auto& r : tryOrder) {
            if (!used.count(r)) { chosen = r; break; }
        }
        if (!chosen.empty()) {
            iv->reg = chosen;
            regMap[iv->value] = chosen;
            if (std::find(usedCalleeSaved.begin(), usedCalleeSaved.end(), chosen)
                == usedCalleeSaved.end()) {
                usedCalleeSaved.push_back(chosen);
            }
        } else {
            // 实际溢出：不写 regMap，codegen 自动落栈（与 spillAtInterval 一致）
            iv->spillSlot = nextSpillSlot;
            spillMap[iv->value] = nextSpillSlot;
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

// ── 通用 move coalescing 支持函数 ──

LiveInterval* RegisterAllocator::intervalOf(IR::Value* v) {
    auto it = valToInterval.find(v);
    return it != valToInterval.end() ? it->second : nullptr;
}

// 两个值的活跃区间是否重叠（可能同时活跃）。
// 区间经 buildIntervals 的多处 liveness extension 保守放大，
// 因此“不重叠”是保守正确的判断——漏合并可接受，错合并不可接受。
// 任一值无区间（如未提升的 ALLOCA 指针）→ 保守视为重叠，拒绝合并。
bool RegisterAllocator::intervalsOverlap(IR::Value* a, IR::Value* b) const {
    auto ia = valToInterval.find(a);
    auto ib = valToInterval.find(b);
    if (ia == valToInterval.end() || ib == valToInterval.end()) return true;
    const LiveInterval* x = ia->second;
    const LiveInterval* y = ib->second;
    return x->start <= y->end && y->start <= x->end;
}

// 物理寄存器 reg 在区间 range 内是否被“第三方”值（≠ self）占用。
// 用于确保把 self 合并到 reg 后，不会踩踏另一个仍活跃且已占用 reg 的值。
bool RegisterAllocator::regBusyDuring(const std::string& reg,
                                      const LiveInterval* range,
                                      IR::Value* self) const {
    if (!range) return true;
    for (const auto& iv : intervals) {
        if (iv.value == self) continue;
        if (iv.reg != reg) continue;
        if (iv.start <= range->end && range->start <= iv.end) return true;
    }
    return false;
}

// 经典 read-modify-write coalescing 判据（旧版逻辑，已在历史用例上验证安全）。
// 成立条件（全部满足）：
//   1. incoming 单 use，且唯一 use 是该 PHI；
//   2. incoming 由某指令定义，该指令以 phi 自身为源操作数（RMW，如 subw s7,s7,s1）；
//   3. PHI 在 incoming 定义所在 BB 中除该 RMW 指令外无其他 use。
// 保留此判据作为独立接受路径，确保新版严格包含旧版已验证的安全合并集。
bool RegisterAllocator::isClassicRmwCoalesce(IR::Value* incoming,
                                             IR::Instruction* phi,
                                             const std::string& phiReg) const {
    (void)phiReg;
    if (!incoming->hasOneUse()) return false;
    const auto& uses = incoming->getUses();
    if (uses.size() != 1) return false;
    if (uses[0].user != phi) return false;

    auto* defInst = dynamic_cast<IR::Instruction*>(incoming);
    if (!defInst) return false;

    bool usesPhi = false;
    for (unsigned j = 0; j < defInst->getNumOperands(); ++j) {
        if (defInst->getOperand(j) == phi) { usesPhi = true; break; }
    }
    if (!usesPhi) return false;

    auto* defBB = defInst->getParent();
    for (const auto& phiUse : phi->getUses()) {
        auto* phiUser = dynamic_cast<IR::Instruction*>(phiUse.user);
        if (!phiUser) continue;
        if (phiUser == defInst) continue; // read-modify-write 本身
        if (phiUser->getParent() == defBB) return false; // 同 BB 其他 use → 不安全

        // PHI operands are used on predecessor edges. Coalescing the RMW result
        // into phiReg overwrites the old PHI value at defInst; reject it when a
        // different PHI still needs that old value on defBB's outgoing edge.
        // Example: B.next -> B.phi cannot be coalesced when the same edge also
        // performs C.phi <- B.phi as part of a parallel copy.
        if (phiUser->getOpcode() == IR::Instruction::Opcode::PHI &&
            phiUse.operandNo % 2 == 0 &&
            phiUse.operandNo + 1 < phiUser->getNumOperands() &&
            phiUser->getOperand(phiUse.operandNo + 1) == defBB) {
            return false;
        }
    }
    return true;
}

// ================================================================
// PHI copy coalescing（通用干涉判断版）:
// 若 PHI 与其某个 incoming 的活跃区间不重叠，且 PHI 的寄存器在
// incoming 区间内未被第三方占用，则把 incoming 合并到 PHI 的寄存器，
// 消除 emitPhiMovesForEdge 发射的 mv/fmv.s（该处 phiReg==srcReg 时自动跳过）。
//
// 相比旧版的“read-modify-write + 单 use + 无同 BB 其他 use”三条语法限制，
// 本版改为区间重叠判断：
//   - PHI 在 incoming 定义点之后仍有 use → 区间覆盖定义点 → 重叠 → 拒绝（等价旧安全检查）
//   - 但不再要求 incoming 单 use / 必须是 read-modify-write，覆盖面更广。
//
// Example (h-5-01 inner loop):
//   subw s11, s7, s1    # t27 = w - mul
//   mv  s7, s11         # w.phi = t27
// After coalescing:
//   subw s7, s7, s1     # w = w - mul  (no mv needed!)
// ================================================================
void RegisterAllocator::coalescePhis(IR::Function& func) {
    // [EXP-SCAFFOLD] A/B 实验开关：RA_COALESCE_MODE=rmw 时只走判据(A)（等价 v3.1.0），
    //   默认或 =full 时走 (A)∪(B)（v3.2.0）。实验结束后连同下方判断一并移除。
    static const bool rmwOnly = [] {
        const char* m = std::getenv("RA_COALESCE_MODE");
        return m && std::string(m) == "rmw";
    }();
    for (auto& bb : func.getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::PHI)
                continue;

            auto* phi = inst.get();
            std::string phiReg = getReg(phi);
            if (phiReg.empty()) continue; // PHI spilled, can't coalesce
            bool phiFloat = floatValues.count(phi) > 0;

            for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
                auto* incoming = phi->getOperand(i);
                if (!incoming) continue;
                if (dynamic_cast<IR::Constant*>(incoming)) continue;

                // incoming 必须已分配到寄存器且尚未与 PHI 同寄存器
                std::string incomingReg = getReg(incoming);
                if (incomingReg.empty()) continue;      // spilled
                if (incomingReg == phiReg) continue;    // 已合并

                // 寄存器类必须一致（int↔int / float↔float）
                if ((floatValues.count(incoming) > 0) != phiFloat) continue;

                LiveInterval* inIv = intervalOf(incoming);
                if (!inIv) continue;                    // 无区间信息，保守放弃

                // ★ 接受路径为两条判据的并集，严格包含旧的已验证安全集：
                //   (A) 经典 read-modify-write（旧判据，已在历史用例上验证安全）：
                //       incoming 单 use 于该 PHI、由指令定义、该指令以 PHI 为源、
                //       且 PHI 在 incoming 定义所在 BB 无其他 use。
                //   (B) 通用区间不重叠（保守扩展）：incoming 与 phi 区间不重叠，
                //       且 phiReg 在 incoming 区间内未被第三方占用。
                //   任一成立即可合并。(B) 因区间只放大不缩小而保守正确。
                bool acceptRMW = isClassicRmwCoalesce(incoming, phi, phiReg);
                bool acceptDisjoint = !rmwOnly    // [EXP-SCAFFOLD] rmw 模式下禁用 (B)
                    && !intervalsOverlap(incoming, phi)
                    && !regBusyDuring(phiReg, inIv, incoming);
                if (!acceptRMW && !acceptDisjoint) continue;

                if (std::getenv("RA_COALESCE_TRACE")) {
                    std::fprintf(stderr,
                        "[phi-coalesce] %s %s -> %s reg=%s rmw=%d disjoint=%d in=[%d,%d]\n",
                        func.getName().c_str(), incoming->getName().c_str(),
                        phi->getName().c_str(), phiReg.c_str(), acceptRMW,
                        acceptDisjoint, inIv->start, inIv->end);
                }

                // Coalesce: assign the PHI's register to the incoming value
                regMap[incoming] = phiReg;
                inIv->reg = phiReg;  // 同步区间，供后续 incoming 的 regBusyDuring 判断
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
