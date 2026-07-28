// ================================================================
// P3-3: 指令调度（Instruction Scheduling）
// 策略：基本块内"分段"列表调度
//   - 将 BB 中连续的可移动指令视为一个"段"（segment）
//   - 非可移动指令（STORE/CALL/ALLOCA/PHI/terminator）作为段边界
//   - 每个段内构建数据依赖 DAG，优先调度 LOAD 和多使用者指令
//   - 段边界不可跨越，保证 STORE/CALL 等指令的依赖关系不被打乱
//
// ★ P8: LOAD 现在是可移动的（不再作为段边界）
//   - 在无 STORE 的段内，LOAD 可被提前调度，隐藏 BOOM 4 周期 load-use 延迟
//   - 安全性：段内无 STORE（STORE 是段边界），LOAD 不会跨越 STORE
//   - 数据依赖由 DAG 保证：LOAD 的地址操作数必须先计算
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
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

// ---- 对一个段内的可移动指令做列表调度 ----
std::vector<IR::Instruction*> scheduleSegment(const std::vector<IR::Instruction*>& seg) {
    if (seg.size() <= 1) return seg;

    // 构建值→生产者映射
    std::unordered_map<IR::Value*, IR::Instruction*> producer;
    for (auto* inst : seg) {
        producer[inst] = inst;
    }

    // 入度 + 邻接表
    std::unordered_map<IR::Instruction*, int> indegree;
    std::unordered_map<IR::Instruction*, std::vector<IR::Instruction*>> succs;
    for (auto* inst : seg) {
        indegree[inst] = 0;
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto* op = inst->getOperand(i);
            auto it = producer.find(op);
            if (it != producer.end() && it->second != inst) {
                indegree[inst]++;
                succs[it->second].push_back(inst);
            }
        }
    }

    // 优先级
    auto priority = [&](IR::Instruction* inst) -> int {
        int p = 0;
        if (inst->getOpcode() == Opc::LOAD) p += 100;
        p += static_cast<int>(inst->getNumUses());
        auto it = succs.find(inst);
        if (it != succs.end()) p += static_cast<int>(it->second.size());
        return p;
    };

    // Ready 集合
    std::vector<IR::Instruction*> ready;
    for (auto* inst : seg) {
        if (indegree[inst] == 0)
            ready.push_back(inst);
    }

    // 列表调度
    std::vector<IR::Instruction*> schedule;
    schedule.reserve(seg.size());
    while (!ready.empty()) {
        size_t best = 0;
        for (size_t i = 1; i < ready.size(); ++i) {
            if (priority(ready[i]) > priority(ready[best]))
                best = i;
        }
        auto* inst = ready[best];
        ready.erase(ready.begin() + static_cast<long>(best));
        schedule.push_back(inst);

        auto sit = succs.find(inst);
        if (sit != succs.end()) {
            for (auto* dep : sit->second) {
                if (--indegree[dep] == 0)
                    ready.push_back(dep);
            }
        }
    }

    // 如果顺序没变，返回原顺序
    if (schedule == seg) return seg;
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