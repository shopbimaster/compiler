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
           op != Opc::PHI && op != Opc::CALL && op != Opc::ALLOCA &&
           op != Opc::STORE && op != Opc::LOAD;
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

    // 构建值→生产者映射（仅可移动指令）
    std::unordered_map<IR::Value*, IR::Instruction*> producer;
    for (auto* inst : movable) {
        producer[inst] = inst;
    }

    // 入度 + 邻接表（仅可移动指令之间的依赖）
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

    // 提取所有非终止指令（size-1 排除 terminator）
    size_t nonTermCount = insts.size() - 1;
    std::vector<std::unique_ptr<IR::Instruction>> extracted;
    extracted.reserve(nonTermCount);
    for (size_t i = 0; i < nonTermCount; ++i) {
        auto it = bb->begin();
        extracted.push_back(std::move(*it));
        bb->erase(it);
    }

    if (extracted.empty()) return;

    // 构建位置映射：非可移动指令保持原位，可移动指令按 schedule 填充
    std::unordered_map<IR::Instruction*, int> posMap;
    for (size_t i = 0; i < extracted.size(); ++i) {
        if (!isMovable(extracted[i].get())) {
            posMap[extracted[i].get()] = static_cast<int>(i);
        }
    }
    int schedIdx = 0;
    for (size_t i = 0; i < extracted.size(); ++i) {
        if (isMovable(extracted[i].get())) {
            posMap[schedule[schedIdx++]] = static_cast<int>(i);
        }
    }

    // 按位置排序
    std::sort(extracted.begin(), extracted.end(),
        [&](const std::unique_ptr<IR::Instruction>& a,
            const std::unique_ptr<IR::Instruction>& b) {
            return posMap[a.get()] < posMap[b.get()];
        });

    // 按排序后的顺序重新插入（在 terminator 之前）
    auto termIt = bb->end();
    --termIt;
    for (auto& up : extracted) {
        bb->insert(termIt, up.release());
    }
}

} // namespace

bool instructionScheduling(IR::Module* mod) {
    bool anyChanged = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        for (auto& bb : func->getBlocks()) {
            // scheduleBB 修改 BB 内部顺序，需跟踪是否变化
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