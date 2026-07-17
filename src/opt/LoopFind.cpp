// ================================================================
// 自然循环森林（Natural Loop Forest）
// 借鉴 Cpl3 的 LoopFind 设计
// 基于 Lengauer-Tarjan 支配树检测自然循环，构建嵌套层次结构
// 替代当前各 Pass 中 ad-hoc 回边检测，提供统一的循环分析基础设施
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {

// ================================================================
// 构建循环森林
// 算法：
// 1. 计算支配树
// 2. 检测回边：B → H 且 H 严格支配 B
// 3. 为每条回边构建自然循环体
// 4. 按大小排序（最内层优先），构建嵌套关系
// ================================================================
std::vector<NaturalLoop> findNaturalLoops(IR::Function* func) {
    std::vector<NaturalLoop> result;
    if (func->isExternal() || func->getBlocks().empty()) return result;

    auto dom = computeDominators(func);
    auto preds = buildPredecessors(func);
    auto succs = buildSuccessors(func);

    // 1. 检测回边：边 B→H 且 H strictly dominates B
    struct BackEdge {
        IR::BasicBlock* from;
        IR::BasicBlock* to;
    };
    std::vector<BackEdge> backEdges;

    for (auto& bb : func->getBlocks()) {
        for (auto* succ : succs[bb.get()]) {
            if (strictlyDominates(succ, bb.get(), dom)) {
                backEdges.push_back({bb.get(), succ});
            }
        }
    }
    if (backEdges.empty()) return result;

    // 2. 为每条回边构建自然循环体
    for (auto& be : backEdges) {
        NaturalLoop loop;
        loop.header = be.to;
        loop.latch = be.from;
        loop.body.insert(be.to);  // header

        // 从 latch 反向遍历前驱，直到到达 header
        std::vector<IR::BasicBlock*> worklist;
        std::unordered_set<IR::BasicBlock*> visited;
        worklist.push_back(be.from);
        visited.insert(be.from);

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
        result.push_back(std::move(loop));
    }

    // 3. 合并具有相同 header 的自然循环
    //    多条回边指向同一 header 时，每条回边产生一个独立 NaturalLoop，
    //    body 仅包含从该 latch 可达的块。合并后 body 取并集，
    //    使 LICM 等后续 Pass 看到完整的循环体，避免将其他回边的 latch
    //    误判为"循环外前驱"而错误重定向（19_search 无限循环根因）。
    {
        std::unordered_map<IR::BasicBlock*, size_t> headerToLoop;
        std::vector<NaturalLoop> merged;
        for (auto& loop : result) {
            auto it = headerToLoop.find(loop.header);
            if (it == headerToLoop.end()) {
                headerToLoop[loop.header] = merged.size();
                merged.push_back(std::move(loop));
            } else {
                auto& existing = merged[it->second];
                for (auto* bb : loop.body) {
                    existing.body.insert(bb);
                }
                // 合并 latch：保留第一个，多 latch 信息通过 body 隐含
                // （LICM 不依赖 latch 字段做安全性判断）
            }
        }
        result = std::move(merged);
    }

    // 4. 按 body 大小排序（最小 → 最大，即最内层优先）
    std::sort(result.begin(), result.end(),
        [](const NaturalLoop& a, const NaturalLoop& b) {
            return a.body.size() < b.body.size();
        });

    // 5. 构建嵌套关系
    for (size_t i = 0; i < result.size(); ++i) {
        for (size_t j = i + 1; j < result.size(); ++j) {
            // 如果 i 的 header 在 j 的 body 中，则 i 嵌套在 j 中
            if (result[j].body.count(result[i].header)) {
                result[i].parent = &result[j];
                result[j].subLoops.push_back(&result[i]);
                result[i].depth = result[j].depth + 1;
                break;  // 找到第一个包含它的父循环即可
            }
        }
    }

    // 6. 计算退出块和退出边界
    for (auto& loop : result) {
        for (auto* bb : loop.body) {
            for (auto* succ : succs[bb]) {
                if (!loop.body.count(succ)) {
                    loop.exitingBlocks.push_back(bb);
                    if (std::find(loop.exitBlocks.begin(), loop.exitBlocks.end(), succ) == loop.exitBlocks.end()) {
                        loop.exitBlocks.push_back(succ);
                    }
                }
            }
        }
    }

    // 7. 去重 exitingBlocks
    for (auto& loop : result) {
        std::sort(loop.exitingBlocks.begin(), loop.exitingBlocks.end());
        loop.exitingBlocks.erase(
            std::unique(loop.exitingBlocks.begin(), loop.exitingBlocks.end()),
            loop.exitingBlocks.end());
    }

    return result;
}

// ================================================================
// 获取函数的所有循环（按嵌套深度从内到外排序）
// ================================================================
std::vector<NaturalLoop> getLoopsInnermostFirst(IR::Function* func) {
    auto loops = findNaturalLoops(func);
    std::sort(loops.begin(), loops.end(),
        [](const NaturalLoop& a, const NaturalLoop& b) {
            return a.depth > b.depth;  // 深度大的（内层）优先
        });
    return loops;
}

// ================================================================
// 判断基本块是否在指定循环中
// ================================================================
bool isBlockInLoop(IR::BasicBlock* bb, const NaturalLoop& loop) {
    return loop.body.count(bb) > 0;
}

// ================================================================
// 判断指令是否在指定循环中
// ================================================================
bool isInstInLoop(IR::Instruction* inst, const NaturalLoop& loop) {
    auto* bb = inst->getParent();
    return bb && loop.body.count(bb) > 0;
}

// ================================================================
// 判断指令是否是循环不变量
// （所有操作数要么是常量，要么定义在循环外）
// ================================================================
bool isLoopInvariantSimple(IR::Instruction* inst, const NaturalLoop& loop) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    if (op == Opc::PHI || op == Opc::STORE || op == Opc::CALL ||
        op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
        op == Opc::ALLOCA || op == Opc::LOAD)
        return false;

    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        auto* val = inst->getOperand(i);
        if (!val) continue;
        if (dynamic_cast<IR::ConstantInt*>(val)) continue;
        if (dynamic_cast<IR::ConstantFloat*>(val)) continue;
        if (dynamic_cast<IR::GlobalVariable*>(val)) continue;
        if (dynamic_cast<IR::Function*>(val)) continue;
        if (auto* defInst = dynamic_cast<IR::Instruction*>(val)) {
            auto* defBB = defInst->getParent();
            if (defBB && loop.body.count(defBB)) return false;
        }
    }
    return true;
}

} // namespace Opt