// ================================================================
// P3-3: 指令调度（Instruction Scheduling）
// 策略：基本块内"分段"列表调度
//   - 将 BB 中连续的可移动指令视为一个"段"（segment）
//   - STORE/CALL/ALLOCA/PHI/terminator 作为段边界，段边界不可跨越
//   - 每个段内构建数据依赖 DAG，优先调度 LOAD 和多使用者指令
//
// ★ P8: LOAD 现在是可移动的（不再作为段边界）
//   - 在无 STORE 的段内，LOAD 可被提前调度，隐藏 BOOM 4 周期 load-use 延迟
//   - 安全性：段内无 STORE（STORE 是段边界），LOAD 不会跨越 STORE
//   - 数据依赖由 DAG 保证：LOAD 的地址操作数必须先计算
//
// ★ 压力感知：段重排仅在峰值活跃值严格下降时采用新顺序，避免无收益的 RA 扰动
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

bool isMovable(IR::Instruction* inst) {
    auto op = inst->getOpcode();
    // P8: LOAD 现在可移动——允许调度器将 LOAD 提前，隐藏 load-use 延迟
    // STORE 仍为段边界——保证 LOAD 不会跨越 STORE（维护内存访问顺序）
    // P8_OFF=1 可回退到 LOAD 不可移动（原行为）
    static const bool p8Off = [] {
        const char* v = std::getenv("P8_OFF");
        return v && std::string(v) == "1";
    }();
    if (p8Off && op == Opc::LOAD) return false;
    return op != Opc::BR && op != Opc::COND_BR && op != Opc::RET &&
           op != Opc::PHI && op != Opc::CALL && op != Opc::ALLOCA &&
           op != Opc::STORE;
}

// Pressure-aware scheduling for a region without stores or calls. Loads may
// move among pure instructions because every memory-writing operation remains
// a hard region boundary. The reorder is accepted only when estimated peak
// SSA liveness strictly decreases.
std::vector<IR::Instruction*> scheduleSegment(
    const std::vector<IR::Instruction*>& seg) {
    if (seg.size() <= 1) return seg;

    std::unordered_map<IR::Value*, IR::Instruction*> producer;
    std::unordered_set<IR::Instruction*> segmentSet(seg.begin(), seg.end());
    std::unordered_map<IR::Instruction*, size_t> originalPosition;
    for (size_t i = 0; i < seg.size(); ++i) {
        producer[seg[i]] = seg[i];
        originalPosition[seg[i]] = i;
    }

    std::unordered_map<IR::Instruction*, int> indegree;
    std::unordered_map<IR::Instruction*, std::vector<IR::Instruction*>> succs;
    std::unordered_map<IR::Instruction*, int> remainingUses;
    std::unordered_map<IR::Instruction*, bool> hasExternalUse;
    for (auto* inst : seg) {
        indegree[inst] = 0;
        remainingUses[inst] = 0;
        hasExternalUse[inst] = false;
    }

    for (auto* inst : seg) {
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto it = producer.find(inst->getOperand(i));
            if (it != producer.end() && it->second != inst) {
                ++indegree[inst];
                succs[it->second].push_back(inst);
                ++remainingUses[it->second];
            }
        }
        for (const auto& use : inst->getUses()) {
            auto* user = dynamic_cast<IR::Instruction*>(use.user);
            if (!user || !segmentSet.count(user)) {
                hasExternalUse[inst] = true;
                break;
            }
        }
    }

    std::unordered_map<IR::Instruction*, int> criticalDepth;
    std::function<int(IR::Instruction*)> getCriticalDepth =
        [&](IR::Instruction* inst) -> int {
            auto cached = criticalDepth.find(inst);
            if (cached != criticalDepth.end()) return cached->second;
            int depth = 1;
            auto it = succs.find(inst);
            if (it != succs.end()) {
                for (auto* succ : it->second) {
                    depth = std::max(depth, 1 + getCriticalDepth(succ));
                }
            }
            criticalDepth[inst] = depth;
            return depth;
        };

    struct Priority {
        int pressureGain;
        int criticalDepth;
        int loadBonus;
        size_t originalPosition;
    };
    auto priority = [&](IR::Instruction* inst) -> Priority {
        std::unordered_map<IR::Instruction*, int> usesInInstruction;
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto it = producer.find(inst->getOperand(i));
            if (it != producer.end() && it->second != inst) {
                ++usesInInstruction[it->second];
            }
        }

        int deaths = 0;
        for (const auto& entry : usesInInstruction) {
            auto* value = entry.first;
            if (!hasExternalUse[value] &&
                remainingUses[value] == entry.second) {
                ++deaths;
            }
        }
        const bool createsLiveValue =
            remainingUses[inst] > 0 || hasExternalUse[inst];
        return {deaths - static_cast<int>(createsLiveValue),
                getCriticalDepth(inst),
                inst->getOpcode() == Opc::LOAD ? 1 : 0,
                originalPosition[inst]};
    };
    auto isBetter = [](const Priority& lhs, const Priority& rhs) {
        if (lhs.pressureGain != rhs.pressureGain)
            return lhs.pressureGain > rhs.pressureGain;
        if (lhs.criticalDepth != rhs.criticalDepth)
            return lhs.criticalDepth > rhs.criticalDepth;
        if (lhs.loadBonus != rhs.loadBonus)
            return lhs.loadBonus > rhs.loadBonus;
        return lhs.originalPosition < rhs.originalPosition;
    };

    std::vector<IR::Instruction*> ready;
    for (auto* inst : seg) {
        if (indegree[inst] == 0) ready.push_back(inst);
    }

    std::vector<IR::Instruction*> schedule;
    schedule.reserve(seg.size());
    while (!ready.empty()) {
        size_t best = 0;
        for (size_t i = 1; i < ready.size(); ++i) {
            if (isBetter(priority(ready[i]), priority(ready[best]))) best = i;
        }
        auto* inst = ready[best];
        ready.erase(ready.begin() + static_cast<long>(best));
        schedule.push_back(inst);

        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto it = producer.find(inst->getOperand(i));
            if (it != producer.end() && it->second != inst) {
                --remainingUses[it->second];
            }
        }
        auto sit = succs.find(inst);
        if (sit != succs.end()) {
            for (auto* succ : sit->second) {
                if (--indegree[succ] == 0) ready.push_back(succ);
            }
        }
    }

    auto estimatePeakLive = [&](const std::vector<IR::Instruction*>& order) {
        std::unordered_map<IR::Instruction*, int> usesLeft;
        for (auto* inst : seg) usesLeft[inst] = 0;
        for (auto* inst : seg) {
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                auto it = producer.find(inst->getOperand(i));
                if (it != producer.end() && it->second != inst) {
                    ++usesLeft[it->second];
                }
            }
        }

        int live = 0;
        int peak = 0;
        for (auto* inst : order) {
            if (usesLeft[inst] > 0 || hasExternalUse[inst]) {
                ++live;
                peak = std::max(peak, live);
            }
            std::unordered_set<IR::Instruction*> operands;
            for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                auto it = producer.find(inst->getOperand(i));
                if (it != producer.end() && it->second != inst) {
                    --usesLeft[it->second];
                    operands.insert(it->second);
                }
            }
            for (auto* operand : operands) {
                if (usesLeft[operand] == 0 && !hasExternalUse[operand]) --live;
            }
        }
        return peak;
    };

    if (schedule.size() != seg.size()) return seg;
    if (estimatePeakLive(schedule) >= estimatePeakLive(seg)) return seg;
    return schedule;
}

