// ================================================================
// BasicBlockReordering — 基于支配树的拓扑排序基本块重排
//
// 算法：
// 1. 计算支配树（dominator tree）
// 2. 从 entry block 开始，按支配树 DFS 先序遍历排列块
// 3. 在同一支配者的子节点中，优先选择当前链尾的 CFG 后继
//    （以保持 fall-through 优化）
// 4. 确保：任何块都不会排在它的支配者之前
//    → 由于 SSA 中定义支配所有使用，这保证了定义在使用之前
//    → 寄存器分配器的线性扫描从不需要处理"使用先于定义"的情况
//
// 注意：entry block 必须保持为第一个块，因为 getEntryBlock() 返回 blocks.front()
// 且代码生成器（computeStackLayout, ALLOCA promotion 等）依赖此顺序
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

namespace Opt {
namespace {

// 计算 immediate dominator（idom）
// 返回 idom[bb] = immediate dominator of bb
std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>
computeImmediateDominators(IR::Function* func, const DomMap& dom) {
    std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> idom;

    for (auto& bb : func->getBlocks()) {
        auto* block = bb.get();
        if (block == func->getEntryBlock()) {
            idom[block] = nullptr;
            continue;
        }

        // 严格支配者中，离 block 最近的那个就是 idom
        // 支配者链：entry → ... → idom → block
        // idom 是支配 block 且被所有其他严格支配者支配的块
        // 等价于：在严格支配者中，找到支配所有其他严格支配者的那个
        const auto& domSet = dom.at(block);
        IR::BasicBlock* best = nullptr;
        for (auto* d : domSet) {
            if (d == block) continue; // 跳过自身
            if (!best) {
                best = d;
                continue;
            }
            // 如果 d 支配 best，则 d 比 best 更靠近 block
            // （因为支配链中 d 在 best 和 block 之间）
            if (dom.at(d).count(best)) {
                best = d;
            }
        }
        idom[block] = best;
    }

    return idom;
}

// 构建支配树子节点映射
std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>>
buildDomChildren(IR::Function* func, const std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>& idom) {
    std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> children;
    for (auto& bb : func->getBlocks()) {
        auto* block = bb.get();
        auto it = idom.find(block);
        if (it != idom.end() && it->second) {
            children[it->second].push_back(block);
        }
    }
    return children;
}

bool reorderBlocks(IR::Function* func) {
    if (func->isExternal()) return false;
    if (func->getBlocks().size() <= 2) return false; // 1-2个块无需重排

    auto succs = buildSuccessors(func);
    auto dom = computeDominators(func);
    auto idom = computeImmediateDominators(func, dom);
    auto domChildren = buildDomChildren(func, idom);

    auto* entry = func->getEntryBlock();
    if (!entry) return false;

    std::vector<IR::BasicBlock*> newOrder;
    newOrder.reserve(func->getBlocks().size());

    // DFS 遍历支配树，按先序遍历排列块
    // 使用显式栈来支持对子节点排序
    std::vector<IR::BasicBlock*> stack;
    std::unordered_set<IR::BasicBlock*> visited;
    stack.push_back(entry);

    while (!stack.empty()) {
        auto* cur = stack.back();
        stack.pop_back();

        if (visited.count(cur)) continue;
        visited.insert(cur);
        newOrder.push_back(cur);

        // 获取支配树子节点
        auto it = domChildren.find(cur);
        if (it != domChildren.end()) {
            auto& children = it->second;
            // 按照启发式排序：优先选择当前链尾的 CFG 后继（保持 fall-through）
            // 然后按原名排序以保证确定性
            std::sort(children.begin(), children.end(),
                [&](IR::BasicBlock* a, IR::BasicBlock* b) {
                    // 检查 a 或 b 是否是 cur 的 CFG 后继
                    bool aIsSucc = false, bIsSucc = false;
                    auto succIt = succs.find(cur);
                    if (succIt != succs.end()) {
                        for (auto* s : succIt->second) {
                            if (s == a) aIsSucc = true;
                            if (s == b) bIsSucc = true;
                        }
                    }
                    // 如果是后继，优先排在前面（这样会先入栈，后出栈，排在前面）
                    if (aIsSucc != bIsSucc) return aIsSucc;
                    // 同时是后继时，保持原顺序（cond_br 的 then 分支在前）
                    return a->getName() < b->getName();
                });

            // 逆序入栈，以保证排序后正序出栈
            for (auto it2 = children.rbegin(); it2 != children.rend(); ++it2) {
                stack.push_back(*it2);
            }
        }
    }

    // 检查是否需要重排
    bool changed = false;
    auto& blocks = func->getBlocks();
    for (size_t i = 0; i < newOrder.size(); ++i) {
        if (blocks[i].get() != newOrder[i]) {
            changed = true;
            break;
        }
    }
    if (!changed) return false;

    // 重排：提取所有 unique_ptr，按新顺序放回
    std::vector<std::unique_ptr<IR::BasicBlock>> temp;
    temp.reserve(newOrder.size());
    for (auto* bb : newOrder) {
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == bb) {
                temp.push_back(std::move(*it));
                blocks.erase(it);
                break;
            }
        }
    }
    blocks = std::move(temp);

    return true;
}

} // namespace

bool basicBlockReordering(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (reorderBlocks(func.get()))
            changed = true;
    }
    return changed;
}

} // namespace Opt