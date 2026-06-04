// ================================================================
// P3-3: 指令调度（Instruction Scheduling）
// 策略：基本块内列表调度
//   - 构建数据依赖 DAG
//   - 优先调度 LOAD（减少访存延迟）和多使用者指令
//   - 保持 PHI/CALL/terminator 位置不变
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
    return op != Opc::BR && op != Opc::COND_BR && op != Opc::RET &&
           op != Opc::PHI && op != Opc::CALL && op != Opc::ALLOCA;
}

void scheduleBB(IR::BasicBlock* bb) {
    auto& insts = bb->getInstructions();
    if (insts.size() <= 2) return;

    std::vector<IR::Instruction*> movable;
    for (auto& inst : insts) {
        if (isMovable(inst.get()))
            movable.push_back(inst.get());
    }
    if (movable.size() <= 1) return;

    // 构建值→生产者映射
    std::unordered_map<IR::Value*, IR::Instruction*> producer;
    for (auto& inst : insts) {
        producer[inst.get()] = inst.get();
    }

    // 入度 + 邻接表
    std::unordered_map<IR::Instruction*, int> indegree;
    std::unordered_map<IR::Instruction*, std::vector<IR::Instruction*>> succs;
    for (auto* inst : movable) {
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

    // 优先级：LOAD > 多使用者 + 使用次数权重
    auto priority = [&](IR::Instruction* inst) -> int {
        int p = 0;
        if (inst->getOpcode() == Opc::LOAD) p += 100;
        if (inst->getOpcode() == Opc::STORE) p -= 10;
        p += static_cast<int>(inst->getNumUses());
        auto it = succs.find(inst);
        if (it != succs.end()) p += static_cast<int>(it->second.size());
        return p;
    };

    // Ready 集合
    std::vector<IR::Instruction*> ready;
    for (auto* inst : movable) {
        if (indegree[inst] == 0)
            ready.push_back(inst);
    }

    // 列表调度
    std::vector<IR::Instruction*> schedule;
    schedule.reserve(movable.size());
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

    // 如果顺序没变，跳过
    if (schedule == movable) return;

    // 构建 schedule 位置映射
    std::unordered_map<IR::Instruction*, int> schedPos;
    for (size_t i = 0; i < schedule.size(); ++i)
        schedPos[schedule[i]] = static_cast<int>(i);

    // 原地 stable_sort：非可移动指令保持原始相对顺序，
    // 可移动指令按 schedule 顺序排列
    // 排序范围 [begin, end-1) 排除 terminator
    auto termPos = bb->end();
    --termPos;

    std::stable_sort(bb->begin(), termPos,
        [&](const std::unique_ptr<IR::Instruction>& a,
            const std::unique_ptr<IR::Instruction>& b) {
            auto itA = schedPos.find(a.get());
            auto itB = schedPos.find(b.get());
            if (itA != schedPos.end() && itB != schedPos.end())
                return itA->second < itB->second;
            if (itA == schedPos.end() && itB == schedPos.end())
                return false;
            return itA == schedPos.end();
        });
}

} // namespace

void instructionScheduling(IR::Module* mod) {
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        for (auto& bb : func->getBlocks()) {
            scheduleBB(bb.get());
        }
    }
}

} // namespace Opt