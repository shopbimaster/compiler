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
//
// ----------------------------------------------------------------
// E5: 分支概率引导布局（概率启发式 fall-through）
// ----------------------------------------------------------------
// 在支配树 DFS 框架内，对子节点排序加入静态分支概率启发式：
//   - CFG 后继优先于非后继（保持 fall-through 机会，原有不变量）
//   - 在均为 CFG 后继时，按"边优先级"排序：
//       * 循环继续边（succ 在 cur 所在循环体内）：热（+100），应 fall-through
//       * 循环退出边（succ 不在循环体内）：冷（-100），应 out-of-line
//       * RET 终止的后继块：冷（-50），早返回/错误路径 out-of-line
//       * 无信号：保持原 name-sort（零回归，与基线完全一致）
//   - 严格"改进或不变"：仅在循环/RET 信号上偏离基线，中性 if-then-else 不变
//     → 直接规避 E2 教训（无 PGO 时分支密集代码 h-4-03/huffman 回归）
//
// 关键设计：自循环（LoopRotation 产物）不被 findNaturalLoops 检测，其布局
//   （H, exit：冷出口 fall-through + 热回边预测 taken 后向分支）已最优，无需干预。
//   仅非自循环的 while 形循环头由循环启发式处理（body fall-through, exit 跳转）。
//
// 开关：LAYOUT_PROB_OFF=1 关闭概率启发式，回退到纯 name-sort（A/B 对照）
// ================================================================

#include "opt/Optimizer.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>

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

// ---- E5: 概率引导布局辅助 ----

bool layoutProbDisabled() {
    if (const char* v = std::getenv("LAYOUT_PROB_OFF"))
        return std::string(v) == "1";
    return false;
}

static const bool dbgLayout = [] {
    const char* v = std::getenv("DBG_LAYOUT");
    return v && std::string(v) == "1";
}();

// bb 的终止指令是否为 RET（冷：早返回/错误路径/函数出口）
bool isRetTerminated(IR::BasicBlock* bb) {
    if (bb->getInstructions().empty()) return false;
    return bb->getInstructions().back()->getOpcode() == IR::Instruction::Opcode::RET;
}

// 边 cur→succ 的布局优先级（succ 是 cur 的 CFG 后继）。
// 越高 → 越应排在前面（成为 fall-through）。
// 启发式（无 PGO，保守）：
//   - 循环继续边（succ 在 cur 所在循环体内）：热 +100
//   - 循环退出边（succ 不在循环体内）：冷 -100
//   - RET 终止后继：冷 -50
//   - 无信号：0（由调用方按 name-sort 保持基线行为）
// 注意：自循环不被 findNaturalLoops 检测，cur 无 loop 条目 → 不触发循环启发式，
//   退化为 name-sort，保持自循环 (H, exit) 最优布局。
int succPriority(IR::BasicBlock* cur, IR::BasicBlock* succ,
                 const std::vector<NaturalLoop>& loops,
                 const std::unordered_map<IR::BasicBlock*, size_t>& blockToLoopIdx) {
    int score = 0;
    auto it = blockToLoopIdx.find(cur);
    if (it != blockToLoopIdx.end()) {
        const NaturalLoop& L = loops[it->second];
        if (L.body.count(succ)) score += 100;   // 循环继续，热
        else score -= 100;                       // 循环退出，冷
    }
    if (isRetTerminated(succ)) score -= 50;      // RET 冷路径
    return score;
}

// 构建块 → 内层循环索引映射（findNaturalLoops 按 body 大小升序=内层优先）
std::unordered_map<IR::BasicBlock*, size_t>
buildBlockToLoopIdx(const std::vector<NaturalLoop>& loops) {
    std::unordered_map<IR::BasicBlock*, size_t> map;
    for (size_t i = 0; i < loops.size(); ++i) {
        for (auto* bb : loops[i].body) {
            if (!map.count(bb)) map[bb] = i;  // 内层（最小 body）优先
        }
    }
    return map;
}

// 判断 a 是否为 cur 的 CFG 后继
bool isCfgSucc(IR::BasicBlock* cur, IR::BasicBlock* a,
               const SuccMap& succs) {
    auto it = succs.find(cur);
    if (it == succs.end()) return false;
    for (auto* s : it->second) if (s == a) return true;
    return false;
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

    // E5: 概率引导布局数据
    bool useProb = !layoutProbDisabled();
    std::vector<NaturalLoop> loops;
    std::unordered_map<IR::BasicBlock*, size_t> blockToLoopIdx;
    if (useProb) {
        loops = findNaturalLoops(func);
        blockToLoopIdx = buildBlockToLoopIdx(loops);
    }

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
        if (it == domChildren.end()) continue;
        auto& children = it->second;

        if (useProb) {
            // E5: 概率引导排序
            //   后继优先 > 非后继；均为后继时按 succPriority；无信号时 name-sort（基线）
            std::sort(children.begin(), children.end(),
                [&](IR::BasicBlock* a, IR::BasicBlock* b) {
                    bool aIsSucc = isCfgSucc(cur, a, succs);
                    bool bIsSucc = isCfgSucc(cur, b, succs);
                    if (aIsSucc != bIsSucc) return aIsSucc;  // 后继优先
                    if (aIsSucc) {
                        int pa = succPriority(cur, a, loops, blockToLoopIdx);
                        int pb = succPriority(cur, b, loops, blockToLoopIdx);
                        if (pa != pb) return pa > pb;  // 有信号：热者在前
                        // 无信号：保持基线 name-sort（零回归）
                    }
                    return a->getName() < b->getName();
                });
        } else {
            // 原始行为：后继优先 + name
            std::sort(children.begin(), children.end(),
                [&](IR::BasicBlock* a, IR::BasicBlock* b) {
                    bool aIsSucc = isCfgSucc(cur, a, succs);
                    bool bIsSucc = isCfgSucc(cur, b, succs);
                    if (aIsSucc != bIsSucc) return aIsSucc;
                    return a->getName() < b->getName();
                });
        }

        if (dbgLayout && useProb) {
            // 记录概率启发式是否改变了排序（相比纯 name-sort）
            // 仅在 cur 有 ≥2 个后继子节点时可能生效
            int succChildCount = 0;
            for (auto* c : children) if (isCfgSucc(cur, c, succs)) ++succChildCount;
            if (succChildCount >= 2) {
                // 检查是否有非零优先级（即启发式生效）
                bool anySignal = false;
                for (auto* c : children) {
                    if (isCfgSucc(cur, c, succs) &&
                        succPriority(cur, c, loops, blockToLoopIdx) != 0) {
                        anySignal = true;
                        break;
                    }
                }
                if (anySignal) {
                    fprintf(stderr, "[layout] %s: cur=%s children(order)=",
                            func->getName().c_str(), cur->getName().c_str());
                    for (auto* c : children) {
                        int p = isCfgSucc(cur, c, succs)
                                    ? succPriority(cur, c, loops, blockToLoopIdx)
                                    : 0;
                        fprintf(stderr, " %s(%d)", c->getName().c_str(), p);
                    }
                    fprintf(stderr, "\n");
                }
            }
        }

        // 逆序入栈，以保证排序后正序出栈
        for (auto it2 = children.rbegin(); it2 != children.rend(); ++it2) {
            stack.push_back(*it2);
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