void scheduleBB(IR::BasicBlock* bb) {
    auto& insts = bb->getInstructions();
    if (insts.size() <= 2) return;

    // ---- 收集段：连续的可移动指令为一个段，非可移动指令为边界 ----
    // 同时构建新顺序：segment（可能重排）→ 边界指令 → ...
    std::vector<std::vector<IR::Instruction*>> segments;
    std::vector<IR::Instruction*> boundaries;
    // 记录原始顺序：segments 和 boundaries 的交替模式
    // true = segment, false = boundary
    std::vector<bool> pattern;
    std::vector<IR::Instruction*> currentSeg;

    for (auto& inst : insts) {
        auto op = inst->getOpcode();
        // terminator 结束
        if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET) break;

        if (isMovable(inst.get())) {
            currentSeg.push_back(inst.get());
        } else {
            if (!currentSeg.empty()) {
                segments.push_back(std::move(currentSeg));
                currentSeg.clear();
                pattern.push_back(true);
            }
            boundaries.push_back(inst.get());
            pattern.push_back(false);
        }
    }
    if (!currentSeg.empty()) {
        segments.push_back(std::move(currentSeg));
        pattern.push_back(true);
    }

    if (segments.empty()) return;

    // 调度每个段
    bool changed = false;
    for (auto& seg : segments) {
        auto scheduled = scheduleSegment(seg);
        if (scheduled != seg) {
            changed = true;
            seg = std::move(scheduled);
        }
    }

    if (!changed) return;

    // ---- 重建 BB（在 terminator 之前） ----
    // 先保存并移除 terminator，避免 vector::insert 导致迭代器失效
    auto termIt = bb->end();
    --termIt;
    auto termInst = std::move(*termIt);
    bb->erase(termIt);

    // 提取所有非 terminator 指令
    std::vector<std::unique_ptr<IR::Instruction>> extracted;
    while (bb->begin() != bb->end()) {
        auto it = bb->begin();
        extracted.push_back(std::move(*it));
        bb->erase(it);
    }

    if (extracted.empty()) {
        // 恢复 terminator
        bb->pushBack(termInst.release());
        return;
    }

    // 按 segments + boundaries 顺序重建
    size_t segIdx = 0, bndIdx = 0;
    std::vector<IR::Instruction*> newOrder;
    for (bool isSeg : pattern) {
        if (isSeg) {
            for (auto* inst : segments[segIdx]) {
                newOrder.push_back(inst);
            }
            segIdx++;
        } else {
            newOrder.push_back(boundaries[bndIdx++]);
        }
    }

    // 按新顺序插入指令（BB 为空，直接 pushBack 即可）
    for (auto* inst : newOrder) {
        for (auto& up : extracted) {
            if (up.get() == inst) {
                bb->pushBack(up.release());
                break;
            }
        }
    }

    // 恢复 terminator 到末尾
    bb->pushBack(termInst.release());
}

} // namespace

bool instructionScheduling(IR::Module* mod) {
    bool anyChanged = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        for (auto& bb : func->getBlocks()) {
            auto& insts = bb->getInstructions();
            if (insts.size() <= 2) continue;
            // 保存原始顺序
            std::vector<IR::Instruction*> origOrder;
            for (auto& inst : insts) origOrder.push_back(inst.get());
            scheduleBB(bb.get());
            // 检查是否变化
            std::vector<IR::Instruction*> newOrder;
            for (auto& inst : insts) newOrder.push_back(inst.get());
            if (origOrder != newOrder) anyChanged = true;
        }
    }
    return anyChanged;
}

} // namespace Opt
